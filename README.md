A high-performance, cross-platform RTMP streaming client that integrates GStreamer for media pipeline handling and FFmpeg for RTMP protocol multiplexing.  
🚀 Key Features  

    Dual-Platform Support: Optimized for both x86 (Ubuntu/Linux) and Qualcomm QCS610 (ARM) platforms.

    Low Latency: Fine-tuned x264/OMX encoding parameters for real-time streaming.

    Interactive CLI: Real-time trace log toggling during streaming for debugging.

    Hybrid Architecture: Uses GStreamer for efficient hardware-accelerated encoding and FFmpeg for stable RTMP publishing.

🛠 Prerequisites

Ensure the following libraries are installed on your system:

    GStreamer 1.0 (core, base, good, bad, and libav plugins)

    FFmpeg 5.x+ (specifically libavformat, libavcodec, libavutil)

🏗 Build Instructions

The project uses a unified Makefile to handle cross-compilation:
For x86 (Standard PC)
Bash

make clean
make x86

For QCS610 (ARM)
Bash

make clean
make qcs610

🖥 Usage

Run the generated binary to start streaming:
Bash

./RtmpPublisher_x86

Interactive Commands

    Press t: Toggle Trace Logs (Displays real-time PTS/DTS and frame count).

    Press q: Exit the application safely.

📝 Technical Implementation Details
Pipeline Configuration

The application intelligently selects the encoder based on the platform:

    x86: Uses x264enc (video) and voaacenc (audio).

    QCS610: Uses hardware-accelerated omxh264enc (video) and avenc_aac (audio).

Synchronization

Frame-level synchronization is maintained by preserving PTS (Presentation Timestamp) and DTS (Decoding Timestamp) during the transition from GStreamer AppSink to FFmpeg's AVPackets.
