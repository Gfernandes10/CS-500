#ifndef TELLO_VIDEO_STREAM_READER_FFMPEG_HPP
#define TELLO_VIDEO_STREAM_READER_FFMPEG_HPP

#include "types.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace tello {

class VideoStreamReaderFfmpeg {
public:
    struct DecodedFrame {
        int32_t width = 0;
        int32_t height = 0;
        int32_t rgb_stride = 0;
        bool is_key_frame = false;
        std::vector<uint8_t> rgb;
    };

    using FrameCallback = std::function<void(const DecodedFrame&)>;

    struct Stats {
        uint64_t packets_read = 0;
        uint64_t bytes_read = 0;
        uint64_t frames_decoded = 0;
        uint64_t decode_errors = 0;
        double decode_fps_ema = 0.0;
        int64_t last_frame_age_ms = -1;
        int32_t frame_width = 0;
        int32_t frame_height = 0;
        uint64_t keyframes = 0;
    };

    VideoStreamReaderFfmpeg();
    ~VideoStreamReaderFfmpeg();

    ResponseCode start(const std::string& url);
    void stop();
    bool isRunning() const;

    void setFrameCallback(FrameCallback callback);
    Stats getStats() const;
    std::string getLastError() const;

private:
    void readerLoop(std::string url);
    static int interruptCallback(void* opaque);

    std::atomic<bool> running_;
    std::atomic<bool> stop_requested_;
    std::thread worker_;

    mutable std::mutex callback_mutex_;
    FrameCallback frame_callback_;

    mutable std::mutex stats_mutex_;
    Stats stats_;
    bool has_last_frame_tp_;
    std::chrono::steady_clock::time_point last_frame_tp_;
    bool has_fps_window_start_;
    std::chrono::steady_clock::time_point fps_window_start_;
    uint64_t frames_since_fps_update_;

    mutable std::mutex error_mutex_;
    std::string last_error_;
};

} // namespace tello

#endif // TELLO_VIDEO_STREAM_READER_FFMPEG_HPP
