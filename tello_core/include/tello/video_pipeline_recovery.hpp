#ifndef TELLO_VIDEO_PIPELINE_RECOVERY_HPP
#define TELLO_VIDEO_PIPELINE_RECOVERY_HPP

#include "tello_client.hpp"
#include "video_receiver.hpp"
#include "video_stream_assembler.hpp"

#ifdef TELLO_HAS_FFMPEG
#include "video_decoder_ffmpeg.hpp"
#endif

#include <cstdint>
#include <functional>
#include <string>

namespace tello {

/// Runtime options for restarting the local video receive/decode pipeline.
struct VideoPipelineRecoveryOptions {
    std::string bind_ip = "0.0.0.0";
    uint16_t video_port = 11111;
    int32_t receiver_timeout_ms = 500;
    bool reset_decoder = true;
};

/// Application hooks for clearing UI-specific state and counters around recovery.
struct VideoPipelineRecoveryHooks {
    std::function<void()> before_restart;
    std::function<void()> after_restart;
};

/// Result of a shared video/session recovery attempt.
struct VideoPipelineRecoveryResult {
    TelloClient::VideoRecoveryStatus session;
    ResponseCode video_restart_result = ResponseCode::OK;
    bool video_pipeline_restarted = false;
    bool power_cycle_recovery_used = false;
};

/// Shared recovery service for SDK/video stream recovery and local pipeline restart.
class VideoPipelineRecovery {
public:
    /// Restart local UDP receiver, assembler, and optional decoder state.
    static ResponseCode restartLocalPipeline(
        VideoReceiver& receiver,
        VideoStreamAssembler& assembler,
        const VideoPipelineRecoveryOptions& options = VideoPipelineRecoveryOptions{},
        const VideoPipelineRecoveryHooks& hooks = VideoPipelineRecoveryHooks{}
#ifdef TELLO_HAS_FFMPEG
        ,
        VideoDecoderFfmpeg* decoder = nullptr,
        bool* decoder_ready = nullptr
#endif
    );

    /// Recover a stalled stream, including power-cycle session recovery when needed.
    static VideoPipelineRecoveryResult recoverIfStalled(
        TelloClient& client,
        VideoReceiver& receiver,
        VideoStreamAssembler& assembler,
        int64_t last_packet_age_ms,
        int64_t stall_threshold_ms,
        int64_t recovery_cooldown_ms,
        const VideoPipelineRecoveryOptions& options = VideoPipelineRecoveryOptions{},
        const VideoPipelineRecoveryHooks& hooks = VideoPipelineRecoveryHooks{}
#ifdef TELLO_HAS_FFMPEG
        ,
        VideoDecoderFfmpeg* decoder = nullptr,
        bool* decoder_ready = nullptr
#endif
    );

    /// Recover after a known drone power-cycle and restart the local video pipeline.
    static VideoPipelineRecoveryResult recoverAfterPowerCycle(
        TelloClient& client,
        VideoReceiver& receiver,
        VideoStreamAssembler& assembler,
        const VideoPipelineRecoveryOptions& options = VideoPipelineRecoveryOptions{},
        const VideoPipelineRecoveryHooks& hooks = VideoPipelineRecoveryHooks{}
#ifdef TELLO_HAS_FFMPEG
        ,
        VideoDecoderFfmpeg* decoder = nullptr,
        bool* decoder_ready = nullptr
#endif
    );
};

} // namespace tello

#endif // TELLO_VIDEO_PIPELINE_RECOVERY_HPP
