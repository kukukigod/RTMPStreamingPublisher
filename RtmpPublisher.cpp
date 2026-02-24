#include <iostream>
#include <vector>
#include <string>
#include <mutex>
#include <thread>
#include <cstring>
#include <atomic>
#include <chrono>
#include "GstManager.h"
#include "Logger.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/time.h>
#include <libavutil/dict.h>
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/mathematics.h>
}

#define APP_VERSION "v1.0.0"
#define RTMP_URL "rtmp://rtmpurl"

std::atomic<bool> g_should_exit(false);
// Global flag to control trace logs via stdin, default to false
std::atomic<bool> g_enable_trace(false);

/**
 * Helper to find NALU start codes in the byte stream
 */
static const uint8_t* find_nalu(const uint8_t* start, const uint8_t* end) {
    for (const uint8_t* p = start; p < end - 4; ++p) {
        if (p[0] == 0x00 && p[1] == 0x00 && (p[2] == 0x01 || (p[2] == 0x00 && p[3] == 0x01))) return p;
    }
    return nullptr;
}

class RTMPStreamer {
public:
    RTMPStreamer(const std::string& rtmpUrl)
        : rtmpUrl_(rtmpUrl), outContext_(nullptr), videoStream_(nullptr), audioStream_(nullptr),
          _previousVideoPts(-1), isRunning_(false), isHeaderWritten_(false),
          videoFrameCnt_(0), audioFrameCnt_(0) {
        avformat_network_init();
    }

    ~RTMPStreamer() { stop(); }

    bool start(int width, int height, int sampleRate = 44100, int channels = 1) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (avformat_alloc_output_context2(&outContext_, nullptr, "flv", rtmpUrl_.c_str()) < 0) return false;

        videoStream_ = avformat_new_stream(outContext_, nullptr);
        videoStream_->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
        videoStream_->codecpar->codec_id = AV_CODEC_ID_H264;
        videoStream_->codecpar->width = width;
        videoStream_->codecpar->height = height;
        videoStream_->time_base = {1, 1000};

        audioStream_ = avformat_new_stream(outContext_, nullptr);
        audioStream_->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
        audioStream_->codecpar->codec_id = AV_CODEC_ID_AAC;
        audioStream_->codecpar->sample_rate = sampleRate;
        av_channel_layout_default(&audioStream_->codecpar->ch_layout, channels);
        audioStream_->time_base = {1, 1000};

        isRunning_ = true;
        return true;
    }

    void stop() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (outContext_) {
            if (isHeaderWritten_) av_write_trailer(outContext_);
            if (outContext_->pb) avio_closep(&outContext_->pb);
            avformat_free_context(outContext_);
            outContext_ = nullptr;
        }
        isRunning_ = false;
        isHeaderWritten_ = false;
    }

    int64_t getRelativeMs() {
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(now - _baseTimePoint).count();
    }

    void pushVideoFrame(const std::vector<uint8_t>& data) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!isRunning_ || g_should_exit) return;

        const uint8_t* p = data.data();
        const uint8_t* end = p + data.size();
        const uint8_t* sps = nullptr; size_t sps_len = 0;
        const uint8_t* pps = nullptr; size_t pps_len = 0;
        const uint8_t* idr = nullptr;
        const uint8_t* curr = find_nalu(p, end);

        while (curr) {
            int offset = (curr[2] == 0x01) ? 3 : 4;
            int type = curr[offset] & 0x1F;
            const uint8_t* next = find_nalu(curr + offset, end);
            size_t len = (next ? next : end) - curr;
            if (type == 7) { sps = curr; sps_len = len; }
            else if (type == 8) { pps = curr; pps_len = len; }
            else if (type == 5) { idr = curr; }
            curr = next;
        }

        if (!isHeaderWritten_) {
            if (sps && pps) {
                videoStream_->codecpar->extradata_size = sps_len + pps_len;
                videoStream_->codecpar->extradata = (uint8_t*)av_mallocz(videoStream_->codecpar->extradata_size + AV_INPUT_BUFFER_PADDING_SIZE);
                memcpy(videoStream_->codecpar->extradata, sps, sps_len);
                memcpy(videoStream_->codecpar->extradata + sps_len, pps, pps_len);

                AVDictionary* opts = nullptr;
                av_dict_set(&opts, "rtmp_live", "live", 0);
                if (avio_open2(&outContext_->pb, rtmpUrl_.c_str(), AVIO_FLAG_WRITE, nullptr, &opts) >= 0) {
                    if (avformat_write_header(outContext_, &opts) >= 0) {
                        isHeaderWritten_ = true;
                        _baseTimePoint = std::chrono::steady_clock::now();
                        logWithTime("[RTMP] Header written. Version: " + std::string(APP_VERSION));
                        std::cout << "\n>>> PRESS 't' TO TOGGLE TRACE LOGS, 'q' TO EXIT <<<\n" << std::endl;
                    }
                }
                av_dict_free(&opts);
            }
            if (!isHeaderWritten_) return;
        }

        AVPacket *pkt = av_packet_alloc();
        pkt->data = const_cast<uint8_t*>(data.data());
        pkt->size = data.size();
        pkt->stream_index = videoStream_->index;

        int64_t pts = getRelativeMs();
        if (_previousVideoPts != -1 && pts <= _previousVideoPts) pts = _previousVideoPts + 1;

        pkt->pts = pkt->dts = pts;
        if (idr) pkt->flags |= AV_PKT_FLAG_KEY;

        // Conditional Trace Log
        if (g_enable_trace) {
            logWithTime("[V TRACE] Frm: " + std::to_string(videoFrameCnt_++) +
                        " | PTS: " + std::to_string(pkt->pts) + " | DTS: " + std::to_string(pkt->dts));
        }

        if (av_write_frame(outContext_, pkt) < 0) g_should_exit = true;

        _previousVideoPts = pts;
        av_packet_free(&pkt);
    }

    void pushAudioFrame(const std::vector<uint8_t>& data, int nb_samples) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!isHeaderWritten_ || g_should_exit) return;

        AVPacket *pkt = av_packet_alloc();
        pkt->data = const_cast<uint8_t*>(data.data());
        pkt->size = data.size();
        pkt->stream_index = audioStream_->index;
        pkt->pts = pkt->dts = getRelativeMs();

        // Conditional Trace Log
        if (g_enable_trace) {
            logWithTime("[A TRACE] Pkt: " + std::to_string(audioFrameCnt_++) +
                        " | PTS: " + std::to_string(pkt->pts) + " | DTS: " + std::to_string(pkt->dts));
        }

        if (av_write_frame(outContext_, pkt) < 0) g_should_exit = true;

        av_packet_free(&pkt);
    }

private:
    std::string rtmpUrl_;
    AVFormatContext* outContext_;
    AVStream *videoStream_, *audioStream_;
    std::chrono::steady_clock::time_point _baseTimePoint;
    int64_t _previousVideoPts;
    bool isRunning_, isHeaderWritten_;
    uint64_t videoFrameCnt_, audioFrameCnt_;
    std::mutex mutex_;
};

/**
 * Thread function to handle CLI commands
 */
void command_listener() {
    std::string cmd;
    while (!g_should_exit) {
        if (std::cin >> cmd) {
            if (cmd == "t" || cmd == "T") {
                g_enable_trace = !g_enable_trace;
                logWithTime(std::string("[CLI] Trace logs ") + (g_enable_trace ? "ENABLED" : "DISABLED"));
            } else if (cmd == "q" || cmd == "Q") {
                logWithTime("[CLI] Exit command received.");
                g_should_exit = true;
                break;
            }
        }
    }
}

int main(int argc, char* argv[]) {
    logWithTime("RtmpPublisher Starting... Version: " + std::string(APP_VERSION));
    RTMPStreamer rtmp(RTMP_URL);

    if (!rtmp.start(720, 480, 44100, 1)) return -1;

    // Start CLI listener thread
    std::thread cliThread(command_listener);

    GstManager gst(720, 480, 30, 800000);

    gst.setOnVideoAnnexBFrame([&rtmp](const std::vector<uint8_t>& data) {
        rtmp.pushVideoFrame(data);
    });

    gst.setOnAudioAACFrame([&rtmp](const std::vector<uint8_t>& data) {
        rtmp.pushAudioFrame(data, 1024);
    });

    gst.startVideo();
    gst.startAudio();

    // Main loop remains simple; logic is handled by atomic flags
    while (!g_should_exit) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    gst.stopVideo();
    gst.stopAudio();
    rtmp.stop();

    if (cliThread.joinable()) cliThread.join();

    return 0;
}
