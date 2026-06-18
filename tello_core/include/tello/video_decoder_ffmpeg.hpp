#ifndef TELLO_VIDEO_DECODER_FFMPEG_HPP
#define TELLO_VIDEO_DECODER_FFMPEG_HPP

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

namespace tello {

// ============================================================================
// FFmpeg H264 Decoder (optional build)
// ============================================================================
/// Decodes H264 NAL units into raw frames using libavcodec.
class VideoDecoderFfmpeg {
public:
    struct DecodedFrameInfo {
        int32_t width = 0;
        int32_t height = 0;
        int32_t pixel_format = -1;
        bool is_key_frame = false;
        int64_t pts = 0;
    };

    using FrameCallback = std::function<void(const DecodedFrameInfo& frame_info)>;

    struct DecodedFrame {
        DecodedFrameInfo info;
        std::vector<uint8_t> luma;
        int32_t luma_stride = 0;
        std::vector<uint8_t> bgr;
        int32_t bgr_stride = 0;
    };

    using FrameDataCallback = std::function<void(const DecodedFrame& frame)>;

    struct Stats {
        uint64_t nals_in = 0;         ///< NAL units submitted to decoder
        uint64_t packets_sent = 0;    ///< AVPackets sent to codec
        uint64_t frames_decoded = 0;  ///< Decoded frames produced
        uint64_t decode_errors = 0;   ///< send/receive decode errors
        double decode_fps_ema = 0.0;  ///< Smoothed decode throughput (fps)
    };

    VideoDecoderFfmpeg();
    ~VideoDecoderFfmpeg();

    /// Initialize H264 decoder context.
    /// Returns false if FFmpeg decoder cannot be opened.
    bool initialize();

    /// Release decoder context and buffers.
    void shutdown();

    /// Decode one NAL unit. Returns true if at least one frame was produced.
    bool decodeNal(const std::vector<uint8_t>& nal);

    /// Register callback invoked for each decoded frame.
    void setFrameCallback(FrameCallback callback);

    /// Register callback invoked with copied frame payload (Y plane/luma).
    void setFrameDataCallback(FrameDataCallback callback);

    /// Get a snapshot of decode statistics.
    Stats getStats() const;

    /// Reset decoder statistics only (decoder remains initialized).
    void resetStats();

    /// Whether decoder context is currently initialized.
    bool isInitialized() const;

private:
    struct Impl;

    mutable std::mutex mutex_;
    std::unique_ptr<Impl> impl_;
    FrameCallback frame_callback_;
    FrameDataCallback frame_data_callback_;
    Stats stats_;
};

} // namespace tello

#endif // TELLO_VIDEO_DECODER_FFMPEG_HPP
