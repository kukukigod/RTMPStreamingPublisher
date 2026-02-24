#include "GstManager.h"
#include <gst/app/gstappsrc.h>
#include <gst/app/gstappsink.h>
#include <iostream>
#include "Logger.h"

GstManager::GstManager(int width, int height, int fps, int videoBitrate,
                        uint32_t videoSSRC, uint32_t audioSSRC)
    : width_(width), height_(height), fps_(fps), videoBitrate_(videoBitrate),
      videoSSRC_(videoSSRC), audioSSRC_(audioSSRC),
      videoPipeline_(nullptr), audioPipeline_(nullptr), audioPlayerPipeline_(nullptr),
      audioAppSrc_(nullptr), mainLoop_(nullptr) {
    
    // Initialize GStreamer once
    if (!gst_is_initialized()) {
        gst_init(nullptr, nullptr);
    }
}

GstManager::~GstManager() {
    stopVideo();
    stopAudio();
    stopAudioPlayer();

    if (mainLoop_) {
        g_main_loop_quit(mainLoop_);
    }

    if (mainThread_.joinable()) {
        mainThread_.join();
    }

    if (mainLoop_) {
        g_main_loop_unref(mainLoop_);
        mainLoop_ = nullptr;
    }
}

// ---------------- Helper ----------------
void GstManager::setupSink(GstElement* pipeline, const std::string& name, GCallback callback) {
    GstElement* sink = gst_bin_get_by_name(GST_BIN(pipeline), name.c_str());
    if (sink) {
        g_signal_connect(sink, "new-sample", callback, this);
        gst_object_unref(sink);
    }
}

// ---------------- Video ----------------
void GstManager::startVideo() {
    std::lock_guard<std::mutex> lk(mutex_);
    if (videoPipeline_) return;

    std::string videoPipelineDesc;

#if PLATFORM_NUM == 0x610
    // QCS610 Hardware Encoding
    videoPipelineDesc =
        "qtiqmmfsrc ! video/x-raw,format=NV12,width=" + std::to_string(width_) + ",height=" + std::to_string(height_) +
        ",framerate=" + std::to_string(fps_) + "/1 ! "
        "omxh264enc periodicity-idr=1 interval-intraframes=29 control-rate=2 target-bitrate=" + std::to_string(videoBitrate_) + 
        " b-frames=0 entropy-mode=0 ! " 
        "video/x-h264,profile=baseline ! "
        "h264parse config-interval=1 ! " 
        "video/x-h264,stream-format=byte-stream,alignment=au ! tee name=t_video "
        "t_video. ! queue ! appsink name=h264sink emit-signals=true sync=true "
        "t_video. ! queue ! rtph264pay config-interval=1 pt=96 ssrc=" + std::to_string(videoSSRC_) +
        " mtu=1200 ! appsink name=rtpsink emit-signals=true sync=false";
#else
    // x86 Video (Fixed profile caps)
    videoPipelineDesc =
        "videotestsrc is-live=true pattern=ball ! video/x-raw,width=" + std::to_string(width_) + ",height=" + std::to_string(height_) +
        ",framerate=" + std::to_string(fps_) + "/1,format=NV12 ! videoconvert ! "
        "x264enc tune=zerolatency key-int-max=30 speed-preset=ultrafast bitrate=" + std::to_string(videoBitrate_ / 1000) + " ! "
        "video/x-h264,profile=baseline ! " 
        "h264parse config-interval=1 ! video/x-h264,stream-format=byte-stream,alignment=au ! tee name=t_video "
        "t_video. ! queue ! appsink name=h264sink emit-signals=true sync=false "
        "t_video. ! queue ! rtph264pay config-interval=1 pt=96 ssrc=" + std::to_string(videoSSRC_) +
        " mtu=1200 ! appsink name=rtpsink emit-signals=true sync=false";
#endif

    GError* error = nullptr;
    logWithTime("Video Pipeline = " + videoPipelineDesc);
    videoPipeline_ = gst_parse_launch(videoPipelineDesc.c_str(), &error);
    if (!videoPipeline_ || error) {
        std::cerr << "[GstManager] Failed to create video pipeline: " << (error ? error->message : "Unknown") << std::endl;
        if (error) g_error_free(error);
        return;
    }

    setupSink(videoPipeline_, "rtpsink", G_CALLBACK(onVideoRTPSample));
    setupSink(videoPipeline_, "h264sink", G_CALLBACK(onVideoAnnexBSample));

    gst_element_set_state(videoPipeline_, GST_STATE_PLAYING);
}

void GstManager::stopVideo() {
    if (!videoPipeline_) return;
    gst_element_set_state(videoPipeline_, GST_STATE_NULL);
    gst_object_unref(videoPipeline_);
    videoPipeline_ = nullptr;
}

// ---------------- Audio ----------------
void GstManager::startAudio() {
    std::lock_guard<std::mutex> lk(mutex_);
    if (audioPipeline_) return;

    std::string audioPipelineDesc;

#if PLATFORM_NUM == 0x610
    // QCS610 Audio: Uses avenc_aac (standard in that environment)
    audioPipelineDesc =
        "pulsesrc provide-clock=false ! audio/x-raw,format=S16LE,rate=48000,channels=1 ! tee name=t_audio "
        "t_audio. ! queue ! opusenc ! rtpopuspay pt=111 ssrc=" + std::to_string(audioSSRC_) + 
        " ! appsink name=rtpsink emit-signals=true sync=false "
        "t_audio. ! queue ! audioconvert ! audioresample ! audio/x-raw,format=F32LE,rate=44100,channels=1 ! avenc_aac ! aacparse ! appsink name=aacsink emit-signals=true sync=false";
#else
    // x86 Audio: Replacing avenc_aac with voaacenc (based on your earlier logs)
    // If voaacenc also fails, try 'fdkaacenc' or 'faac'
    audioPipelineDesc =
        "pulsesrc ! audio/x-raw,format=S16LE,rate=48000,channels=1 ! "
        "audioconvert ! audioresample ! tee name=t_audio "
        "t_audio. ! queue ! opusenc ! rtpopuspay pt=111 ssrc=" + std::to_string(audioSSRC_) + 
        " ! appsink name=rtpsink emit-signals=true sync=false "
        "t_audio. ! queue ! audioconvert ! audioresample ! audio/x-raw,rate=44100,channels=1 ! "
        "voaacenc ! aacparse ! appsink name=aacsink emit-signals=true sync=false";
#endif

    GError* error = nullptr;
    audioPipeline_ = gst_parse_launch(audioPipelineDesc.c_str(), &error);
    logWithTime("Audio Pipeline = " + audioPipelineDesc);
    if (!audioPipeline_ || error) {
        std::cerr << "[GstManager] Failed to create audio pipeline: " << (error ? error->message : "Unknown") << std::endl;
        if (error) g_error_free(error);
        return;
    }

    setupSink(audioPipeline_, "rtpsink", G_CALLBACK(onAudioRTPSample));
    setupSink(audioPipeline_, "aacsink", G_CALLBACK(onAudioAACSample));

    gst_element_set_state(audioPipeline_, GST_STATE_PLAYING);
}

void GstManager::stopAudio() {
    if (!audioPipeline_) return;
    gst_element_set_state(audioPipeline_, GST_STATE_NULL);
    gst_object_unref(audioPipeline_);
    audioPipeline_ = nullptr;
}

void GstManager::startAudioPlayer() {
    std::lock_guard<std::mutex> lk(mutex_);
    if (audioPlayerPipeline_) return;

    std::string audioPlayDesc =
        "appsrc name=audio_src is-live=true do-timestamp=true format=time ! "
        "application/x-rtp,media=audio,clock-rate=48000,encoding-name=OPUS,payload=111 ! "
        "rtpjitterbuffer latency=200 ! rtpopusdepay ! opusdec ! audioconvert ! audioresample ! autoaudiosink";

    audioPlayerPipeline_ = gst_parse_launch(audioPlayDesc.c_str(), nullptr);
    audioAppSrc_ = gst_bin_get_by_name(GST_BIN(audioPlayerPipeline_), "audio_src");
    
    gst_element_set_state(audioPlayerPipeline_, GST_STATE_PLAYING);

    if (!mainLoop_) {
        mainLoop_ = g_main_loop_new(nullptr, FALSE);
        mainThread_ = std::thread([this]() { g_main_loop_run(mainLoop_); });
    }
}

void GstManager::stopAudioPlayer() {
    std::lock_guard<std::mutex> lk(mutex_);
    if (!audioPlayerPipeline_) return;
    
    if (audioAppSrc_) {
        gst_object_unref(audioAppSrc_);
        audioAppSrc_ = nullptr;
    }
    gst_element_set_state(audioPlayerPipeline_, GST_STATE_NULL);
    gst_object_unref(audioPlayerPipeline_);
    audioPlayerPipeline_ = nullptr;
}

void GstManager::pushAudioFrame(const uint8_t* data, size_t size) {
    std::lock_guard<std::mutex> lk(mutex_);
    if (!audioAppSrc_) return;

    GstBuffer* buffer = gst_buffer_new_allocate(nullptr, size, nullptr);
    gst_buffer_fill(buffer, 0, data, size);
    
    GST_BUFFER_PTS(buffer) = gst_util_get_timestamp();
    GST_BUFFER_DTS(buffer) = GST_BUFFER_PTS(buffer);

    GstFlowReturn ret;
    g_signal_emit_by_name(audioAppSrc_, "push-buffer", buffer, &ret);
    gst_buffer_unref(buffer);
}

// ---------------- Callbacks (Identical to before) ----------------
GstFlowReturn GstManager::onVideoRTPSample(GstAppSink* appsink, gpointer user_data) {
    GstManager* self = static_cast<GstManager*>(user_data);
    GstSample* sample = gst_app_sink_pull_sample(appsink);
    if (sample && self->onVideoRTPFrame_) {
        GstBuffer* buffer = gst_sample_get_buffer(sample);
        GstMapInfo map;
        if (gst_buffer_map(buffer, &map, GST_MAP_READ)) {
            std::vector<uint8_t> rtp(map.data, map.data + map.size);
            self->onVideoRTPFrame_(rtp);
            gst_buffer_unmap(buffer, &map);
        }
    }
    if (sample) gst_sample_unref(sample);
    return GST_FLOW_OK;
}

GstFlowReturn GstManager::onVideoAnnexBSample(GstAppSink* appsink, gpointer user_data) {
    GstManager* self = static_cast<GstManager*>(user_data);
    GstSample* sample = gst_app_sink_pull_sample(appsink);
    if (sample && self->onVideoAnnexBFrame_) {
        GstBuffer* buffer = gst_sample_get_buffer(sample);
        GstMapInfo map;
        if (gst_buffer_map(buffer, &map, GST_MAP_READ)) {
            std::vector<uint8_t> frame(map.data, map.data + map.size);
            self->onVideoAnnexBFrame_(frame);
            gst_buffer_unmap(buffer, &map);
        }
    }
    if (sample) gst_sample_unref(sample);
    return GST_FLOW_OK;
}

GstFlowReturn GstManager::onAudioRTPSample(GstAppSink* appsink, gpointer user_data) {
    GstManager* self = static_cast<GstManager*>(user_data);
    GstSample* sample = gst_app_sink_pull_sample(appsink);
    if (sample && self->onAudioRTPFrame_) {
        GstBuffer* buffer = gst_sample_get_buffer(sample);
        GstMapInfo map;
        if (gst_buffer_map(buffer, &map, GST_MAP_READ)) {
            std::vector<uint8_t> rtp(map.data, map.data + map.size);
            self->onAudioRTPFrame_(rtp);
            gst_buffer_unmap(buffer, &map);
        }
    }
    if (sample) gst_sample_unref(sample);
    return GST_FLOW_OK;
}

GstFlowReturn GstManager::onAudioAACSample(GstAppSink* appsink, gpointer user_data) {
    GstManager* self = static_cast<GstManager*>(user_data);
    GstSample* sample = gst_app_sink_pull_sample(appsink);
    if (sample && self->onAudioAACFrame_) {
        GstBuffer* buffer = gst_sample_get_buffer(sample);
        GstMapInfo map;
        if (gst_buffer_map(buffer, &map, GST_MAP_READ)) {
            std::vector<uint8_t> frame(map.data, map.data + map.size);
            self->onAudioAACFrame_(frame);
            gst_buffer_unmap(buffer, &map);
        }
    }
    if (sample) gst_sample_unref(sample);
    return GST_FLOW_OK;
}