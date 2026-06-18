#include "tello/tello_client.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QDateTime>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <fstream>
#include <string>

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

QString sanitizeCsvField(QString s) {
    s.replace(',', ';');
    s.replace('\n', ';');
    s.replace('\r', ';');
    return s;
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

class ControlPanelWidget final : public QWidget {
public:
    ControlPanelWidget() {
        setWindowTitle("Tello Control Panel (Qt - Phase G Delivery C)");
        resize(1220, 900);

        auto* root = new QVBoxLayout(this);

        status_label_ = new QLabel(this);
        wifi_quality_label_ = new QLabel("wifi: unknown", this);

        auto* status_row = new QHBoxLayout();
        status_row->addWidget(status_label_, 1);
        status_row->addWidget(wifi_quality_label_);
        root->addLayout(status_row);

        auto* conn_row = new QHBoxLayout();
        auto* connect_btn = new QPushButton("Connect + SDK", this);
        auto* disconnect_btn = new QPushButton("Disconnect", this);
        conn_row->addWidget(connect_btn);
        conn_row->addWidget(disconnect_btn);
        conn_row->addStretch(1);
        root->addLayout(conn_row);

        buildReadGroup(root);
        buildAutoRefreshAndCsvGroup(root);
        buildSetGroup(root);
        buildControlBasicsGroup(root);
        buildMotionGroup(root);
        buildAdvancedGroup(root);
        buildRawGroup(root);

        log_view_ = new QTextEdit(this);
        log_view_->setReadOnly(true);
        root->addWidget(log_view_, 1);

        auto_refresh_timer_ = new QTimer(this);

        connect(connect_btn, &QPushButton::clicked, this, [this]() { (void)connectSdk(); });
        connect(disconnect_btn, &QPushButton::clicked, this, [this]() { disconnectSdk(); });

        connect(auto_refresh_timer_, &QTimer::timeout, this, [this]() {
            if (!sdk_ready_) {
                return;
            }
            runReadCommand("battery?", "auto");
            runReadCommand("wifi?", "auto");
        });

        appendLog("Panel ready. Click Connect + SDK first.");
        appendLog("Delivery C active: optional auto-refresh + CSV export.");
        appendLog("Buttons added for control/set/read command families.");
        updateStatusLabel();
    }

    ~ControlPanelWidget() override {
        closeCsv();
        client_.shutdown();
    }

private:
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
        auto* g = new QGroupBox("Set Commands", this);
        auto* l = new QGridLayout(g);

        speed_spin_ = new QSpinBox(g);
        speed_spin_->setRange(10, 100);
        speed_spin_->setValue(50);
        auto* speed_btn = new QPushButton("Send speed x", g);

        rc_a_ = new QSpinBox(g); rc_a_->setRange(-100, 100);
        rc_b_ = new QSpinBox(g); rc_b_->setRange(-100, 100);
        rc_c_ = new QSpinBox(g); rc_c_->setRange(-100, 100);
        rc_d_ = new QSpinBox(g); rc_d_->setRange(-100, 100);
        auto* rc_btn = new QPushButton("Send rc a b c d", g);

        wifi_ssid_ = new QLineEdit(g);
        wifi_pass_ = new QLineEdit(g);
        auto* wifi_btn = new QPushButton("Send wifi ssid pass", g);

        ap_ssid_ = new QLineEdit(g);
        ap_pass_ = new QLineEdit(g);
        auto* ap_btn = new QPushButton("Send ap ssid pass", g);

        mdirection_spin_ = new QSpinBox(g);
        mdirection_spin_->setRange(0, 2);
        auto* mdirection_btn = new QPushButton("Send mdirection x", g);

        auto* mon_btn = new QPushButton("mon", g);
        auto* moff_btn = new QPushButton("moff", g);

        l->addWidget(new QLabel("speed x"), 0, 0);
        l->addWidget(speed_spin_, 0, 1);
        l->addWidget(speed_btn, 0, 2);

        auto* rc_row = new QHBoxLayout();
        rc_row->addWidget(rc_a_); rc_row->addWidget(rc_b_); rc_row->addWidget(rc_c_); rc_row->addWidget(rc_d_);
        auto* rc_host = new QWidget(g);
        rc_host->setLayout(rc_row);
        l->addWidget(new QLabel("rc a b c d"), 1, 0);
        l->addWidget(rc_host, 1, 1);
        l->addWidget(rc_btn, 1, 2);

        auto* wifi_row = new QHBoxLayout();
        wifi_row->addWidget(wifi_ssid_); wifi_row->addWidget(wifi_pass_);
        auto* wifi_host = new QWidget(g);
        wifi_host->setLayout(wifi_row);
        l->addWidget(new QLabel("wifi ssid pass"), 2, 0);
        l->addWidget(wifi_host, 2, 1);
        l->addWidget(wifi_btn, 2, 2);

        auto* ap_row = new QHBoxLayout();
        ap_row->addWidget(ap_ssid_); ap_row->addWidget(ap_pass_);
        auto* ap_host = new QWidget(g);
        ap_host->setLayout(ap_row);
        l->addWidget(new QLabel("ap ssid pass"), 3, 0);
        l->addWidget(ap_host, 3, 1);
        l->addWidget(ap_btn, 3, 2);

        l->addWidget(new QLabel("mdirection x"), 4, 0);
        l->addWidget(mdirection_spin_, 4, 1);
        l->addWidget(mdirection_btn, 4, 2);

        auto* mp_row = new QHBoxLayout();
        mp_row->addWidget(mon_btn);
        mp_row->addWidget(moff_btn);
        auto* mp_host = new QWidget(g);
        mp_host->setLayout(mp_row);
        l->addWidget(new QLabel("mission pad"), 5, 0);
        l->addWidget(mp_host, 5, 1, 1, 2);

        connect(speed_btn, &QPushButton::clicked, this, [this]() {
            runCommandWithResponse("speed " + std::to_string(speed_spin_->value()), "manual");
        });
        connect(rc_btn, &QPushButton::clicked, this, [this]() {
            runCommandWithResponse("rc "
                + std::to_string(rc_a_->value()) + " "
                + std::to_string(rc_b_->value()) + " "
                + std::to_string(rc_c_->value()) + " "
                + std::to_string(rc_d_->value()), "manual");
        });
        connect(wifi_btn, &QPushButton::clicked, this, [this]() {
            if (wifi_ssid_->text().trimmed().isEmpty() || wifi_pass_->text().isEmpty()) {
                appendLog("wifi command requires ssid and pass");
                return;
            }
            runCommandWithResponse("wifi " + wifi_ssid_->text().trimmed().toStdString() + " " + wifi_pass_->text().toStdString(), "manual");
        });
        connect(ap_btn, &QPushButton::clicked, this, [this]() {
            if (ap_ssid_->text().trimmed().isEmpty() || ap_pass_->text().isEmpty()) {
                appendLog("ap command requires ssid and pass");
                return;
            }
            runCommandWithResponse("ap " + ap_ssid_->text().trimmed().toStdString() + " " + ap_pass_->text().toStdString(), "manual");
        });
        connect(mdirection_btn, &QPushButton::clicked, this, [this]() {
            runCommandWithResponse("mdirection " + std::to_string(mdirection_spin_->value()), "manual");
        });
        connect(mon_btn, &QPushButton::clicked, this, [this]() { runCommandWithResponse("mon", "manual"); });
        connect(moff_btn, &QPushButton::clicked, this, [this]() { runCommandWithResponse("moff", "manual"); });

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

    void buildAdvancedGroup(QVBoxLayout* root) {
        auto* g = new QGroupBox("Control Commands (advanced)", this);
        auto* l = new QGridLayout(g);

        go_x_ = new QSpinBox(g); go_x_->setRange(-500, 500);
        go_y_ = new QSpinBox(g); go_y_->setRange(-500, 500);
        go_z_ = new QSpinBox(g); go_z_->setRange(-500, 500); go_z_->setValue(50);
        go_speed_ = new QSpinBox(g); go_speed_->setRange(10, 100); go_speed_->setValue(30);
        auto* go_btn = new QPushButton("Send go x y z speed", g);

        go_mid_x_ = new QSpinBox(g); go_mid_x_->setRange(-500, 500);
        go_mid_y_ = new QSpinBox(g); go_mid_y_->setRange(-500, 500);
        go_mid_z_ = new QSpinBox(g); go_mid_z_->setRange(-500, 500); go_mid_z_->setValue(50);
        go_mid_speed_ = new QSpinBox(g); go_mid_speed_->setRange(10, 100); go_mid_speed_->setValue(30);
        go_mid_id_ = new QSpinBox(g); go_mid_id_->setRange(1, 8);
        auto* go_mid_btn = new QPushButton("Send go ... mid", g);

        curve_x1_ = new QSpinBox(g); curve_x1_->setRange(-500, 500);
        curve_y1_ = new QSpinBox(g); curve_y1_->setRange(-500, 500);
        curve_z1_ = new QSpinBox(g); curve_z1_->setRange(-500, 500); curve_z1_->setValue(50);
        curve_x2_ = new QSpinBox(g); curve_x2_->setRange(-500, 500); curve_x2_->setValue(100);
        curve_y2_ = new QSpinBox(g); curve_y2_->setRange(-500, 500);
        curve_z2_ = new QSpinBox(g); curve_z2_->setRange(-500, 500); curve_z2_->setValue(50);
        curve_speed_ = new QSpinBox(g); curve_speed_->setRange(10, 60); curve_speed_->setValue(20);
        auto* curve_btn = new QPushButton("Send curve", g);

        curve_mid_x1_ = new QSpinBox(g); curve_mid_x1_->setRange(-500, 500);
        curve_mid_y1_ = new QSpinBox(g); curve_mid_y1_->setRange(-500, 500);
        curve_mid_z1_ = new QSpinBox(g); curve_mid_z1_->setRange(-500, 500); curve_mid_z1_->setValue(50);
        curve_mid_x2_ = new QSpinBox(g); curve_mid_x2_->setRange(-500, 500); curve_mid_x2_->setValue(100);
        curve_mid_y2_ = new QSpinBox(g); curve_mid_y2_->setRange(-500, 500);
        curve_mid_z2_ = new QSpinBox(g); curve_mid_z2_->setRange(-500, 500); curve_mid_z2_->setValue(50);
        curve_mid_speed_ = new QSpinBox(g); curve_mid_speed_->setRange(10, 60); curve_mid_speed_->setValue(20);
        curve_mid_id_ = new QSpinBox(g); curve_mid_id_->setRange(1, 8);
        auto* curve_mid_btn = new QPushButton("Send curve ... mid", g);

        jump_x_ = new QSpinBox(g); jump_x_->setRange(-500, 500);
        jump_y_ = new QSpinBox(g); jump_y_->setRange(-500, 500);
        jump_z_ = new QSpinBox(g); jump_z_->setRange(-500, 500); jump_z_->setValue(50);
        jump_speed_ = new QSpinBox(g); jump_speed_->setRange(10, 100); jump_speed_->setValue(30);
        jump_yaw_ = new QSpinBox(g); jump_yaw_->setRange(1, 360); jump_yaw_->setValue(90);
        jump_mid1_ = new QSpinBox(g); jump_mid1_->setRange(1, 8);
        jump_mid2_ = new QSpinBox(g); jump_mid2_->setRange(1, 8); jump_mid2_->setValue(2);
        auto* jump_btn = new QPushButton("Send jump", g);

        auto addFields = [l](int row, const QString& label, const QList<QWidget*>& widgets, QPushButton* btn) {
            auto* r = new QHBoxLayout();
            for (QWidget* w : widgets) {
                r->addWidget(w);
            }
            auto* host = new QWidget();
            host->setLayout(r);
            l->addWidget(new QLabel(label), row, 0);
            l->addWidget(host, row, 1);
            l->addWidget(btn, row, 2);
        };

        addFields(0, "go x y z speed", {go_x_, go_y_, go_z_, go_speed_}, go_btn);
        addFields(1, "go x y z speed mid", {go_mid_x_, go_mid_y_, go_mid_z_, go_mid_speed_, go_mid_id_}, go_mid_btn);
        addFields(2, "curve x1 y1 z1 x2 y2 z2 speed", {curve_x1_, curve_y1_, curve_z1_, curve_x2_, curve_y2_, curve_z2_, curve_speed_}, curve_btn);
        addFields(3, "curve ... speed mid", {curve_mid_x1_, curve_mid_y1_, curve_mid_z1_, curve_mid_x2_, curve_mid_y2_, curve_mid_z2_, curve_mid_speed_, curve_mid_id_}, curve_mid_btn);
        addFields(4, "jump x y z speed yaw mid1 mid2", {jump_x_, jump_y_, jump_z_, jump_speed_, jump_yaw_, jump_mid1_, jump_mid2_}, jump_btn);

        connect(go_btn, &QPushButton::clicked, this, [this]() {
            runCommandWithResponse("go "
                + std::to_string(go_x_->value()) + " "
                + std::to_string(go_y_->value()) + " "
                + std::to_string(go_z_->value()) + " "
                + std::to_string(go_speed_->value()), "manual");
        });
        connect(go_mid_btn, &QPushButton::clicked, this, [this]() {
            runCommandWithResponse("go "
                + std::to_string(go_mid_x_->value()) + " "
                + std::to_string(go_mid_y_->value()) + " "
                + std::to_string(go_mid_z_->value()) + " "
                + std::to_string(go_mid_speed_->value()) + " m" + std::to_string(go_mid_id_->value()), "manual");
        });
        connect(curve_btn, &QPushButton::clicked, this, [this]() {
            runCommandWithResponse("curve "
                + std::to_string(curve_x1_->value()) + " "
                + std::to_string(curve_y1_->value()) + " "
                + std::to_string(curve_z1_->value()) + " "
                + std::to_string(curve_x2_->value()) + " "
                + std::to_string(curve_y2_->value()) + " "
                + std::to_string(curve_z2_->value()) + " "
                + std::to_string(curve_speed_->value()), "manual");
        });
        connect(curve_mid_btn, &QPushButton::clicked, this, [this]() {
            runCommandWithResponse("curve "
                + std::to_string(curve_mid_x1_->value()) + " "
                + std::to_string(curve_mid_y1_->value()) + " "
                + std::to_string(curve_mid_z1_->value()) + " "
                + std::to_string(curve_mid_x2_->value()) + " "
                + std::to_string(curve_mid_y2_->value()) + " "
                + std::to_string(curve_mid_z2_->value()) + " "
                + std::to_string(curve_mid_speed_->value()) + " m" + std::to_string(curve_mid_id_->value()), "manual");
        });
        connect(jump_btn, &QPushButton::clicked, this, [this]() {
            runCommandWithResponse("jump "
                + std::to_string(jump_x_->value()) + " "
                + std::to_string(jump_y_->value()) + " "
                + std::to_string(jump_z_->value()) + " "
                + std::to_string(jump_speed_->value()) + " "
                + std::to_string(jump_yaw_->value()) + " m" + std::to_string(jump_mid1_->value())
                + " m" + std::to_string(jump_mid2_->value()), "manual");
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

    void appendLog(const QString& msg) {
        log_view_->append("[" + nowClockString() + "] " + msg);
    }

    void updateStatusLabel() {
        status_label_->setText(
            "State: " + QString::fromStdString(connectionStateToString(client_.getConnectionState()))
            + " | SDK: " + QString(sdk_ready_ ? "READY" : "NOT_READY")
            + " | speed_setpoint=" + QString::number(speed_spin_ != nullptr ? speed_spin_->value() : 0));
    }

    bool connectSdk() {
        if (!client_.isInitialized()) {
            const auto init_rc = client_.initialize("192.168.10.1", 8889, 9000);
            appendLog("initialize: " + QString::fromStdString(responseCodeToString(init_rc)));
            if (init_rc != tello::ResponseCode::OK) {
                sdk_ready_ = false;
                updateStatusLabel();
                return false;
            }
        }

        const auto sdk_rc = client_.enterSdkMode();
        appendLog("command (enterSdkMode): " + QString::fromStdString(responseCodeToString(sdk_rc)));
        sdk_ready_ = (sdk_rc == tello::ResponseCode::OK);
        updateStatusLabel();
        return sdk_ready_;
    }

    void disconnectSdk() {
        auto_refresh_timer_->stop();
        auto_refresh_check_->setChecked(false);
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

        csv_file_ << "timestamp,source,command,result,response,cmd_state" << std::endl;
        return true;
    }

    void closeCsv() {
        if (csv_file_.is_open()) {
            csv_file_.close();
        }
    }

    void writeCsvRow(const QString& source,
                     const QString& command,
                     const QString& result,
                     const QString& response,
                     const QString& state) {
        if (!csv_enabled_->isChecked() || !csv_file_.is_open()) {
            return;
        }

        const QString ts = QDateTime::currentDateTime().toString(Qt::ISODate);
        csv_file_
            << sanitizeCsvField(ts).toStdString() << ","
            << sanitizeCsvField(source).toStdString() << ","
            << sanitizeCsvField(command).toStdString() << ","
            << sanitizeCsvField(result).toStdString() << ","
            << sanitizeCsvField(response).toStdString() << ","
            << sanitizeCsvField(state).toStdString()
            << std::endl;
    }

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

        std::string response;
        const auto rc = client_.sendCommandWithResponse(cmd, response);
        const QString rc_q = QString::fromStdString(responseCodeToString(rc));
        const QString response_q = QString::fromStdString(response);
        const QString state_q = QString::fromStdString(connectionStateToString(client_.getConnectionState()));

        QString line = QString::fromStdString(cmd) + " => " + rc_q;
        if (!response.empty()) {
            line += " | response=\"" + response_q + "\"";
        }
        line += " | state=" + state_q;
        appendLog(line);

        if (cmd == "wifi?") {
            wifi_quality_label_->setText(wifiQualityLabel(response_q));
        }

        writeCsvRow(source, QString::fromStdString(cmd), rc_q, response_q, state_q);
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
    QTextEdit* log_view_ = nullptr;

    QCheckBox* auto_refresh_check_ = nullptr;
    QSpinBox* auto_refresh_interval_ms_ = nullptr;
    QTimer* auto_refresh_timer_ = nullptr;

    QCheckBox* csv_enabled_ = nullptr;
    QLineEdit* csv_path_edit_ = nullptr;
    std::ofstream csv_file_;

    QSpinBox* speed_spin_ = nullptr;
    QSpinBox* rc_a_ = nullptr;
    QSpinBox* rc_b_ = nullptr;
    QSpinBox* rc_c_ = nullptr;
    QSpinBox* rc_d_ = nullptr;
    QLineEdit* wifi_ssid_ = nullptr;
    QLineEdit* wifi_pass_ = nullptr;
    QLineEdit* ap_ssid_ = nullptr;
    QLineEdit* ap_pass_ = nullptr;
    QSpinBox* mdirection_spin_ = nullptr;

    QSpinBox* up_cm_ = nullptr;
    QSpinBox* down_cm_ = nullptr;
    QSpinBox* left_cm_ = nullptr;
    QSpinBox* right_cm_ = nullptr;
    QSpinBox* forward_cm_ = nullptr;
    QSpinBox* back_cm_ = nullptr;
    QSpinBox* cw_deg_ = nullptr;
    QSpinBox* ccw_deg_ = nullptr;
    QLineEdit* flip_dir_ = nullptr;

    QSpinBox* go_x_ = nullptr;
    QSpinBox* go_y_ = nullptr;
    QSpinBox* go_z_ = nullptr;
    QSpinBox* go_speed_ = nullptr;

    QSpinBox* go_mid_x_ = nullptr;
    QSpinBox* go_mid_y_ = nullptr;
    QSpinBox* go_mid_z_ = nullptr;
    QSpinBox* go_mid_speed_ = nullptr;
    QSpinBox* go_mid_id_ = nullptr;

    QSpinBox* curve_x1_ = nullptr;
    QSpinBox* curve_y1_ = nullptr;
    QSpinBox* curve_z1_ = nullptr;
    QSpinBox* curve_x2_ = nullptr;
    QSpinBox* curve_y2_ = nullptr;
    QSpinBox* curve_z2_ = nullptr;
    QSpinBox* curve_speed_ = nullptr;

    QSpinBox* curve_mid_x1_ = nullptr;
    QSpinBox* curve_mid_y1_ = nullptr;
    QSpinBox* curve_mid_z1_ = nullptr;
    QSpinBox* curve_mid_x2_ = nullptr;
    QSpinBox* curve_mid_y2_ = nullptr;
    QSpinBox* curve_mid_z2_ = nullptr;
    QSpinBox* curve_mid_speed_ = nullptr;
    QSpinBox* curve_mid_id_ = nullptr;

    QSpinBox* jump_x_ = nullptr;
    QSpinBox* jump_y_ = nullptr;
    QSpinBox* jump_z_ = nullptr;
    QSpinBox* jump_speed_ = nullptr;
    QSpinBox* jump_yaw_ = nullptr;
    QSpinBox* jump_mid1_ = nullptr;
    QSpinBox* jump_mid2_ = nullptr;

    QLineEdit* raw_command_edit_ = nullptr;
};

} // namespace

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    ControlPanelWidget panel;
    panel.show();
    return app.exec();
}
