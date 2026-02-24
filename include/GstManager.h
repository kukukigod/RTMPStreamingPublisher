#pragma once
#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <vector>
#include <functional>
#include <thread>
#include <mutex>
#include <cstdint>
#include <string>

/**
 * GstManager: Manages GStreamer pipelines for video/audio capture and playback.
 * Supports dual-stream output:
 * - WebRTC Path: Video (RTP/H.264), Audio (RTP/Opus)
 * - RTMP Path: Video (Annex B/H.264), Audio (Raw AAC ADTS)
 */
class GstManager {
public:
    using FrameCallback = std::function<void(const std::vector<uint8_t>&)>;

    GstManager(int width, int height, int fps, int videoBitrate,
               uint32_t videoSSRC = 42, uint32_t audioSSRC = 43);

    ~GstManager();

    // ---------------- Video ----------------
    void startVideo();
    void stopVideo();
    
    /** @brief For WebRTC: H.264 RTP Packets */
    void setOnVideoRTPFrame(FrameCallback cb) { onVideoRTPFrame_ = cb; }

    /** @brief For RTMP: Raw H.264 Annex B frames */
    void setOnVideoAnnexBFrame(FrameCallback cb) { onVideoAnnexBFrame_ = cb; }

    // ---------------- Audio ----------------
    void startAudio();
    void stopAudio();
    
    /** @brief For WebRTC: Opus RTP Packets */
    void setOnAudioRTPFrame(FrameCallback cb) { onAudioRTPFrame_ = cb; }

    /** @brief For RTMP: Raw AAC frames (ADTS) */
    void setOnAudioAACFrame(FrameCallback cb) { onAudioAACFrame_ = cb; }

    // ---------------- Audio Playback ----------------
    void startAudioPlayer();
    void stopAudioPlayer();
    void pushAudioFrame(const uint8_t* data, size_t size);

private:
    /** * @brief Helper to connect appsink signals and reduce boilerplate 
     * Added to fix the 'no declaration matches' compilation error.
     */
    void setupSink(GstElement* pipeline, const std::string& name, GCallback callback);

    // Callbacks for GStreamer appsinks
    static GstFlowReturn onVideoRTPSample(GstAppSink* appsink, gpointer user_data);
    static GstFlowReturn onVideoAnnexBSample(GstAppSink* appsink, gpointer user_data);
    static GstFlowReturn onAudioRTPSample(GstAppSink* appsink, gpointer user_data);
    static GstFlowReturn onAudioAACSample(GstAppSink* appsink, gpointer user_data);

private:
    // Pipeline elements
    GstElement* videoPipeline_ = nullptr;
    GstElement* audioPipeline_ = nullptr;
    GstElement* audioPlayerPipeline_ = nullptr;
    GstElement* audioAppSrc_ = nullptr;

    // Callbacks
    FrameCallback onVideoRTPFrame_;
    FrameCallback onVideoAnnexBFrame_;
    FrameCallback onAudioRTPFrame_;
    FrameCallback onAudioAACFrame_;

    // Video / audio params
    int width_;
    int height_;
    int fps_;
    int videoBitrate_;
    uint32_t videoSSRC_;
    uint32_t audioSSRC_;

    // Threads & loop
    GMainLoop* mainLoop_ = nullptr;
    std::thread mainThread_;
    std::mutex mutex_;
};