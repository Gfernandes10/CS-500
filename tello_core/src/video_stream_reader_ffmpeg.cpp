#include "tello/video_stream_reader_ffmpeg.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libswscale/swscale.h>
}

#include <chrono>
#include <cstring>

namespace tello {

namespace {

std::string avErrorToString(int errnum) {
    char buffer[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(errnum, buffer, sizeof(buffer));
    return std::string(buffer);
}

void setDictionaryOption(AVDictionary** dict, const char* key, const char* value) {
    (void)av_dict_set(dict, key, value, 0);
}

} // namespace

VideoStreamReaderFfmpeg::VideoStreamReaderFfmpeg()
    : running_(false),
      stop_requested_(false),
      worker_(),
      callback_mutex_(),
      frame_callback_(nullptr),
      stats_mutex_(),
      stats_(),
      has_last_frame_tp_(false),
      last_frame_tp_(std::chrono::steady_clock::now()),
      has_fps_window_start_(false),
      fps_window_start_(std::chrono::steady_clock::now()),
      frames_since_fps_update_(0),
      error_mutex_(),
      last_error_() {}

VideoStreamReaderFfmpeg::~VideoStreamReaderFfmpeg() {
    stop();
}

ResponseCode VideoStreamReaderFfmpeg::start(const std::string& url) {
    if (url.empty()) {
        return ResponseCode::ERROR;
    }
    if (running_.load()) {
        return ResponseCode::OK;
    }

    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_ = Stats{};
        has_last_frame_tp_ = false;
        has_fps_window_start_ = false;
        frames_since_fps_update_ = 0;
    }
    {
        std::lock_guard<std::mutex> lock(error_mutex_);
        last_error_.clear();
    }

    stop_requested_ = false;
    running_ = true;
    worker_ = std::thread(&VideoStreamReaderFfmpeg::readerLoop, this, url);
    return ResponseCode::OK;
}

void VideoStreamReaderFfmpeg::stop() {
    if (!running_.load() && !worker_.joinable()) {
        return;
    }

    stop_requested_ = true;
    running_ = false;

    if (worker_.joinable()) {
        worker_.join();
    }
}

bool VideoStreamReaderFfmpeg::isRunning() const {
    return running_.load();
}

void VideoStreamReaderFfmpeg::setFrameCallback(FrameCallback callback) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    frame_callback_ = std::move(callback);
}

VideoStreamReaderFfmpeg::Stats VideoStreamReaderFfmpeg::getStats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    Stats snapshot = stats_;
    if (has_last_frame_tp_) {
        snapshot.last_frame_age_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - last_frame_tp_).count();
    } else {
        snapshot.last_frame_age_ms = -1;
    }
    return snapshot;
}

std::string VideoStreamReaderFfmpeg::getLastError() const {
    std::lock_guard<std::mutex> lock(error_mutex_);
    return last_error_;
}

int VideoStreamReaderFfmpeg::interruptCallback(void* opaque) {
    auto* self = static_cast<VideoStreamReaderFfmpeg*>(opaque);
    return self != nullptr && self->stop_requested_.load() ? 1 : 0;
}

void VideoStreamReaderFfmpeg::readerLoop(std::string url) {
    av_log_set_level(AV_LOG_QUIET);
    avformat_network_init();

    AVFormatContext* format_ctx = avformat_alloc_context();
    AVCodecContext* codec_ctx = nullptr;
    AVFrame* frame = nullptr;
    AVPacket* packet = nullptr;
    SwsContext* sws_ctx = nullptr;
    int video_stream_index = -1;

    auto set_error = [this](const std::string& msg) {
        std::lock_guard<std::mutex> lock(error_mutex_);
        last_error_ = msg;
    };

    if (format_ctx == nullptr) {
        set_error("avformat_alloc_context failed");
        running_ = false;
        return;
    }

    format_ctx->interrupt_callback.callback = &VideoStreamReaderFfmpeg::interruptCallback;
    format_ctx->interrupt_callback.opaque = this;

    AVDictionary* options = nullptr;
    setDictionaryOption(&options, "fflags", "nobuffer");
    setDictionaryOption(&options, "flags", "low_delay");
    setDictionaryOption(&options, "probesize", "32768");
    setDictionaryOption(&options, "analyzeduration", "0");
    setDictionaryOption(&options, "reorder_queue_size", "0");
    setDictionaryOption(&options, "overrun_nonfatal", "1");
    setDictionaryOption(&options, "fifo_size", "5000000");

    int rc = avformat_open_input(&format_ctx, url.c_str(), nullptr, &options);
    av_dict_free(&options);
    if (rc < 0) {
        set_error("avformat_open_input failed: " + avErrorToString(rc));
        avformat_free_context(format_ctx);
        running_ = false;
        return;
    }

    rc = avformat_find_stream_info(format_ctx, nullptr);
    if (rc < 0 && !stop_requested_.load()) {
        set_error("avformat_find_stream_info failed: " + avErrorToString(rc));
    }

    for (unsigned int i = 0; i < format_ctx->nb_streams; ++i) {
        if (format_ctx->streams[i] != nullptr
            && format_ctx->streams[i]->codecpar != nullptr
            && format_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_index = static_cast<int>(i);
            break;
        }
    }

    if (video_stream_index < 0) {
        set_error("no video stream found");
        avformat_close_input(&format_ctx);
        running_ = false;
        return;
    }

    const AVCodecParameters* codecpar = format_ctx->streams[video_stream_index]->codecpar;
    const AVCodec* codec = avcodec_find_decoder(codecpar->codec_id);
    if (codec == nullptr) {
        set_error("video decoder not found");
        avformat_close_input(&format_ctx);
        running_ = false;
        return;
    }

    codec_ctx = avcodec_alloc_context3(codec);
    if (codec_ctx == nullptr) {
        set_error("avcodec_alloc_context3 failed");
        avformat_close_input(&format_ctx);
        running_ = false;
        return;
    }

    rc = avcodec_parameters_to_context(codec_ctx, codecpar);
    if (rc < 0) {
        set_error("avcodec_parameters_to_context failed: " + avErrorToString(rc));
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&format_ctx);
        running_ = false;
        return;
    }
    codec_ctx->flags |= AV_CODEC_FLAG_LOW_DELAY;
    codec_ctx->thread_count = 1;

    rc = avcodec_open2(codec_ctx, codec, nullptr);
    if (rc < 0) {
        set_error("avcodec_open2 failed: " + avErrorToString(rc));
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&format_ctx);
        running_ = false;
        return;
    }

    frame = av_frame_alloc();
    packet = av_packet_alloc();
    if (frame == nullptr || packet == nullptr) {
        set_error("frame/packet allocation failed");
        if (packet != nullptr) {
            av_packet_free(&packet);
        }
        if (frame != nullptr) {
            av_frame_free(&frame);
        }
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&format_ctx);
        running_ = false;
        return;
    }

    int sws_width = 0;
    int sws_height = 0;
    int sws_format = -1;

    while (!stop_requested_.load()) {
        rc = av_read_frame(format_ctx, packet);
        if (rc < 0) {
            if (!stop_requested_.load() && rc != AVERROR(EAGAIN)) {
                std::lock_guard<std::mutex> lock(stats_mutex_);
                ++stats_.decode_errors;
            }
            av_packet_unref(packet);
            continue;
        }

        if (packet->stream_index != video_stream_index) {
            av_packet_unref(packet);
            continue;
        }

        {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            ++stats_.packets_read;
            stats_.bytes_read += static_cast<uint64_t>(packet->size > 0 ? packet->size : 0);
        }

        rc = avcodec_send_packet(codec_ctx, packet);
        av_packet_unref(packet);
        if (rc < 0) {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            ++stats_.decode_errors;
            continue;
        }

        while (!stop_requested_.load()) {
            rc = avcodec_receive_frame(codec_ctx, frame);
            if (rc == AVERROR(EAGAIN) || rc == AVERROR_EOF) {
                break;
            }
            if (rc < 0) {
                std::lock_guard<std::mutex> lock(stats_mutex_);
                ++stats_.decode_errors;
                break;
            }

            const bool reconfigure =
                sws_ctx == nullptr ||
                sws_width != frame->width ||
                sws_height != frame->height ||
                sws_format != frame->format;
            if (reconfigure) {
                if (sws_ctx != nullptr) {
                    sws_freeContext(sws_ctx);
                    sws_ctx = nullptr;
                }
                sws_ctx = sws_getContext(
                    frame->width,
                    frame->height,
                    static_cast<AVPixelFormat>(frame->format),
                    frame->width,
                    frame->height,
                    AV_PIX_FMT_RGB24,
                    SWS_BILINEAR,
                    nullptr,
                    nullptr,
                    nullptr);
                sws_width = frame->width;
                sws_height = frame->height;
                sws_format = frame->format;
            }

            DecodedFrame decoded;
            decoded.width = frame->width;
            decoded.height = frame->height;
            decoded.rgb_stride = frame->width * 3;
#ifdef AV_FRAME_FLAG_KEY
            decoded.is_key_frame = (frame->flags & AV_FRAME_FLAG_KEY) != 0;
#else
            // FFmpeg versions shipped before AV_FRAME_FLAG_KEY expose the
            // decoded-frame key status through the legacy key_frame field.
            decoded.is_key_frame = frame->key_frame != 0;
#endif
            decoded.rgb.resize(static_cast<size_t>(decoded.rgb_stride) * static_cast<size_t>(decoded.height));

            if (sws_ctx != nullptr) {
                uint8_t* dst_slices[4] = {decoded.rgb.data(), nullptr, nullptr, nullptr};
                int dst_linesizes[4] = {decoded.rgb_stride, 0, 0, 0};
                const int converted_rows = sws_scale(
                    sws_ctx,
                    frame->data,
                    frame->linesize,
                    0,
                    frame->height,
                    dst_slices,
                    dst_linesizes);
                if (converted_rows <= 0) {
                    decoded.rgb.clear();
                    std::lock_guard<std::mutex> lock(stats_mutex_);
                    ++stats_.decode_errors;
                    av_frame_unref(frame);
                    continue;
                }
            }

            const auto now = std::chrono::steady_clock::now();
            {
                std::lock_guard<std::mutex> lock(stats_mutex_);
                ++stats_.frames_decoded;
                stats_.frame_width = decoded.width;
                stats_.frame_height = decoded.height;
                if (decoded.is_key_frame) {
                    ++stats_.keyframes;
                }
                // Estimate throughput over a time window instead of taking
                // 1 / inter-frame time. FFmpeg can release several decoded
                // frames in one burst, which made the old EMA report
                // artificial 40-70 FPS spikes for a stable 30 FPS stream.
                if (!has_fps_window_start_) {
                    fps_window_start_ = now;
                    frames_since_fps_update_ = 0;
                    has_fps_window_start_ = true;
                } else {
                    ++frames_since_fps_update_;
                    const double window_s =
                        std::chrono::duration<double>(now - fps_window_start_).count();
                    if (window_s >= 0.5) {
                        const double fps =
                            static_cast<double>(frames_since_fps_update_) / window_s;
                        const double alpha = 0.2;
                        stats_.decode_fps_ema = stats_.decode_fps_ema <= 0.0
                            ? fps
                            : (alpha * fps) + ((1.0 - alpha) * stats_.decode_fps_ema);
                        fps_window_start_ = now;
                        frames_since_fps_update_ = 0;
                    }
                }
                last_frame_tp_ = now;
                has_last_frame_tp_ = true;
            }

            FrameCallback callback_copy;
            {
                std::lock_guard<std::mutex> lock(callback_mutex_);
                callback_copy = frame_callback_;
            }
            if (callback_copy && !decoded.rgb.empty()) {
                callback_copy(decoded);
            }

            av_frame_unref(frame);
        }
    }

    if (sws_ctx != nullptr) {
        sws_freeContext(sws_ctx);
    }
    av_packet_free(&packet);
    av_frame_free(&frame);
    avcodec_free_context(&codec_ctx);
    avformat_close_input(&format_ctx);
    avformat_network_deinit();
    running_ = false;
}

} // namespace tello
