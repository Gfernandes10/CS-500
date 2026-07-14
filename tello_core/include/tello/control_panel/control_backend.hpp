#ifndef TELLO_CONTROL_PANEL_CONTROL_BACKEND_HPP
#define TELLO_CONTROL_PANEL_CONTROL_BACKEND_HPP

#include "tello/command_executor.hpp"
#include "tello/metrics.hpp"
#include "tello/state_receiver.hpp"
#include "tello/tello_client.hpp"
#include "tello/video_stream_reader_ffmpeg.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace tello::control_panel {

class CommandBackend {
public:
  virtual ~CommandBackend() = default;
  virtual ResponseCode initialize(const std::string &, uint16_t, uint16_t) = 0;
  virtual void shutdown() = 0;
  virtual ResponseCode enterSdkMode() = 0;
  virtual ResponseCode sendCommandWithResponse(const std::string &,
                                               std::string &) = 0;
  virtual ResponseCode sendCommandNoWait(const std::string &) = 0;
  virtual ResponseCode startSdkKeepalive(int32_t,
                                         const std::string & = "battery?") = 0;
  virtual void stopSdkKeepalive() = 0;
  virtual void requestSdkKeepaliveStop() = 0;
  virtual bool isSdkKeepaliveRunning() const = 0;
  virtual TelloClient::KeepaliveStats getSdkKeepaliveStats() const = 0;
  virtual TelloClient::ConnectionState getConnectionState() const = 0;
  virtual bool isInitialized() const = 0;
  virtual int32_t getLastOutageFailures() const = 0;
  virtual TelloClient::ReliabilityConfig getReliabilityConfig() const = 0;
  virtual void setReliabilityConfig(const TelloClient::ReliabilityConfig &) = 0;
  virtual TelloClient::VideoRecoveryStatus
      recoverVideoStreamIfStalled(int64_t, int64_t, int64_t) = 0;
  virtual TelloClient::VideoRecoveryStatus recoverAfterPowerCycle() = 0;
  virtual int64_t getLastCommandMutexWaitMs() const = 0;
  virtual int64_t getLastCommandClientTotalMs() const = 0;
  virtual int64_t getLastEnsureSdkModeMs() const = 0;
  virtual int64_t getLastCommandExecutorMs() const = 0;
  virtual int64_t getLastCommandExecutorRecvWaitTotalMs() const = 0;
  virtual int64_t getLastCommandExecutorInternalTotalMs() const = 0;
  virtual int64_t getLastCommandRecoveryMs() const = 0;
  virtual int64_t getLastCommandExecutorCalls() const = 0;
  virtual int64_t getLastCommandRecoveryCount() const = 0;
  virtual std::string getLastCommandInternalAttemptLog() const = 0;
  virtual std::shared_ptr<CommandExecutor> getCommandExecutor() const = 0;
};

class TelemetryBackend {
public:
  virtual ~TelemetryBackend() = default;
  virtual ResponseCode start(const std::string &, uint16_t, int32_t) = 0;
  virtual void stop() = 0;
  virtual bool isRunning() const = 0;
  virtual TelloState getLatestState() const = 0;
  virtual bool hasReceivedState() const = 0;
  virtual StateReceiver::TelemetryStats getTelemetryStats() const = 0;
  virtual void setStateBufferCapacity(size_t) = 0;
  virtual size_t getStateBufferCapacity() const = 0;
  virtual std::vector<StateReceiver::StateSample>
  getBufferedStateSamples() const = 0;
  virtual void startStateRecording() = 0;
  virtual void stopStateRecording() = 0;
  virtual bool isStateRecording() const = 0;
  virtual size_t getRecordedStateSampleCount() const = 0;
  virtual size_t getRecordedRcCommandSampleCount() const = 0;
  virtual void recordRcCommandSample(int, int, int, int, const std::string &,
                                     ResponseCode) = 0;
  virtual void clearRecordedStateSamples() = 0;
  virtual ResponseCode exportRecordedStateCsv(const std::string &) const = 0;
};

class VideoBackend {
public:
  virtual ~VideoBackend() = default;
  virtual ResponseCode start(const std::string &) = 0;
  virtual void stop() = 0;
  virtual bool isRunning() const = 0;
  virtual void setFrameCallback(VideoStreamReaderFfmpeg::FrameCallback) = 0;
  virtual VideoStreamReaderFfmpeg::Stats getStats() const = 0;
  virtual std::string getLastError() const = 0;
};

class ControlBackend {
public:
  virtual ~ControlBackend() = default;
  virtual CommandBackend &command() = 0;
  virtual TelemetryBackend &telemetry() = 0;
  virtual VideoBackend &video() = 0;
  virtual std::string name() const = 0;
  virtual bool hasAuthoritativeLinkQuality() const = 0;
  virtual MetricsCollector::LinkQualitySnapshot linkQuality() const = 0;
};

std::unique_ptr<ControlBackend> makeStandaloneBackend();
int runControlPanel(int argc, char *argv[],
                    std::unique_ptr<ControlBackend> backend);

} // namespace tello::control_panel

#endif
