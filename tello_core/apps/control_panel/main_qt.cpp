#include "tello/tello_client.hpp"
#include "tello/metrics.hpp"
#include "tello/state_receiver.hpp"
#include "tello/video_decoder_ffmpeg.hpp"
#include "tello/video_pipeline_recovery.hpp"
#include "tello/video_receiver.hpp"
#include "tello/video_stream_assembler.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QFileDialog>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QSignalBlocker>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <mutex>
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

        const QRectF plot_rect = rect().adjusted(42, 12, -12, -28);
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
        p.drawText(QPointF(plot_rect.left(), 10),
                   QString("current=%1")
                       .arg(metricFromState(samples_.back().state, metric_), 0, 'f', 2));
        p.drawText(QPointF(plot_rect.left(), plot_rect.top() - 2),
                   QString("%1  [min=%2 max=%3]")
                       .arg(metric_)
                       .arg(y_min, 0, 'f', 2)
                       .arg(y_max, 0, 'f', 2));
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
    ControlPanelWidget() {
        setWindowTitle("Tello Control Panel (Qt - Phase G Delivery C)");
        resize(1220, 900);

        auto* root = new QVBoxLayout(this);

        status_label_ = new QLabel(this);
        wifi_quality_label_ = new QLabel("wifi: unknown", this);
        battery_status_label_ = new QLabel("battery: --%", this);

        auto* status_row = new QHBoxLayout();
        status_row->addWidget(status_label_, 1);
        status_row->addStretch(1);
        status_row->addWidget(wifi_quality_label_);
        status_row->addWidget(battery_status_label_);
        root->addLayout(status_row);

        auto* conn_row = new QHBoxLayout();
        auto* connect_btn = new QPushButton("Connect + SDK", this);
        auto* disconnect_btn = new QPushButton("Disconnect", this);
        conn_row->addWidget(connect_btn);
        conn_row->addWidget(disconnect_btn);
        conn_row->addStretch(1);
        root->addLayout(conn_row);

        buildControlBasicsGroup(root);

        log_view_ = new QTextEdit(this);
        log_view_->setReadOnly(true);
        log_view_->setMinimumHeight(180);
        root->addWidget(log_view_);

        auto* panel_sel_row = new QHBoxLayout();
        panel_sel_row->addWidget(new QLabel("Panel:", this));
        panel_selector_ = new QComboBox(this);
        panel_selector_->addItem("Config");
        panel_selector_->addItem("Operation");
        panel_sel_row->addWidget(panel_selector_);
        panel_sel_row->addStretch(1);
        root->addLayout(panel_sel_row);

        panel_stack_ = new QStackedWidget(this);

        auto* config_page = new QWidget(this);
        auto* config_layout = new QVBoxLayout(config_page);
        buildAutoRefreshAndCsvGroup(config_layout);
        buildReadGroup(config_layout);
        buildSetGroup(config_layout);
        buildMotionGroup(config_layout);
        buildRawGroup(config_layout);
        config_layout->addStretch(1);
        panel_stack_->addWidget(config_page);

        auto* operation_page = new QWidget(this);
        auto* operation_layout = new QVBoxLayout(operation_page);
        buildRcOperationGroup(operation_layout);
        buildStateHistoryGroup(operation_layout);
        buildVisionGroup(operation_layout);
        operation_layout->addStretch(1);
        panel_stack_->addWidget(operation_page);

        root->addWidget(panel_stack_, 1);

        auto_refresh_timer_ = new QTimer(this);
        rc_stream_timer_ = new QTimer(this);
        state_view_timer_ = new QTimer(this);
        vision_view_timer_ = new QTimer(this);

        connect(connect_btn, &QPushButton::clicked, this, [this]() { (void)connectSdk(); });
        connect(disconnect_btn, &QPushButton::clicked, this, [this]() { disconnectSdk(); });

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

        connect(panel_selector_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int idx) {
            if (panel_stack_ != nullptr) {
                panel_stack_->setCurrentIndex(idx);
            }
            appendLog(QString("panel switched to %1").arg(idx == 0 ? "Config" : "Operation"));
        });

        appendLog("Panel ready. Click Connect + SDK first.");
        appendLog("Delivery C active: optional auto-refresh + CSV export.");
        appendLog("Buttons added for control/set/read command families.");
        updateStatusLabel();
    }

    ~ControlPanelWidget() override {
        stopVisionPipeline();
        setRcStreamingEnabled(false, "panel shutdown");
        closeCsv();
        state_receiver_.stop();
        client_.shutdown();
    }

private:
    struct CommandTimingResult {
        tello::ResponseCode rc = tello::ResponseCode::ERROR;
        QString response;
        int64_t elapsed_ms = 0;
    };

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

    void buildAutoRefreshAndCsvGroup(QVBoxLayout* root) {
        auto* g = new QGroupBox("Delivery C: Auto Refresh + CSV", this);
        auto* l = new QGridLayout(g);

        auto_refresh_check_ = new QCheckBox("Enable battery?/wifi? auto-refresh", g);
        auto_refresh_interval_ms_ = new QSpinBox(g);
        auto_refresh_interval_ms_->setRange(200, 10000);
        auto_refresh_interval_ms_->setValue(1000);

        csv_enabled_ = new QCheckBox("Enable CSV", g);
        csv_path_edit_ = new QLineEdit("control_panel_commands.csv", g);

        l->addWidget(auto_refresh_check_, 0, 0, 1, 2);
        l->addWidget(new QLabel("interval ms:", g), 0, 2);
        l->addWidget(auto_refresh_interval_ms_, 0, 3);

        l->addWidget(csv_enabled_, 1, 0);
        l->addWidget(new QLabel("csv path:", g), 1, 1);
        l->addWidget(csv_path_edit_, 1, 2, 1, 2);

        connect(auto_refresh_check_, &QCheckBox::toggled, this, [this](bool enabled) {
            if (!enabled) {
                auto_refresh_timer_->stop();
                appendLog("auto-refresh OFF");
                return;
            }
            if (!sdk_ready_) {
                appendLog("auto-refresh requires Connect + SDK first");
                auto_refresh_check_->setChecked(false);
                return;
            }
            auto_refresh_timer_->start(auto_refresh_interval_ms_->value());
            appendLog(QString("auto-refresh ON (%1 ms)").arg(auto_refresh_interval_ms_->value()));
        });

        connect(auto_refresh_interval_ms_, qOverload<int>(&QSpinBox::valueChanged), this, [this](int) {
            if (auto_refresh_check_->isChecked()) {
                auto_refresh_timer_->start(auto_refresh_interval_ms_->value());
            }
        });

        connect(csv_enabled_, &QCheckBox::toggled, this, [this](bool enabled) {
            if (!enabled) {
                closeCsv();
                appendLog("csv export OFF");
                return;
            }
            if (openCsv()) {
                appendLog("csv export ON");
            } else {
                csv_enabled_->setChecked(false);
            }
        });

        root->addWidget(g);
    }

    void buildSetGroup(QVBoxLayout* root) {
        auto* g = new QGroupBox("Set Speed", this);
        auto* l = new QGridLayout(g);

        speed_spin_ = new QSpinBox(g);
        speed_spin_->setRange(10, 100);
        speed_spin_->setValue(50);
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
            runCommandWithResponse(buildRcCommandFromInputs(), "manual");
        });

        connect(zero_send_btn, &QPushButton::clicked, this, [this]() {
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

    void buildControlBasicsGroup(QVBoxLayout* root) {
        auto* g = new QGroupBox("Control Commands (basic)", this);
        auto* l = new QHBoxLayout(g);

        auto* takeoff_btn = new QPushButton("takeoff", g);
        auto* land_btn = new QPushButton("land", g);
        auto* emergency_btn = new QPushButton("emergency", g);
        auto* streamon_btn = new QPushButton("streamon", g);
        auto* streamoff_btn = new QPushButton("streamoff", g);
        auto* stop_btn = new QPushButton("stop", g);

        l->addWidget(takeoff_btn);
        l->addWidget(land_btn);
        l->addWidget(emergency_btn);
        l->addWidget(streamon_btn);
        l->addWidget(streamoff_btn);
        l->addWidget(stop_btn);
        l->addStretch(1);

        connect(takeoff_btn, &QPushButton::clicked, this, [this]() { runCommandWithResponse("takeoff", "manual"); });
        connect(land_btn, &QPushButton::clicked, this, [this]() { runCommandWithResponse("land", "manual"); });
        connect(emergency_btn, &QPushButton::clicked, this, [this]() { runCommandWithResponse("emergency", "manual"); });
        connect(streamon_btn, &QPushButton::clicked, this, [this]() { runCommandWithResponse("streamon", "manual"); });
        connect(streamoff_btn, &QPushButton::clicked, this, [this]() { runCommandWithResponse("streamoff", "manual"); });
        connect(stop_btn, &QPushButton::clicked, this, [this]() { runCommandWithResponse("stop", "manual"); });

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

        auto addRow = [this, g, l](int row, const QString& label, QSpinBox* spin, const QString& cmd_prefix) {
            auto* btn = new QPushButton("Send " + cmd_prefix + " x", g);
            l->addWidget(new QLabel(label), row, 0);
            l->addWidget(spin, row, 1);
            l->addWidget(btn, row, 2);
            connect(btn, &QPushButton::clicked, this, [this, spin, cmd_prefix]() {
                runCommandWithResponse(cmd_prefix.toStdString() + " " + std::to_string(spin->value()), "manual");
            });
        };

        addRow(0, "up x", up_cm_, "up");
        addRow(1, "down x", down_cm_, "down");
        addRow(2, "left x", left_cm_, "left");
        addRow(3, "right x", right_cm_, "right");
        addRow(4, "forward x", forward_cm_, "forward");
        addRow(5, "back x", back_cm_, "back");
        addRow(6, "cw x", cw_deg_, "cw");
        addRow(7, "ccw x", ccw_deg_, "ccw");

        auto* flip_btn = new QPushButton("Send flip x", g);
        l->addWidget(new QLabel("flip x"), 8, 0);
        l->addWidget(flip_dir_, 8, 1);
        l->addWidget(flip_btn, 8, 2);
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

        state_record_path_edit_ = new QLineEdit("state_recording.csv", g);
        auto* browse_btn = new QPushButton("Browse...", g);
        auto* start_rec_btn = new QPushButton("Start Recording", g);
        auto* stop_rec_btn = new QPushButton("Stop Recording", g);
        auto* clear_rec_btn = new QPushButton("Clear Recording", g);
        auto* export_rec_btn = new QPushButton("Export CSV", g);

        state_buffer_info_label_ = new QLabel("buffer=0", g);
        state_record_info_label_ = new QLabel("recorded=0", g);

        state_plot_widget_ = new StatePlotWidget(g);

        l->addWidget(new QLabel("buffer size:"), 0, 0);
        l->addWidget(state_buffer_capacity_spin_, 0, 1);
        l->addWidget(apply_capacity_btn, 0, 2);
        l->addWidget(new QLabel("metric:"), 0, 3);
        l->addWidget(state_metric_combo_, 0, 4);

        l->addWidget(start_rec_btn, 1, 0);
        l->addWidget(stop_rec_btn, 1, 1);
        l->addWidget(clear_rec_btn, 1, 2);
        l->addWidget(export_rec_btn, 1, 3);
        l->addWidget(state_record_info_label_, 1, 4);

        l->addWidget(new QLabel("recording csv:"), 2, 0);
        l->addWidget(state_record_path_edit_, 2, 1, 1, 3);
        l->addWidget(browse_btn, 2, 4);

        l->addWidget(state_plot_widget_, 3, 0, 1, 5);
        l->addWidget(state_buffer_info_label_, 4, 0, 1, 5);

        connect(apply_capacity_btn, &QPushButton::clicked, this, [this]() {
            state_receiver_.setStateBufferCapacity(static_cast<size_t>(state_buffer_capacity_spin_->value()));
            appendLog(QString("state buffer size set to %1").arg(state_buffer_capacity_spin_->value()));
        });

        connect(state_metric_combo_, &QComboBox::currentTextChanged, this, [this](const QString& metric) {
            appendLog("plot metric changed => " + metric);
            writeEventMetricsRow("plot_metric_changed:" + metric);
        });

        connect(start_rec_btn, &QPushButton::clicked, this, [this]() {
            state_receiver_.startStateRecording();
            appendLog("state recording started");
        });

        connect(stop_rec_btn, &QPushButton::clicked, this, [this]() {
            state_receiver_.stopStateRecording();
            appendLog("state recording stopped");
        });

        connect(clear_rec_btn, &QPushButton::clicked, this, [this]() {
            state_receiver_.clearRecordedStateSamples();
            appendLog("state recording cleared");
        });

        connect(export_rec_btn, &QPushButton::clicked, this, [this]() {
            const QString path = state_record_path_edit_->text().trimmed();
            if (path.isEmpty()) {
                appendLog("state csv path is empty");
                return;
            }
            const auto rc = state_receiver_.exportRecordedStateCsv(path.toStdString());
            appendLog(QString("state csv export => %1 (%2)")
                          .arg(QString::fromStdString(responseCodeToString(rc)))
                          .arg(path));
        });

        connect(browse_btn, &QPushButton::clicked, this, [this]() {
            const QString picked = QFileDialog::getSaveFileName(
                this,
                "Export State CSV",
                state_record_path_edit_->text().trimmed(),
                "CSV (*.csv);;All files (*)");
            if (!picked.isEmpty()) {
                state_record_path_edit_->setText(picked);
            }
        });

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
        vision_rx_label_ = new QLabel("rx packets: 0", right_host);
        vision_nal_label_ = new QLabel("nal units: 0", right_host);
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

    void refreshStateHistoryView() {
        const auto tick_started_at = std::chrono::steady_clock::now();
        if (has_last_state_gui_tick_) {
            last_gui_state_tick_delay_ms_ = std::chrono::duration_cast<std::chrono::milliseconds>(
                tick_started_at - last_state_gui_tick_tp_).count();
        }
        last_state_gui_tick_tp_ = tick_started_at;
        has_last_state_gui_tick_ = true;

        if (state_receiver_.isRunning()) {
            const auto history_fetch_started_at = std::chrono::steady_clock::now();
            const auto buffered = state_receiver_.getBufferedStateSamples();
            const auto recorded_count = state_receiver_.getRecordedStateSampleCount();
            const auto rc_recorded_count = state_receiver_.getRecordedRcCommandSampleCount();
            last_state_history_fetch_ms_ = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - history_fetch_started_at).count();
            const auto metric = state_metric_combo_ != nullptr ? state_metric_combo_->currentText() : QString("pitch");

            if (state_receiver_.hasReceivedState() && battery_status_label_ != nullptr) {
                const auto latest = state_receiver_.getLatestState();
                battery_status_label_->setText(QString("battery: %1%").arg(latest.bat));
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
    }

    void refreshVisionView() {
#ifdef TELLO_HAS_FFMPEG
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

        if (vision_pipeline_running_) {
            const auto rx_stats = video_receiver_.getVideoStats();

            maybeRecoverVisionPipeline(rx_stats);
            writeVisionMetricsRowIfDue();

            const auto refreshed_rx_stats = video_receiver_.getVideoStats();
            const auto refreshed_nal_stats = video_assembler_.getStats();
            const auto refreshed_dec_stats = video_decoder_.getStats();

            if (vision_rx_label_ != nullptr) {
                vision_rx_label_->setText(QString("rx packets: %1 (ema=%2 pps)")
                                              .arg(static_cast<qulonglong>(refreshed_rx_stats.packets_total))
                                              .arg(refreshed_rx_stats.rx_pps_ema, 0, 'f', 1));
            }
            if (vision_nal_label_ != nullptr) {
                vision_nal_label_->setText(QString("nal units: %1")
                                               .arg(static_cast<qulonglong>(refreshed_nal_stats.nal_units_out)));
            }
            if (vision_fps_label_ != nullptr) {
                vision_fps_label_->setText(QString("decode fps: %1")
                                               .arg(refreshed_dec_stats.decode_fps_ema, 0, 'f', 2));
            }
            if (vision_size_label_ != nullptr) {
                vision_size_label_->setText(QString("size: %1x%2")
                                                .arg(vision_frame_width_.load())
                                                .arg(vision_frame_height_.load()));
            }
            if (vision_decode_err_label_ != nullptr) {
                vision_decode_err_label_->setText(QString("decode errors: %1")
                                                      .arg(static_cast<qulonglong>(refreshed_dec_stats.decode_errors)));
            }
            if (vision_frame_info_label_ != nullptr) {
                vision_frame_info_label_->setText(
                    QString("frames: %1 | keyframes: %2")
                        .arg(static_cast<qulonglong>(refreshed_dec_stats.frames_decoded))
                        .arg(static_cast<qulonglong>(vision_keyframes_.load())));
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
            const auto dec_stats = video_decoder_.getStats();
            const auto rx_stats = video_receiver_.getVideoStats();
            const auto nal_stats = video_assembler_.getStats();

            QPainter p(&composed);
            p.setRenderHint(QPainter::Antialiasing, true);
            p.setPen(QPen(QColor(0, 255, 110), 1));
            p.drawText(10, 22, QString("fps=%1 frames=%2 keyframes=%3")
                                .arg(dec_stats.decode_fps_ema, 0, 'f', 2)
                                .arg(static_cast<qulonglong>(dec_stats.frames_decoded))
                                .arg(static_cast<qulonglong>(vision_keyframes_.load())));
            p.drawText(10, 42, QString("packets=%1 nal=%2 dec_errors=%3")
                                .arg(static_cast<qulonglong>(rx_stats.packets_total))
                                .arg(static_cast<qulonglong>(nal_stats.nal_units_out))
                                .arg(static_cast<qulonglong>(dec_stats.decode_errors)));
            p.drawText(10, 62, QString("state=%1 controls: Pause, Snapshot, Overlay")
                                .arg(vision_paused_ ? "PAUSED" : "LIVE"));
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
#else
        if (vision_status_label_ != nullptr) {
            vision_status_label_->setText("status: FFmpeg backend not available");
        }
#endif
    }

#ifdef TELLO_HAS_FFMPEG
    void resetVisionRuntimeStateForRestart() {
        video_decoder_seen_sps_.store(false);
        video_decoder_seen_pps_.store(false);
        video_decoder_synced_.store(false);
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
        const tello::VideoPipelineRecoveryResult& recovery,
        const tello::VideoReceiver::VideoStats& rx_stats) {
        if (!csv_enabled_->isChecked() || !csv_file_.is_open()) {
            return;
        }

        const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - metrics_started_at_).count();
        const auto nal_stats = video_assembler_.getStats();
        const auto dec_stats = video_decoder_.getStats();

        ++metrics_attempt_;
        updateRuntimeMetricsContext();
        metrics_.setElapsedMs(elapsed_ms);
        metrics_.setAttempt(metrics_attempt_);
        metrics_.updateVideoReceiverStats(rx_stats);
        metrics_.setVideoPacketDelta(0);
        metrics_.updateVideoAssemblerStats(nal_stats);
        metrics_.updateDecoderStats(dec_stats);
        metrics_.updateFrameInfo(vision_frame_width_.load(), vision_frame_height_.load(), false);
        metrics_.updateDisplayState(vision_paused_, vision_overlay_enabled_);
        metrics_.recordRecoveryEvent(
            recovery.session.attempted,
            recovery.session.result,
            recovery.session.used_hard_recovery,
            recovery.session.stage,
            recovery.session.command_channel_available);
        metrics_.setConnectionState(connectionStateToString(client_.getConnectionState()));
        metrics_.setEvent(recovery.power_cycle_recovery_used ? "vision_recovery:power_cycle" : "vision_recovery");
        metrics_.setLastOutageFailures(client_.getLastOutageFailures());
        csv_file_ << metrics_.toCsvLine() << std::endl;
    }

    void maybeRecoverVisionPipeline(const tello::VideoReceiver::VideoStats& rx_stats) {
        if (!vision_pipeline_running_ || !sdk_ready_) {
            return;
        }

        constexpr int64_t kVisionStallThresholdMs = 3000;
        constexpr int64_t kVisionRecoveryCooldownMs = 3000;
        tello::VideoPipelineRecoveryOptions options;
        options.bind_ip = "0.0.0.0";
        options.video_port = 11111;
        options.receiver_timeout_ms = 500;

        tello::VideoPipelineRecoveryHooks hooks;
        hooks.before_restart = [this]() {
            resetVisionRuntimeStateForRestart();
        };

        const tello::VideoPipelineRecoveryResult recovery =
            tello::VideoPipelineRecovery::recoverIfStalled(
                client_,
                video_receiver_,
                video_assembler_,
                rx_stats.last_packet_age_ms,
                kVisionStallThresholdMs,
                kVisionRecoveryCooldownMs,
                options,
                hooks,
                &video_decoder_,
                nullptr);

        if (!recovery.session.attempted) {
            return;
        }

        if (recovery.session.command_channel_available || recovery.session.result == tello::ResponseCode::OK) {
            sdk_ready_ = true;
        } else if (recovery.power_cycle_recovery_used) {
            sdk_ready_ = false;
        }

        appendLog(QString("vision recovery => %1 | stage=%2 | command_channel=%3 | hard=%4 | video_restart=%5")
                      .arg(QString::fromStdString(responseCodeToString(recovery.session.result)))
                      .arg(QString::fromStdString(recovery.session.stage))
                      .arg(recovery.session.command_channel_available ? "available" : "unavailable")
                      .arg(recovery.session.used_hard_recovery ? "yes" : "no")
                      .arg(recovery.video_pipeline_restarted ? "yes" : "no"));

        if (recovery.video_restart_result != tello::ResponseCode::OK) {
            appendLog("vision local pipeline restart failed => "
                      + QString::fromStdString(responseCodeToString(recovery.video_restart_result)));
        }

        writeVisionRecoveryMetricsRow(recovery, rx_stats);
        updateStatusLabel();
    }

    bool startVisionWithStreamOn() {
        if (!ensureSdkReady()) {
            return false;
        }

        std::string response;
        const auto command_started_at = std::chrono::steady_clock::now();
        const auto rc = client_.sendCommandWithResponse("streamon", response);
        const auto command_elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - command_started_at).count();
        const QString rc_q = QString::fromStdString(responseCodeToString(rc));
        const QString response_q = QString::fromStdString(response);
        const QString state_q = QString::fromStdString(connectionStateToString(client_.getConnectionState()));

        QString line = "streamon (vision) => " + rc_q;
        if (!response.empty()) {
            line += " | response=\"" + response_q + "\"";
        }
        line += " | state=" + state_q;
        appendLog(line);
        writeCommandMetricsRow("vision", "streamon", static_cast<double>(command_elapsed_ms), rc, response_q, state_q);

        return startVisionPipeline();
    }

    void stopVisionWithStreamOff() {
        stopVisionPipeline();

        if (!sdk_ready_) {
            return;
        }

        std::string response;
        const auto command_started_at = std::chrono::steady_clock::now();
        const auto rc = client_.sendCommandWithResponse("streamoff", response);
        const auto command_elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - command_started_at).count();
        const QString rc_q = QString::fromStdString(responseCodeToString(rc));
        const QString response_q = QString::fromStdString(response);
        const QString state_q = QString::fromStdString(connectionStateToString(client_.getConnectionState()));

        QString line = "streamoff (vision) => " + rc_q;
        if (!response.empty()) {
            line += " | response=\"" + response_q + "\"";
        }
        line += " | state=" + state_q;
        appendLog(line);
        writeCommandMetricsRow("vision", "streamoff", static_cast<double>(command_elapsed_ms), rc, response_q, state_q);
    }

    bool startVisionPipeline() {
        if (vision_pipeline_running_) {
            return true;
        }
        if (!ensureSdkReady()) {
            return false;
        }

        video_assembler_.reset();
        video_decoder_seen_sps_.store(false);
        video_decoder_seen_pps_.store(false);
        video_decoder_synced_.store(false);
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

        if (!video_decoder_.initialize()) {
            appendLog("vision start failed: decoder initialize error");
            return false;
        }
        video_decoder_.resetStats();

        video_decoder_.setFrameCallback([this](const tello::VideoDecoderFfmpeg::DecodedFrameInfo& frame_info) {
            if (frame_info.is_key_frame) {
                vision_keyframes_.fetch_add(1);
            }
            vision_frame_width_.store(frame_info.width);
            vision_frame_height_.store(frame_info.height);
        });

        video_decoder_.setFrameDataCallback([this](const tello::VideoDecoderFfmpeg::DecodedFrame& frame) {
            if (frame.bgr.empty() || frame.info.width <= 0 || frame.info.height <= 0 || frame.bgr_stride <= 0) {
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
            QImage rgb(frame.info.width, frame.info.height, QImage::Format_RGB888);
            for (int y = 0; y < frame.info.height; ++y) {
                const uint8_t* src = frame.bgr.data() + (static_cast<size_t>(y) * static_cast<size_t>(frame.bgr_stride));
                uint8_t* dst = rgb.scanLine(y);
                for (int x = 0; x < frame.info.width; ++x) {
                    const uint8_t b = src[x * 3 + 0];
                    const uint8_t g = src[x * 3 + 1];
                    const uint8_t r = src[x * 3 + 2];
                    dst[x * 3 + 0] = r;
                    dst[x * 3 + 1] = g;
                    dst[x * 3 + 2] = b;
                }
            }
            last_frame_convert_ms_ = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - convert_started_at).count();
            ui_frames_converted_.fetch_add(1);

            std::lock_guard<std::mutex> lock(vision_frame_mutex_);
            vision_latest_frame_ = rgb;
            if (!vision_paused_) {
                vision_paused_frame_ = rgb;
            }
            vision_latest_frame_sequence_.fetch_add(1);
        });

        video_assembler_.setNalCallback([this](const std::vector<uint8_t>& nal) {
            if (nal.empty()) {
                return;
            }

            const uint8_t nal_type = static_cast<uint8_t>(nal[0] & 0x1F);
            if (nal_type == 7) {
                video_decoder_seen_sps_.store(true);
            } else if (nal_type == 8) {
                video_decoder_seen_pps_.store(true);
            } else if (nal_type == 5 && video_decoder_seen_sps_.load() && video_decoder_seen_pps_.load()) {
                video_decoder_synced_.store(true);
            }

            const bool allow_decode = (nal_type == 7 || nal_type == 8 || nal_type == 5 || video_decoder_synced_.load());
            if (allow_decode) {
                (void)video_decoder_.decodeNal(nal);
            }
        });

        video_receiver_.setPacketCallback([this](const std::vector<uint8_t>& packet) {
            video_assembler_.pushPacket(packet);
        });

        const auto recv_rc = video_receiver_.start("0.0.0.0", 11111, 500);
        if (recv_rc != tello::ResponseCode::OK) {
            appendLog("vision start failed: video receiver start error");
            video_decoder_.shutdown();
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
        appendLog("vision pipeline started (ensure streamon is active)");
        return true;
    }

    void stopVisionPipeline() {
        if (!vision_pipeline_running_) {
            return;
        }

        vision_pipeline_running_ = false;
        video_receiver_.stop();
        video_decoder_.shutdown();

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
#else
    bool startVisionWithStreamOn() {
        appendLog("vision not available: FFmpeg backend disabled");
        return false;
    }

    void stopVisionWithStreamOff() {
        appendLog("vision not available: FFmpeg backend disabled");
    }

    bool startVisionPipeline() {
        appendLog("vision not available: FFmpeg backend disabled");
        return false;
    }

    void stopVisionPipeline() {}

    void toggleVisionPause() {
        appendLog("vision not available: FFmpeg backend disabled");
    }

    void saveVisionSnapshot() {
        appendLog("vision not available: FFmpeg backend disabled");
    }
#endif

    void appendLog(const QString& msg) {
        log_view_->append("[" + nowClockString() + "] " + msg);
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
        return (rc_lr_slider_ != nullptr && rc_lr_slider_->value() != 0)
            || (rc_fb_slider_ != nullptr && rc_fb_slider_->value() != 0)
            || (rc_ud_slider_ != nullptr && rc_ud_slider_->value() != 0)
            || (rc_yaw_slider_ != nullptr && rc_yaw_slider_->value() != 0);
    }

    CommandTimingResult sendRcNeutralBestEffort(const QString& reason, bool write_metrics_row = true) {
        CommandTimingResult result;
        if (!sdk_ready_) {
            return result;
        }

        const auto started_at = std::chrono::steady_clock::now();
        result.rc = client_.sendCommandNoWait("rc 0 0 0 0");
        result.elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started_at).count();
        result.response = "no_wait";

        state_receiver_.recordRcCommandSample(0, 0, 0, 0, ("rc-neutral-" + reason).toStdString(), result.rc);
        const QString rc_q = QString::fromStdString(responseCodeToString(result.rc));
        if (result.rc == tello::ResponseCode::OK) {
            appendLog("rc stream neutralized no-wait (" + reason + ") => " + rc_q);
        } else {
            appendLog("rc stream neutralize no-wait failed (" + reason + ") => " + rc_q);
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
            if (rc_stream_timer_ != nullptr) {
                rc_stream_timer_->start(rc_stream_interval_ms_ != nullptr ? rc_stream_interval_ms_->value() : 50);
            }
            appendLog(QString("continuous RC ON (%1 ms)").arg(rc_stream_interval_ms_ != nullptr ? rc_stream_interval_ms_->value() : 50));
            writeEventMetricsRow(QString("rc-stream:on:") + reason);
            return;
        }

        if (rc_stream_timer_ != nullptr) {
            rc_stream_timer_->stop();
        }
        sendRcNeutralBestEffort(reason);
        appendLog("continuous RC OFF (" + reason + ")");
        writeEventMetricsRow(QString("rc-stream:off:") + reason);
    }

    void sendRcStreamTick() {
        if (!sdk_ready_ || critical_command_active_
            || rc_stream_check_ == nullptr || !rc_stream_check_->isChecked()) {
            return;
        }

        const std::string cmd = buildRcCommandFromInputs();
        const auto rc = client_.sendCommandNoWait(cmd);
        int a = 0;
        int b = 0;
        int c = 0;
        int d = 0;
        if (parseRcCommand(cmd, a, b, c, d)) {
            state_receiver_.recordRcCommandSample(a, b, c, d, "rc-stream", rc);
        }
        if (rc != tello::ResponseCode::OK) {
            appendLog("rc stream tick => " + QString::fromStdString(responseCodeToString(rc)));
        }
    }

    void updateStatusLabel() {
        status_label_->setText(
            "State: " + QString::fromStdString(connectionStateToString(client_.getConnectionState()))
            + " | SDK: " + QString(sdk_ready_ ? "READY" : "NOT_READY")
            + " | RC_STREAM=" + QString((rc_stream_timer_ != nullptr && rc_stream_timer_->isActive()) ? "ON" : "OFF")
            + " | speed_setpoint=" + QString::number(speed_spin_ != nullptr ? speed_spin_->value() : 0));
    }

    bool connectSdk() {
        // Recovery-friendly connect flow: always refresh command channel state.
        // This is important after drone power-cycle while the app stays open.
        constexpr int kConnectAttempts = 6;
        constexpr int kSdkProbesPerAttempt = 3;
        constexpr int kRebindPauseMs = 350;
        constexpr int kSdkProbeGapMs = 500;
        constexpr int kPostInitSettleMs = 900;

        for (int attempt = 1; attempt <= kConnectAttempts; ++attempt) {
            client_.shutdown();
            std::this_thread::sleep_for(std::chrono::milliseconds(kRebindPauseMs));

            const auto init_rc = client_.initialize("192.168.10.1", 8889, 9000);
            appendLog("initialize(attempt=" + QString::number(attempt) + "): "
                      + QString::fromStdString(responseCodeToString(init_rc)));
            if (init_rc != tello::ResponseCode::OK) {
                const int backoff_ms = 600 + attempt * 400;
                std::this_thread::sleep_for(std::chrono::milliseconds(backoff_ms));
                continue;
            }

            // After power-cycle, the drone can accept Wi-Fi but still be finishing SDK stack startup.
            std::this_thread::sleep_for(std::chrono::milliseconds(kPostInitSettleMs));

            for (int probe = 1; probe <= kSdkProbesPerAttempt; ++probe) {
                const auto sdk_rc = client_.enterSdkMode();
                appendLog("command (enterSdkMode, attempt=" + QString::number(attempt)
                          + ", probe=" + QString::number(probe) + "): "
                          + QString::fromStdString(responseCodeToString(sdk_rc)));
                if (sdk_rc == tello::ResponseCode::OK) {
                    if (!state_receiver_.isRunning()) {
                        const auto state_rc = state_receiver_.start("0.0.0.0", 8890, 1000);
                        appendLog("state receiver start => " + QString::fromStdString(responseCodeToString(state_rc)));
                        state_receiver_.setStateBufferCapacity(static_cast<size_t>(state_buffer_capacity_spin_->value()));
                    }
                    sdk_ready_ = true;
                    const auto keepalive_rc = client_.startSdkKeepalive(5000);
                    appendLog("sdk keepalive start => " + QString::fromStdString(responseCodeToString(keepalive_rc)));
                    updateStatusLabel();
                    return true;
                }

                if (probe < kSdkProbesPerAttempt) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(kSdkProbeGapMs));
                }
            }

            const int backoff_ms = 1000 + attempt * 500;
            std::this_thread::sleep_for(std::chrono::milliseconds(backoff_ms));
        }

        sdk_ready_ = false;
        appendLog("Connect + SDK failed after retries. If the drone was just powered on, wait 10-15s and try again.");
        updateStatusLabel();
        return false;
    }

    void disconnectSdk() {
        client_.stopSdkKeepalive();
        auto_refresh_timer_->stop();
        auto_refresh_check_->setChecked(false);
        if (rc_stream_check_ != nullptr && rc_stream_check_->isChecked()) {
            QSignalBlocker blocker(rc_stream_check_);
            rc_stream_check_->setChecked(false);
        }
        setRcStreamingEnabled(false, "disconnect");
        stopVisionPipeline();
        state_receiver_.stop();
        client_.shutdown();
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

    bool openCsv() {
        const QString path = csv_path_edit_->text().trimmed();
        if (path.isEmpty()) {
            appendLog("csv path is empty");
            return false;
        }

        if (csv_file_.is_open()) {
            csv_file_.close();
        }
        csv_file_.open(path.toStdString(), std::ios::out | std::ios::trunc);
        if (!csv_file_.is_open()) {
            appendLog("failed to open csv: " + path);
            return false;
        }

        metrics_.reset();
        tello::MetricsCollector::ExperimentMetadata metadata{};
        metadata.run_mode = "CONTROL_PANEL";
        metadata.scenario = "gui";
        metrics_.setExperimentMetadata(metadata);
        metrics_started_at_ = std::chrono::steady_clock::now();
        metrics_attempt_ = 0;
        last_vision_metrics_csv_tp_ = metrics_started_at_;
        last_metrics_state_packets_valid_ = state_receiver_.getTelemetryStats().packets_valid;

        csv_file_ << metrics_.toCsvHeader() << std::endl;
        return true;
    }

    void closeCsv() {
        if (csv_file_.is_open()) {
            csv_file_.close();
        }
    }

    void updateRuntimeMetricsContext() {
        int a = 0;
        int b = 0;
        int c = 0;
        int d = 0;
        (void)parseRcCommand(buildRcCommandFromInputs(), a, b, c, d);

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
        metrics_.updatePanelDiagnostics(
            state_metric_combo_ != nullptr ? state_metric_combo_->currentText().toStdString() : "",
            state_sequence_delta,
            state_receiver_.isRunning());
        metrics_.updateRuntimeContext(
            rc_stream_timer_ != nullptr && rc_stream_timer_->isActive(),
            a,
            b,
            c,
            d,
            client_.isSdkKeepaliveRunning(),
            auto_refresh_timer_ != nullptr && auto_refresh_timer_->isActive(),
            critical_command_active_);
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
            ui_frames_converted_.load(),
            ui_frames_dropped_.load(),
            ui_frames_displayed_.load(),
            state_plot_widget_ != nullptr
                ? static_cast<uint64_t>(state_plot_widget_->plottedSamplesCount())
                : 0);
    }

    void writeEventMetricsRow(const QString& event) {
        if (!csv_enabled_->isChecked() || !csv_file_.is_open()) {
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
        metrics_.setLastOutageFailures(client_.getLastOutageFailures());
        csv_file_ << metrics_.toCsvLine() << std::endl;
    }

    void writeCommandMetricsRow(const QString& source,
                                const QString& command,
                                double latency_ms,
                                tello::ResponseCode result,
                                const QString& response,
                                const QString& state) {
        if (!csv_enabled_->isChecked() || !csv_file_.is_open()) {
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
        metrics_.updateCommandDiagnostics(source.toStdString(), executor_error, executor_attempt_log);
        metrics_.setConnectionState(state.toStdString());
        metrics_.setEvent((QString("command:") + source).toStdString());
        metrics_.setLastOutageFailures(client_.getLastOutageFailures());
        csv_file_ << metrics_.toCsvLine() << std::endl;
    }

#ifdef TELLO_HAS_FFMPEG
    void writeVisionMetricsRowIfDue() {
        if (!csv_enabled_->isChecked() || !csv_file_.is_open() || !vision_pipeline_running_) {
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
        const auto rx_stats = video_receiver_.getVideoStats();
        const auto nal_stats = video_assembler_.getStats();
        const auto dec_stats = video_decoder_.getStats();

        const uint64_t packets_total = rx_stats.packets_total;
        const uint64_t delta = packets_total - last_vision_metrics_packets_;
        last_vision_metrics_packets_ = packets_total;
        last_vision_metrics_csv_tp_ = now;

        ++metrics_attempt_;
        updateRuntimeMetricsContext();
        metrics_.setElapsedMs(elapsed_ms);
        metrics_.setAttempt(metrics_attempt_);
        metrics_.updateVideoReceiverStats(rx_stats);
        metrics_.setVideoPacketDelta(delta);
        metrics_.updateVideoAssemblerStats(nal_stats);
        metrics_.updateDecoderStats(dec_stats);
        metrics_.updateFrameInfo(vision_frame_width_.load(), vision_frame_height_.load(), false);
        metrics_.updateDisplayState(vision_paused_, vision_overlay_enabled_);
        metrics_.setConnectionState(connectionStateToString(client_.getConnectionState()));
        metrics_.setEvent("vision");
        metrics_.setLastOutageFailures(client_.getLastOutageFailures());
        csv_file_ << metrics_.toCsvLine() << std::endl;
    }
#endif

    void runReadCommand(const std::string& cmd, const QString& source) {
        runCommandWithResponse(cmd, source);
    }

    void runCommandWithResponse(const std::string& cmd, const QString& source) {
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
            if (rc_stream_check_ != nullptr && rc_stream_check_->isChecked()) {
                QSignalBlocker blocker(rc_stream_check_);
                rc_stream_check_->setChecked(false);
            }
            if (rc_stream_timer_ != nullptr && rc_stream_timer_->isActive()) {
                rc_stream_timer_->stop();
            }

            if (cmd == "takeoff" || cmd == "land") {
                if (rc_stream_was_active || rc_input_was_nonzero) {
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

        std::string response;
        const auto command_started_at = std::chrono::steady_clock::now();
        int parsed_rc_a = 0;
        int parsed_rc_b = 0;
        int parsed_rc_c = 0;
        int parsed_rc_d = 0;
        const bool is_rc_command = parseRcCommand(cmd, parsed_rc_a, parsed_rc_b, parsed_rc_c, parsed_rc_d);
        const auto rc = is_rc_command
            ? client_.sendCommandNoWait(cmd)
            : client_.sendCommandWithResponse(cmd, response);
        const auto command_elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - command_started_at).count();
        if (critical) {
            current_critical_command_ms_ = command_elapsed_ms;
        }

        if (is_rc_command) {
            response = "no_wait";
            state_receiver_.recordRcCommandSample(
                parsed_rc_a,
                parsed_rc_b,
                parsed_rc_c,
                parsed_rc_d,
                source.toStdString(),
                rc);
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
                    if (sdk_ready_ && auto_refresh_check_ != nullptr && auto_refresh_check_->isChecked()
                        && auto_refresh_timer_ != nullptr && !auto_refresh_timer_->isActive()) {
                        auto_refresh_timer_->start(auto_refresh_interval_ms_ != nullptr
                                                       ? auto_refresh_interval_ms_->value()
                                                       : 1000);
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

    tello::TelloClient client_;
    bool sdk_ready_ = false;

    QLabel* status_label_ = nullptr;
    QLabel* wifi_quality_label_ = nullptr;
    QLabel* battery_status_label_ = nullptr;
    QTextEdit* log_view_ = nullptr;

    QCheckBox* auto_refresh_check_ = nullptr;
    QSpinBox* auto_refresh_interval_ms_ = nullptr;
    QTimer* auto_refresh_timer_ = nullptr;
    QCheckBox* rc_stream_check_ = nullptr;
    QSpinBox* rc_stream_interval_ms_ = nullptr;
    QTimer* rc_stream_timer_ = nullptr;
    QTimer* state_view_timer_ = nullptr;
    QTimer* vision_view_timer_ = nullptr;
    QComboBox* panel_selector_ = nullptr;
    QStackedWidget* panel_stack_ = nullptr;

    tello::StateReceiver state_receiver_;
    QSpinBox* state_buffer_capacity_spin_ = nullptr;
    QComboBox* state_metric_combo_ = nullptr;
    QLineEdit* state_record_path_edit_ = nullptr;
    QLabel* state_buffer_info_label_ = nullptr;
    QLabel* state_record_info_label_ = nullptr;
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

#ifdef TELLO_HAS_FFMPEG
    tello::VideoReceiver video_receiver_;
    tello::VideoStreamAssembler video_assembler_;
    tello::VideoDecoderFfmpeg video_decoder_;
    std::mutex vision_frame_mutex_;
    QImage vision_latest_frame_;
    QImage vision_paused_frame_;
    std::atomic<uint64_t> vision_keyframes_{0};
    std::atomic<int32_t> vision_frame_width_{0};
    std::atomic<int32_t> vision_frame_height_{0};
    std::atomic<bool> video_decoder_seen_sps_{false};
    std::atomic<bool> video_decoder_seen_pps_{false};
    std::atomic<bool> video_decoder_synced_{false};
    std::atomic<int64_t> vision_last_ui_frame_convert_ms_{0};
    std::atomic<uint64_t> vision_latest_frame_sequence_{0};
    uint64_t vision_displayed_frame_sequence_ = 0;
#endif
    bool vision_pipeline_running_ = false;
    bool vision_paused_ = false;
    bool vision_overlay_enabled_ = true;
    bool critical_command_active_ = false;
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

    QCheckBox* csv_enabled_ = nullptr;
    QLineEdit* csv_path_edit_ = nullptr;
    std::ofstream csv_file_;
    tello::MetricsCollector metrics_;
    std::chrono::steady_clock::time_point metrics_started_at_;
    uint64_t metrics_attempt_ = 0;
    std::chrono::steady_clock::time_point last_vision_metrics_csv_tp_;
    uint64_t last_vision_metrics_packets_ = 0;
    uint64_t last_metrics_state_packets_valid_ = 0;

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

    std::cerr << "[tello_control_panel] QT_QPA_PLATFORM="
              << qgetenv("QT_QPA_PLATFORM").constData()
              << " QT_OPENGL=" << qgetenv("QT_OPENGL").constData()
              << " QT_XCB_GL_INTEGRATION=" << qgetenv("QT_XCB_GL_INTEGRATION").constData()
              << " LIBGL_ALWAYS_SOFTWARE=" << qgetenv("LIBGL_ALWAYS_SOFTWARE").constData()
              << std::endl;

    ControlPanelWidget panel;
    panel.show();
    panel.showNormal();
    panel.raise();
    panel.activateWindow();
    return app.exec();
}
