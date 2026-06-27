#include "tello/video_pipeline_recovery.hpp"

namespace tello {

namespace {

bool shouldRestartLocalPipeline(const TelloClient::VideoRecoveryStatus& status) {
    if (!status.attempted || status.result != ResponseCode::OK) {
        return false;
    }
    return status.stage == "power_streamon"
        || status.stage == "streamon_after_reinit"
        || status.stage == "streamon_after_command";
}

} // namespace

ResponseCode VideoPipelineRecovery::restartLocalPipeline(
    VideoReceiver& receiver,
    VideoStreamAssembler& assembler,
    const VideoPipelineRecoveryOptions& options,
    const VideoPipelineRecoveryHooks& hooks
#ifdef TELLO_HAS_FFMPEG
    ,
    VideoDecoderFfmpeg* decoder,
    bool* decoder_ready
#endif
) {
    receiver.stop();
    assembler.reset();

    if (hooks.before_restart) {
        hooks.before_restart();
    }

#ifdef TELLO_HAS_FFMPEG
    if (decoder != nullptr && options.reset_decoder) {
        decoder->shutdown();
        const bool initialized = decoder->initialize();
        if (decoder_ready != nullptr) {
            *decoder_ready = initialized;
        }
        if (!initialized) {
            return ResponseCode::ERROR;
        }
        decoder->resetStats();
    }
#endif

    const ResponseCode start_rc = receiver.start(
        options.bind_ip,
        options.video_port,
        options.receiver_timeout_ms);
    if (start_rc == ResponseCode::OK && hooks.after_restart) {
        hooks.after_restart();
    }
    return start_rc;
}

VideoPipelineRecoveryResult VideoPipelineRecovery::recoverIfStalled(
    TelloClient& client,
    VideoReceiver& receiver,
    VideoStreamAssembler& assembler,
    int64_t last_packet_age_ms,
    int64_t stall_threshold_ms,
    int64_t recovery_cooldown_ms,
    const VideoPipelineRecoveryOptions& options,
    const VideoPipelineRecoveryHooks& hooks
#ifdef TELLO_HAS_FFMPEG
    ,
    VideoDecoderFfmpeg* decoder,
    bool* decoder_ready
#endif
) {
    VideoPipelineRecoveryResult result;
    result.session = client.recoverVideoStreamIfStalled(
        last_packet_age_ms,
        stall_threshold_ms,
        recovery_cooldown_ms);

    if (result.session.attempted
        && result.session.stage == "battery_probe"
        && !result.session.command_channel_available) {
        result.session = client.recoverAfterPowerCycle();
        result.power_cycle_recovery_used = true;
    }

    if (shouldRestartLocalPipeline(result.session)) {
        result.video_restart_result = restartLocalPipeline(
            receiver,
            assembler,
            options,
            hooks
#ifdef TELLO_HAS_FFMPEG
            ,
            decoder,
            decoder_ready
#endif
        );
        result.video_pipeline_restarted = (result.video_restart_result == ResponseCode::OK);
    }

    return result;
}

VideoPipelineRecoveryResult VideoPipelineRecovery::recoverAfterPowerCycle(
    TelloClient& client,
    VideoReceiver& receiver,
    VideoStreamAssembler& assembler,
    const VideoPipelineRecoveryOptions& options,
    const VideoPipelineRecoveryHooks& hooks
#ifdef TELLO_HAS_FFMPEG
    ,
    VideoDecoderFfmpeg* decoder,
    bool* decoder_ready
#endif
) {
    VideoPipelineRecoveryResult result;
    result.session = client.recoverAfterPowerCycle();
    result.power_cycle_recovery_used = true;

    if (shouldRestartLocalPipeline(result.session)) {
        result.video_restart_result = restartLocalPipeline(
            receiver,
            assembler,
            options,
            hooks
#ifdef TELLO_HAS_FFMPEG
            ,
            decoder,
            decoder_ready
#endif
        );
        result.video_pipeline_restarted = (result.video_restart_result == ResponseCode::OK);
    }

    return result;
}

} // namespace tello
