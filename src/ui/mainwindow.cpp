#include "mainwindow.h"
#include <QMessageBox>
#include <QApplication>
#include <cstring>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent) {
    setWindowTitle("GigaACE Virtual Sound Card");
    resize(1040, 640);
    setMinimumSize(920, 560);

    m_demo_check = new QCheckBox("Demo Source");
    m_demo_check->setChecked(true);

    m_interface_edit = new QLineEdit("en0");
    m_interface_edit->setFixedWidth(140);

    m_channel_spin = new QSpinBox();
    m_channel_spin->setRange(2, 128);
    m_channel_spin->setSingleStep(2);
    m_channel_spin->setValue(64);
    m_channel_spin->setFixedWidth(120);

    m_start_btn = new QPushButton("Start Audio");
    m_stop_btn = new QPushButton("Stop");
    m_monitor_btn = new QPushButton("Start Monitor");
    m_monitor_btn->setCheckable(true);
    m_monitor_btn->setEnabled(false);
    m_output_combo = new QComboBox();
    m_output_combo->setMinimumWidth(260);

    m_status_label = new QLabel("Ready");
    m_mode_label = new QLabel("Demo source");
    m_asio_label = new QLabel("ASIO bridge idle");

    m_frames_rx_label = new QLabel("0");
    m_frames_ok_label = new QLabel("0");
    m_drops_label = new QLabel("0");
    m_channels_label = new QLabel("0");
    m_buffered_label = new QLabel("0");

    m_refresh_timer = new QTimer(this);
    connect(m_refresh_timer, &QTimer::timeout, this, &MainWindow::refreshStats);

    connect(m_start_btn, &QPushButton::clicked, this, &MainWindow::onStartClicked);
    connect(m_stop_btn, &QPushButton::clicked, this, &MainWindow::onStopClicked);
    connect(m_monitor_btn, &QPushButton::clicked, this, &MainWindow::onMonitorToggled);
    connect(m_demo_check, &QCheckBox::toggled, [this](bool checked) {
        m_interface_edit->setEnabled(!checked);
    });
    m_interface_edit->setEnabled(false);

    setupUI();
    populateOutputDevices();
    setStatus("Ready", "idle");
}

MainWindow::~MainWindow() {
    onStopClicked();
}

void MainWindow::setupUI() {
    auto* central = new QWidget(this);
    setCentralWidget(central);
    central->setStyleSheet(
        "QWidget { background: #151719; color: #edf0f2; font-size: 13px; }"
        "QLabel#Title { font-size: 25px; font-weight: 700; color: #ffffff; }"
        "QLabel#Subtitle { color: #9aa4ad; }"
        "QFrame#Panel { background: #202327; border: 1px solid #31363b; border-radius: 8px; }"
        "QFrame#StatusBar { background: #111315; border: 1px solid #2b3035; border-radius: 8px; }"
        "QGroupBox { border: 1px solid #31363b; border-radius: 8px; margin-top: 16px; padding: 12px; font-weight: 600; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 5px; color: #cfd6dc; }"
        "QLineEdit, QSpinBox, QComboBox { background: #272b30; border: 1px solid #3a4148; border-radius: 5px; padding: 5px 8px; color: #f4f7f9; }"
        "QPushButton { background: #30363d; border: 1px solid #454d56; border-radius: 6px; padding: 7px 14px; font-weight: 600; }"
        "QPushButton:hover { background: #39414a; }"
        "QPushButton:disabled { color: #6f7880; background: #24282c; border-color: #30343a; }"
        "QPushButton#PrimaryButton { background: #2f8f5b; border-color: #43a86f; color: white; }"
        "QPushButton#DangerButton { background: #512b2f; border-color: #85434a; color: #ffd9de; }"
        "QProgressBar { background: #272b30; border: 1px solid #343a40; border-radius: 5px; height: 18px; text-align: right; padding-right: 6px; }"
        "QProgressBar::chunk { background: #48b96a; border-radius: 4px; }"
    );
    auto* main_layout = new QVBoxLayout(central);
    main_layout->setSpacing(16);
    main_layout->setContentsMargins(24, 24, 24, 24);

    auto* title = new QLabel("GigaACE Virtual Sound Card");
    title->setObjectName("Title");
    main_layout->addWidget(title);

    auto* subtitle = new QLabel("Decode GigaACE/SLink frames and expose them as a 64-channel ASIO input device.");
    subtitle->setObjectName("Subtitle");
    main_layout->addWidget(subtitle);

    auto* controls_frame = new QFrame();
    controls_frame->setObjectName("Panel");
    auto* controls = new QGridLayout(controls_frame);
    controls->setHorizontalSpacing(14);
    controls->setVerticalSpacing(10);
    controls->setContentsMargins(16, 14, 16, 14);

    m_start_btn->setObjectName("PrimaryButton");
    m_stop_btn->setObjectName("DangerButton");

    controls->addWidget(new QLabel("Source"), 0, 0);
    controls->addWidget(m_demo_check, 0, 1);
    controls->addWidget(new QLabel("Interface"), 0, 2);
    controls->addWidget(m_interface_edit, 0, 3);
    controls->addWidget(new QLabel("Channels"), 0, 4);
    controls->addWidget(m_channel_spin, 0, 5);
    controls->addWidget(new QLabel("Monitor out"), 1, 0);
    controls->addWidget(m_output_combo, 1, 1);
    controls->addWidget(m_start_btn, 1, 2);
    controls->addWidget(m_monitor_btn, 1, 3);
    controls->addWidget(m_stop_btn, 1, 4);
    controls->setColumnStretch(6, 1);
    main_layout->addWidget(controls_frame);

    auto* status_frame = new QFrame();
    status_frame->setObjectName("StatusBar");
    auto* status_layout = new QHBoxLayout(status_frame);
    status_layout->setContentsMargins(14, 10, 14, 10);
    status_layout->addWidget(m_status_label);
    status_layout->addStretch();
    status_layout->addWidget(m_mode_label);
    status_layout->addSpacing(16);
    status_layout->addWidget(m_asio_label);
    main_layout->addWidget(status_frame);

    auto* stats = new QGroupBox("Statistics");
    auto* stats_layout = new QGridLayout(stats);
    stats_layout->setHorizontalSpacing(22);
    stats_layout->setVerticalSpacing(8);
    stats_layout->addWidget(makeMetricLabel("Frames RX"), 0, 0);
    stats_layout->addWidget(m_frames_rx_label, 1, 0);
    stats_layout->addWidget(makeMetricLabel("Frames OK"), 0, 1);
    stats_layout->addWidget(m_frames_ok_label, 1, 1);
    stats_layout->addWidget(makeMetricLabel("Drops"), 0, 2);
    stats_layout->addWidget(m_drops_label, 1, 2);
    stats_layout->addWidget(makeMetricLabel("Channels"), 0, 3);
    stats_layout->addWidget(m_channels_label, 1, 3);
    stats_layout->addWidget(makeMetricLabel("Buffered"), 0, 4);
    stats_layout->addWidget(m_buffered_label, 1, 4);
    for (auto* label : {m_frames_rx_label, m_frames_ok_label, m_drops_label, m_channels_label, m_buffered_label})
        label->setStyleSheet("font-size: 20px; font-weight: 700; color: #ffffff;");
    main_layout->addWidget(stats);

    auto* meter_group = new QGroupBox("First 8 Channel Levels");
    auto* meter_layout = new QVBoxLayout(meter_group);
    for (int i = 0; i < 8; ++i) {
        auto* row = new QHBoxLayout();
        auto* ch_label = new QLabel(QString("IN %1").arg(i + 1, 2, 10, QChar('0')));
        ch_label->setFixedWidth(46);
        row->addWidget(ch_label);
        auto* bar = new QProgressBar();
        bar->setRange(0, 100);
        bar->setValue(0);
        bar->setObjectName(QString("meter_%1").arg(i));
        row->addWidget(bar);
        meter_layout->addLayout(row);
    }
    main_layout->addWidget(meter_group);

    auto* info = new QLabel(
        "Start this app before opening the ASIO device in your DAW. Demo Source produces test audio; disabling it captures live GigaACE frames through Npcap."
    );
    info->setWordWrap(true);
    info->setStyleSheet("color: gray; font-size: 11px;");
    main_layout->addWidget(info);
}

void MainWindow::onStartClicked() {
    onStopClicked();

    GigaACEConfig config;
    config.channels = m_channel_spin->value();
    config.sample_rate = 48000.0;
    config.ring_buffer_frames = 96000;
    config.capture_mode = m_demo_check->isChecked()
        ? GIGAACE_CAPTURE_MODE_DEMO
        : GIGAACE_CAPTURE_MODE_PCAP;
    std::strncpy(config.interface_name, m_interface_edit->text().toUtf8().constData(),
                 sizeof(config.interface_name) - 1);
    config.shared_memory_name = "Local\\GigaACEVirtualDevice";
    config.shared_memory_frames = 96000;

    m_engine = std::make_unique<GigaACEEngine>(config);

    if (m_engine->start()) {
        QString status = m_demo_check->isChecked()
            ? "Running"
            : "Capturing";
        setStatus(status, m_engine->sharedBridgeReady() ? "ok" : "warning");
        m_mode_label->setText(m_demo_check->isChecked() ? "Demo source" : "Npcap capture");
        m_asio_label->setText(m_engine->sharedBridgeReady() ? "ASIO bridge ready" : "ASIO bridge unavailable");
        m_start_btn->setText("Restart Audio");
        m_monitor_btn->setEnabled(true);
        m_refresh_timer->start(100);
    } else {
        setStatus("Failed to start engine", "error");
        m_engine.reset();
    }
}

void MainWindow::onStopClicked() {
    m_refresh_timer->stop();

    if (m_monitor_running && m_monitor) {
        m_monitor->stop();
        m_monitor_running = false;
        m_monitor_btn->setChecked(false);
        m_monitor_btn->setText("Start Monitor");
    }

    if (m_engine) {
        m_engine->stop();
        m_engine.reset();
    }

    setStatus("Ready", "idle");
    m_mode_label->setText(m_demo_check->isChecked() ? "Demo source" : "Npcap capture");
    m_asio_label->setText("ASIO bridge idle");
    m_start_btn->setText("Start Audio");
    m_monitor_btn->setEnabled(false);
    m_monitor_btn->setChecked(false);
}

void MainWindow::onMonitorToggled(bool checked) {
    if (checked) {
        if (!m_engine) return;

        std::wstring endpoint_id = m_output_combo->currentData().toString().toStdWString();
        m_monitor = std::make_unique<WASAPIOutput>(m_engine->config().sample_rate, endpoint_id);

        auto callback = [this](int frame_count, float* left, float* right) {
            if (m_engine) {
                if ((int)m_left_buf.size() < frame_count) {
                    m_left_buf.resize(frame_count);
                    m_right_buf.resize(frame_count);
                }
                m_engine->consumeStereo(frame_count, 0, 1, m_left_buf.data(), m_right_buf.data());
                std::memcpy(left, m_left_buf.data(), frame_count * sizeof(float));
                std::memcpy(right, m_right_buf.data(), frame_count * sizeof(float));
            }
        };

        if (m_monitor->start(callback)) {
            m_monitor_running = true;
            m_monitor_btn->setText("Stop Monitor");
            setStatus(QString("Monitoring: %1").arg(m_output_combo->currentText()), "ok");
        } else {
            m_monitor_btn->setChecked(false);
            m_monitor.reset();
            setStatus("Failed to start WASAPI monitor", "error");
        }
    } else {
        if (m_monitor) {
            m_monitor->stop();
            m_monitor.reset();
        }
        m_monitor_running = false;
        m_monitor_btn->setText("Start Monitor");
    }
}

QLabel* MainWindow::makeMetricLabel(const QString& text) {
    auto* label = new QLabel(text);
    label->setStyleSheet("color: #9aa4ad; font-weight: 600;");
    return label;
}

void MainWindow::setStatus(const QString& text, const QString& state) {
    QString color = "#7d8790";
    if (state == "ok")
        color = "#4fce7b";
    else if (state == "warning")
        color = "#e6b450";
    else if (state == "error")
        color = "#ff6670";
    m_status_label->setText(text);
    m_status_label->setStyleSheet(QString("font-weight: 700; color: %1;").arg(color));
}

void MainWindow::populateOutputDevices() {
    m_output_combo->clear();
    m_output_combo->addItem("Default output", QString());

    auto devices = WASAPIOutput::enumerateRenderDevices();
    for (const auto& device : devices) {
        m_output_combo->addItem(
            QString::fromStdWString(device.name),
            QString::fromStdWString(device.id)
        );
    }

    for (int i = 0; i < m_output_combo->count(); ++i) {
        QString label = m_output_combo->itemText(i).toLower();
        if (label.contains("voicemeeter") || label.contains("vb-audio") || label.contains("cable input")) {
            m_output_combo->setCurrentIndex(i);
            break;
        }
    }
}

void MainWindow::refreshStats() {
    if (!m_engine) return;
    updateStatsDisplay();
}

void MainWindow::updateStatsDisplay() {
    auto stats = m_engine->snapshotStatistics();

    m_frames_rx_label->setText(QString::number(stats.frames_received));
    m_frames_ok_label->setText(QString::number(stats.frames_decoded));
    m_drops_label->setText(QString::number(stats.counter_drops));
    m_channels_label->setText(QString::number(stats.active_channels));
    m_buffered_label->setText(QString::number(m_engine->bufferedFrames()));

    auto levels = m_engine->latestLevels(8);
    for (int i = 0; i < 8; ++i) {
        auto* bar = findChild<QProgressBar*>(QString("meter_%1").arg(i));
        if (bar) {
            float level = (i < (int)levels.size()) ? levels[i] : 0.0f;
            bar->setValue((int)(level * 100.0f));
        }
    }
}
