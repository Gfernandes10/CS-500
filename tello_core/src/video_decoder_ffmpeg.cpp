#include "tello/video_decoder_ffmpeg.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libswscale/swscale.h>
}

#include <chrono>
#include <cstring>

namespace tello {

struct VideoDecoderFfmpeg::Impl {
    const AVCodec* codec = nullptr;
    AVCodecContext* codec_ctx = nullptr;
    AVPacket* packet = nullptr;
    AVFrame* frame = nullptr;

    std::chrono::steady_clock::time_point last_frame_tp{};
    bool has_last_frame_tp = false;
    SwsContext* sws_ctx = nullptr;
    int32_t sws_src_width = 0;
    int32_t sws_src_height = 0;
    int32_t sws_src_format = -1;
};

VideoDecoderFfmpeg::VideoDecoderFfmpeg()
    : mutex_(),
      impl_(std::make_unique<Impl>()),
    frame_callback_(nullptr),
    frame_data_callback_(nullptr),
      stats_() {}

VideoDecoderFfmpeg::~VideoDecoderFfmpeg() {
    shutdown();
}

bool VideoDecoderFfmpeg::initialize() {
    std::lock_guard<std::mutex> lock(mutex_);

    av_log_set_level(AV_LOG_QUIET);

    if (impl_ != nullptr && impl_->codec_ctx != nullptr) {
        return true;
    }

    if (impl_ == nullptr) {
        impl_ = std::make_unique<Impl>();
    }

    impl_->codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (impl_->codec == nullptr) {
        return false;
    }

    impl_->codec_ctx = avcodec_alloc_context3(impl_->codec);
    if (impl_->codec_ctx == nullptr) {
        return false;
    }

    if (avcodec_open2(impl_->codec_ctx, impl_->codec, nullptr) < 0) {
        avcodec_free_context(&impl_->codec_ctx);
        return false;
    }

    impl_->packet = av_packet_alloc();
    impl_->frame = av_frame_alloc();
    if (impl_->packet == nullptr || impl_->frame == nullptr) {
        shutdown();
        return false;
    }

    stats_ = Stats{};
    impl_->has_last_frame_tp = false;
    return true;
}

void VideoDecoderFfmpeg::shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (impl_ == nullptr) {
        return;
    }

    if (impl_->packet != nullptr) {
        av_packet_free(&impl_->packet);
    }
    if (impl_->frame != nullptr) {
        av_frame_free(&impl_->frame);
    }
    if (impl_->codec_ctx != nullptr) {
        avcodec_free_context(&impl_->codec_ctx);
    }
    if (impl_->sws_ctx != nullptr) {
        sws_freeContext(impl_->sws_ctx);
        impl_->sws_ctx = nullptr;
    }

    impl_->codec = nullptr;
    impl_->has_last_frame_tp = false;
    impl_->sws_src_width = 0;
    impl_->sws_src_height = 0;
    impl_->sws_src_format = -1;
}

bool VideoDecoderFfmpeg::decodeNal(const std::vector<uint8_t>& nal) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (impl_ == nullptr || impl_->codec_ctx == nullptr || impl_->packet == nullptr || impl_->frame == nullptr) {
        ++stats_.decode_errors;
        return false;
    }

    if (nal.empty()) {
        return false;
    }

    ++stats_.nals_in;

    // FFmpeg H264 parser works reliably with Annex-B start code prefix.
    std::vector<uint8_t> annexb;
    annexb.reserve(nal.size() + 4);
    annexb.push_back(0x00);
    annexb.push_back(0x00);
    annexb.push_back(0x00);
    annexb.push_back(0x01);
    annexb.insert(annexb.end(), nal.begin(), nal.end());

    av_packet_unref(impl_->packet);
    impl_->packet->data = annexb.data();
    impl_->packet->size = static_cast<int>(annexb.size());

    const int send_rc = avcodec_send_packet(impl_->codec_ctx, impl_->packet);
    if (send_rc < 0) {
        ++stats_.decode_errors;
        return false;
    }

    ++stats_.packets_sent;

    bool produced_frame = false;
    std::vector<DecodedFrameInfo> decoded_infos;
    std::vector<DecodedFrame> decoded_frames;
    decoded_infos.reserve(4);
    decoded_frames.reserve(4);
    while (true) {
        const int recv_rc = avcodec_receive_frame(impl_->codec_ctx, impl_->frame);
        if (recv_rc == AVERROR(EAGAIN) || recv_rc == AVERROR_EOF) {
            break;
        }
        if (recv_rc < 0) {
            ++stats_.decode_errors;
            break;
        }

        produced_frame = true;
        ++stats_.frames_decoded;

        DecodedFrameInfo frame_info;
        frame_info.width = impl_->frame->width;
        frame_info.height = impl_->frame->height;
        frame_info.pixel_format = impl_->frame->format;
        frame_info.is_key_frame = (impl_->frame->flags & AV_FRAME_FLAG_KEY) != 0;
        frame_info.pts = impl_->frame->pts;
        decoded_infos.push_back(frame_info);

        if (impl_->frame->data[0] != nullptr && impl_->frame->linesize[0] > 0 &&
            frame_info.width > 0 && frame_info.height > 0) {
            DecodedFrame decoded_frame;
            decoded_frame.info = frame_info;
            decoded_frame.luma_stride = frame_info.width;
            decoded_frame.luma.resize(static_cast<size_t>(frame_info.width) * static_cast<size_t>(frame_info.height));

            for (int32_t y = 0; y < frame_info.height; ++y) {
                const uint8_t* src_row = impl_->frame->data[0] + (static_cast<ptrdiff_t>(y) * impl_->frame->linesize[0]);
                uint8_t* dst_row = decoded_frame.luma.data() +
                    (static_cast<size_t>(y) * static_cast<size_t>(frame_info.width));
                std::memcpy(dst_row, src_row, static_cast<size_t>(frame_info.width));
            }

            const bool sws_reconfigure =
                (impl_->sws_ctx == nullptr) ||
                (impl_->sws_src_width != frame_info.width) ||
                (impl_->sws_src_height != frame_info.height) ||
                (impl_->sws_src_format != frame_info.pixel_format);

            if (sws_reconfigure) {
                if (impl_->sws_ctx != nullptr) {
                    sws_freeContext(impl_->sws_ctx);
                    impl_->sws_ctx = nullptr;
                }

                impl_->sws_ctx = sws_getContext(
                    frame_info.width,
                    frame_info.height,
                    static_cast<AVPixelFormat>(frame_info.pixel_format),
                    frame_info.width,
                    frame_info.height,
                    AV_PIX_FMT_BGR24,
                    SWS_BILINEAR,
                    nullptr,
                    nullptr,
                    nullptr);

                impl_->sws_src_width = frame_info.width;
                impl_->sws_src_height = frame_info.height;
                impl_->sws_src_format = frame_info.pixel_format;
            }

            if (impl_->sws_ctx != nullptr) {
                decoded_frame.bgr_stride = frame_info.width * 3;
                decoded_frame.bgr.resize(
                    static_cast<size_t>(decoded_frame.bgr_stride) * static_cast<size_t>(frame_info.height));

                uint8_t* dst_slices[4] = {
                    decoded_frame.bgr.data(),
                    nullptr,
                    nullptr,
                    nullptr,
                };
                int dst_linesizes[4] = {
                    decoded_frame.bgr_stride,
                    0,
                    0,
                    0,
                };

                const int converted_rows = sws_scale(
                    impl_->sws_ctx,
                    impl_->frame->data,
                    impl_->frame->linesize,
                    0,
                    frame_info.height,
                    dst_slices,
                    dst_linesizes);

                if (converted_rows <= 0) {
                    decoded_frame.bgr.clear();
                    decoded_frame.bgr_stride = 0;
                }
            }

            decoded_frames.push_back(std::move(decoded_frame));
        }

        const auto now = std::chrono::steady_clock::now();
        if (impl_->has_last_frame_tp) {
            const double dt_s = std::chrono::duration<double>(now - impl_->last_frame_tp).count();
            if (dt_s > 0.0) {
                const double fps_instant = 1.0 / dt_s;
                const double alpha = 0.2;
                if (stats_.decode_fps_ema <= 0.0) {
                    stats_.decode_fps_ema = fps_instant;
                } else {
                    stats_.decode_fps_ema = (alpha * fps_instant) + ((1.0 - alpha) * stats_.decode_fps_ema);
                }
            }
        }
        impl_->last_frame_tp = now;
        impl_->has_last_frame_tp = true;

        av_frame_unref(impl_->frame);
    }

    const FrameCallback callback_copy = frame_callback_;
    if (callback_copy) {
        for (const DecodedFrameInfo& info : decoded_infos) {
            callback_copy(info);
        }
    }

    const FrameDataCallback frame_data_callback_copy = frame_data_callback_;
    if (frame_data_callback_copy) {
        for (const DecodedFrame& frame : decoded_frames) {
            frame_data_callback_copy(frame);
        }
    }

    return produced_frame;
}

void VideoDecoderFfmpeg::setFrameCallback(FrameCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    frame_callback_ = std::move(callback);
}

void VideoDecoderFfmpeg::setFrameDataCallback(FrameDataCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    frame_data_callback_ = std::move(callback);
}

VideoDecoderFfmpeg::Stats VideoDecoderFfmpeg::getStats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_;
}

void VideoDecoderFfmpeg::resetStats() {
    std::lock_guard<std::mutex> lock(mutex_);
    stats_ = Stats{};
    if (impl_ != nullptr) {
        impl_->has_last_frame_tp = false;
    }
}

bool VideoDecoderFfmpeg::isInitialized() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return impl_ != nullptr && impl_->codec_ctx != nullptr;
}

} // namespace tello
