#include "tello/tello_client.hpp"
#include "tello/metrics.hpp"
#include "tello/state_receiver.hpp"
#include "tello/video_stream_reader_ffmpeg.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileDialog>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QImage>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QMetaObject>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QProcess>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QSignalBlocker>
#include <QTextEdit>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <QFile>

#ifdef TELLO_CONTROL_PANEL_ROS_NATIVE
#include <geometry_msgs/msg/twist.hpp>
#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/image.hpp>
#include <std_msgs/msg/int32.hpp>
#include <std_msgs/msg/string.hpp>
#include <tello_interfaces/msg/link_quality.hpp>
#include <tello_interfaces/msg/tello_state.hpp>
#endif

#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <csignal>
#include <deque>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

std::string responseCodeToString(tello::ResponseCode rc) {
    switch (rc) {
        case tello::ResponseCode::OK:
            return "OK";
        case tello::ResponseCode::ERROR:
            return "ERROR";
        case tello::ResponseCode::TIMEOUT:
            return "TIMEOUT";
        case tello::ResponseCode::PARSE_ERROR:
            return "PARSE_ERROR";
        default:
            return "UNKNOWN";
    }
}

std::string connectionStateToString(tello::TelloClient::ConnectionState state) {
    switch (state) {
        case tello::TelloClient::ConnectionState::CONNECTED:
            return "CONNECTED";
        case tello::TelloClient::ConnectionState::RECOVERING:
            return "RECOVERING";
        case tello::TelloClient::ConnectionState::DISCONNECTED:
            return "DISCONNECTED";
        default:
            return "UNKNOWN";
    }
}

bool shouldRestartLocalVideoPipeline(const tello::TelloClient::VideoRecoveryStatus& status) {
    if (!status.attempted || status.result != tello::ResponseCode::OK) {
        return false;
    }
    return status.stage == "power_streamon"
        || status.stage == "streamon_after_reinit"
        || status.stage == "streamon_after_command";
}

QString nowClockString() {
    return QDateTime::currentDateTime().toString("HH:mm:ss");
}

QString wifiQualityLabel(const QString& raw) {
    bool ok = false;
    const int v = raw.trimmed().toInt(&ok);
    if (!ok) {
        return "wifi: unknown";
    }
    if (v >= 40) {
        return QString("wifi: good (snr=%1)").arg(v);
    }
    if (v >= 20) {
        return QString("wifi: medium (snr=%1)").arg(v);
    }
    return QString("wifi: weak (snr=%1)").arg(v);
}

bool isCriticalFlightCommand(const std::string& cmd) {
    return cmd == "takeoff" || cmd == "land" || cmd == "emergency";
}

struct ControlProfile {
    QString name = "Default";
    QString input_type = "keyboard";
    int aggression = 20;
    std::map<QString, int> key_by_action;
};

struct RcChannels {
    int a = 0;
    int b = 0;
    int c = 0;
    int d = 0;
};

std::vector<QString> controlProfileActions() {
    return {
        "left",
        "right",
        "forward",
        "back",
        "up",
        "down",
        "yaw_left",
        "yaw_right",
    };
}

QString controlProfileActionLabel(const QString& action) {
    if (action == "left") return "Left / rc a -";
    if (action == "right") return "Right / rc a +";
    if (action == "forward") return "Forward / rc b +";
    if (action == "back") return "Back / rc b -";
    if (action == "up") return "Up / rc c +";
    if (action == "down") return "Down / rc c -";
    if (action == "yaw_left") return "Yaw left / rc d -";
    if (action == "yaw_right") return "Yaw right / rc d +";
    return action;
}

int sequenceToKey(const QKeySequence& sequence) {
    if (sequence.isEmpty()) {
        return 0;
    }
    return sequence[0] & ~Qt::KeyboardModifierMask;
}

ControlProfile defaultControlProfile() {
    ControlProfile profile;
    profile.name = "Default";
    profile.input_type = "keyboard";
    profile.aggression = 20;
    profile.key_by_action = {
        {"left", Qt::Key_A},
        {"right", Qt::Key_D},
        {"forward", Qt::Key_Up},
        {"back", Qt::Key_Down},
        {"up", Qt::Key_W},
        {"down", Qt::Key_S},
        {"yaw_left", Qt::Key_Left},
        {"yaw_right", Qt::Key_Right},
    };
    return profile;
}

double metricFromState(const tello::TelloState& s, const QString& metric) {
    if (metric == "pitch") return static_cast<double>(s.pitch);
    if (metric == "roll") return static_cast<double>(s.roll);
    if (metric == "yaw") return static_cast<double>(s.yaw);
    if (metric == "vgx") return static_cast<double>(s.vgx);
    if (metric == "vgy") return static_cast<double>(s.vgy);
    if (metric == "vgz") return static_cast<double>(s.vgz);
    if (metric == "h") return static_cast<double>(s.h);
    if (metric == "tof") return static_cast<double>(s.tof);
    if (metric == "battery" || metric == "bat") return static_cast<double>(s.bat);
    if (metric == "baro") return s.baro;
    if (metric == "agx") return s.agx;
    if (metric == "agy") return s.agy;
    if (metric == "agz") return s.agz;
    return 0.0;
}

class StatePlotWidget final : public QWidget {
public:
    explicit StatePlotWidget(QWidget* parent = nullptr) : QWidget(parent) {
        setMinimumHeight(220);
    }

    void setSamples(std::vector<tello::StateReceiver::StateSample> samples, QString metric) {
        constexpr size_t kMaxPlotSamples = 300;
        if (samples.size() > kMaxPlotSamples) {
            std::vector<tello::StateReceiver::StateSample> downsampled;
            downsampled.reserve(kMaxPlotSamples);
            for (size_t i = 0; i < kMaxPlotSamples; ++i) {
                const size_t idx = (i * (samples.size() - 1)) / (kMaxPlotSamples - 1);
                downsampled.push_back(samples[idx]);
            }
            samples_ = std::move(downsampled);
        } else {
            samples_ = std::move(samples);
        }
        metric_ = std::move(metric);
        plotted_samples_count_ = samples_.size();
        update();
    }

    int64_t lastPaintDurationMs() const {
        return last_paint_duration_ms_;
    }

    size_t plottedSamplesCount() const {
        return plotted_samples_count_;
    }

protected:
    void paintEvent(QPaintEvent* event) override {
        const auto paint_started_at = std::chrono::steady_clock::now();
        QWidget::paintEvent(event);

        QPainter p(this);
        p.fillRect(rect(), QColor(20, 24, 28));

        const QRectF plot_rect = rect().adjusted(42, 34, -12, -28);
        p.setPen(QPen(QColor(80, 86, 94), 1));
        p.drawRect(plot_rect);

        if (samples_.size() < 2) {
            p.setPen(QPen(Qt::lightGray));
            p.drawText(plot_rect, Qt::AlignCenter, "Waiting telemetry...");
            last_paint_duration_ms_ = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - paint_started_at).count();
            return;
        }

        const int64_t t0 = samples_.front().timestamp_ms;
        const int64_t t1 = samples_.back().timestamp_ms;
        const double dt = static_cast<double>(std::max<int64_t>(1, t1 - t0));

        double y_min = std::numeric_limits<double>::infinity();
        double y_max = -std::numeric_limits<double>::infinity();
        for (const auto& s : samples_) {
            const double v = metricFromState(s.state, metric_);
            y_min = std::min(y_min, v);
            y_max = std::max(y_max, v);
        }

        if (!std::isfinite(y_min) || !std::isfinite(y_max)) {
            last_paint_duration_ms_ = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - paint_started_at).count();
            return;
        }

        if (std::abs(y_max - y_min) < 1e-9) {
            y_max += 1.0;
            y_min -= 1.0;
        }

        const double data_y_min = y_min;
        const double data_y_max = y_max;
        const double current_value = metricFromState(samples_.back().state, metric_);
        const double y_pad = (y_max - y_min) * 0.08;
        y_max += y_pad;
        y_min -= y_pad;

        p.setPen(QPen(QColor(120, 128, 138), 1, Qt::DashLine));
        for (int i = 0; i <= 4; ++i) {
            const double t = static_cast<double>(i) / 4.0;
            const qreal y = plot_rect.bottom() - (t * plot_rect.height());
            p.drawLine(QPointF(plot_rect.left(), y), QPointF(plot_rect.right(), y));

            const double axis_value = y_min + (t * (y_max - y_min));
            p.setPen(QPen(QColor(170, 178, 188), 1));
            p.drawText(QRectF(2, y - 9, plot_rect.left() - 6, 18),
                       Qt::AlignRight | Qt::AlignVCenter,
                       QString::number(axis_value, 'f', 1));
            p.setPen(QPen(QColor(120, 128, 138), 1, Qt::DashLine));
        }

        QPainterPath path;
        for (size_t i = 0; i < samples_.size(); ++i) {
            const double tx = static_cast<double>(samples_[i].timestamp_ms - t0) / dt;
            const double vy = metricFromState(samples_[i].state, metric_);
            const double ty = (vy - y_min) / (y_max - y_min);

            const qreal x = plot_rect.left() + tx * plot_rect.width();
            const qreal y = plot_rect.bottom() - ty * plot_rect.height();
            if (i == 0) {
                path.moveTo(x, y);
            } else {
                path.lineTo(x, y);
            }
        }

        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(QPen(QColor(0, 200, 160), 2));
        p.drawPath(path);

        p.setPen(QPen(Qt::lightGray));
        p.drawText(QPointF(plot_rect.left(), 14),
                   QString("%1  [min=%2 max=%3]")
                       .arg(metric_)
                       .arg(data_y_min, 0, 'f', 2)
                       .arg(data_y_max, 0, 'f', 2));
        p.drawText(QPointF(plot_rect.left(), 30),
                   QString("current=%1").arg(current_value, 0, 'f', 2));
        p.drawText(QPointF(plot_rect.left(), rect().bottom() - 6), "t=0");
        p.drawText(QPointF(plot_rect.right() - 80, rect().bottom() - 6),
                   QString("t=%1 ms").arg(t1 - t0));

        last_paint_duration_ms_ = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - paint_started_at).count();
    }

private:
    std::vector<tello::StateReceiver::StateSample> samples_;
    QString metric_ = "pitch";
    int64_t last_paint_duration_ms_ = 0;
    size_t plotted_samples_count_ = 0;
};

class ControlPanelWidget final : public QWidget {
public:
    explicit ControlPanelWidget(bool ros_mode = false) : ros_mode_(ros_mode) {
        setWindowTitle("Tello Control Panel");
        resize(1220, 900);
        setMinimumSize(720, 520);
        setFocusPolicy(Qt::StrongFocus);

        auto* root = new QVBoxLayout(this);
        metrics_started_at_ = std::chrono::steady_clock::now();

        status_label_ = new QLabel(this);
        link_quality_label_ = new QLabel("link: NO_DATA", this);
        wifi_quality_label_ = new QLabel("wifi: unknown", this);
        battery_status_label_ = new QLabel("battery: --%", this);
        temperature_status_label_ = new QLabel("temp: --/-- C", this);
        top_state_record_dot_label_ = new QLabel(this);
        top_state_record_dot_label_->setFixedSize(12, 12);
        top_state_record_status_label_ = new QLabel("REC OFF", this);
        recording_elapsed_label_ = new QLabel("rec: 0 s", this);

        auto* status_row = new QHBoxLayout();
        status_row->addWidget(status_label_, 1);
        status_row->addStretch(1);
        status_row->addWidget(link_quality_label_);
        status_row->addWidget(wifi_quality_label_);
        status_row->addWidget(battery_status_label_);
        status_row->addWidget(temperature_status_label_);
        status_row->addWidget(top_state_record_dot_label_);
        status_row->addWidget(top_state_record_status_label_);
        status_row->addWidget(recording_elapsed_label_);
        root->addLayout(status_row);

        auto* conn_row = new QHBoxLayout();
        connect_btn_ = new QPushButton("Connect + SDK", this);
        disconnect_btn_ = new QPushButton("Disconnect", this);
        conn_row->addWidget(connect_btn_);
        conn_row->addWidget(disconnect_btn_);
        conn_row->addStretch(1);
        root->addLayout(conn_row);

        buildControlBasicsGroup(root);

        log_view_ = new QTextEdit(this);
        log_view_->setReadOnly(true);
        log_view_->setMinimumHeight(100);
        root->addWidget(log_view_);

        panel_tabs_ = new QTabWidget(this);
        auto make_scroll_page = [this](QWidget* page) {
            auto* scroll = new QScrollArea(this);
            scroll->setWidgetResizable(true);
            scroll->setFrameShape(QFrame::NoFrame);
            scroll->setWidget(page);
            return scroll;
        };

        auto* config_page = new QWidget(this);
        auto* config_layout = new QVBoxLayout(config_page);
        buildLoggingExportConfigurationGroup(config_layout);
        buildControlProfileGroup(config_layout);
        buildReadGroup(config_layout);
        buildSetGroup(config_layout);
        buildMotionGroup(config_layout);
        buildRawGroup(config_layout);
        config_layout->addStretch(1);
        panel_tabs_->addTab(make_scroll_page(config_page), "Config");

        auto* operation_page = new QWidget(this);
        auto* operation_layout = new QVBoxLayout(operation_page);
        buildKeyboardControlGroup(operation_layout);
        buildRcOperationGroup(operation_layout);
        buildStateHistoryGroup(operation_layout);
        buildVisionGroup(operation_layout);
        operation_layout->addStretch(1);
        panel_tabs_->addTab(make_scroll_page(operation_page), "Operation");

        root->addWidget(panel_tabs_, 1);

        auto_refresh_timer_ = new QTimer(this);
        rc_stream_timer_ = new QTimer(this);
        state_view_timer_ = new QTimer(this);
        vision_view_timer_ = new QTimer(this);
        recording_elapsed_timer_ = new QTimer(this);
        recording_elapsed_timer_->setInterval(1000);
        startCommandWorker();

        connect(connect_btn_, &QPushButton::clicked, this, [this]() {
            if (connect_in_progress_.load()) {
                requestConnectCancel("button");
                return;
            }
            (void)connectSdk();
        });
        connect(disconnect_btn_, &QPushButton::clicked, this, [this]() { disconnectSdk(); });

        connect(auto_refresh_timer_, &QTimer::timeout, this, [this]() {
            if (!sdk_ready_ || critical_command_active_) {
                return;
            }
            runReadCommand("battery?", "auto");
            runReadCommand("wifi?", "auto");
        });

        connect(rc_stream_timer_, &QTimer::timeout, this, [this]() { sendRcStreamTick(); });

        connect(state_view_timer_, &QTimer::timeout, this, [this]() { refreshStateHistoryView(); });
        state_view_timer_->start(250);

        connect(vision_view_timer_, &QTimer::timeout, this, [this]() { refreshVisionView(); });
        vision_view_timer_->start(120);

        connect(recording_elapsed_timer_, &QTimer::timeout, this, [this]() {
            updateRecordingElapsedLabel();
        });

        connect(panel_tabs_, &QTabWidget::currentChanged, this, [this](int idx) {
            appendLog(QString("panel switched to %1").arg(idx == 0 ? "Config" : "Operation"));
        });
        connect(qApp, &QApplication::aboutToQuit, this, [this]() {
            performEmergencyShutdownIfConnected("application quit");
        });

        loadControlProfiles();
        qApp->installEventFilter(this);

        appendLog(ros_mode_ ? "Panel ready in ROS mode. Commands are sent to /tello/gui_command."
                            : "Panel ready. Click Connect + SDK first.");
#ifdef TELLO_CONTROL_PANEL_ROS_NATIVE
        if (ros_mode_) {
            startRosNativeInterface();
        }
#else
        if (ros_mode_) {
            appendLog("ROS native topic integration is not compiled; using ROS service calls only.");
        }
#endif
        appendLog("Logging export configuration active.");
        appendLog("Buttons added for control/set/read command families.");
        appendLog("Keyboard control profiles loaded from control_profiles.json.");
        updateStatusLabel();
        updateStateRecordingStatusIndicators();
    }

    ~ControlPanelWidget() override {
#ifdef TELLO_CONTROL_PANEL_ROS_NATIVE
        stopRosNativeInterface();
#endif
        stopConnectWorker();
        performEmergencyShutdownIfConnected("panel destructor");
        qApp->removeEventFilter(this);
        stopKeyboardRcWorker("panel-shutdown");
        stopVisionPipeline();
        setRcStreamingEnabled(false, "panel shutdown");
        stopCommandWorker();
        closeCsv();
        state_receiver_.stop();
        client_.shutdown();
    }

protected:
    void closeEvent(QCloseEvent* event) override {
        performEmergencyShutdownIfConnected("window close");
        QWidget::closeEvent(event);
    }

    bool eventFilter(QObject* watched, QEvent* event) override {
        (void)watched;
        if (!keyboard_control_active_ || critical_command_active_) {
            return false;
        }
        if (event->type() != QEvent::KeyPress && event->type() != QEvent::KeyRelease) {
            return false;
        }

        QWidget* focused = QApplication::focusWidget();
        if (focused != nullptr
            && (qobject_cast<QLineEdit*>(focused) != nullptr
                || qobject_cast<QTextEdit*>(focused) != nullptr
                || qobject_cast<QKeySequenceEdit*>(focused) != nullptr
                || qobject_cast<QSpinBox*>(focused) != nullptr)) {
            return false;
        }

        auto* key_event = static_cast<QKeyEvent*>(event);
        if (key_event->isAutoRepeat()) {
            return true;
        }

        const int key = key_event->key() & ~Qt::KeyboardModifierMask;
        if (!isKeyMappedInActiveProfile(key)) {
            return false;
        }

        if (event->type() == QEvent::KeyPress) {
            active_keyboard_keys_.insert(key);
        } else {
            active_keyboard_keys_.erase(key);
        }

        updateKeyboardControlPreview();
        if (active_keyboard_keys_.empty()) {
            publishKeyboardRcDesired(RcChannels{0, 0, 0, 0});
        } else {
            publishKeyboardRcDesiredFromKeys();
        }
        return true;
    }

private:
    struct CommandTimingResult {
        tello::ResponseCode rc = tello::ResponseCode::ERROR;
        QString response;
        int64_t elapsed_ms = 0;
    };

    struct AsyncCommandResult {
        std::string command;
        QString source;
        tello::ResponseCode rc = tello::ResponseCode::ERROR;
        QString response;
        QString state;
        int64_t elapsed_ms = 0;
        bool critical = false;
        bool was_auto_refresh_active = false;
        bool telemetry_confirmed = false;
        QString telemetry_confirmation_detail;
    };

    struct TelemetryActionConfirmation {
        bool confirmed = false;
        QString detail;
    };

    struct AsyncRecoveryResult {
        tello::TelloClient::VideoRecoveryStatus recovery;
        bool power_cycle_recovery_used = false;
    };

    struct ConnectWorkerResult {
        bool ok = false;
        bool cancelled = false;
        QString message;
    };

    void updateConnectButtons() {
        const bool connecting = connect_in_progress_.load();
        if (connect_btn_ != nullptr) {
            connect_btn_->setText(connecting ? "Cancel Connect" : "Connect + SDK");
        }
        if (disconnect_btn_ != nullptr) {
            disconnect_btn_->setEnabled(true);
        }
    }

    void postConnectLog(const QString& message) {
        QMetaObject::invokeMethod(this, [this, message]() {
            appendLog(message);
        }, Qt::QueuedConnection);
    }

    bool sleepConnectCancelable(int duration_ms) {
        constexpr int kStepMs = 25;
        int elapsed_ms = 0;
        while (elapsed_ms < duration_ms) {
            if (connect_cancel_requested_.load()) {
                return false;
            }
            const int step = std::min(kStepMs, duration_ms - elapsed_ms);
            std::this_thread::sleep_for(std::chrono::milliseconds(step));
            elapsed_ms += step;
        }
        return !connect_cancel_requested_.load();
    }

    void requestConnectCancel(const QString& reason) {
        if (!connect_in_progress_.load()) {
            return;
        }
        connect_cancel_requested_.store(true);
        appendLog("Connect + SDK cancel requested (" + reason + ")");
        updateConnectButtons();
    }

    void stopConnectWorker() {
        connect_cancel_requested_.store(true);
        if (connect_worker_thread_.joinable()) {
            connect_worker_thread_.join();
        }
        connect_in_progress_.store(false);
        connect_cancel_requested_.store(false);
    }

    void finishConnectWorkerOnGui(const ConnectWorkerResult& result) {
        if (connect_worker_thread_.joinable()) {
            connect_worker_thread_.join();
        }

        connect_in_progress_.store(false);
        connect_cancel_requested_.store(false);

        if (result.ok) {
            if (!state_receiver_.isRunning()) {
                const auto state_rc = state_receiver_.start("0.0.0.0", 8890, 1000);
                appendLog("state receiver start => " + QString::fromStdString(responseCodeToString(state_rc)));
                if (state_buffer_capacity_spin_ != nullptr) {
                    state_receiver_.setStateBufferCapacity(
                        static_cast<size_t>(state_buffer_capacity_spin_->value()));
                }
            }
            sdk_ready_ = true;
            emergency_shutdown_sent_ = false;
            const auto keepalive_rc = client_.startSdkKeepalive(5000);
            appendLog("sdk keepalive start => " + QString::fromStdString(responseCodeToString(keepalive_rc)));
            appendLog(result.message.isEmpty() ? "Connect + SDK complete" : result.message);
            startDefaultVisionIfReady("connect");
        } else {
            sdk_ready_ = false;
            client_.shutdown();
            appendLog(result.message.isEmpty()
                          ? "Connect + SDK failed"
                          : result.message);
        }

        updateConnectButtons();
        updateStatusLabel();
    }

    void startConnectWorker() {
        if (connect_in_progress_.exchange(true)) {
            requestConnectCancel("already running");
            return;
        }

        connect_cancel_requested_.store(false);
        sdk_ready_ = false;
        updateConnectButtons();
        updateStatusLabel();
        appendLog("Connect + SDK started");

        connect_worker_thread_ = std::thread([this]() {
            ConnectWorkerResult result;
            const tello::TelloClient::ReliabilityConfig normal_config = client_.getReliabilityConfig();
            tello::TelloClient::ReliabilityConfig connect_config = normal_config;
            connect_config.command_max_attempts = 1;
            connect_config.command_timeout_ms = 800;
            connect_config.command_retry_delay_ms = 0;
            client_.setReliabilityConfig(connect_config);

            auto finish = [&](ConnectWorkerResult finished) {
                client_.setReliabilityConfig(normal_config);
                if (!finished.ok) {
                    client_.shutdown();
                }
                QMetaObject::invokeMethod(this, [this, finished]() {
                    finishConnectWorkerOnGui(finished);
                }, Qt::QueuedConnection);
            };

            constexpr int kConnectAttempts = 6;
            constexpr int kSdkProbesPerAttempt = 3;
            constexpr int kRebindPauseMs = 350;
            constexpr int kSdkProbeGapMs = 500;
            constexpr int kPostInitSettleMs = 900;

            for (int attempt = 1; attempt <= kConnectAttempts; ++attempt) {
                if (connect_cancel_requested_.load()) {
                    result.cancelled = true;
                    result.message = "Connect + SDK cancelled";
                    finish(result);
                    return;
                }

                client_.shutdown();
                if (!sleepConnectCancelable(kRebindPauseMs)) {
                    result.cancelled = true;
                    result.message = "Connect + SDK cancelled";
                    finish(result);
                    return;
                }

                const auto init_rc = client_.initialize("192.168.10.1", 8889, 8889);
                postConnectLog("initialize(attempt=" + QString::number(attempt) + "): "
                               + QString::fromStdString(responseCodeToString(init_rc)));
                if (init_rc != tello::ResponseCode::OK) {
                    const int backoff_ms = 600 + attempt * 400;
                    if (!sleepConnectCancelable(backoff_ms)) {
                        result.cancelled = true;
                        result.message = "Connect + SDK cancelled";
                        finish(result);
                        return;
                    }
                    continue;
                }

                if (!sleepConnectCancelable(kPostInitSettleMs)) {
                    result.cancelled = true;
                    result.message = "Connect + SDK cancelled";
                    finish(result);
                    return;
                }

                for (int probe = 1; probe <= kSdkProbesPerAttempt; ++probe) {
                    if (connect_cancel_requested_.load()) {
                        result.cancelled = true;
                        result.message = "Connect + SDK cancelled";
                        finish(result);
                        return;
                    }

                    const auto sdk_rc = client_.enterSdkMode();
                    postConnectLog("command (enterSdkMode, attempt=" + QString::number(attempt)
                                   + ", probe=" + QString::number(probe) + "): "
                                   + QString::fromStdString(responseCodeToString(sdk_rc)));
                    if (sdk_rc == tello::ResponseCode::OK) {
                        result.ok = true;
                        result.message = "Connect + SDK complete";
                        finish(result);
                        return;
                    }

                    if (probe < kSdkProbesPerAttempt
                        && !sleepConnectCancelable(kSdkProbeGapMs)) {
                        result.cancelled = true;
                        result.message = "Connect + SDK cancelled";
                        finish(result);
                        return;
                    }
                }

                const int backoff_ms = 1000 + attempt * 500;
                if (!sleepConnectCancelable(backoff_ms)) {
                    result.cancelled = true;
                    result.message = "Connect + SDK cancelled";
                    finish(result);
                    return;
                }
            }

            result.ok = false;
            result.message = "Connect + SDK failed after retries. If the drone was just powered on, wait 10-15s and try again.";
            finish(result);
        });
    }

    void startCommandWorker() {
        std::lock_guard<std::mutex> lock(command_worker_mutex_);
        if (command_worker_running_) {
            return;
        }
        command_worker_stop_ = false;
        command_worker_running_ = true;
        command_worker_thread_ = std::thread([this]() { commandWorkerLoop(); });
    }

    void stopCommandWorker() {
        {
            std::lock_guard<std::mutex> lock(command_worker_mutex_);
            command_worker_stop_ = true;
        }
        command_worker_cv_.notify_all();
        if (command_worker_thread_.joinable()) {
            command_worker_thread_.join();
        }
        command_worker_running_ = false;
    }

    void enqueueCommandWorkerTask(std::function<void()> task) {
        {
            std::lock_guard<std::mutex> lock(command_worker_mutex_);
            command_worker_tasks_.push_back(std::move(task));
            command_worker_pending_.fetch_add(1);
        }
        command_worker_cv_.notify_one();
    }

    void commandWorkerLoop() {
        while (true) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(command_worker_mutex_);
                command_worker_cv_.wait(lock, [this]() {
                    return command_worker_stop_ || !command_worker_tasks_.empty();
                });
                if (command_worker_stop_ && command_worker_tasks_.empty()) {
                    break;
                }
                task = std::move(command_worker_tasks_.front());
                command_worker_tasks_.pop_front();
            }

            if (task) {
                task();
            }
            command_worker_pending_.fetch_sub(1);
        }
    }

    void buildReadGroup(QVBoxLayout* root) {
        auto* g = new QGroupBox("Read Commands", this);
        auto* l = new QHBoxLayout(g);

        auto* battery_btn = new QPushButton("battery?", g);
        auto* speed_btn = new QPushButton("speed?", g);
        auto* time_btn = new QPushButton("time?", g);
        auto* wifi_btn = new QPushButton("wifi?", g);
        auto* sdk_btn = new QPushButton("sdk?", g);
        auto* sn_btn = new QPushButton("sn?", g);

        l->addWidget(battery_btn);
        l->addWidget(speed_btn);
        l->addWidget(time_btn);
        l->addWidget(wifi_btn);
        l->addWidget(sdk_btn);
        l->addWidget(sn_btn);
        l->addStretch(1);

        connect(battery_btn, &QPushButton::clicked, this, [this]() { runReadCommand("battery?", "manual"); });
        connect(speed_btn, &QPushButton::clicked, this, [this]() { runReadCommand("speed?", "manual"); });
        connect(time_btn, &QPushButton::clicked, this, [this]() { runReadCommand("time?", "manual"); });
        connect(wifi_btn, &QPushButton::clicked, this, [this]() { runReadCommand("wifi?", "manual"); });
        connect(sdk_btn, &QPushButton::clicked, this, [this]() { runReadCommand("sdk?", "manual"); });
        connect(sn_btn, &QPushButton::clicked, this, [this]() { runReadCommand("sn?", "manual"); });

        root->addWidget(g);
    }

    void buildLoggingExportConfigurationGroup(QVBoxLayout* root) {
        auto* g = new QGroupBox("Logging Export Configuration", this);
        auto* l = new QGridLayout(g);

        csv_path_edit_ = new QLineEdit("control_panel_commands.csv", g);
        state_record_path_edit_ = new QLineEdit("state_recording.csv", g);
        loadPersistentCsvPaths();
        auto* start_logging_btn = new QPushButton("Start Recording", g);
        auto* stop_logging_btn = new QPushButton("Stop Recording", g);
        auto* clear_logging_btn = new QPushButton("Clear Recording", g);
        auto* export_csvs_btn = new QPushButton("Export CSVs", g);
        auto* browse_gui_btn = new QPushButton("Browse...", g);
        auto* browse_state_btn = new QPushButton("Browse...", g);
        state_record_info_label_ = new QLabel("recorded=0", g);

        l->addWidget(new QLabel("GUI metrics csv:", g), 0, 0);
        l->addWidget(csv_path_edit_, 0, 1, 1, 3);
        l->addWidget(browse_gui_btn, 0, 4);

        l->addWidget(new QLabel("state csv:"), 1, 0);
        l->addWidget(state_record_path_edit_, 1, 1, 1, 3);
        l->addWidget(browse_state_btn, 1, 4);

        l->addWidget(start_logging_btn, 2, 0);
        l->addWidget(stop_logging_btn, 2, 1);
        l->addWidget(clear_logging_btn, 2, 2);
        l->addWidget(export_csvs_btn, 2, 3, 1, 2);
        l->addWidget(state_record_info_label_, 3, 0, 1, 5);

        connect(start_logging_btn, &QPushButton::clicked, this, [this]() { startLoggingRecording(); });
        connect(stop_logging_btn, &QPushButton::clicked, this, [this]() { stopLoggingRecording(); });
        connect(clear_logging_btn, &QPushButton::clicked, this, [this]() { clearLoggingRecording(); });
        connect(export_csvs_btn, &QPushButton::clicked, this, [this]() { exportAllCsvs(); });

        connect(browse_gui_btn, &QPushButton::clicked, this, [this]() {
            const QString picked = QFileDialog::getSaveFileName(
                this,
                "GUI Metrics CSV",
                csv_path_edit_->text().trimmed(),
                "CSV (*.csv);;All files (*)");
            if (!picked.isEmpty()) {
                csv_path_edit_->setText(picked);
                savePersistentCsvPaths();
            }
        });

        connect(browse_state_btn, &QPushButton::clicked, this, [this]() {
            const QString picked = QFileDialog::getSaveFileName(
                this,
                "State CSV",
                state_record_path_edit_->text().trimmed(),
                "CSV (*.csv);;All files (*)");
            if (!picked.isEmpty()) {
                state_record_path_edit_->setText(picked);
                savePersistentCsvPaths();
            }
        });

        root->addWidget(g);
    }

    void loadPersistentCsvPaths() {
        QSettings settings;
        if (csv_path_edit_ != nullptr) {
            csv_path_edit_->setText(
                settings.value("logging/gui_metrics_csv_path", csv_path_edit_->text()).toString());
        }
        if (state_record_path_edit_ != nullptr) {
            state_record_path_edit_->setText(
                settings.value("logging/state_csv_path", state_record_path_edit_->text()).toString());
        }
    }

    void savePersistentCsvPaths() const {
        QSettings settings;
        if (csv_path_edit_ != nullptr) {
            settings.setValue("logging/gui_metrics_csv_path", csv_path_edit_->text().trimmed());
        }
        if (state_record_path_edit_ != nullptr) {
            settings.setValue("logging/state_csv_path", state_record_path_edit_->text().trimmed());
        }
        settings.sync();
    }

    void buildControlProfileGroup(QVBoxLayout* root) {
        auto* g = new QGroupBox("Control Profiles", this);
        auto* l = new QGridLayout(g);

        control_profile_combo_ = new QComboBox(g);
        control_profile_name_edit_ = new QLineEdit(g);
        control_profile_input_combo_ = new QComboBox(g);
        control_profile_input_combo_->addItems({"keyboard", "joystick"});
        control_profile_input_combo_->setEnabled(false);
        control_profile_aggression_spin_ = new QSpinBox(g);
        control_profile_aggression_spin_->setRange(1, 100);
        control_profile_aggression_spin_->setValue(20);

        auto* new_btn = new QPushButton("New", g);
        auto* save_btn = new QPushButton("Save", g);
        auto* delete_btn = new QPushButton("Delete", g);

        l->addWidget(new QLabel("profile:"), 0, 0);
        l->addWidget(control_profile_combo_, 0, 1);
        l->addWidget(new_btn, 0, 2);
        l->addWidget(save_btn, 0, 3);
        l->addWidget(delete_btn, 0, 4);

        l->addWidget(new QLabel("name:"), 1, 0);
        l->addWidget(control_profile_name_edit_, 1, 1);
        l->addWidget(new QLabel("input:"), 1, 2);
        l->addWidget(control_profile_input_combo_, 1, 3);
        l->addWidget(new QLabel("RC aggression:"), 2, 0);
        l->addWidget(control_profile_aggression_spin_, 2, 1);

        const std::vector<QString> left_column_actions = {"left", "right", "forward", "back"};
        const std::vector<QString> right_column_actions = {"up", "down", "yaw_left", "yaw_right"};
        for (int i = 0; i < static_cast<int>(left_column_actions.size()); ++i) {
            const QString action = left_column_actions[static_cast<size_t>(i)];
            auto* edit = new QKeySequenceEdit(g);
            control_key_edits_[action] = edit;
            l->addWidget(new QLabel(controlProfileActionLabel(action) + ":"), 3 + i, 0);
            l->addWidget(edit, 3 + i, 1);
        }
        for (int i = 0; i < static_cast<int>(right_column_actions.size()); ++i) {
            const QString action = right_column_actions[static_cast<size_t>(i)];
            auto* edit = new QKeySequenceEdit(g);
            control_key_edits_[action] = edit;
            l->addWidget(new QLabel(controlProfileActionLabel(action) + ":"), 3 + i, 2);
            l->addWidget(edit, 3 + i, 3);
        }

        auto* joystick_note = new QLabel(
            "Joystick profiles use the same schema, but live joystick input is not enabled in this build.",
            g);
        joystick_note->setWordWrap(true);
        l->addWidget(joystick_note, 7, 0, 1, 4);

        connect(control_profile_combo_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int idx) {
            if (idx >= 0) {
                active_control_profile_index_ = idx;
                populateControlProfileEditorFromActive();
                updateKeyboardControlPreview();
            }
        });

        connect(new_btn, &QPushButton::clicked, this, [this]() { createControlProfile(); });
        connect(save_btn, &QPushButton::clicked, this, [this]() { saveControlProfileFromEditor(); });
        connect(delete_btn, &QPushButton::clicked, this, [this]() { deleteActiveControlProfile(); });

        root->addWidget(g);
    }

    void buildSetGroup(QVBoxLayout* root) {
        auto* g = new QGroupBox("Set Speed", this);
        auto* l = new QGridLayout(g);

        speed_spin_ = new QSpinBox(g);
        speed_spin_->setRange(10, 100);
        speed_spin_->setValue(100);
        auto* speed_btn = new QPushButton("Send speed x", g);

        l->addWidget(new QLabel("speed x"), 0, 0);
        l->addWidget(speed_spin_, 0, 1);
        l->addWidget(speed_btn, 0, 2);

        connect(speed_btn, &QPushButton::clicked, this, [this]() {
            runCommandWithResponse("speed " + std::to_string(speed_spin_->value()), "manual");
        });

        root->addWidget(g);
    }

    void buildRcOperationGroup(QVBoxLayout* root) {
        auto* g = new QGroupBox("RC Operation", this);
        auto* l = new QGridLayout(g);

        rc_stream_check_ = new QCheckBox("Continuous RC stream", g);
        rc_stream_interval_ms_ = new QSpinBox(g);
        rc_stream_interval_ms_->setRange(20, 200);
        rc_stream_interval_ms_->setValue(50);

        rc_lr_slider_ = new QSlider(Qt::Horizontal, g);
        rc_fb_slider_ = new QSlider(Qt::Horizontal, g);
        rc_ud_slider_ = new QSlider(Qt::Horizontal, g);
        rc_yaw_slider_ = new QSlider(Qt::Horizontal, g);
        rc_lr_value_ = new QLabel("0", g);
        rc_fb_value_ = new QLabel("0", g);
        rc_ud_value_ = new QLabel("0", g);
        rc_yaw_value_ = new QLabel("0", g);

        auto setup_slider = [](QSlider* s) {
            s->setRange(-100, 100);
            s->setSingleStep(10);
            s->setPageStep(10);
            s->setTickInterval(10);
            s->setTickPosition(QSlider::TicksBelow);
            s->setValue(0);
        };

        setup_slider(rc_lr_slider_);
        setup_slider(rc_fb_slider_);
        setup_slider(rc_ud_slider_);
        setup_slider(rc_yaw_slider_);

        auto* send_once_btn = new QPushButton("Send rc once", g);
        auto* zero_send_btn = new QPushButton("Zero all + send", g);

        l->addWidget(rc_stream_check_, 0, 0, 1, 2);
        l->addWidget(new QLabel("interval ms:"), 0, 2);
        l->addWidget(rc_stream_interval_ms_, 0, 3);

        l->addWidget(new QLabel("LR (a)"), 1, 0);
        l->addWidget(rc_lr_slider_, 1, 1, 1, 2);
        l->addWidget(rc_lr_value_, 1, 3);

        l->addWidget(new QLabel("FB (b)"), 2, 0);
        l->addWidget(rc_fb_slider_, 2, 1, 1, 2);
        l->addWidget(rc_fb_value_, 2, 3);

        l->addWidget(new QLabel("UD (c)"), 3, 0);
        l->addWidget(rc_ud_slider_, 3, 1, 1, 2);
        l->addWidget(rc_ud_value_, 3, 3);

        l->addWidget(new QLabel("Yaw (d)"), 4, 0);
        l->addWidget(rc_yaw_slider_, 4, 1, 1, 2);
        l->addWidget(rc_yaw_value_, 4, 3);

        l->addWidget(send_once_btn, 5, 0, 1, 2);
        l->addWidget(zero_send_btn, 5, 2, 1, 2);

        auto connect_slider = [this](QSlider* s, QLabel* value_label) {
            connect(s, &QSlider::valueChanged, this, [this, s, value_label](int value) {
                const int snapped = ((value >= 0 ? value + 5 : value - 5) / 10) * 10;
                if (snapped != value) {
                    QSignalBlocker blocker(s);
                    s->setValue(snapped);
                }
                if (value_label != nullptr) {
                    value_label->setText(QString::number(s->value()));
                }
            });
        };

        connect_slider(rc_lr_slider_, rc_lr_value_);
        connect_slider(rc_fb_slider_, rc_fb_value_);
        connect_slider(rc_ud_slider_, rc_ud_value_);
        connect_slider(rc_yaw_slider_, rc_yaw_value_);

        connect(send_once_btn, &QPushButton::clicked, this, [this]() {
            forceKeyboardControlOff("send-rc-once");
            runCommandWithResponse(buildRcCommandFromInputs(), "manual");
        });

        connect(zero_send_btn, &QPushButton::clicked, this, [this]() {
            forceKeyboardControlOff("zero-all-send");
            setRcSliders(0, 0, 0, 0);
            runCommandWithResponse("rc 0 0 0 0", "manual");
        });

        connect(rc_stream_check_, &QCheckBox::toggled, this, [this](bool enabled) {
            if (enabled && !sdk_ready_) {
                appendLog("continuous RC requires Connect + SDK first");
                QSignalBlocker blocker(rc_stream_check_);
                rc_stream_check_->setChecked(false);
                return;
            }
            setRcStreamingEnabled(enabled, enabled ? "enabled" : "disabled");
        });

        connect(rc_stream_interval_ms_, qOverload<int>(&QSpinBox::valueChanged), this, [this](int) {
            if (rc_stream_timer_ != nullptr && rc_stream_timer_->isActive()) {
                rc_stream_timer_->start(rc_stream_interval_ms_->value());
            }
        });

        root->addWidget(g);
    }

    void buildKeyboardControlGroup(QVBoxLayout* root) {
        auto* g = new QGroupBox("Keyboard Control", this);
        auto* l = new QGridLayout(g);

        keyboard_control_check_ = new QCheckBox("Enable keyboard RC control", g);
        keyboard_control_profile_label_ = new QLabel("profile: --", g);
        keyboard_control_vector_label_ = new QLabel("rc 0 0 0 0", g);

        l->addWidget(keyboard_control_check_, 0, 0, 1, 2);
        l->addWidget(keyboard_control_profile_label_, 1, 0, 1, 2);
        l->addWidget(keyboard_control_vector_label_, 2, 0, 1, 2);

        connect(keyboard_control_check_, &QCheckBox::toggled, this, [this](bool enabled) {
            setKeyboardControlEnabled(enabled);
        });

        root->addWidget(g);
    }

    QString controlProfilesPath() const {
        return "control_profiles.json";
    }

    QJsonObject controlProfileToJson(const ControlProfile& profile) const {
        QJsonObject root;
        root["name"] = profile.name;
        root["input_type"] = profile.input_type;
        root["aggression"] = profile.aggression;

        QJsonObject bindings;
        for (const auto& action : controlProfileActions()) {
            const auto it = profile.key_by_action.find(action);
            bindings[action] = it != profile.key_by_action.end() ? it->second : 0;
        }
        root["key_bindings"] = bindings;
        return root;
    }

    ControlProfile controlProfileFromJson(const QJsonObject& obj) const {
        ControlProfile profile = defaultControlProfile();
        profile.name = obj.value("name").toString(profile.name).trimmed();
        if (profile.name.isEmpty()) {
            profile.name = "Default";
        }
        profile.input_type = obj.value("input_type").toString("keyboard");
        profile.aggression = std::clamp(obj.value("aggression").toInt(20), 1, 100);

        const QJsonObject bindings = obj.value("key_bindings").toObject();
        for (const auto& action : controlProfileActions()) {
            profile.key_by_action[action] = bindings.value(action).toInt(profile.key_by_action[action]);
        }
        return profile;
    }

    void loadControlProfiles() {
        control_profiles_.clear();

        QFile file(controlProfilesPath());
        if (file.open(QIODevice::ReadOnly)) {
            const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
            const QJsonArray profiles = doc.object().value("profiles").toArray();
            for (const auto& value : profiles) {
                if (value.isObject()) {
                    control_profiles_.push_back(controlProfileFromJson(value.toObject()));
                }
            }
        }

        if (control_profiles_.empty()) {
            control_profiles_.push_back(defaultControlProfile());
            saveControlProfiles();
        }

        active_control_profile_index_ = std::clamp(active_control_profile_index_, 0, static_cast<int>(control_profiles_.size()) - 1);
        refreshControlProfileCombo();
        populateControlProfileEditorFromActive();
        updateKeyboardControlPreview();
    }

    void saveControlProfiles() {
        QJsonObject root;
        QJsonArray profiles;
        for (const auto& profile : control_profiles_) {
            profiles.append(controlProfileToJson(profile));
        }
        root["version"] = 1;
        root["profiles"] = profiles;

        QFile file(controlProfilesPath());
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            appendLog("failed to save control profiles: " + controlProfilesPath());
            return;
        }
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    }

    void refreshControlProfileCombo() {
        if (control_profile_combo_ == nullptr) {
            return;
        }
        QSignalBlocker blocker(control_profile_combo_);
        control_profile_combo_->clear();
        for (const auto& profile : control_profiles_) {
            control_profile_combo_->addItem(profile.name);
        }
        control_profile_combo_->setCurrentIndex(active_control_profile_index_);
    }

    ControlProfile activeControlProfile() const {
        if (control_profiles_.empty()) {
            return defaultControlProfile();
        }
        const int idx = std::clamp(active_control_profile_index_, 0, static_cast<int>(control_profiles_.size()) - 1);
        return control_profiles_[static_cast<size_t>(idx)];
    }

    void populateControlProfileEditorFromActive() {
        if (control_profiles_.empty()) {
            return;
        }
        const ControlProfile profile = activeControlProfile();
        if (control_profile_name_edit_ != nullptr) {
            control_profile_name_edit_->setText(profile.name);
        }
        if (control_profile_input_combo_ != nullptr) {
            control_profile_input_combo_->setCurrentText(profile.input_type);
        }
        if (control_profile_aggression_spin_ != nullptr) {
            control_profile_aggression_spin_->setValue(profile.aggression);
        }
        for (auto& item : control_key_edits_) {
            const auto it = profile.key_by_action.find(item.first);
            item.second->setKeySequence(QKeySequence(it != profile.key_by_action.end() ? it->second : 0));
        }
    }

    ControlProfile controlProfileFromEditor() const {
        ControlProfile profile = activeControlProfile();
        if (control_profile_name_edit_ != nullptr) {
            profile.name = control_profile_name_edit_->text().trimmed();
        }
        if (profile.name.isEmpty()) {
            profile.name = "Default";
        }
        profile.input_type = control_profile_input_combo_ != nullptr
            ? control_profile_input_combo_->currentText()
            : "keyboard";
        profile.aggression = control_profile_aggression_spin_ != nullptr
            ? control_profile_aggression_spin_->value()
            : 20;
        for (const auto& item : control_key_edits_) {
            profile.key_by_action[item.first] = sequenceToKey(item.second->keySequence());
        }
        return profile;
    }

    void createControlProfile() {
        bool accepted = false;
        const QString name = QInputDialog::getText(
            this,
            "New Control Profile",
            "Profile name:",
            QLineEdit::Normal,
            "New Profile",
            &accepted).trimmed();
        if (!accepted || name.isEmpty()) {
            return;
        }

        ControlProfile profile = activeControlProfile();
        profile.name = name;
        control_profiles_.push_back(profile);
        active_control_profile_index_ = static_cast<int>(control_profiles_.size()) - 1;
        refreshControlProfileCombo();
        populateControlProfileEditorFromActive();
        saveControlProfiles();
        appendLog("control profile created: " + name);
    }

    void saveControlProfileFromEditor() {
        if (control_profiles_.empty()) {
            control_profiles_.push_back(defaultControlProfile());
        }
        active_control_profile_index_ = std::clamp(active_control_profile_index_, 0, static_cast<int>(control_profiles_.size()) - 1);
        control_profiles_[static_cast<size_t>(active_control_profile_index_)] = controlProfileFromEditor();
        refreshControlProfileCombo();
        saveControlProfiles();
        updateKeyboardControlPreview();
        appendLog("control profile saved: " + activeControlProfile().name);
    }

    void deleteActiveControlProfile() {
        if (control_profiles_.size() <= 1) {
            appendLog("at least one control profile must exist");
            return;
        }
        const QString removed = activeControlProfile().name;
        control_profiles_.erase(control_profiles_.begin() + active_control_profile_index_);
        active_control_profile_index_ = std::max(0, active_control_profile_index_ - 1);
        refreshControlProfileCombo();
        populateControlProfileEditorFromActive();
        saveControlProfiles();
        updateKeyboardControlPreview();
        appendLog("control profile deleted: " + removed);
    }

    bool isKeyMappedInActiveProfile(int key) const {
        const ControlProfile profile = activeControlProfile();
        for (const auto& item : profile.key_by_action) {
            if (item.second == key) {
                return true;
            }
        }
        return false;
    }

    RcChannels rcChannelsFromKeyboard() const {
        const ControlProfile profile = activeControlProfile();
        const int v = profile.aggression;
        RcChannels rc;

        auto pressed = [this, &profile](const QString& action) {
            const auto it = profile.key_by_action.find(action);
            return it != profile.key_by_action.end() && active_keyboard_keys_.count(it->second) > 0;
        };

        if (pressed("left")) rc.a -= v;
        if (pressed("right")) rc.a += v;
        if (pressed("forward")) rc.b += v;
        if (pressed("back")) rc.b -= v;
        if (pressed("up")) rc.c += v;
        if (pressed("down")) rc.c -= v;
        if (pressed("yaw_left")) rc.d -= v;
        if (pressed("yaw_right")) rc.d += v;

        rc.a = std::clamp(rc.a, -100, 100);
        rc.b = std::clamp(rc.b, -100, 100);
        rc.c = std::clamp(rc.c, -100, 100);
        rc.d = std::clamp(rc.d, -100, 100);
        return rc;
    }

    std::string buildRcCommand(const RcChannels& rc) const {
        return "rc "
            + std::to_string(rc.a) + " "
            + std::to_string(rc.b) + " "
            + std::to_string(rc.c) + " "
            + std::to_string(rc.d);
    }

    std::string buildRcCommandFromKeyboard() const {
        return buildRcCommand(rcChannelsFromKeyboard());
    }

    int64_t steadyNowMs() const {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    void recordRcLinkSend(const RcChannels& rc) {
        const int64_t now_ms = steadyNowMs();
        const int64_t previous_ms = last_rc_send_steady_ms_.exchange(now_ms);
        if (previous_ms > 0) {
            const int64_t gap_ms = now_ms - previous_ms;
            rc_packet_gap_ms_.store(gap_ms);
            if (gap_ms > 1000) {
                rc_blackout_count_.fetch_add(1);
            }
        } else {
            rc_packet_gap_ms_.store(-1);
        }

        if (rc.a != 0 || rc.b != 0 || rc.c != 0 || rc.d != 0) {
            last_nonzero_rc_steady_ms_.store(now_ms);
        }
    }

    void publishKeyboardRcDesired(const RcChannels& rc) {
        keyboard_rc_desired_a_.store(rc.a);
        keyboard_rc_desired_b_.store(rc.b);
        keyboard_rc_desired_c_.store(rc.c);
        keyboard_rc_desired_d_.store(rc.d);
        keyboard_rc_last_update_ms_.store(steadyNowMs());
    }

    void publishKeyboardRcDesiredFromKeys() {
        publishKeyboardRcDesired(rcChannelsFromKeyboard());
    }

    void startKeyboardRcWorker() {
        if (keyboard_rc_worker_running_.load()) {
            publishKeyboardRcDesiredFromKeys();
            return;
        }

        keyboard_rc_worker_stop_.store(false);
        keyboard_rc_worker_running_.store(true);
        publishKeyboardRcDesiredFromKeys();
        keyboard_rc_worker_thread_ = std::thread([this]() { keyboardRcWorkerLoop(); });
    }

    void stopKeyboardRcWorker(const std::string& source) {
        if (!keyboard_rc_worker_running_.load()) {
            return;
        }

        keyboard_rc_worker_stop_.store(true);
        if (keyboard_rc_worker_thread_.joinable()) {
            keyboard_rc_worker_thread_.join();
        }
        keyboard_rc_worker_running_.store(false);

        if (sdk_ready_) {
            (void)sendRcCommandNoWait("rc 0 0 0 0", source);
        }
    }

    void keyboardRcWorkerLoop() {
        constexpr int64_t kRcWorkerIntervalMs = 50;
        constexpr int64_t kRcInputStaleMs = 250;

        while (!keyboard_rc_worker_stop_.load()) {
            const int64_t now_ms = steadyNowMs();
            const int64_t last_update_ms = keyboard_rc_last_update_ms_.load();
            RcChannels rc{
                keyboard_rc_desired_a_.load(),
                keyboard_rc_desired_b_.load(),
                keyboard_rc_desired_c_.load(),
                keyboard_rc_desired_d_.load()
            };

            if (last_update_ms <= 0 || now_ms - last_update_ms > kRcInputStaleMs) {
                rc = RcChannels{0, 0, 0, 0};
            }

            const std::string cmd = buildRcCommand(rc);
            (void)sendRcCommandNoWait(cmd, "keyboard-worker");

            std::this_thread::sleep_for(std::chrono::milliseconds(kRcWorkerIntervalMs));
        }
    }

    void updateKeyboardControlPreview() {
        const ControlProfile profile = activeControlProfile();
        if (keyboard_control_profile_label_ != nullptr) {
            keyboard_control_profile_label_->setText(
                QString("profile: %1 | aggression=%2").arg(profile.name).arg(profile.aggression));
        }
        if (keyboard_control_vector_label_ != nullptr) {
            keyboard_control_vector_label_->setText(QString::fromStdString(buildRcCommandFromKeyboard()));
        }
    }

    void setKeyboardControlEnabled(bool enabled) {
        if (enabled && !sdk_ready_) {
            appendLog("keyboard control requires Connect + SDK first");
            if (keyboard_control_check_ != nullptr) {
                QSignalBlocker blocker(keyboard_control_check_);
                keyboard_control_check_->setChecked(false);
            }
            return;
        }

        keyboard_control_active_ = enabled;
        active_keyboard_keys_.clear();
        updateKeyboardControlPreview();

        if (enabled) {
            client_.requestSdkKeepaliveStop();
            appendLog("sdk keepalive paused while keyboard RC worker is active");
            startKeyboardRcWorker();
            if (rc_stream_timer_ != nullptr) {
                rc_stream_timer_->start(rc_stream_interval_ms_ != nullptr ? rc_stream_interval_ms_->value() : 50);
            }
            appendLog("keyboard control ON: " + activeControlProfile().name);
            writeEventMetricsRow(QString("keyboard-control:on:") + activeControlProfile().name);
            setFocus();
            return;
        }

        stopKeyboardRcWorker("keyboard-neutral");
        if (rc_stream_timer_ != nullptr && (rc_stream_check_ == nullptr || !rc_stream_check_->isChecked())) {
            rc_stream_timer_->stop();
        }
        const auto keepalive_rc = client_.startSdkKeepalive(5000);
        appendLog("sdk keepalive restart after keyboard RC => "
                  + QString::fromStdString(responseCodeToString(keepalive_rc)));
        appendLog("keyboard control OFF");
        writeEventMetricsRow("keyboard-control:off");
    }

    void forceKeyboardControlOff(const QString& reason) {
        const bool was_enabled =
            keyboard_control_active_
            || (keyboard_control_check_ != nullptr && keyboard_control_check_->isChecked());
        if (!was_enabled) {
            return;
        }

        if (keyboard_control_check_ != nullptr) {
            QSignalBlocker blocker(keyboard_control_check_);
            keyboard_control_check_->setChecked(false);
        }
        keyboard_control_active_ = false;
        active_keyboard_keys_.clear();
        updateKeyboardControlPreview();

        stopKeyboardRcWorker(("keyboard-neutral-" + reason).toStdString());
        if (rc_stream_timer_ != nullptr && (rc_stream_check_ == nullptr || !rc_stream_check_->isChecked())) {
            rc_stream_timer_->stop();
        }
        if (sdk_ready_) {
            const auto keepalive_rc = client_.startSdkKeepalive(5000);
            appendLog("sdk keepalive restart after keyboard forced-off => "
                      + QString::fromStdString(responseCodeToString(keepalive_rc)));
        }

        appendLog("keyboard control forced OFF (" + reason + ")");
        writeEventMetricsRow("keyboard-control:forced-off:" + reason);
    }

    bool confirmTakeoff() {
        const auto answer = QMessageBox::question(
            this,
            "Confirm takeoff",
            "Do you really want to send takeoff?",
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);
        if (answer != QMessageBox::Yes) {
            appendLog("takeoff cancelled");
            return false;
        }
        appendLog("takeoff confirmed by user");
        return true;
    }

    void buildControlBasicsGroup(QVBoxLayout* root) {
        auto* g = new QGroupBox("Control Commands (basic)", this);
        auto* l = new QHBoxLayout(g);

        auto* takeoff_btn = new QPushButton("takeoff", g);
        auto* land_btn = new QPushButton("land", g);
        auto* emergency_btn = new QPushButton("emergency", g);
        auto* streamon_btn = new QPushButton("streamon", g);
        auto* streamoff_btn = new QPushButton("streamoff", g);
        auto* stop_btn = new QPushButton("hover (stop)", g);
        emergency_btn->setStyleSheet(
            "QPushButton { background-color: #EE5858; color: white; font-weight: 600; }"
            "QPushButton:hover { background-color: #F06A6A; }"
            "QPushButton:pressed { background-color: #D94A4A; }");

        l->addWidget(takeoff_btn);
        l->addWidget(land_btn);
        l->addWidget(emergency_btn);
        l->addWidget(streamon_btn);
        l->addWidget(streamoff_btn);
        l->addWidget(stop_btn);
        l->addStretch(1);

        connect(takeoff_btn, &QPushButton::clicked, this, [this]() {
            if (confirmTakeoff()) {
                runCommandWithResponse("takeoff", "manual");
            }
        });
        connect(land_btn, &QPushButton::clicked, this, [this]() { runCommandWithResponse("land", "manual"); });
        connect(emergency_btn, &QPushButton::clicked, this, [this]() { runCommandWithResponse("emergency", "manual"); });
        connect(streamon_btn, &QPushButton::clicked, this, [this]() { runCommandWithResponse("streamon", "manual"); });
        connect(streamoff_btn, &QPushButton::clicked, this, [this]() { runCommandWithResponse("streamoff", "manual"); });
        connect(stop_btn, &QPushButton::clicked, this, [this]() {
            forceKeyboardControlOff("hover-stop");
            runCommandWithResponse("stop", "manual");
        });

        root->addWidget(g);
    }

    void buildMotionGroup(QVBoxLayout* root) {
        auto* g = new QGroupBox("Control Commands (movement/rotation/flip)", this);
        auto* l = new QGridLayout(g);

        up_cm_ = new QSpinBox(g); up_cm_->setRange(20, 500); up_cm_->setValue(30);
        down_cm_ = new QSpinBox(g); down_cm_->setRange(20, 500); down_cm_->setValue(30);
        left_cm_ = new QSpinBox(g); left_cm_->setRange(20, 500); left_cm_->setValue(30);
        right_cm_ = new QSpinBox(g); right_cm_->setRange(20, 500); right_cm_->setValue(30);
        forward_cm_ = new QSpinBox(g); forward_cm_->setRange(20, 500); forward_cm_->setValue(30);
        back_cm_ = new QSpinBox(g); back_cm_->setRange(20, 500); back_cm_->setValue(30);

        cw_deg_ = new QSpinBox(g); cw_deg_->setRange(1, 360); cw_deg_->setValue(45);
        ccw_deg_ = new QSpinBox(g); ccw_deg_->setRange(1, 360); ccw_deg_->setValue(45);

        flip_dir_ = new QLineEdit("l", g);
        flip_dir_->setMaxLength(1);

        auto addRow = [this, g, l](
            int row,
            int col,
            const QString& label,
            QSpinBox* spin,
            const QString& cmd_prefix
        ) {
            auto* btn = new QPushButton("Send " + cmd_prefix + " x", g);
            l->addWidget(new QLabel(label), row, col);
            l->addWidget(spin, row, col + 1);
            l->addWidget(btn, row, col + 2);
            connect(btn, &QPushButton::clicked, this, [this, spin, cmd_prefix]() {
                runCommandWithResponse(cmd_prefix.toStdString() + " " + std::to_string(spin->value()), "manual");
            });
        };

        addRow(0, 0, "left x", left_cm_, "left");
        addRow(1, 0, "right x", right_cm_, "right");
        addRow(2, 0, "forward x", forward_cm_, "forward");
        addRow(3, 0, "back x", back_cm_, "back");

        addRow(0, 3, "up x", up_cm_, "up");
        addRow(1, 3, "down x", down_cm_, "down");
        addRow(2, 3, "cw x", cw_deg_, "cw");
        addRow(3, 3, "ccw x", ccw_deg_, "ccw");

        auto* flip_btn = new QPushButton("Send flip x", g);
        l->addWidget(new QLabel("flip x"), 4, 3);
        l->addWidget(flip_dir_, 4, 4);
        l->addWidget(flip_btn, 4, 5);
        connect(flip_btn, &QPushButton::clicked, this, [this]() {
            const QString d = flip_dir_->text().trimmed().toLower();
            if (!(d == "l" || d == "r" || d == "f" || d == "b")) {
                appendLog("flip dir must be one of l/r/f/b");
                return;
            }
            runCommandWithResponse("flip " + d.toStdString(), "manual");
        });

        root->addWidget(g);
    }

    void buildRawGroup(QVBoxLayout* root) {
        auto* g = new QGroupBox("Raw Command", this);
        auto* l = new QHBoxLayout(g);
        raw_command_edit_ = new QLineEdit(g);
        raw_command_edit_->setPlaceholderText("Ex: mon, moff, mdirection 1, wifi?, speed 40");
        auto* raw_btn = new QPushButton("Send Raw", g);
        l->addWidget(raw_command_edit_, 1);
        l->addWidget(raw_btn);

        connect(raw_btn, &QPushButton::clicked, this, [this]() { runRaw(); });
        connect(raw_command_edit_, &QLineEdit::returnPressed, this, [this]() { runRaw(); });

        root->addWidget(g);
    }

    void buildStateHistoryGroup(QVBoxLayout* root) {
        auto* g = new QGroupBox("State History (Telemetry)", this);
        auto* l = new QGridLayout(g);

        state_buffer_capacity_spin_ = new QSpinBox(g);
        state_buffer_capacity_spin_->setRange(50, 20000);
        state_buffer_capacity_spin_->setValue(300);
        auto* apply_capacity_btn = new QPushButton("Apply buffer", g);

        state_metric_combo_ = new QComboBox(g);
        state_metric_combo_->addItems({"pitch", "roll", "yaw", "vgx", "vgy", "vgz", "h", "tof", "battery", "baro", "agx", "agy", "agz"});

        state_buffer_info_label_ = new QLabel("buffer=0", g);
        state_record_dot_label_ = new QLabel(g);
        state_record_dot_label_->setFixedSize(12, 12);
        state_record_status_label_ = new QLabel("REC OFF", g);

        state_plot_widget_ = new StatePlotWidget(g);

        l->addWidget(new QLabel("buffer size:"), 0, 0);
        l->addWidget(state_buffer_capacity_spin_, 0, 1);
        l->addWidget(apply_capacity_btn, 0, 2);
        l->addWidget(new QLabel("metric:"), 0, 3);
        l->addWidget(state_metric_combo_, 0, 4);
        l->addWidget(state_record_dot_label_, 0, 5);
        l->addWidget(state_record_status_label_, 0, 6);

        l->addWidget(state_plot_widget_, 1, 0, 1, 7);
        l->addWidget(state_buffer_info_label_, 2, 0, 1, 7);

        connect(apply_capacity_btn, &QPushButton::clicked, this, [this]() {
            state_receiver_.setStateBufferCapacity(static_cast<size_t>(state_buffer_capacity_spin_->value()));
            appendLog(QString("state buffer size set to %1").arg(state_buffer_capacity_spin_->value()));
        });

        connect(state_metric_combo_, &QComboBox::currentTextChanged, this, [this](const QString& metric) {
            appendLog("plot metric changed => " + metric);
            writeEventMetricsRow("plot_metric_changed:" + metric);
        });

        updateStateRecordingStatusIndicators();
        root->addWidget(g);
    }

    void buildVisionGroup(QVBoxLayout* root) {
        auto* g = new QGroupBox("Vision", this);
        auto* outer = new QHBoxLayout(g);

        auto* left = new QVBoxLayout();
        vision_frame_label_ = new QLabel("Vision not started", g);
        vision_frame_label_->setMinimumSize(560, 315);
        vision_frame_label_->setAlignment(Qt::AlignCenter);
        vision_frame_label_->setStyleSheet("QLabel { background-color: #111; color: #ddd; border: 1px solid #333; }");
        left->addWidget(vision_frame_label_);
        outer->addLayout(left, 2);

        auto* right_host = new QWidget(g);
        auto* right = new QVBoxLayout(right_host);
        vision_status_label_ = new QLabel("status: idle", right_host);
        vision_rx_label_ = new QLabel("stream packets: 0", right_host);
        vision_nal_label_ = new QLabel("backend: ffmpeg stream", right_host);
        vision_fps_label_ = new QLabel("decode fps: 0.00", right_host);
        vision_size_label_ = new QLabel("size: 0x0", right_host);
        vision_decode_err_label_ = new QLabel("decode errors: 0", right_host);
        vision_frame_info_label_ = new QLabel("frames: 0 | keyframes: 0", right_host);

        right->addWidget(vision_status_label_);
        right->addWidget(vision_rx_label_);
        right->addWidget(vision_nal_label_);
        right->addWidget(vision_fps_label_);
        right->addWidget(vision_size_label_);
        right->addWidget(vision_decode_err_label_);
        right->addWidget(vision_frame_info_label_);

        vision_start_btn_ = new QPushButton("Start View", right_host);
        vision_stop_btn_ = new QPushButton("Stop View", right_host);
        vision_pause_btn_ = new QPushButton("Pause", right_host);
        vision_snapshot_btn_ = new QPushButton("Snapshot", right_host);
        vision_overlay_btn_ = new QPushButton("Overlay: ON", right_host);

        right->addWidget(vision_start_btn_);
        right->addWidget(vision_stop_btn_);
        right->addWidget(vision_pause_btn_);
        right->addWidget(vision_snapshot_btn_);
        right->addWidget(vision_overlay_btn_);
        right->addStretch(1);

        connect(vision_start_btn_, &QPushButton::clicked, this, [this]() {
            (void)startVisionWithStreamOn();
        });
        connect(vision_stop_btn_, &QPushButton::clicked, this, [this]() {
            stopVisionWithStreamOff();
        });
        connect(vision_pause_btn_, &QPushButton::clicked, this, [this]() {
            toggleVisionPause();
        });
        connect(vision_overlay_btn_, &QPushButton::clicked, this, [this]() {
            vision_overlay_enabled_ = !vision_overlay_enabled_;
            if (vision_overlay_btn_ != nullptr) {
                vision_overlay_btn_->setText(vision_overlay_enabled_ ? "Overlay: ON" : "Overlay: OFF");
            }
            appendLog(QString("vision overlay => %1").arg(vision_overlay_enabled_ ? "ON" : "OFF"));
        });
        connect(vision_snapshot_btn_, &QPushButton::clicked, this, [this]() {
            saveVisionSnapshot();
        });

#ifndef TELLO_HAS_FFMPEG
        vision_start_btn_->setEnabled(false);
        vision_stop_btn_->setEnabled(false);
        vision_pause_btn_->setEnabled(false);
        vision_snapshot_btn_->setEnabled(false);
        vision_overlay_btn_->setEnabled(false);
        vision_status_label_->setText("status: FFmpeg backend not available");
        vision_frame_label_->setText("FFmpeg backend not available. Reconfigure with TELLO_ENABLE_FFMPEG=ON.");
#endif

        outer->addWidget(right_host, 1);
        root->addWidget(g);
    }

#ifdef TELLO_CONTROL_PANEL_ROS_NATIVE
    void startRosNativeInterface() {
        if (ros_native_started_) {
            return;
        }
        if (!rclcpp::ok()) {
            appendLog("ROS native topic integration unavailable: rclcpp is not initialized.");
            return;
        }

        ros_node_ = std::make_shared<rclcpp::Node>("tello_control_panel");
        ros_manual_cmd_vel_pub_ = ros_node_->create_publisher<geometry_msgs::msg::Twist>("/tello/manual_cmd_vel", 10);

        ros_state_sub_ = ros_node_->create_subscription<tello_interfaces::msg::TelloState>(
            "/tello/state", 10,
            [this](const tello_interfaces::msg::TelloState::SharedPtr msg) {
                QMetaObject::invokeMethod(this, [this, msg]() { applyRosState(*msg); }, Qt::QueuedConnection);
            });
        ros_battery_sub_ = ros_node_->create_subscription<std_msgs::msg::Int32>(
            "/tello/battery", 10,
            [this](const std_msgs::msg::Int32::SharedPtr msg) {
                QMetaObject::invokeMethod(this, [this, value = msg->data]() {
                    if (battery_status_label_ != nullptr) {
                        battery_status_label_->setText(QString("battery: %1%").arg(value));
                    }
                }, Qt::QueuedConnection);
            });
        ros_connection_sub_ = ros_node_->create_subscription<std_msgs::msg::String>(
            "/tello/connection_state", 10,
            [this](const std_msgs::msg::String::SharedPtr msg) {
                QMetaObject::invokeMethod(this, [this, value = QString::fromStdString(msg->data)]() {
                    ros_connection_state_ = value;
                    sdk_ready_ = value.contains("CONNECTED");
                    updateStatusLabel();
                }, Qt::QueuedConnection);
            });
        ros_link_quality_sub_ = ros_node_->create_subscription<tello_interfaces::msg::LinkQuality>(
            "/tello/link_quality", 10,
            [this](const tello_interfaces::msg::LinkQuality::SharedPtr msg) {
                QMetaObject::invokeMethod(this, [this, msg]() { applyRosLinkQuality(*msg); }, Qt::QueuedConnection);
            });
        ros_image_sub_ = ros_node_->create_subscription<sensor_msgs::msg::Image>(
            "/tello/video/image_raw", 5,
            [this](const sensor_msgs::msg::Image::SharedPtr msg) {
                QMetaObject::invokeMethod(this, [this, msg]() { applyRosImage(*msg); }, Qt::QueuedConnection);
            });

        ros_native_started_ = true;
        ros_spin_thread_ = std::thread([this]() {
            rclcpp::spin(ros_node_);
        });
        appendLog("ROS native topics active: state, battery, connection, link quality, image, manual_cmd_vel.");
    }

    void stopRosNativeInterface() {
        if (!ros_native_started_) {
            return;
        }
        if (ros_node_) {
            ros_node_->get_node_base_interface()->get_context()->shutdown("control panel closing");
        }
        if (ros_spin_thread_.joinable()) {
            ros_spin_thread_.join();
        }
        ros_native_started_ = false;
    }

    void applyRosState(const tello_interfaces::msg::TelloState& msg) {
        tello::TelloState state{};
        state.pitch = msg.pitch;
        state.roll = msg.roll;
        state.yaw = msg.yaw;
        state.vgx = msg.vgx;
        state.vgy = msg.vgy;
        state.vgz = msg.vgz;
        state.templ = msg.templ;
        state.temph = msg.temph;
        state.tof = msg.tof;
        state.h = msg.h;
        state.bat = msg.bat;
        state.baro = msg.baro;
        state.time = msg.time;
        state.agx = msg.agx;
        state.agy = msg.agy;
        state.agz = msg.agz;
        state.mid = msg.mid;
        state.x = msg.x;
        state.y = msg.y;
        state.z = msg.z;

        tello::StateReceiver::StateSample sample;
        sample.sequence = ++ros_state_sequence_;
        sample.timestamp_ms = QDateTime::currentMSecsSinceEpoch();
        sample.recording_elapsed_ms = gui_metrics_recording_
            ? std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::steady_clock::now() - recording_started_at_).count()
            : -1;
        sample.steady_elapsed_ms = sample.recording_elapsed_ms;
        sample.state = state;

        {
            std::lock_guard<std::mutex> lock(ros_state_mutex_);
            ros_state_buffer_.push_back(sample);
            while (ros_state_buffer_.size() > ros_state_buffer_capacity_) {
                ros_state_buffer_.pop_front();
            }
        }

        if (battery_status_label_ != nullptr) {
            battery_status_label_->setText(QString("battery: %1%").arg(state.bat));
        }
        if (temperature_status_label_ != nullptr) {
            temperature_status_label_->setText(QString("temp: %1/%2 C").arg(state.templ).arg(state.temph));
        }
        metrics_.updateTelemetryState(state, msg.valid);
        updateStatusLabel();
    }

    void applyRosLinkQuality(const tello_interfaces::msg::LinkQuality& msg) {
        ros_link_overall_ = QString::fromStdString(msg.overall);
        ros_link_reason_ = QString::fromStdString(msg.reason);
        ros_link_score_ = msg.score;
        ros_link_safe_for_nonzero_rc_ = msg.safe_for_nonzero_rc;
        ros_have_link_quality_ = true;
        updateTopLinkQualityLabel();
    }

    void applyRosImage(const sensor_msgs::msg::Image& msg) {
        if (msg.encoding != "rgb8" || msg.width == 0 || msg.height == 0 || msg.step == 0 || msg.data.empty()) {
            return;
        }
        vision_pipeline_running_ = true;
        publishVisionRgbFrame(
            msg.data.data(),
            static_cast<int32_t>(msg.width),
            static_cast<int32_t>(msg.height),
            static_cast<int32_t>(msg.step),
            false);
        if (vision_status_label_ != nullptr) {
            vision_status_label_->setText("status: ROS image topic | LIVE");
        }
        if (vision_size_label_ != nullptr) {
            vision_size_label_->setText(QString("size: %1x%2").arg(msg.width).arg(msg.height));
        }
    }

    bool publishRosManualCmdVel(int a, int b, int c, int d) {
        if (!ros_manual_cmd_vel_pub_) {
            return false;
        }
        geometry_msgs::msg::Twist msg;
        msg.linear.y = static_cast<double>(a) / 100.0;
        msg.linear.x = static_cast<double>(b) / 100.0;
        msg.linear.z = static_cast<double>(c) / 100.0;
        msg.angular.z = static_cast<double>(d) / 100.0;
        ros_manual_cmd_vel_pub_->publish(msg);
        return true;
    }
#endif

    void refreshStateHistoryView() {
        const auto tick_started_at = std::chrono::steady_clock::now();
        updateStateRecordingStatusIndicators();
        if (has_last_state_gui_tick_) {
            last_gui_state_tick_delay_ms_ = std::chrono::duration_cast<std::chrono::milliseconds>(
                tick_started_at - last_state_gui_tick_tp_).count();
        }
        last_state_gui_tick_tp_ = tick_started_at;
        has_last_state_gui_tick_ = true;

#ifdef TELLO_CONTROL_PANEL_ROS_NATIVE
        if (ros_mode_ && ros_native_started_) {
            std::vector<tello::StateReceiver::StateSample> buffered;
            tello::TelloState latest{};
            bool has_latest = false;
            {
                std::lock_guard<std::mutex> lock(ros_state_mutex_);
                buffered.assign(ros_state_buffer_.begin(), ros_state_buffer_.end());
                if (!ros_state_buffer_.empty()) {
                    latest = ros_state_buffer_.back().state;
                    has_latest = true;
                }
            }

            if (has_latest) {
                if (battery_status_label_ != nullptr) {
                    battery_status_label_->setText(QString("battery: %1%").arg(latest.bat));
                }
                if (temperature_status_label_ != nullptr) {
                    temperature_status_label_->setText(
                        QString("temp: %1/%2 C").arg(latest.templ).arg(latest.temph));
                }
                metrics_.updateTelemetryState(latest, true);
            }

            const auto metric = state_metric_combo_ != nullptr ? state_metric_combo_->currentText() : QString("pitch");
            if (state_plot_widget_ != nullptr) {
                state_plot_widget_->setSamples(buffered, metric);
            }
            if (state_buffer_info_label_ != nullptr) {
                state_buffer_info_label_->setText(
                    QString("ROS samples=%1 / cap=%2")
                        .arg(static_cast<int>(buffered.size()))
                        .arg(ros_state_buffer_capacity_));
            }
            if (state_record_info_label_ != nullptr) {
                state_record_info_label_->setText("ROS telemetry topic active");
            }

            last_state_refresh_duration_ms_ = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - tick_started_at).count();
            updateRuntimeMetricsContext();
            updateTopLinkQualityLabel();
            return;
        }
#endif

        if (state_receiver_.isRunning()) {
            const auto history_fetch_started_at = std::chrono::steady_clock::now();
            const auto buffered = state_receiver_.getBufferedStateSamples();
            const auto recorded_count = state_receiver_.getRecordedStateSampleCount();
            const auto rc_recorded_count = state_receiver_.getRecordedRcCommandSampleCount();
            last_state_history_fetch_ms_ = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - history_fetch_started_at).count();
            const auto metric = state_metric_combo_ != nullptr ? state_metric_combo_->currentText() : QString("pitch");

            if (state_receiver_.hasReceivedState()) {
                const auto latest = state_receiver_.getLatestState();
                if (battery_status_label_ != nullptr) {
                    battery_status_label_->setText(QString("battery: %1%").arg(latest.bat));
                }
                if (temperature_status_label_ != nullptr) {
                    temperature_status_label_->setText(
                        QString("temp: %1/%2 C").arg(latest.templ).arg(latest.temph));
                }
            }

            if (state_plot_widget_ != nullptr) {
                state_plot_widget_->setSamples(buffered, metric);
            }

            if (state_buffer_info_label_ != nullptr) {
                state_buffer_info_label_->setText(
                    QString("buffer=%1 / cap=%2")
                        .arg(static_cast<int>(buffered.size()))
                        .arg(state_receiver_.getStateBufferCapacity()));
            }

            if (state_record_info_label_ != nullptr) {
                state_record_info_label_->setText(
                    QString("recorded=%1 | rc=%2 | %3")
                        .arg(static_cast<int>(recorded_count))
                        .arg(static_cast<int>(rc_recorded_count))
                        .arg(state_receiver_.isStateRecording() ? "REC ON" : "REC OFF"));
            }
        } else {
            if (state_buffer_info_label_ != nullptr) {
                state_buffer_info_label_->setText("state receiver offline");
            }
            if (state_record_info_label_ != nullptr) {
                state_record_info_label_->setText(
                state_receiver_.isStateRecording() ? "recording pending (receiver offline)" : "recorded=0");
            }
        }

        last_state_refresh_duration_ms_ = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - tick_started_at).count();
        updateRuntimeMetricsContext();
        updateTopLinkQualityLabel();
    }

    void publishVisionRgbFrame(
        const uint8_t* rgb_data,
        int32_t width,
        int32_t height,
        int32_t stride,
        bool is_key_frame
    ) {
        if (rgb_data == nullptr || width <= 0 || height <= 0 || stride <= 0) {
            return;
        }

        constexpr int64_t kUiFrameMinIntervalMs = 100;
        const auto now = std::chrono::steady_clock::now();
        const int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count();
        const int64_t previous_ms = vision_last_ui_frame_convert_ms_.load();
        if (previous_ms > 0 && now_ms - previous_ms < kUiFrameMinIntervalMs) {
            ui_frames_dropped_.fetch_add(1);
            return;
        }
        vision_last_ui_frame_convert_ms_.store(now_ms);

        const auto convert_started_at = std::chrono::steady_clock::now();
        const QImage rgb(rgb_data, width, height, stride, QImage::Format_RGB888);
        const QImage owned = rgb.copy();
        last_frame_convert_ms_ = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - convert_started_at).count();
        ui_frames_converted_.fetch_add(1);
        if (is_key_frame) {
            vision_keyframes_.fetch_add(1);
        }
        vision_frame_width_.store(width);
        vision_frame_height_.store(height);

        std::lock_guard<std::mutex> lock(vision_frame_mutex_);
        vision_latest_frame_ = owned;
        if (!vision_paused_) {
            vision_paused_frame_ = owned;
        }
        vision_latest_frame_sequence_.fetch_add(1);
    }

    void refreshVisionView() {
        const auto tick_started_at = std::chrono::steady_clock::now();
        if (has_last_vision_gui_tick_) {
            last_gui_vision_tick_delay_ms_ = std::chrono::duration_cast<std::chrono::milliseconds>(
                tick_started_at - last_vision_gui_tick_tp_).count();
        }
        last_vision_gui_tick_tp_ = tick_started_at;
        has_last_vision_gui_tick_ = true;

        if (vision_status_label_ != nullptr) {
            vision_status_label_->setText(
                QString("status: %1 | %2")
                    .arg(vision_pipeline_running_ ? "running" : "idle")
                    .arg(vision_paused_ ? "PAUSED" : "LIVE"));
        }

        bool use_standalone_vision_pipeline = vision_pipeline_running_;
#ifdef TELLO_CONTROL_PANEL_ROS_NATIVE
        if (ros_mode_ && ros_native_started_) {
            use_standalone_vision_pipeline = false;
        }
#endif

        if (use_standalone_vision_pipeline) {
            ensureSdkKeepaliveRunning("vision");
            writeVisionMetricsRowIfDue();

            const auto stream_stats = video_stream_reader_.getStats();
            maybeRecoverVisionPipeline(stream_stats.last_frame_age_ms);

            if (vision_rx_label_ != nullptr) {
                vision_rx_label_->setText(QString("stream packets: %1")
                                              .arg(static_cast<qulonglong>(stream_stats.packets_read)));
            }
            if (vision_nal_label_ != nullptr) {
                vision_nal_label_->setText("backend: ffmpeg stream");
            }
            if (vision_fps_label_ != nullptr) {
                vision_fps_label_->setText(QString("decode fps: %1")
                                               .arg(stream_stats.decode_fps_ema, 0, 'f', 2));
            }
            if (vision_size_label_ != nullptr) {
                vision_size_label_->setText(QString("size: %1x%2")
                                                .arg(stream_stats.frame_width)
                                                .arg(stream_stats.frame_height));
            }
            if (vision_decode_err_label_ != nullptr) {
                vision_decode_err_label_->setText(QString("decode errors: %1")
                                                      .arg(static_cast<qulonglong>(stream_stats.decode_errors)));
            }
            if (vision_frame_info_label_ != nullptr) {
                vision_frame_info_label_->setText(
                    QString("frames: %1 | keyframes: %2")
                        .arg(static_cast<qulonglong>(stream_stats.frames_decoded))
                        .arg(static_cast<qulonglong>(stream_stats.keyframes)));
            }
        }

        QImage frame_to_show;
        uint64_t frame_sequence = 0;
        const auto mutex_wait_started_at = std::chrono::steady_clock::now();
        {
            std::lock_guard<std::mutex> lock(vision_frame_mutex_);
            last_vision_frame_mutex_wait_ms_ = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - mutex_wait_started_at).count();
            frame_to_show = vision_paused_ ? vision_paused_frame_ : vision_latest_frame_;
            frame_sequence = vision_latest_frame_sequence_.load();
        }

        if (frame_to_show.isNull()) {
            if (vision_frame_label_ != nullptr && !vision_pipeline_running_) {
                vision_frame_label_->setText("Vision not started");
            }
            last_vision_refresh_duration_ms_ = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - tick_started_at).count();
            return;
        }

        if (!vision_paused_ && frame_sequence == vision_displayed_frame_sequence_) {
            last_vision_refresh_duration_ms_ = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - tick_started_at).count();
            return;
        }

        QImage composed = frame_to_show.copy();
        if (vision_overlay_enabled_) {
            QPainter p(&composed);
            p.setRenderHint(QPainter::Antialiasing, true);
            p.setPen(QPen(QColor(0, 255, 110), 1));
#ifdef TELLO_CONTROL_PANEL_ROS_NATIVE
            if (ros_mode_ && ros_native_started_) {
                p.drawText(10, 22, QString("ROS image topic frames=%1")
                                    .arg(static_cast<qulonglong>(vision_latest_frame_sequence_.load())));
                p.drawText(10, 42, "source=/tello/video/image_raw");
                p.drawText(10, 62, QString("state=%1 controls: Pause, Snapshot, Overlay")
                                    .arg(vision_paused_ ? "PAUSED" : "LIVE"));
            } else
#endif
            {
                const auto stream_stats = video_stream_reader_.getStats();

            p.drawText(10, 22, QString("fps=%1 frames=%2 keyframes=%3")
                                .arg(stream_stats.decode_fps_ema, 0, 'f', 2)
                                .arg(static_cast<qulonglong>(stream_stats.frames_decoded))
                                .arg(static_cast<qulonglong>(stream_stats.keyframes)));
            p.drawText(10, 42, QString("packets=%1 dec_errors=%2")
                                .arg(static_cast<qulonglong>(stream_stats.packets_read))
                                .arg(static_cast<qulonglong>(stream_stats.decode_errors)));
            p.drawText(10, 62, QString("state=%1 controls: Pause, Snapshot, Overlay")
                                .arg(vision_paused_ ? "PAUSED" : "LIVE"));
            }
        }

        if (vision_frame_label_ != nullptr) {
            const auto scale_started_at = std::chrono::steady_clock::now();
            vision_frame_label_->setPixmap(QPixmap::fromImage(composed).scaled(
                vision_frame_label_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
            last_frame_scale_ms_ = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - scale_started_at).count();
            vision_displayed_frame_sequence_ = frame_sequence;
            ui_frames_displayed_.fetch_add(1);
        }
        last_vision_refresh_duration_ms_ = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - tick_started_at).count();
    }

    void resetVisionRuntimeStateForRestart() {
        vision_keyframes_.store(0);
        vision_frame_width_.store(0);
        vision_frame_height_.store(0);
        last_vision_metrics_packets_ = 0;
        vision_latest_frame_sequence_.store(0);
        vision_displayed_frame_sequence_ = 0;
        vision_last_ui_frame_convert_ms_.store(0);
        ui_frames_converted_.store(0);
        ui_frames_dropped_.store(0);
        ui_frames_displayed_.store(0);

        {
            std::lock_guard<std::mutex> lock(vision_frame_mutex_);
            vision_latest_frame_ = QImage();
            vision_paused_frame_ = QImage();
        }

        vision_paused_ = false;
        if (vision_pause_btn_ != nullptr) {
            vision_pause_btn_->setText("Pause");
        }
    }

    void writeVisionRecoveryMetricsRow(
        const tello::TelloClient::VideoRecoveryStatus& recovery,
        bool power_cycle_recovery_used,
        const tello::VideoStreamReaderFfmpeg::Stats& stream_stats) {
        if (!isGuiCsvRecording()) {
            return;
        }

        const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - metrics_started_at_).count();

        tello::MetricsCollector::VideoTransportStats rx_stats{};
        rx_stats.packets_total = stream_stats.packets_read;
        rx_stats.bytes_total = stream_stats.bytes_read;
        rx_stats.last_packet_age_ms = stream_stats.last_frame_age_ms;
        rx_stats.rx_pps_ema = stream_stats.decode_fps_ema;

        tello::MetricsCollector::VideoAssemblyStats nal_stats{};
        nal_stats.packets_in = stream_stats.packets_read;
        nal_stats.bytes_in = stream_stats.bytes_read;
        nal_stats.nal_units_out = stream_stats.frames_decoded;

        tello::MetricsCollector::VideoDecodeStats dec_stats{};
        dec_stats.frames_decoded = stream_stats.frames_decoded;
        dec_stats.decode_errors = stream_stats.decode_errors;
        dec_stats.decode_fps_ema = stream_stats.decode_fps_ema;

        ++metrics_attempt_;
        updateRuntimeMetricsContext();
        metrics_.setElapsedMs(elapsed_ms);
        metrics_.setAttempt(metrics_attempt_);
        metrics_.updateVideoTransportStats(rx_stats);
        metrics_.setVideoPacketDelta(0);
        metrics_.updateVideoAssemblerStats(nal_stats);
        metrics_.updateDecoderStats(dec_stats);
        metrics_.updateFrameInfo(vision_frame_width_.load(), vision_frame_height_.load(), false);
        metrics_.updateDisplayState(vision_paused_, vision_overlay_enabled_);
        metrics_.recordRecoveryEvent(
            recovery.attempted,
            recovery.result,
            recovery.used_hard_recovery,
            recovery.stage,
            recovery.command_channel_available);
        metrics_.setConnectionState(connectionStateToString(client_.getConnectionState()));
        metrics_.setEvent(power_cycle_recovery_used ? "vision_recovery:power_cycle" : "vision_recovery");
        metrics_.updateLogMessage("", "");
        metrics_.setLastOutageFailures(client_.getLastOutageFailures());
        gui_metrics_rows_.push_back(metrics_.toCsvLine());
    }

    void maybeRecoverVisionPipeline(int64_t last_frame_age_ms) {
        if (!vision_pipeline_running_ || !sdk_ready_) {
            return;
        }
        if (vision_recovery_in_progress_) {
            return;
        }

        constexpr int64_t kVisionStallThresholdMs = 3000;
        if (last_frame_age_ms < kVisionStallThresholdMs) {
            return;
        }

        constexpr int64_t kVisionRecoveryQueueCooldownMs = 3000;
        const auto now = std::chrono::steady_clock::now();
        if (has_last_vision_recovery_queue_) {
            const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - last_vision_recovery_queue_tp_).count();
            if (elapsed_ms < kVisionRecoveryQueueCooldownMs) {
                return;
            }
        }
        has_last_vision_recovery_queue_ = true;
        last_vision_recovery_queue_tp_ = now;

        vision_recovery_in_progress_ = true;
        appendLog(QString("vision recovery queued | last_frame_age_ms=%1").arg(last_frame_age_ms));
        enqueueCommandWorkerTask([this, last_frame_age_ms]() {
            AsyncRecoveryResult result;
            constexpr int64_t kWorkerVisionStallThresholdMs = 3000;
            constexpr int64_t kWorkerVisionRecoveryCooldownMs = 3000;
            result.recovery = client_.recoverVideoStreamIfStalled(
                last_frame_age_ms,
                kWorkerVisionStallThresholdMs,
                kWorkerVisionRecoveryCooldownMs);

            if (result.recovery.attempted
                && result.recovery.stage == "battery_probe"
                && !result.recovery.command_channel_available) {
                result.recovery = client_.recoverAfterPowerCycle();
                result.power_cycle_recovery_used = true;
            }

            QMetaObject::invokeMethod(this, [this, result]() {
                handleAsyncVisionRecoveryResult(result);
            }, Qt::QueuedConnection);
        });
    }

    void handleAsyncVisionRecoveryResult(const AsyncRecoveryResult& result) {
        vision_recovery_in_progress_ = false;
        const tello::TelloClient::VideoRecoveryStatus& recovery = result.recovery;
        const bool power_cycle_recovery_used = result.power_cycle_recovery_used;

        if (!recovery.attempted) {
            return;
        }

        bool video_pipeline_restarted = false;
        tello::ResponseCode video_restart_rc = tello::ResponseCode::OK;
        if (shouldRestartLocalVideoPipeline(recovery)) {
            resetVisionRuntimeStateForRestart();
            video_stream_reader_.stop();
            const std::string url = "udp://@0.0.0.0:11111?overrun_nonfatal=1&fifo_size=5000000";
            video_restart_rc = video_stream_reader_.start(url);
            video_pipeline_restarted = (video_restart_rc == tello::ResponseCode::OK);
        }

        if (recovery.command_channel_available || recovery.result == tello::ResponseCode::OK) {
            sdk_ready_ = true;
        } else if (power_cycle_recovery_used) {
            sdk_ready_ = false;
        }

        appendLog(QString("vision recovery => %1 | stage=%2 | command_channel=%3 | hard=%4 | video_restart=%5")
                      .arg(QString::fromStdString(responseCodeToString(recovery.result)))
                      .arg(QString::fromStdString(recovery.stage))
                      .arg(recovery.command_channel_available ? "available" : "unavailable")
                      .arg(recovery.used_hard_recovery ? "yes" : "no")
                      .arg(video_pipeline_restarted ? "yes" : "no"));

        if (video_restart_rc != tello::ResponseCode::OK) {
            appendLog("vision local pipeline restart failed => "
                      + QString::fromStdString(responseCodeToString(video_restart_rc)));
        }

        writeVisionRecoveryMetricsRow(recovery, power_cycle_recovery_used, video_stream_reader_.getStats());
        ensureSdkKeepaliveRunning("vision-recovery");
        updateStatusLabel();
    }

    bool startVisionWithStreamOn() {
        if (ros_mode_) {
            runCommandWithResponse("streamon", "vision");
            vision_pipeline_running_ = true;
            if (vision_status_label_ != nullptr) {
                vision_status_label_->setText("status: waiting for /tello/video/image_raw");
            }
            return true;
        }

        if (!ensureSdkReady()) {
            return false;
        }
        appendLog("streamon (vision) queued on command worker");
        enqueueCommandWorkerTask([this]() {
            std::string response;
            const auto command_started_at = std::chrono::steady_clock::now();
            const auto rc = client_.sendCommandWithResponse("streamon", response);
            const auto command_elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - command_started_at).count();
            const QString response_q = QString::fromStdString(response);
            const QString state_q = QString::fromStdString(connectionStateToString(client_.getConnectionState()));

            QMetaObject::invokeMethod(this, [this, rc, response_q, state_q, command_elapsed_ms]() {
                const QString rc_q = QString::fromStdString(responseCodeToString(rc));
                QString line = "streamon (vision) => " + rc_q;
                if (!response_q.isEmpty()) {
                    line += " | response=\"" + response_q + "\"";
                }
                line += " | state=" + state_q;
                appendLog(line);
                writeCommandMetricsRow("vision", "streamon", static_cast<double>(command_elapsed_ms), rc, response_q, state_q);
                (void)startVisionPipeline();
            }, Qt::QueuedConnection);
        });
        return true;
    }

    void startDefaultVisionIfReady(const QString& reason) {
        if (!sdk_ready_ || vision_pipeline_running_) {
            return;
        }
        appendLog("default camera auto-start (" + reason + ")");
        QTimer::singleShot(250, this, [this]() {
            if (sdk_ready_ && !vision_pipeline_running_) {
                (void)startVisionWithStreamOn();
            }
        });
    }

    void stopVisionWithStreamOff() {
        if (ros_mode_) {
            runCommandWithResponse("streamoff", "vision");
            stopVisionPipeline();
            return;
        }

        stopVisionPipeline();

        if (!sdk_ready_) {
            return;
        }
        appendLog("streamoff (vision) queued on command worker");
        enqueueCommandWorkerTask([this]() {
            std::string response;
            const auto command_started_at = std::chrono::steady_clock::now();
            const auto rc = client_.sendCommandWithResponse("streamoff", response);
            const auto command_elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - command_started_at).count();
            const QString response_q = QString::fromStdString(response);
            const QString state_q = QString::fromStdString(connectionStateToString(client_.getConnectionState()));

            QMetaObject::invokeMethod(this, [this, rc, response_q, state_q, command_elapsed_ms]() {
                const QString rc_q = QString::fromStdString(responseCodeToString(rc));
                QString line = "streamoff (vision) => " + rc_q;
                if (!response_q.isEmpty()) {
                    line += " | response=\"" + response_q + "\"";
                }
                line += " | state=" + state_q;
                appendLog(line);
                writeCommandMetricsRow("vision", "streamoff", static_cast<double>(command_elapsed_ms), rc, response_q, state_q);
            }, Qt::QueuedConnection);
        });
    }

    bool startVisionPipeline() {
        if (vision_pipeline_running_) {
            return true;
        }
        if (!ensureSdkReady()) {
            return false;
        }

        vision_keyframes_.store(0);
        vision_frame_width_.store(0);
        vision_frame_height_.store(0);
        last_vision_metrics_packets_ = 0;
        vision_latest_frame_sequence_.store(0);
        vision_displayed_frame_sequence_ = 0;
        vision_last_ui_frame_convert_ms_.store(0);
        ui_frames_converted_.store(0);
        ui_frames_dropped_.store(0);
        ui_frames_displayed_.store(0);

        video_stream_reader_.setFrameCallback([this](const tello::VideoStreamReaderFfmpeg::DecodedFrame& frame) {
            publishVisionRgbFrame(
                frame.rgb.data(),
                frame.width,
                frame.height,
                frame.rgb_stride,
                frame.is_key_frame);
        });

        const std::string url = "udp://@0.0.0.0:11111?overrun_nonfatal=1&fifo_size=5000000";
        const auto stream_rc = video_stream_reader_.start(url);
        if (stream_rc != tello::ResponseCode::OK) {
            appendLog("vision start failed: ffmpeg stream reader start error");
            return false;
        }

        {
            std::lock_guard<std::mutex> lock(vision_frame_mutex_);
            vision_latest_frame_ = QImage();
            vision_paused_frame_ = QImage();
        }
        vision_paused_ = false;
        if (vision_pause_btn_ != nullptr) {
            vision_pause_btn_->setText("Pause");
        }

        vision_pipeline_running_ = true;
        appendLog("vision pipeline started (FFmpeg stream backend)");
        return true;
    }

    void stopVisionPipeline() {
        if (!vision_pipeline_running_) {
            return;
        }

        vision_pipeline_running_ = false;
        video_stream_reader_.stop();

        {
            std::lock_guard<std::mutex> lock(vision_frame_mutex_);
            vision_latest_frame_ = QImage();
            vision_paused_frame_ = QImage();
        }
        if (vision_frame_label_ != nullptr) {
            vision_frame_label_->setPixmap(QPixmap());
            vision_frame_label_->setText("Vision stopped");
        }
        appendLog("vision pipeline stopped");
    }

    void toggleVisionPause() {
        if (!vision_pipeline_running_) {
            appendLog("vision pause ignored: pipeline not running");
            return;
        }

        vision_paused_ = !vision_paused_;
        if (vision_paused_) {
            std::lock_guard<std::mutex> lock(vision_frame_mutex_);
            vision_paused_frame_ = vision_latest_frame_;
        }
        if (vision_pause_btn_ != nullptr) {
            vision_pause_btn_->setText(vision_paused_ ? "Resume" : "Pause");
        }
        appendLog(QString("vision %1").arg(vision_paused_ ? "paused" : "resumed"));
    }

    void saveVisionSnapshot() {
        QImage snapshot;
        {
            std::lock_guard<std::mutex> lock(vision_frame_mutex_);
            snapshot = vision_paused_ ? vision_paused_frame_ : vision_latest_frame_;
        }

        if (snapshot.isNull()) {
            appendLog("snapshot skipped: no frame available");
            return;
        }

        const QString file = "vision_snapshot_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss_zzz") + ".png";
        if (snapshot.save(file)) {
            appendLog("snapshot saved => " + file);
        } else {
            appendLog("snapshot save failed => " + file);
        }
    }
    void appendLog(const QString& msg) {
        const QString timestamp = QDateTime::currentDateTime().toString(Qt::ISODateWithMs);
        log_view_->append("[" + nowClockString() + "] " + msg);
        log_view_->repaint();
        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        writeLogMetricsRow(timestamp, msg);
    }

    std::string buildRcCommandFromInputs() const {
        return "rc "
            + std::to_string(rc_lr_slider_ != nullptr ? rc_lr_slider_->value() : 0) + " "
            + std::to_string(rc_fb_slider_ != nullptr ? rc_fb_slider_->value() : 0) + " "
            + std::to_string(rc_ud_slider_ != nullptr ? rc_ud_slider_->value() : 0) + " "
            + std::to_string(rc_yaw_slider_ != nullptr ? rc_yaw_slider_->value() : 0);
    }

    bool parseRcCommand(const std::string& cmd, int& a, int& b, int& c, int& d) const {
        std::istringstream iss(cmd);
        std::string head;
        if (!(iss >> head) || head != "rc") {
            return false;
        }
        if (!(iss >> a >> b >> c >> d)) {
            return false;
        }
        return true;
    }

    void setRcSliders(int lr, int fb, int ud, int yaw) {
        if (rc_lr_slider_ != nullptr) {
            rc_lr_slider_->setValue(lr);
        }
        if (rc_fb_slider_ != nullptr) {
            rc_fb_slider_->setValue(fb);
        }
        if (rc_ud_slider_ != nullptr) {
            rc_ud_slider_->setValue(ud);
        }
        if (rc_yaw_slider_ != nullptr) {
            rc_yaw_slider_->setValue(yaw);
        }
    }

    bool hasNonZeroRcInput() const {
        const RcChannels keyboard_rc = rcChannelsFromKeyboard();
        return keyboard_rc.a != 0 || keyboard_rc.b != 0 || keyboard_rc.c != 0 || keyboard_rc.d != 0
            || (rc_lr_slider_ != nullptr && rc_lr_slider_->value() != 0)
            || (rc_fb_slider_ != nullptr && rc_fb_slider_->value() != 0)
            || (rc_ud_slider_ != nullptr && rc_ud_slider_->value() != 0)
            || (rc_yaw_slider_ != nullptr && rc_yaw_slider_->value() != 0);
    }

    tello::ResponseCode sendRcCommandNoWait(const std::string& cmd, const std::string& source) {
        if (!sdk_ready_) {
            return tello::ResponseCode::ERROR;
        }

        int a = 0;
        int b = 0;
        int c = 0;
        int d = 0;
        const bool parsed_rc = parseRcCommand(cmd, a, b, c, d);

        if (ros_mode_) {
            bool ok = false;
#ifdef TELLO_CONTROL_PANEL_ROS_NATIVE
            if (parsed_rc && ros_native_started_) {
                ok = publishRosManualCmdVel(a, b, c, d);
            } else {
                ok = callRosCommandService(QString::fromStdString(cmd), QString::fromStdString(source));
            }
#else
            ok = callRosCommandService(QString::fromStdString(cmd), QString::fromStdString(source));
#endif
            if (parsed_rc) {
                const auto rc = ok ? tello::ResponseCode::OK : tello::ResponseCode::ERROR;
                state_receiver_.recordRcCommandSample(a, b, c, d, source, rc);
                last_sent_rc_channels_ = RcChannels{a, b, c, d};
                recordRcLinkSend(last_sent_rc_channels_);
            }
            return ok ? tello::ResponseCode::OK : tello::ResponseCode::ERROR;
        }

        if (!parsed_rc) {
            const auto rc = client_.sendCommandNoWait(cmd);
            if (rc != tello::ResponseCode::OK) {
                QMetaObject::invokeMethod(this, [this, source, rc]() {
                    appendLog(QString::fromStdString(source) + " => "
                              + QString::fromStdString(responseCodeToString(rc)));
                }, Qt::QueuedConnection);
            }
            return rc;
        }

        const auto rc = client_.sendCommandNoWait(cmd);
        state_receiver_.recordRcCommandSample(a, b, c, d, source, rc);
        last_sent_rc_channels_ = RcChannels{a, b, c, d};
        recordRcLinkSend(last_sent_rc_channels_);
        if (rc != tello::ResponseCode::OK) {
            const QString source_q = QString::fromStdString(source);
            const QString rc_q = QString::fromStdString(responseCodeToString(rc));
            QMetaObject::invokeMethod(this, [this, source_q, rc_q]() {
                appendLog(source_q + " => " + rc_q);
            }, Qt::QueuedConnection);
        }
        return rc;
    }

    CommandTimingResult sendRcNeutralBestEffort(const QString& reason, bool write_metrics_row = true) {
        CommandTimingResult result;
        if (!sdk_ready_) {
            return result;
        }

        const auto started_at = std::chrono::steady_clock::now();
        result.rc = sendRcCommandNoWait("rc 0 0 0 0", ("rc-neutral-" + reason).toStdString());
        result.elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started_at).count();
        result.response = "no_wait";

        const QString rc_q = QString::fromStdString(responseCodeToString(result.rc));
        if (result.rc == tello::ResponseCode::OK) {
            appendLog("rc stream neutralized (" + reason + ") => " + rc_q);
        } else {
            appendLog("rc stream neutralize failed (" + reason + ") => " + rc_q);
        }

        if (write_metrics_row) {
            const int64_t previous_neutral_rc_ms = current_neutral_rc_ms_;
            current_neutral_rc_ms_ = result.elapsed_ms;
            writeCommandMetricsRow(
                "preflight",
                "rc 0 0 0 0",
                static_cast<double>(result.elapsed_ms),
                result.rc,
                result.response,
                QString::fromStdString(connectionStateToString(client_.getConnectionState())));
            current_neutral_rc_ms_ = previous_neutral_rc_ms;
        }
        return result;
    }

    void setRcStreamingEnabled(bool enabled, const QString& reason) {
        if (enabled) {
            client_.requestSdkKeepaliveStop();
            appendLog("sdk keepalive paused while continuous RC stream is active");
            if (rc_stream_timer_ != nullptr) {
                rc_stream_timer_->start(rc_stream_interval_ms_ != nullptr ? rc_stream_interval_ms_->value() : 50);
            }
            appendLog(QString("continuous RC ON (%1 ms)").arg(rc_stream_interval_ms_ != nullptr ? rc_stream_interval_ms_->value() : 50));
            writeEventMetricsRow(QString("rc-stream:on:") + reason);
            return;
        }

        if (rc_stream_timer_ != nullptr && !keyboard_control_active_) {
            rc_stream_timer_->stop();
        }
        sendRcNeutralBestEffort(reason);
        if (sdk_ready_ && !keyboard_control_active_) {
            const auto keepalive_rc = client_.startSdkKeepalive(5000);
            appendLog("sdk keepalive restart after RC stream => "
                      + QString::fromStdString(responseCodeToString(keepalive_rc)));
        }
        appendLog("continuous RC OFF (" + reason + ")");
        writeEventMetricsRow(QString("rc-stream:off:") + reason);
    }

    void sendRcStreamTick() {
        if (!sdk_ready_ || critical_command_active_) {
            return;
        }

        if (keyboard_control_active_) {
            publishKeyboardRcDesiredFromKeys();
            updateKeyboardControlPreview();
            return;
        }

        if (rc_stream_check_ == nullptr || !rc_stream_check_->isChecked()) {
            return;
        }

        const std::string cmd = buildRcCommandFromInputs();
        (void)sendRcCommandNoWait(cmd, "rc-stream");
    }

    void updateTopLinkQualityLabel() {
        if (link_quality_label_ == nullptr) {
            return;
        }

#ifdef TELLO_CONTROL_PANEL_ROS_NATIVE
        if (ros_mode_ && ros_have_link_quality_) {
            const QString safe = ros_link_safe_for_nonzero_rc_ ? "RC SAFE" : "RC HOLD";
            link_quality_label_->setText(
                QString("link: %1 (%2) | %3").arg(ros_link_overall_).arg(ros_link_score_).arg(safe));
            link_quality_label_->setToolTip(ros_link_reason_);

            QString color = "#8E8E93";
            if (ros_link_overall_ == "OK") {
                color = "#2ECC71";
            } else if (ros_link_overall_ == "DEGRADED") {
                color = "#D6A21A";
            } else if (ros_link_overall_ == "STALE" || ros_link_overall_ == "BLACKOUT") {
                color = "#EE5858";
            }
            link_quality_label_->setStyleSheet(QString("QLabel { color: %1; font-weight: 600; }").arg(color));
            return;
        }
#endif

        const auto link = metrics_.getLinkQualitySnapshot();
        const QString label = QString::fromStdString(link.overall);
        const QString safe = link.safe_for_nonzero_rc ? "RC SAFE" : "RC HOLD";
        link_quality_label_->setText(
            QString("link: %1 (%2) | %3").arg(label).arg(link.score).arg(safe));
        link_quality_label_->setToolTip(QString::fromStdString(link.reason));

        QString color = "#8E8E93";
        if (link.overall == "OK") {
            color = "#2ECC71";
        } else if (link.overall == "DEGRADED") {
            color = "#D6A21A";
        } else if (link.overall == "STALE" || link.overall == "BLACKOUT") {
            color = "#EE5858";
        }
        link_quality_label_->setStyleSheet(QString("QLabel { color: %1; font-weight: 600; }").arg(color));
    }

    void updateStatusLabel() {
        updateRuntimeMetricsContext();
        QString state_text = QString::fromStdString(connectionStateToString(client_.getConnectionState()));
#ifdef TELLO_CONTROL_PANEL_ROS_NATIVE
        if (ros_mode_ && !ros_connection_state_.isEmpty()) {
            state_text = ros_connection_state_;
        }
#endif
        status_label_->setText(
            "State: " + state_text
            + " | SDK: " + QString(sdk_ready_ ? "READY" : "NOT_READY")
            + " | CONNECTING=" + QString(connect_in_progress_.load() ? "YES" : "NO")
            + " | RC_STREAM=" + QString((rc_stream_timer_ != nullptr && rc_stream_timer_->isActive()) ? "ON" : "OFF")
            + " | KEYBOARD=" + QString(keyboard_control_active_ ? "ON" : "OFF")
            + " | speed_setpoint=" + QString::number(speed_spin_ != nullptr ? speed_spin_->value() : 0));
        updateTopLinkQualityLabel();
    }

    void waitWithUiEvents(int duration_ms) {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < duration_ms) {
            QApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 50);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    bool connectSdk() {
        if (ros_mode_) {
            const bool ok = callRosTriggerService("/tello/connect", "connect");
            sdk_ready_ = ok;
            updateStatusLabel();
            if (ok) {
                startDefaultVisionIfReady("ros-connect");
            }
            return ok;
        }

        startConnectWorker();
        return false;
    }

    void disconnectSdk() {
        if (ros_mode_) {
            (void)callRosTriggerService("/tello/disconnect", "disconnect");
            sdk_ready_ = false;
            updateStatusLabel();
            return;
        }

        if (connect_in_progress_.load()) {
            requestConnectCancel("disconnect");
            sdk_ready_ = false;
            updateStatusLabel();
            return;
        }

        stopKeyboardRcWorker("disconnect");
        client_.stopSdkKeepalive();
        auto_refresh_timer_->stop();
        if (keyboard_control_check_ != nullptr && keyboard_control_check_->isChecked()) {
            QSignalBlocker blocker(keyboard_control_check_);
            keyboard_control_check_->setChecked(false);
        }
        keyboard_control_active_ = false;
        active_keyboard_keys_.clear();
        if (rc_stream_check_ != nullptr && rc_stream_check_->isChecked()) {
            QSignalBlocker blocker(rc_stream_check_);
            rc_stream_check_->setChecked(false);
        }
        setRcStreamingEnabled(false, "disconnect");
        stopVisionPipeline();
        stopCommandWorker();
        state_receiver_.stop();
        client_.shutdown();
        startCommandWorker();
        sdk_ready_ = false;
        appendLog("disconnected");
        updateStatusLabel();
    }

    bool ensureSdkReady() {
        if (!sdk_ready_) {
            appendLog("connect first (SDK not ready)");
            return false;
        }
        return true;
    }

    void ensureSdkKeepaliveRunning(const QString& reason) {
        if (client_.isSdkKeepaliveRunning() || !client_.isInitialized()) {
            return;
        }
        if (connect_in_progress_.load()) {
            return;
        }
        if (keyboard_control_active_ || (rc_stream_timer_ != nullptr && rc_stream_timer_->isActive())) {
            return;
        }

        const auto now = std::chrono::steady_clock::now();
        if (has_last_keepalive_restart_attempt_) {
            const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - last_keepalive_restart_attempt_tp_).count();
            if (elapsed_ms < 5000) {
                return;
            }
        }
        has_last_keepalive_restart_attempt_ = true;
        last_keepalive_restart_attempt_tp_ = now;

        const auto rc = client_.startSdkKeepalive(5000);
        appendLog("sdk keepalive restart (" + reason + ") => "
                  + QString::fromStdString(responseCodeToString(rc)));
    }

    void performEmergencyShutdownIfConnected(const QString& reason) {
        if (emergency_shutdown_sent_) {
            return;
        }

        if (ros_mode_) {
            if (!sdk_ready_) {
                return;
            }
            emergency_shutdown_sent_ = true;
            appendLog("shutdown safety emergency via ROS (" + reason + ")");
            (void)callRosCommandService("emergency", "shutdown");
            return;
        }

        const auto state = client_.getConnectionState();
        const bool connected = sdk_ready_ || state == tello::TelloClient::ConnectionState::CONNECTED;
        if (!connected) {
            return;
        }

        emergency_shutdown_sent_ = true;
        keyboard_control_active_ = false;
        active_keyboard_keys_.clear();
        if (rc_stream_timer_ != nullptr) {
            rc_stream_timer_->stop();
        }
        if (auto_refresh_timer_ != nullptr) {
            auto_refresh_timer_->stop();
        }

        appendLog("shutdown safety emergency (" + reason + ")");
        const auto rc = client_.sendCommandNoWait("emergency");
        appendLog("emergency on shutdown => " + QString::fromStdString(responseCodeToString(rc)));
    }

    bool openCsv() {
        gui_metrics_rows_.clear();
        metrics_.reset();
        tello::MetricsCollector::ExperimentMetadata metadata{};
        metadata.run_mode = "CONTROL_PANEL";
        metadata.scenario = "gui";
        metrics_.setExperimentMetadata(metadata);
        metrics_started_at_ = std::chrono::steady_clock::now();
        metrics_attempt_ = 0;
        last_vision_metrics_csv_tp_ = metrics_started_at_;
        last_metrics_state_packets_valid_ = state_receiver_.getTelemetryStats().packets_valid;
        gui_metrics_recording_ = true;
        return true;
    }

    bool isGuiCsvRecording() const {
        return gui_metrics_recording_;
    }

    void closeCsv() {
        gui_metrics_recording_ = false;
    }

    void startLoggingRecording() {
        const bool gui_ok = openCsv();
        state_receiver_.startStateRecording();
        recording_started_at_ = std::chrono::steady_clock::now();
        recording_elapsed_s_ = 0;
        if (recording_elapsed_timer_ != nullptr) {
            recording_elapsed_timer_->start();
        }
        updateRecordingElapsedLabel();
        updateStateRecordingStatusIndicators();
        appendLog(QString("recording started | gui_csv=%1 | state_recording=ON")
                      .arg(gui_ok ? "ON" : "FAILED"));
        writeEventMetricsRow("logging:start");
    }

    void stopLoggingRecording() {
        appendLog("recording stopped");
        writeEventMetricsRow("logging:stop");
        recording_elapsed_s_ = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - recording_started_at_).count();
        closeCsv();
        state_receiver_.stopStateRecording();
        if (recording_elapsed_timer_ != nullptr) {
            recording_elapsed_timer_->stop();
        }
        updateRecordingElapsedLabel();
        updateStateRecordingStatusIndicators();
    }

    void clearLoggingRecording() {
        const bool was_gui_recording = isGuiCsvRecording();
        state_receiver_.clearRecordedStateSamples();
        recording_started_at_ = std::chrono::steady_clock::now();
        recording_elapsed_s_ = 0;
        updateStateRecordingStatusIndicators();
        if (was_gui_recording) {
            (void)openCsv();
            if (recording_elapsed_timer_ != nullptr) {
                recording_elapsed_timer_->start();
            }
        } else {
            metrics_.reset();
            metrics_attempt_ = 0;
            if (recording_elapsed_timer_ != nullptr) {
                recording_elapsed_timer_->stop();
            }
        }
        updateRecordingElapsedLabel();
        appendLog("recording cleared");
        writeEventMetricsRow("logging:clear");
    }

    void exportGuiMetricsCsv() {
        const QString path = csv_path_edit_ != nullptr
            ? csv_path_edit_->text().trimmed()
            : QString();
        if (path.isEmpty()) {
            appendLog("GUI metrics csv path is empty");
            return;
        }

        appendLog("GUI metrics csv export requested => " + path);
        std::ofstream out(path.toStdString(), std::ios::out | std::ios::trunc);
        if (!out.is_open()) {
            appendLog("failed to export GUI metrics csv: " + path);
            return;
        }

        out << metrics_.toCsvHeader() << '\n';
        for (const auto& row : gui_metrics_rows_) {
            out << row << '\n';
        }
        appendLog(QString("GUI metrics csv export => OK (%1 rows, %2)")
                      .arg(static_cast<int>(gui_metrics_rows_.size()))
                      .arg(path));
    }

    void exportStateRecordingCsv() {
        const QString path = state_record_path_edit_ != nullptr
            ? state_record_path_edit_->text().trimmed()
            : QString();
        if (path.isEmpty()) {
            appendLog("state csv path is empty");
            return;
        }
        const auto rc = state_receiver_.exportRecordedStateCsv(path.toStdString());
        appendLog(QString("state csv export => %1 (%2)")
                      .arg(QString::fromStdString(responseCodeToString(rc)))
                      .arg(path));
        writeEventMetricsRow("state-csv:export");
    }

    void exportAllCsvs() {
        savePersistentCsvPaths();
        appendLog("CSV export requested");
        exportGuiMetricsCsv();
        exportStateRecordingCsv();
        appendLog("CSV export finished");
    }

    void updateStateRecordingStatusIndicators() {
        const bool recording = state_receiver_.isStateRecording();
        const QString dot_color = recording ? "#2ECC71" : "#EE5858";
        if (state_record_dot_label_ != nullptr) {
            state_record_dot_label_->setStyleSheet(
                QString("QLabel { background-color: %1; border-radius: 6px; }").arg(dot_color));
        }
        if (state_record_status_label_ != nullptr) {
            state_record_status_label_->setText(recording ? "REC ON" : "REC OFF");
        }
        if (top_state_record_dot_label_ != nullptr) {
            top_state_record_dot_label_->setStyleSheet(
                QString("QLabel { background-color: %1; border-radius: 6px; }").arg(dot_color));
        }
        if (top_state_record_status_label_ != nullptr) {
            top_state_record_status_label_->setText(recording ? "REC ON" : "REC OFF");
        }
    }

    void updateRecordingElapsedLabel() {
        if (recording_elapsed_label_ == nullptr) {
            return;
        }

        int64_t elapsed_s = 0;
        if (isGuiCsvRecording() || state_receiver_.isStateRecording()) {
            recording_elapsed_s_ = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - recording_started_at_).count();
        }
        elapsed_s = recording_elapsed_s_;
        recording_elapsed_label_->setText(QString("rec: %1 s").arg(elapsed_s));
    }

    void updateRuntimeMetricsContext() {
        const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - metrics_started_at_).count();
        metrics_.setElapsedMs(elapsed_ms);

        int a = 0;
        int b = 0;
        int c = 0;
        int d = 0;
        if (keyboard_control_active_) {
            const RcChannels keyboard_rc = rcChannelsFromKeyboard();
            a = keyboard_rc.a;
            b = keyboard_rc.b;
            c = keyboard_rc.c;
            d = keyboard_rc.d;
        } else if (rc_stream_timer_ != nullptr && rc_stream_timer_->isActive()) {
            a = last_sent_rc_channels_.a;
            b = last_sent_rc_channels_.b;
            c = last_sent_rc_channels_.c;
            d = last_sent_rc_channels_.d;
        } else {
            (void)parseRcCommand(buildRcCommandFromInputs(), a, b, c, d);
        }

        const auto telemetry_stats = state_receiver_.getTelemetryStats();
        const uint64_t state_sequence_delta =
            telemetry_stats.packets_valid >= last_metrics_state_packets_valid_
                ? telemetry_stats.packets_valid - last_metrics_state_packets_valid_
                : 0;
        last_metrics_state_packets_valid_ = telemetry_stats.packets_valid;

        metrics_.updateTelemetryStats(telemetry_stats);
        if (state_receiver_.hasReceivedState()) {
            metrics_.updateTelemetryState(state_receiver_.getLatestState(), true);
        } else {
            metrics_.updateTelemetryState(tello::TelloState{}, false);
        }
        if (vision_pipeline_running_) {
            const auto stream_stats = video_stream_reader_.getStats();
            tello::MetricsCollector::VideoTransportStats rx_stats{};
            rx_stats.packets_total = stream_stats.packets_read;
            rx_stats.bytes_total = stream_stats.bytes_read;
            rx_stats.last_packet_age_ms = stream_stats.last_frame_age_ms;
            rx_stats.rx_pps_ema = stream_stats.decode_fps_ema;
            metrics_.updateVideoTransportStats(rx_stats);
        } else {
            metrics_.updateVideoTransportStats(tello::MetricsCollector::VideoTransportStats{});
        }
        metrics_.updatePanelDiagnostics(
            state_metric_combo_ != nullptr ? state_metric_combo_->currentText().toStdString() : "",
            state_sequence_delta,
            state_receiver_.isRunning());
        metrics_.updateRuntimeContext(
            (rc_stream_timer_ != nullptr && rc_stream_timer_->isActive()) || keyboard_control_active_,
            a,
            b,
            c,
            d,
            client_.isSdkKeepaliveRunning(),
            auto_refresh_timer_ != nullptr && auto_refresh_timer_->isActive(),
            critical_command_active_);
        const int64_t now_ms = steadyNowMs();
        const int64_t last_nonzero_ms = last_nonzero_rc_steady_ms_.load();
        const int64_t last_nonzero_age_ms = last_nonzero_ms > 0 ? now_ms - last_nonzero_ms : -1;
        const int64_t rc_expected_period_ms =
            keyboard_control_active_
                ? 50
                : (rc_stream_interval_ms_ != nullptr ? rc_stream_interval_ms_->value() : 50);
        metrics_.updateRcLinkStats(
            rc_packet_gap_ms_.load(),
            rc_expected_period_ms,
            rc_blackout_count_.load(),
            last_nonzero_age_ms,
            false);
        const auto keepalive_stats = client_.getSdkKeepaliveStats();
        metrics_.updateKeepaliveStats(
            keepalive_stats.tick_total,
            keepalive_stats.success_total,
            keepalive_stats.failure_total,
            keepalive_stats.skipped_busy_total,
            keepalive_stats.skipped_uninitialized_total,
            keepalive_stats.last_command,
            keepalive_stats.last_response,
            keepalive_stats.last_result,
            keepalive_stats.last_latency_ms,
            keepalive_stats.last_success_age_ms,
            keepalive_stats.last_tick_age_ms);
        metrics_.updateCriticalCommandDurations(
            current_pause_keepalive_ms_,
            current_neutral_rc_ms_,
            current_critical_command_ms_);
        metrics_.updateGuiPerformance(
            last_gui_vision_tick_delay_ms_,
            last_gui_state_tick_delay_ms_,
            last_vision_refresh_duration_ms_,
            last_state_refresh_duration_ms_,
            last_frame_convert_ms_.load(),
            last_frame_scale_ms_,
            state_plot_widget_ != nullptr ? state_plot_widget_->lastPaintDurationMs() : 0,
            last_vision_frame_mutex_wait_ms_,
            last_state_history_fetch_ms_,
            client_.getLastCommandMutexWaitMs(),
            ui_frames_converted_.load(),
            ui_frames_dropped_.load(),
            ui_frames_displayed_.load(),
            state_plot_widget_ != nullptr
                ? static_cast<uint64_t>(state_plot_widget_->plottedSamplesCount())
                : 0);
    }

    void writeEventMetricsRow(const QString& event) {
        if (!isGuiCsvRecording()) {
            return;
        }

        const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - metrics_started_at_).count();

        ++metrics_attempt_;
        updateRuntimeMetricsContext();
        metrics_.setElapsedMs(elapsed_ms);
        metrics_.setAttempt(metrics_attempt_);
        metrics_.setConnectionState(connectionStateToString(client_.getConnectionState()));
        metrics_.setEvent(event.toStdString());
        metrics_.updateLogMessage("", "");
        metrics_.setLastOutageFailures(client_.getLastOutageFailures());
        gui_metrics_rows_.push_back(metrics_.toCsvLine());
    }

    void writeLogMetricsRow(const QString& timestamp, const QString& message) {
        if (!isGuiCsvRecording()) {
            return;
        }

        const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - metrics_started_at_).count();

        ++metrics_attempt_;
        updateRuntimeMetricsContext();
        metrics_.setElapsedMs(elapsed_ms);
        metrics_.setAttempt(metrics_attempt_);
        metrics_.setConnectionState(connectionStateToString(client_.getConnectionState()));
        metrics_.setEvent("log");
        metrics_.updateLogMessage(timestamp.toStdString(), message.toStdString());
        metrics_.setLastOutageFailures(client_.getLastOutageFailures());
        gui_metrics_rows_.push_back(metrics_.toCsvLine());
    }

    void writeCommandMetricsRow(const QString& source,
                                const QString& command,
                                double latency_ms,
                                tello::ResponseCode result,
                                const QString& response,
                                const QString& state) {
        if (!isGuiCsvRecording()) {
            return;
        }

        const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - metrics_started_at_).count();

        ++metrics_attempt_;
        updateRuntimeMetricsContext();
        metrics_.setElapsedMs(elapsed_ms);
        metrics_.setAttempt(metrics_attempt_);
        metrics_.recordCommandResult(command.toStdString(), latency_ms, result, response.toStdString());
        const auto executor = client_.getCommandExecutor();
        const std::string executor_error = executor != nullptr ? executor->getLastError() : "";
        const std::string executor_attempt_log = executor != nullptr ? executor->getLastAttemptLog() : "";
        metrics_.updateCommandDiagnostics(
            source.toStdString(),
            executor_error,
            executor_attempt_log,
            client_.getLastCommandInternalAttemptLog());
        if (executor != nullptr) {
            const auto timing = executor->getLastTimingStats();
            metrics_.updateCommandTimingDiagnostics(
                client_.getLastCommandClientTotalMs(),
                client_.getLastEnsureSdkModeMs(),
                client_.getLastCommandExecutorMs(),
                timing.send_ms,
                timing.recv_wait_ms,
                timing.parse_ms,
                client_.getLastCommandExecutorRecvWaitTotalMs(),
                client_.getLastCommandExecutorInternalTotalMs(),
                client_.getLastCommandRecoveryMs(),
                client_.getLastCommandExecutorCalls(),
                client_.getLastCommandRecoveryCount());
        }
        metrics_.setConnectionState(state.toStdString());
        metrics_.setEvent((QString("command:") + source).toStdString());
        metrics_.updateLogMessage("", "");
        metrics_.setLastOutageFailures(client_.getLastOutageFailures());
        gui_metrics_rows_.push_back(metrics_.toCsvLine());
    }

    void writeVisionMetricsRowIfDue() {
        if (!isGuiCsvRecording() || !vision_pipeline_running_) {
            return;
        }

        const auto now = std::chrono::steady_clock::now();
        const auto since_last_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - last_vision_metrics_csv_tp_).count();
        if (since_last_ms < 1000) {
            return;
        }

        const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - metrics_started_at_).count();
        const auto stream_stats = video_stream_reader_.getStats();

        tello::MetricsCollector::VideoTransportStats rx_stats{};
        rx_stats.packets_total = stream_stats.packets_read;
        rx_stats.bytes_total = stream_stats.bytes_read;
        rx_stats.last_packet_age_ms = stream_stats.last_frame_age_ms;
        rx_stats.rx_pps_ema = stream_stats.decode_fps_ema;

        tello::MetricsCollector::VideoAssemblyStats nal_stats{};
        nal_stats.packets_in = stream_stats.packets_read;
        nal_stats.bytes_in = stream_stats.bytes_read;
        nal_stats.nal_units_out = stream_stats.frames_decoded;

        tello::MetricsCollector::VideoDecodeStats dec_stats{};
        dec_stats.frames_decoded = stream_stats.frames_decoded;
        dec_stats.decode_errors = stream_stats.decode_errors;
        dec_stats.decode_fps_ema = stream_stats.decode_fps_ema;

        vision_keyframes_.store(stream_stats.keyframes);
        vision_frame_width_.store(stream_stats.frame_width);
        vision_frame_height_.store(stream_stats.frame_height);

        const uint64_t packets_total = rx_stats.packets_total;
        const uint64_t delta = packets_total - last_vision_metrics_packets_;
        last_vision_metrics_packets_ = packets_total;
        last_vision_metrics_csv_tp_ = now;

        ++metrics_attempt_;
        updateRuntimeMetricsContext();
        metrics_.setElapsedMs(elapsed_ms);
        metrics_.setAttempt(metrics_attempt_);
        metrics_.updateVideoTransportStats(rx_stats);
        metrics_.setVideoPacketDelta(delta);
        metrics_.updateVideoAssemblerStats(nal_stats);
        metrics_.updateDecoderStats(dec_stats);
        metrics_.updateFrameInfo(vision_frame_width_.load(), vision_frame_height_.load(), false);
        metrics_.updateDisplayState(vision_paused_, vision_overlay_enabled_);
        metrics_.setConnectionState(connectionStateToString(client_.getConnectionState()));
        metrics_.setEvent("vision:ffmpeg_stream");
        metrics_.updateLogMessage("", "");
        metrics_.setLastOutageFailures(client_.getLastOutageFailures());
        gui_metrics_rows_.push_back(metrics_.toCsvLine());
    }

    void runReadCommand(const std::string& cmd, const QString& source) {
        runCommandWithResponse(cmd, source);
    }

    bool shouldRunCommandAsyncOnGui(const std::string& cmd) const {
        return isCriticalFlightCommand(cmd) || cmd == "streamon" || cmd == "streamoff";
    }

    TelemetryActionConfirmation waitForTelemetryActionConfirmation(
        const std::string& cmd,
        bool has_before,
        const tello::TelloState& before
    ) {
        TelemetryActionConfirmation confirmation;
        if (cmd != "takeoff" && cmd != "land") {
            return confirmation;
        }

        constexpr int kMaxWaitMs = 3000;
        constexpr int kPollMs = 100;
        const auto started_at = std::chrono::steady_clock::now();

        while (true) {
            if (state_receiver_.hasReceivedState()) {
                const tello::TelloState latest = state_receiver_.getLatestState();
                const int dh = latest.h - before.h;
                const int dtof = latest.tof - before.tof;
                bool confirmed = false;

                if (cmd == "takeoff") {
                    confirmed =
                        latest.h >= 20
                        || latest.tof >= 25
                        || (has_before && (dh >= 15 || dtof >= 15));
                } else if (cmd == "land") {
                    confirmed =
                        latest.h <= 10
                        || latest.tof <= 25
                        || (has_before && (dh <= -15 || dtof <= -15));
                }

                if (confirmed) {
                    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - started_at).count();
                    confirmation.confirmed = true;
                    confirmation.detail = QString(
                        "telemetry_confirmed(%1 before_h=%2 before_tof=%3 after_h=%4 after_tof=%5 wait_ms=%6)")
                        .arg(QString::fromStdString(cmd))
                        .arg(has_before ? before.h : -1)
                        .arg(has_before ? before.tof : -1)
                        .arg(latest.h)
                        .arg(latest.tof)
                        .arg(elapsed_ms);
                    return confirmation;
                }
            }

            const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started_at).count();
            if (elapsed_ms >= kMaxWaitMs) {
                confirmation.detail = QString("telemetry_unconfirmed(%1 wait_ms=%2)")
                    .arg(QString::fromStdString(cmd))
                    .arg(elapsed_ms);
                return confirmation;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(kPollMs));
        }
    }

    void enqueueBlockingCommand(const std::string& cmd,
                                const QString& source,
                                bool critical,
                                bool was_auto_refresh_active) {
        appendLog(QString::fromStdString(cmd) + " queued on command worker");
        enqueueCommandWorkerTask([this, cmd, source, critical, was_auto_refresh_active]() {
            AsyncCommandResult result;
            result.command = cmd;
            result.source = source;
            result.critical = critical;
            result.was_auto_refresh_active = was_auto_refresh_active;

            const bool has_before_state = state_receiver_.hasReceivedState();
            const tello::TelloState before_state = has_before_state
                ? state_receiver_.getLatestState()
                : tello::TelloState{};

            std::string response;
            const auto command_started_at = std::chrono::steady_clock::now();
            result.rc = client_.sendCommandWithResponse(cmd, response);
            result.elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - command_started_at).count();
            result.response = QString::fromStdString(response);
            result.state = QString::fromStdString(connectionStateToString(client_.getConnectionState()));
            if (critical && result.rc != tello::ResponseCode::OK) {
                const TelemetryActionConfirmation confirmation =
                    waitForTelemetryActionConfirmation(cmd, has_before_state, before_state);
                result.telemetry_confirmed = confirmation.confirmed;
                result.telemetry_confirmation_detail = confirmation.detail;
            }

            QMetaObject::invokeMethod(this, [this, result]() {
                handleAsyncCommandResult(result);
            }, Qt::QueuedConnection);
        });
    }

    void handleAsyncCommandResult(const AsyncCommandResult& result) {
        if (result.critical) {
            current_critical_command_ms_ = result.elapsed_ms;
        }

        const QString rc_q = QString::fromStdString(responseCodeToString(result.rc));
        QString line = QString::fromStdString(result.command) + " => " + rc_q;
        if (!result.response.isEmpty()) {
            line += " | response=\"" + result.response + "\"";
        }
        if (result.telemetry_confirmed) {
            line += " | " + result.telemetry_confirmation_detail;
        }
        line += " | state=" + result.state;
        appendLog(line);

        if (result.critical
            && result.rc != tello::ResponseCode::OK
            && !result.telemetry_confirmation_detail.isEmpty()) {
            appendLog(result.telemetry_confirmation_detail);
        }

        if (result.critical && result.rc == tello::ResponseCode::ERROR) {
            appendLog(QString::fromStdString(result.command)
                      + " returned ERROR, but control-command ERROR can be ambiguous on Tello UDP; "
                        "the next safety command is not blocked by this result.");
        }

        QString response_for_metrics = result.response;
        if (result.telemetry_confirmed) {
            response_for_metrics = response_for_metrics.isEmpty()
                ? result.telemetry_confirmation_detail
                : response_for_metrics + "|" + result.telemetry_confirmation_detail;
        }

        writeCommandMetricsRow(
            result.source,
            QString::fromStdString(result.command),
            static_cast<double>(result.elapsed_ms),
            result.rc,
            response_for_metrics,
            result.state);

        if (result.critical) {
            writeEventMetricsRow(QString("critical-command:end:") + QString::fromStdString(result.command));
            critical_command_active_ = false;

            constexpr int kCriticalResumeDelayMs = 1500;
            if (result.was_auto_refresh_active && sdk_ready_ && result.command != "emergency") {
                QTimer::singleShot(kCriticalResumeDelayMs, this, [this]() {
                    if (sdk_ready_ && auto_refresh_timer_ != nullptr && !auto_refresh_timer_->isActive()) {
                        auto_refresh_timer_->start(1000);
                        appendLog("auto-refresh resumed");
                        writeEventMetricsRow("auto-refresh:resumed");
                        updateStatusLabel();
                    }
                });
            }
            current_pause_keepalive_ms_ = 0;
            current_neutral_rc_ms_ = 0;
            current_critical_command_ms_ = 0;
        }

        updateStatusLabel();
    }

    void prepareCriticalCommandOnGui(const std::string& cmd) {
        current_pause_keepalive_ms_ = 0;
        current_neutral_rc_ms_ = 0;
        current_critical_command_ms_ = 0;
        critical_command_active_ = true;
        writeEventMetricsRow(QString("critical-command:begin:") + QString::fromStdString(cmd));

        if (auto_refresh_timer_ != nullptr && auto_refresh_timer_->isActive()) {
            auto_refresh_timer_->stop();
            appendLog("auto-refresh paused for " + QString::fromStdString(cmd));
        }
        appendLog("sdk keepalive remains running; cooperative loop skips ticks while command channel is busy");

        const bool rc_stream_was_active = rc_stream_timer_ != nullptr && rc_stream_timer_->isActive();
        const bool rc_input_was_nonzero = hasNonZeroRcInput();
        const bool keyboard_control_was_active = keyboard_control_active_;
        if (keyboard_control_check_ != nullptr && keyboard_control_check_->isChecked()) {
            QSignalBlocker blocker(keyboard_control_check_);
            keyboard_control_check_->setChecked(false);
        }
        keyboard_control_active_ = false;
        active_keyboard_keys_.clear();
        if (keyboard_control_was_active) {
            stopKeyboardRcWorker("keyboard-neutral-critical-" + cmd);
        }
        if (rc_stream_check_ != nullptr && rc_stream_check_->isChecked()) {
            QSignalBlocker blocker(rc_stream_check_);
            rc_stream_check_->setChecked(false);
        }
        if (rc_stream_timer_ != nullptr && rc_stream_timer_->isActive()) {
            rc_stream_timer_->stop();
        }

        if (cmd == "takeoff" || cmd == "land") {
            if (rc_stream_was_active || rc_input_was_nonzero || keyboard_control_was_active) {
                setRcSliders(0, 0, 0, 0);
                const CommandTimingResult neutral = sendRcNeutralBestEffort(QString::fromStdString(cmd));
                current_neutral_rc_ms_ = neutral.elapsed_ms;
            } else {
                appendLog("rc neutral preflight skipped (" + QString::fromStdString(cmd)
                          + "): RC stream was inactive and sliders were already zero");
                writeEventMetricsRow(QString("rc-neutral:skipped:") + QString::fromStdString(cmd));
            }
        } else {
            appendLog("continuous RC stopped for emergency");
            writeEventMetricsRow("rc-stream:off:emergency");
        }
    }

    void runCommandWithResponse(const std::string& cmd, const QString& source) {
        if (ros_mode_) {
            if (cmd == "command") {
                (void)connectSdk();
                return;
            }
            const bool ok = callRosCommandService(QString::fromStdString(cmd), source);
            appendLog(QString::fromStdString(cmd) + " => " + (ok ? "ROS_SENT" : "ROS_FAILED"));
            updateStatusLabel();
            return;
        }

        if (cmd == "command") {
            (void)connectSdk();
            return;
        }
        if (!ensureSdkReady()) {
            return;
        }

        const bool critical = isCriticalFlightCommand(cmd);
        const bool was_auto_refresh_active = auto_refresh_timer_ != nullptr && auto_refresh_timer_->isActive();
        if (critical) {
            prepareCriticalCommandOnGui(cmd);
        }

        if (shouldRunCommandAsyncOnGui(cmd)) {
            enqueueBlockingCommand(cmd, source, critical, was_auto_refresh_active);
            updateStatusLabel();
            return;
        }

        std::string response;
        const auto command_started_at = std::chrono::steady_clock::now();
        int parsed_rc_a = 0;
        int parsed_rc_b = 0;
        int parsed_rc_c = 0;
        int parsed_rc_d = 0;
        const bool is_rc_command = parseRcCommand(cmd, parsed_rc_a, parsed_rc_b, parsed_rc_c, parsed_rc_d);
        tello::ResponseCode rc = tello::ResponseCode::ERROR;
        if (is_rc_command) {
            rc = client_.sendCommandNoWait(cmd);
            response = "no_wait";
        } else {
            rc = client_.sendCommandWithResponse(cmd, response);
        }
        const auto command_elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - command_started_at).count();
        if (critical) {
            current_critical_command_ms_ = command_elapsed_ms;
        }

        if (is_rc_command) {
            state_receiver_.recordRcCommandSample(
                parsed_rc_a,
                parsed_rc_b,
                parsed_rc_c,
                parsed_rc_d,
                source.toStdString(),
                rc);
            last_sent_rc_channels_ = RcChannels{parsed_rc_a, parsed_rc_b, parsed_rc_c, parsed_rc_d};
            recordRcLinkSend(last_sent_rc_channels_);
        }

        const QString rc_q = QString::fromStdString(responseCodeToString(rc));
        const QString response_q = QString::fromStdString(response);
        const QString state_q = QString::fromStdString(connectionStateToString(client_.getConnectionState()));

        QString line = QString::fromStdString(cmd) + " => " + rc_q;
        if (!response.empty()) {
            line += " | response=\"" + response_q + "\"";
        }
        line += " | state=" + state_q;
        appendLog(line);
        if (critical && rc == tello::ResponseCode::ERROR) {
            appendLog(QString::fromStdString(cmd)
                      + " returned ERROR, but control-command ERROR can be ambiguous on Tello UDP; "
                        "the next safety command is not blocked by this result.");
        }

        if (cmd == "wifi?") {
            wifi_quality_label_->setText(wifiQualityLabel(response_q));
        }

        writeCommandMetricsRow(
            source,
            QString::fromStdString(cmd),
            static_cast<double>(command_elapsed_ms),
            rc,
            response_q,
            state_q);

        if (critical) {
            writeEventMetricsRow(QString("critical-command:end:") + QString::fromStdString(cmd));
            critical_command_active_ = false;

            constexpr int kCriticalResumeDelayMs = 1500;
            if (was_auto_refresh_active && sdk_ready_ && cmd != "emergency") {
                QTimer::singleShot(kCriticalResumeDelayMs, this, [this]() {
                    if (sdk_ready_ && auto_refresh_timer_ != nullptr && !auto_refresh_timer_->isActive()) {
                        auto_refresh_timer_->start(1000);
                        appendLog("auto-refresh resumed");
                        writeEventMetricsRow("auto-refresh:resumed");
                        updateStatusLabel();
                    }
                });
            }
            current_pause_keepalive_ms_ = 0;
            current_neutral_rc_ms_ = 0;
            current_critical_command_ms_ = 0;
        }
        updateStatusLabel();
    }

    void runRaw() {
        const QString raw = raw_command_edit_->text().trimmed();
        if (raw.isEmpty()) {
            appendLog("raw command is empty");
            return;
        }
        runCommandWithResponse(raw.toStdString(), "raw");
        raw_command_edit_->clear();
    }

    bool runRosServiceCall(const QStringList& arguments, const QString& label, int timeout_ms) {
        QProcess process;
        process.start("ros2", arguments);
        if (!process.waitForStarted(3000)) {
            appendLog(label + " => failed to start ros2");
            return false;
        }

        if (!process.waitForFinished(timeout_ms)) {
            process.kill();
            process.waitForFinished(1000);
            appendLog(label + " => timeout");
            return false;
        }

        const QString stdout_text = QString::fromLocal8Bit(process.readAllStandardOutput());
        const QString stderr_text = QString::fromLocal8Bit(process.readAllStandardError());
        const QString combined = (stdout_text + "\n" + stderr_text).simplified();
        const bool process_ok =
            process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
        const bool service_success =
            combined.contains("success=True", Qt::CaseInsensitive)
            || combined.contains("success: true", Qt::CaseInsensitive);
        const bool service_failure =
            combined.contains("success=False", Qt::CaseInsensitive)
            || combined.contains("success: false", Qt::CaseInsensitive);

        QString summary = combined;
        constexpr int kMaxSummaryChars = 240;
        if (summary.size() > kMaxSummaryChars) {
            summary = summary.left(kMaxSummaryChars) + "...";
        }

        if (process_ok && service_success && !service_failure) {
            appendLog(label + " => success" + (summary.isEmpty() ? "" : " | " + summary));
            return true;
        }

        appendLog(label + " => failed"
                  + (summary.isEmpty() ? "" : " | " + summary)
                  + " | exit " + QString::number(process.exitCode()));
        return false;
    }

    bool callRosTriggerService(const QString& service, const QString& label) {
        return runRosServiceCall(
            {"service", "call", service, "std_srvs/srv/Trigger", "{}"},
            label + " via ROS service " + service,
            60000);
    }

    bool callRosCommandService(const QString& command, const QString& source) {
        QString escaped_command = command;
        QString escaped_source = source;
        escaped_command.replace("\\", "\\\\").replace("'", "''");
        escaped_source.replace("\\", "\\\\").replace("'", "''");
        const QString payload = "{command: '" + escaped_command + "', source: '" + escaped_source + "'}";
        return runRosServiceCall(
            {"service", "call", "/tello/gui_command", "tello_interfaces/srv/Command", payload},
            command + " via ROS service /tello/gui_command",
            30000);
    }

    tello::TelloClient client_;
    bool sdk_ready_ = false;
    bool emergency_shutdown_sent_ = false;
    bool ros_mode_ = false;
    std::atomic<bool> connect_in_progress_{false};
    std::atomic<bool> connect_cancel_requested_{false};
    std::thread connect_worker_thread_;

    QLabel* status_label_ = nullptr;
    QLabel* link_quality_label_ = nullptr;
    QLabel* wifi_quality_label_ = nullptr;
    QLabel* battery_status_label_ = nullptr;
    QLabel* temperature_status_label_ = nullptr;
    QLabel* top_state_record_dot_label_ = nullptr;
    QLabel* top_state_record_status_label_ = nullptr;
    QLabel* recording_elapsed_label_ = nullptr;
    QTextEdit* log_view_ = nullptr;
    QPushButton* connect_btn_ = nullptr;
    QPushButton* disconnect_btn_ = nullptr;

    QTimer* auto_refresh_timer_ = nullptr;
    QCheckBox* rc_stream_check_ = nullptr;
    QSpinBox* rc_stream_interval_ms_ = nullptr;
    QTimer* rc_stream_timer_ = nullptr;
    QTimer* recording_elapsed_timer_ = nullptr;
    QCheckBox* keyboard_control_check_ = nullptr;
    QLabel* keyboard_control_profile_label_ = nullptr;
    QLabel* keyboard_control_vector_label_ = nullptr;
    QTimer* state_view_timer_ = nullptr;
    QTimer* vision_view_timer_ = nullptr;
    QTabWidget* panel_tabs_ = nullptr;

    std::vector<ControlProfile> control_profiles_;
    int active_control_profile_index_ = 0;
    QComboBox* control_profile_combo_ = nullptr;
    QLineEdit* control_profile_name_edit_ = nullptr;
    QComboBox* control_profile_input_combo_ = nullptr;
    QSpinBox* control_profile_aggression_spin_ = nullptr;
    std::map<QString, QKeySequenceEdit*> control_key_edits_;
    bool keyboard_control_active_ = false;
    std::set<int> active_keyboard_keys_;
    std::atomic<bool> keyboard_rc_worker_running_{false};
    std::atomic<bool> keyboard_rc_worker_stop_{false};
    std::thread keyboard_rc_worker_thread_;
    std::atomic<int> keyboard_rc_desired_a_{0};
    std::atomic<int> keyboard_rc_desired_b_{0};
    std::atomic<int> keyboard_rc_desired_c_{0};
    std::atomic<int> keyboard_rc_desired_d_{0};
    std::atomic<int64_t> keyboard_rc_last_update_ms_{0};
    RcChannels last_sent_rc_channels_;
    std::atomic<int64_t> last_rc_send_steady_ms_{0};
    std::atomic<int64_t> rc_packet_gap_ms_{-1};
    std::atomic<uint64_t> rc_blackout_count_{0};
    std::atomic<int64_t> last_nonzero_rc_steady_ms_{0};
    std::thread command_worker_thread_;
    std::mutex command_worker_mutex_;
    std::condition_variable command_worker_cv_;
    std::deque<std::function<void()>> command_worker_tasks_;
    std::atomic<int32_t> command_worker_pending_{0};
    bool command_worker_stop_ = false;
    bool command_worker_running_ = false;

    tello::StateReceiver state_receiver_;
    QSpinBox* state_buffer_capacity_spin_ = nullptr;
    QComboBox* state_metric_combo_ = nullptr;
    QLineEdit* state_record_path_edit_ = nullptr;
    QLabel* state_buffer_info_label_ = nullptr;
    QLabel* state_record_info_label_ = nullptr;
    QLabel* state_record_dot_label_ = nullptr;
    QLabel* state_record_status_label_ = nullptr;
    StatePlotWidget* state_plot_widget_ = nullptr;

    QLabel* vision_frame_label_ = nullptr;
    QLabel* vision_status_label_ = nullptr;
    QLabel* vision_rx_label_ = nullptr;
    QLabel* vision_nal_label_ = nullptr;
    QLabel* vision_fps_label_ = nullptr;
    QLabel* vision_size_label_ = nullptr;
    QLabel* vision_decode_err_label_ = nullptr;
    QLabel* vision_frame_info_label_ = nullptr;
    QPushButton* vision_start_btn_ = nullptr;
    QPushButton* vision_stop_btn_ = nullptr;
    QPushButton* vision_pause_btn_ = nullptr;
    QPushButton* vision_snapshot_btn_ = nullptr;
    QPushButton* vision_overlay_btn_ = nullptr;

    tello::VideoStreamReaderFfmpeg video_stream_reader_;
    std::mutex vision_frame_mutex_;
    QImage vision_latest_frame_;
    QImage vision_paused_frame_;
    std::atomic<uint64_t> vision_keyframes_{0};
    std::atomic<int32_t> vision_frame_width_{0};
    std::atomic<int32_t> vision_frame_height_{0};
    std::atomic<int64_t> vision_last_ui_frame_convert_ms_{0};
    std::atomic<uint64_t> vision_latest_frame_sequence_{0};
    uint64_t vision_displayed_frame_sequence_ = 0;
    bool vision_pipeline_running_ = false;
    bool vision_paused_ = false;
    bool vision_overlay_enabled_ = true;
    bool vision_recovery_in_progress_ = false;
    bool has_last_vision_recovery_queue_ = false;
    std::chrono::steady_clock::time_point last_vision_recovery_queue_tp_;
    bool critical_command_active_ = false;
    bool has_last_keepalive_restart_attempt_ = false;
    std::chrono::steady_clock::time_point last_keepalive_restart_attempt_tp_;
    int64_t current_pause_keepalive_ms_ = 0;
    int64_t current_neutral_rc_ms_ = 0;
    int64_t current_critical_command_ms_ = 0;
    bool has_last_vision_gui_tick_ = false;
    bool has_last_state_gui_tick_ = false;
    std::chrono::steady_clock::time_point last_vision_gui_tick_tp_;
    std::chrono::steady_clock::time_point last_state_gui_tick_tp_;
    int64_t last_gui_vision_tick_delay_ms_ = 0;
    int64_t last_gui_state_tick_delay_ms_ = 0;
    int64_t last_vision_refresh_duration_ms_ = 0;
    int64_t last_state_refresh_duration_ms_ = 0;
    std::atomic<int64_t> last_frame_convert_ms_{0};
    int64_t last_frame_scale_ms_ = 0;
    int64_t last_vision_frame_mutex_wait_ms_ = 0;
    int64_t last_state_history_fetch_ms_ = 0;
    std::atomic<uint64_t> ui_frames_converted_{0};
    std::atomic<uint64_t> ui_frames_dropped_{0};
    std::atomic<uint64_t> ui_frames_displayed_{0};

    QLineEdit* csv_path_edit_ = nullptr;
    bool gui_metrics_recording_ = false;
    std::vector<std::string> gui_metrics_rows_;
    tello::MetricsCollector metrics_;
    std::chrono::steady_clock::time_point metrics_started_at_;
    std::chrono::steady_clock::time_point recording_started_at_ = std::chrono::steady_clock::now();
    int64_t recording_elapsed_s_ = 0;
    uint64_t metrics_attempt_ = 0;
    std::chrono::steady_clock::time_point last_vision_metrics_csv_tp_;
    uint64_t last_vision_metrics_packets_ = 0;
    uint64_t last_metrics_state_packets_valid_ = 0;

#ifdef TELLO_CONTROL_PANEL_ROS_NATIVE
    bool ros_native_started_ = false;
    std::shared_ptr<rclcpp::Node> ros_node_;
    rclcpp::Publisher<geometry_msgs::msg::Twist>::SharedPtr ros_manual_cmd_vel_pub_;
    rclcpp::Subscription<tello_interfaces::msg::TelloState>::SharedPtr ros_state_sub_;
    rclcpp::Subscription<std_msgs::msg::Int32>::SharedPtr ros_battery_sub_;
    rclcpp::Subscription<std_msgs::msg::String>::SharedPtr ros_connection_sub_;
    rclcpp::Subscription<tello_interfaces::msg::LinkQuality>::SharedPtr ros_link_quality_sub_;
    rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr ros_image_sub_;
    std::thread ros_spin_thread_;
    std::mutex ros_state_mutex_;
    std::deque<tello::StateReceiver::StateSample> ros_state_buffer_;
    size_t ros_state_buffer_capacity_ = 1000;
    uint64_t ros_state_sequence_ = 0;
    QString ros_connection_state_;
    bool ros_have_link_quality_ = false;
    QString ros_link_overall_ = "NO_DATA";
    QString ros_link_reason_ = "no ROS link-quality sample";
    int32_t ros_link_score_ = 0;
    bool ros_link_safe_for_nonzero_rc_ = false;
#endif

    QSpinBox* speed_spin_ = nullptr;
    QSlider* rc_lr_slider_ = nullptr;
    QSlider* rc_fb_slider_ = nullptr;
    QSlider* rc_ud_slider_ = nullptr;
    QSlider* rc_yaw_slider_ = nullptr;
    QLabel* rc_lr_value_ = nullptr;
    QLabel* rc_fb_value_ = nullptr;
    QLabel* rc_ud_value_ = nullptr;
    QLabel* rc_yaw_value_ = nullptr;

    QSpinBox* up_cm_ = nullptr;
    QSpinBox* down_cm_ = nullptr;
    QSpinBox* left_cm_ = nullptr;
    QSpinBox* right_cm_ = nullptr;
    QSpinBox* forward_cm_ = nullptr;
    QSpinBox* back_cm_ = nullptr;
    QSpinBox* cw_deg_ = nullptr;
    QSpinBox* ccw_deg_ = nullptr;
    QLineEdit* flip_dir_ = nullptr;

    QLineEdit* raw_command_edit_ = nullptr;
};

} // namespace

int main(int argc, char* argv[]) {
    // WSLg can expose both Wayland and X11 paths. In some setups, Wayland+EGL
    // fails to initialize and results in no visible window. Prefer a stable
    // QWidget path with software rendering unless user explicitly overrides.
    const bool is_wsl = !qEnvironmentVariableIsEmpty("WSL_DISTRO_NAME");
    if (is_wsl) {
        if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
            qputenv("QT_QPA_PLATFORM", QByteArray("xcb"));
        }
        if (qEnvironmentVariableIsEmpty("QT_OPENGL")) {
            qputenv("QT_OPENGL", QByteArray("software"));
        }
        if (qEnvironmentVariableIsEmpty("QT_XCB_GL_INTEGRATION")) {
            qputenv("QT_XCB_GL_INTEGRATION", QByteArray("none"));
        }
        if (qEnvironmentVariableIsEmpty("LIBGL_ALWAYS_SOFTWARE")) {
            qputenv("LIBGL_ALWAYS_SOFTWARE", QByteArray("1"));
        }
    }

    QApplication::setAttribute(Qt::AA_UseSoftwareOpenGL);
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("CS500");
    QCoreApplication::setApplicationName("Tello Control Panel");

    std::cerr << "[tello_control_panel] QT_QPA_PLATFORM="
              << qgetenv("QT_QPA_PLATFORM").constData()
              << " QT_OPENGL=" << qgetenv("QT_OPENGL").constData()
              << " QT_XCB_GL_INTEGRATION=" << qgetenv("QT_XCB_GL_INTEGRATION").constData()
              << " LIBGL_ALWAYS_SOFTWARE=" << qgetenv("LIBGL_ALWAYS_SOFTWARE").constData()
              << std::endl;

    bool ros_mode = false;
    for (int i = 1; i < argc; ++i) {
        if (QString::fromLocal8Bit(argv[i]) == "--ros-mode") {
            ros_mode = true;
        }
    }

#ifdef TELLO_CONTROL_PANEL_ROS_NATIVE
    if (ros_mode) {
        rclcpp::init(argc, argv);
    }
#endif

    ControlPanelWidget panel(ros_mode);
    std::signal(SIGINT, [](int) {
        QMetaObject::invokeMethod(qApp, "quit", Qt::QueuedConnection);
    });
    std::signal(SIGTERM, [](int) {
        QMetaObject::invokeMethod(qApp, "quit", Qt::QueuedConnection);
    });
    panel.show();
    panel.showNormal();
    panel.raise();
    panel.activateWindow();
    const int rc = app.exec();
#ifdef TELLO_CONTROL_PANEL_ROS_NATIVE
    if (ros_mode && rclcpp::ok()) {
        rclcpp::shutdown();
    }
#endif
    return rc;
}
