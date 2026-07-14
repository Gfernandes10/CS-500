#include "tello/control_panel/control_backend.hpp"

namespace tello::control_panel {
namespace {

class StandaloneCommand final : public CommandBackend {
public:
  ResponseCode initialize(const std::string &ip, uint16_t port,
                          uint16_t local) override {
    return client_.initialize(ip, port, local);
  }
  void shutdown() override { client_.shutdown(); }
  ResponseCode enterSdkMode() override { return client_.enterSdkMode(); }
  ResponseCode sendCommandWithResponse(const std::string &c,
                                       std::string &r) override {
    return client_.sendCommandWithResponse(c, r);
  }
  ResponseCode sendCommandNoWait(const std::string &c) override {
    return client_.sendCommandNoWait(c);
  }
  ResponseCode startSdkKeepalive(int32_t ms, const std::string &c) override {
    return client_.startSdkKeepalive(ms, c);
  }
  void stopSdkKeepalive() override { client_.stopSdkKeepalive(); }
  void requestSdkKeepaliveStop() override { client_.requestSdkKeepaliveStop(); }
  bool isSdkKeepaliveRunning() const override {
    return client_.isSdkKeepaliveRunning();
  }
  TelloClient::KeepaliveStats getSdkKeepaliveStats() const override {
    return client_.getSdkKeepaliveStats();
  }
  TelloClient::ConnectionState getConnectionState() const override {
    return client_.getConnectionState();
  }
  bool isInitialized() const override { return client_.isInitialized(); }
  int32_t getLastOutageFailures() const override {
    return client_.getLastOutageFailures();
  }
  TelloClient::ReliabilityConfig getReliabilityConfig() const override {
    return client_.getReliabilityConfig();
  }
  void setReliabilityConfig(const TelloClient::ReliabilityConfig &c) override {
    client_.setReliabilityConfig(c);
  }
  TelloClient::VideoRecoveryStatus
  recoverVideoStreamIfStalled(int64_t a, int64_t b, int64_t c) override {
    return client_.recoverVideoStreamIfStalled(a, b, c);
  }
  TelloClient::VideoRecoveryStatus recoverAfterPowerCycle() override {
    return client_.recoverAfterPowerCycle();
  }
  int64_t getLastCommandMutexWaitMs() const override {
    return client_.getLastCommandMutexWaitMs();
  }
  int64_t getLastCommandClientTotalMs() const override {
    return client_.getLastCommandClientTotalMs();
  }
  int64_t getLastEnsureSdkModeMs() const override {
    return client_.getLastEnsureSdkModeMs();
  }
  int64_t getLastCommandExecutorMs() const override {
    return client_.getLastCommandExecutorMs();
  }
  int64_t getLastCommandExecutorRecvWaitTotalMs() const override {
    return client_.getLastCommandExecutorRecvWaitTotalMs();
  }
  int64_t getLastCommandExecutorInternalTotalMs() const override {
    return client_.getLastCommandExecutorInternalTotalMs();
  }
  int64_t getLastCommandRecoveryMs() const override {
    return client_.getLastCommandRecoveryMs();
  }
  int64_t getLastCommandExecutorCalls() const override {
    return client_.getLastCommandExecutorCalls();
  }
  int64_t getLastCommandRecoveryCount() const override {
    return client_.getLastCommandRecoveryCount();
  }
  std::string getLastCommandInternalAttemptLog() const override {
    return client_.getLastCommandInternalAttemptLog();
  }
  std::shared_ptr<CommandExecutor> getCommandExecutor() const override {
    return client_.getCommandExecutor();
  }

private:
  TelloClient client_;
};

class StandaloneTelemetry final : public TelemetryBackend {
public:
  ResponseCode start(const std::string &ip, uint16_t port,
                     int32_t timeout) override {
    return receiver_.start(ip, port, timeout);
  }
  void stop() override { receiver_.stop(); }
  bool isRunning() const override { return receiver_.isRunning(); }
  TelloState getLatestState() const override {
    return receiver_.getLatestState();
  }
  bool hasReceivedState() const override {
    return receiver_.hasReceivedState();
  }
  StateReceiver::TelemetryStats getTelemetryStats() const override {
    return receiver_.getTelemetryStats();
  }
  void setStateBufferCapacity(size_t n) override {
    receiver_.setStateBufferCapacity(n);
  }
  size_t getStateBufferCapacity() const override {
    return receiver_.getStateBufferCapacity();
  }
  std::vector<StateReceiver::StateSample>
  getBufferedStateSamples() const override {
    return receiver_.getBufferedStateSamples();
  }
  void startStateRecording() override { receiver_.startStateRecording(); }
  void stopStateRecording() override { receiver_.stopStateRecording(); }
  bool isStateRecording() const override {
    return receiver_.isStateRecording();
  }
  size_t getRecordedStateSampleCount() const override {
    return receiver_.getRecordedStateSampleCount();
  }
  size_t getRecordedRcCommandSampleCount() const override {
    return receiver_.getRecordedRcCommandSampleCount();
  }
  void recordRcCommandSample(int a, int b, int c, int d, const std::string &s,
                             ResponseCode r) override {
    receiver_.recordRcCommandSample(a, b, c, d, s, r);
  }
  void clearRecordedStateSamples() override {
    receiver_.clearRecordedStateSamples();
  }
  ResponseCode exportRecordedStateCsv(const std::string &p) const override {
    return receiver_.exportRecordedStateCsv(p);
  }

private:
  StateReceiver receiver_;
};

class StandaloneVideo final : public VideoBackend {
public:
  ResponseCode start(const std::string &url) override {
    return reader_.start(url);
  }
  void stop() override { reader_.stop(); }
  bool isRunning() const override { return reader_.isRunning(); }
  void setFrameCallback(VideoStreamReaderFfmpeg::FrameCallback cb) override {
    reader_.setFrameCallback(std::move(cb));
  }
  VideoStreamReaderFfmpeg::Stats getStats() const override {
    return reader_.getStats();
  }
  std::string getLastError() const override { return reader_.getLastError(); }

private:
  VideoStreamReaderFfmpeg reader_;
};

class StandaloneBackend final : public ControlBackend {
public:
  CommandBackend &command() override { return command_; }
  TelemetryBackend &telemetry() override { return telemetry_; }
  VideoBackend &video() override { return video_; }
  std::string name() const override { return "standalone"; }
  bool hasAuthoritativeLinkQuality() const override { return false; }
  MetricsCollector::LinkQualitySnapshot linkQuality() const override {
    return {};
  }

private:
  StandaloneCommand command_;
  StandaloneTelemetry telemetry_;
  StandaloneVideo video_;
};

} // namespace

std::unique_ptr<ControlBackend> makeStandaloneBackend() {
  return std::make_unique<StandaloneBackend>();
}

} // namespace tello::control_panel
