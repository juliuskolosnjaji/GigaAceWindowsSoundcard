#include "mainwindow.h"
#include "pcap_source.h"
#include <QDebug>
#include <QMessageBox>
#include <QApplication>
#include <algorithm>
#include <cmath>
#include <cstring>

static constexpr int kMeterCount = 16;
static constexpr float kMeterFloorDb = -72.0f;
static constexpr float kMeterCeilDb = 0.0f;

static float levelToDb(float level) {
    if (level <= 0.00000025f)
        return kMeterFloorDb;
    return std::clamp(20.0f * std::log10(level), kMeterFloorDb, 6.0f);
}

static int dbToMeterValue(float db) {
    return static_cast<int>(std::clamp((db - kMeterFloorDb) / (kMeterCeilDb - kMeterFloorDb) * 1000.0f, 0.0f, 1000.0f));
}

static QString dbText(float db) {
    if (db <= kMeterFloorDb + 0.1f)
        return "-inf";
    return QString::number((double)db, 'f', 1) + " dB";
}

static QString meterStyle(float db) {
    QString color = "#45b96a";
    if (db > -6.0f)
        color = "#e35d6a";
    else if (db > -18.0f)
        color = "#d8b64c";

    return QString(
        "QProgressBar { background: #202428; border: 1px solid #343a40; border-radius: 4px; height: 14px; text-align: center; }"
        "QProgressBar::chunk { background: %1; border-radius: 3px; }"
    ).arg(color);
}

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent) {
    setWindowTitle("GigaACE Virtual Sound Card");
    resize(1320, 780);
    setMinimumSize(1180, 720);

    m_demo_check = new QCheckBox("Demo Source");
    m_demo_check->setChecked(true);

    m_interface_combo = new QComboBox();
    m_interface_combo->setMinimumWidth(260);
    m_interface_combo->setEnabled(false);

    m_channel_combo = new QComboBox();
    for (int count : {2, 8, 16, 24, 32, 48, 64, 96, 128})
        m_channel_combo->addItem(QString("%1 inputs").arg(count), count);
    m_channel_combo->setCurrentIndex(m_channel_combo->findData(64));
    m_channel_combo->setMinimumWidth(120);

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
    m_handshake_label = new QLabel("Handshake: idle");

    m_frames_rx_label = new QLabel("0");
    m_frames_ok_label = new QLabel("0");
    m_drops_label = new QLabel("0");
    m_channels_label = new QLabel("0");
    m_buffered_label = new QLabel("0");
    m_meter_bank_combo = new QComboBox();
    m_meter_bank_combo->setMinimumWidth(120);

    m_refresh_timer = new QTimer(this);
    connect(m_refresh_timer, &QTimer::timeout, this, &MainWindow::refreshStats);

    connect(m_start_btn, &QPushButton::clicked, this, &MainWindow::onStartClicked);
    connect(m_stop_btn, &QPushButton::clicked, this, &MainWindow::onStopClicked);
    connect(m_monitor_btn, &QPushButton::clicked, this, &MainWindow::onMonitorToggled);
    connect(m_demo_check, &QCheckBox::toggled, [this](bool checked) {
        m_interface_combo->setEnabled(!checked);
        if (!checked && m_interface_combo->count() == 0)
            populateInterfaces();
    });
    connect(m_channel_combo, &QComboBox::currentIndexChanged, [this]() {
        updateMeterBanks();
        resetMeters();
        updateMeterDisplay({}, selectedChannelCount());
    });
    connect(m_meter_bank_combo, &QComboBox::currentIndexChanged, [this]() {
        resetMeters();
        if (m_engine)
            updateStatsDisplay();
        else
            updateMeterDisplay({}, selectedChannelCount());
    });

    setupUI();
    populateOutputDevices();
    updateMeterBanks();
    updateMeterDisplay({}, selectedChannelCount());
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
        "QLineEdit, QComboBox { background: #272b30; border: 1px solid #3a4148; border-radius: 5px; padding: 5px 8px; color: #f4f7f9; }"
        "QPushButton { background: #30363d; border: 1px solid #454d56; border-radius: 6px; padding: 7px 14px; font-weight: 600; }"
        "QPushButton:hover { background: #39414a; }"
        "QPushButton:disabled { color: #6f7880; background: #24282c; border-color: #30343a; }"
        "QPushButton#PrimaryButton { background: #2f8f5b; border-color: #43a86f; color: white; }"
        "QPushButton#DangerButton { background: #512b2f; border-color: #85434a; color: #ffd9de; }"
        "QProgressBar { background: #202428; border: 1px solid #343a40; border-radius: 4px; height: 14px; text-align: center; }"
        "QProgressBar::chunk { background: #45b96a; border-radius: 3px; }"
    );
    auto* main_layout = new QVBoxLayout(central);
    main_layout->setSpacing(18);
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
    controls->addWidget(m_interface_combo, 0, 3);
    controls->addWidget(new QLabel("Channels"), 0, 4);
    controls->addWidget(m_channel_combo, 0, 5);
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
    status_layout->addSpacing(16);
    status_layout->addWidget(m_handshake_label);
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

    auto* meter_group = new QGroupBox("Input Metering");
    auto* meter_layout = new QGridLayout(meter_group);
    meter_layout->setHorizontalSpacing(18);
    meter_layout->setVerticalSpacing(8);

    auto* meter_header = new QHBoxLayout();
    auto* meter_hint = new QLabel("Visible bank");
    meter_hint->setFixedWidth(92);
    meter_hint->setStyleSheet("color: #9aa4ad; font-weight: 600;");
    meter_header->addWidget(meter_hint);
    meter_header->addWidget(m_meter_bank_combo);
    meter_header->addStretch();
    auto* meter_legend = new QLabel("Green < -18 dBFS   Yellow -18..-6   Red > -6");
    meter_legend->setMinimumWidth(300);
    meter_legend->setStyleSheet("color: #78838c; font-size: 11px;");
    meter_header->addWidget(meter_legend);
    meter_layout->addLayout(meter_header, 0, 0, 1, 10);

    for (int col = 0; col < 2; ++col) {
        int base_col = col * 5;
        auto* scale = new QLabel("-72        -48        -24      -12   -6   0");
        scale->setMinimumWidth(260);
        scale->setStyleSheet("color: #67717a; font-size: 10px;");
        meter_layout->addWidget(scale, 1, base_col + 1, 1, 3);
        auto* peak_hdr = new QLabel("Peak");
        peak_hdr->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        peak_hdr->setStyleSheet("color: #67717a; font-size: 10px;");
        meter_layout->addWidget(peak_hdr, 1, base_col + 4);
    }

    m_meter_bars.clear();
    m_meter_channel_labels.clear();
    m_meter_db_labels.clear();
    m_meter_peak_labels.clear();
    m_meter_peak_db.assign(kMeterCount, kMeterFloorDb);

    for (int i = 0; i < kMeterCount; ++i) {
        int col = (i >= 8) ? 5 : 0;
        int row = (i % 8) + 2;
        auto* ch_label = new QLabel(QString("IN %1").arg(i + 1, 2, 10, QChar('0')));
        ch_label->setFixedWidth(48);
        ch_label->setStyleSheet("color: #cfd6dc; font-weight: 600;");
        meter_layout->addWidget(ch_label, row, col);

        auto* bar = new QProgressBar();
        bar->setRange(0, 1000);
        bar->setValue(0);
        bar->setTextVisible(false);
        bar->setMinimumWidth(300);
        bar->setObjectName(QString("meter_%1").arg(i));
        bar->setStyleSheet(meterStyle(kMeterFloorDb));
        meter_layout->addWidget(bar, row, col + 1);

        auto* db_label = new QLabel("-inf");
        db_label->setFixedWidth(74);
        db_label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        db_label->setObjectName(QString("meter_db_%1").arg(i));
        db_label->setStyleSheet("color: #d7dde2; font-family: Consolas, 'Cascadia Mono', monospace; font-size: 11px;");
        meter_layout->addWidget(db_label, row, col + 2);

        auto* peak_label = new QLabel("-inf");
        peak_label->setFixedWidth(74);
        peak_label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        peak_label->setStyleSheet("color: #9aa4ad; font-family: Consolas, 'Cascadia Mono', monospace; font-size: 11px;");
        meter_layout->addWidget(peak_label, row, col + 3);

        m_meter_channel_labels.push_back(ch_label);
        m_meter_bars.push_back(bar);
        m_meter_db_labels.push_back(db_label);
        m_meter_peak_labels.push_back(peak_label);
    }
    meter_layout->setColumnStretch(1, 1);
    meter_layout->setColumnStretch(6, 1);
    main_layout->addWidget(meter_group);

    auto* info = new QLabel(
        "Start this app before opening the ASIO device in your DAW. Demo Source produces test audio; disabling it captures live GigaACE frames through Npcap."
    );
    info->setWordWrap(true);
    info->setStyleSheet("color: gray; font-size: 11px;");
    main_layout->addWidget(info);
}

void MainWindow::onStartClicked() {
    qInfo() << "[UI] Start Audio clicked";
    onStopClicked();

    GigaACEConfig config;
    config.channels = selectedChannelCount();
    config.sample_rate = 96000.0;
    config.ring_buffer_frames = 192000;
    config.capture_mode = m_demo_check->isChecked()
        ? GIGAACE_CAPTURE_MODE_DEMO
        : GIGAACE_CAPTURE_MODE_PCAP;

    QString iface = m_interface_combo->currentData().toString();
    std::strncpy(config.interface_name, iface.toUtf8().constData(),
                 sizeof(config.interface_name) - 1);
    config.interface_name[sizeof(config.interface_name) - 1] = '\0';

    config.shared_memory_name = "Local\\GigaACEVirtualDevice";
    config.shared_memory_frames = 192000;

    qInfo() << "[UI] Config: mode=" << (config.capture_mode == GIGAACE_CAPTURE_MODE_DEMO ? "demo" : "pcap")
            << "channels=" << config.channels
            << "interface=" << iface;

    qInfo() << "[UI] Creating engine...";
    try {
        m_engine = std::make_unique<GigaACEEngine>(config);
    } catch (const std::exception& e) {
        qCritical() << "[UI] Engine constructor threw:" << e.what();
        setStatus(QString("Engine init failed: %1").arg(e.what()), "error");
        return;
    }
    qInfo() << "[UI] Engine created, calling start()...";

    if (m_engine && m_engine->start()) {
        qInfo() << "[UI] Engine started OK";
        bool is_demo = m_demo_check->isChecked();
        QString status = is_demo ? "Running" : "Capturing";
        setStatus(status, m_engine->sharedBridgeReady() ? "ok" : "warning");
        m_mode_label->setText(is_demo ? "Demo source" : "Npcap capture");
        m_asio_label->setText(m_engine->sharedBridgeReady() ? "ASIO bridge ready" : "ASIO bridge unavailable");
        m_handshake_label->setVisible(!is_demo);
        if (!is_demo) {
            m_handshake_label->setText("Handshake: announcing");
            m_handshake_label->setStyleSheet("color: #e6b450;");
        }
        m_start_btn->setText("Restart Audio");
        m_monitor_btn->setEnabled(true);
        m_refresh_timer->start(100);
    } else {
        QString err = m_engine ? QString::fromStdString(m_engine ? "" : "") : "engine is null";
        qCritical() << "[UI] Engine start() failed";
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
    m_handshake_label->setText("Handshake: idle");
    m_handshake_label->setStyleSheet("color: #9aa4ad;");
    m_start_btn->setText("Start Audio");
    m_monitor_btn->setEnabled(false);
    m_monitor_btn->setChecked(false);
    resetMeters();
}

void MainWindow::onMonitorToggled(bool checked) {
    if (checked) {
        if (!m_engine) return;

        std::wstring endpoint_id = m_output_combo->currentData().toString().toStdWString();
        m_monitor = std::make_unique<WASAPIOutput>(m_engine->config().sample_rate, endpoint_id);
        m_monitor_resample_phase = 0.0;

        auto callback = [this](int frame_count, float* left, float* right) {
            if (m_engine) {
                const double source_rate = m_engine->config().sample_rate;
                const double output_rate = (m_monitor && m_monitor->sampleRate() > 1.0)
                    ? m_monitor->sampleRate()
                    : source_rate;
                const double ratio = source_rate / output_rate;

                if (std::abs(ratio - 1.0) < 0.0001) {
                    if ((int)m_left_buf.size() < frame_count) {
                        m_left_buf.resize(frame_count);
                        m_right_buf.resize(frame_count);
                    }
                    m_engine->consumeStereo(frame_count, 0, 1, m_left_buf.data(), m_right_buf.data());
                    std::memcpy(left, m_left_buf.data(), frame_count * sizeof(float));
                    std::memcpy(right, m_right_buf.data(), frame_count * sizeof(float));
                    return;
                }

                const int source_frames = std::max(1, static_cast<int>(std::ceil(m_monitor_resample_phase + frame_count * ratio)));
                if ((int)m_monitor_source_left.size() < source_frames) {
                    m_monitor_source_left.resize(source_frames);
                    m_monitor_source_right.resize(source_frames);
                }

                m_engine->consumeStereo(source_frames, 0, 1, m_monitor_source_left.data(), m_monitor_source_right.data());

                for (int i = 0; i < frame_count; ++i) {
                    const double source_pos = m_monitor_resample_phase + i * ratio;
                    const int index = std::min(source_frames - 1, static_cast<int>(source_pos));
                    const int next = std::min(source_frames - 1, index + 1);
                    const float frac = static_cast<float>(source_pos - index);
                    left[i] = m_monitor_source_left[index] + (m_monitor_source_left[next] - m_monitor_source_left[index]) * frac;
                    right[i] = m_monitor_source_right[index] + (m_monitor_source_right[next] - m_monitor_source_right[index]) * frac;
                }

                m_monitor_resample_phase += frame_count * ratio - source_frames;
                if (m_monitor_resample_phase < 0.0 || m_monitor_resample_phase >= 1.0)
                    m_monitor_resample_phase -= std::floor(m_monitor_resample_phase);
            }
        };

        if (m_monitor->start(callback)) {
            m_monitor_running = true;
            m_monitor_btn->setText("Stop Monitor");
            setStatus(QString("Monitoring: %1 (%2 Hz)")
                          .arg(m_output_combo->currentText())
                          .arg(static_cast<int>(m_monitor->sampleRate())),
                      "ok");
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

void MainWindow::resetMeters() {
    std::fill(m_meter_peak_db.begin(), m_meter_peak_db.end(), kMeterFloorDb);
    for (auto* bar : m_meter_bars) {
        if (bar) {
            bar->setValue(0);
            bar->setStyleSheet(meterStyle(kMeterFloorDb));
        }
    }
    for (auto* label : m_meter_db_labels) {
        if (label)
            label->setText("-inf");
    }
    for (auto* label : m_meter_peak_labels) {
        if (label)
            label->setText("-inf");
    }
}

int MainWindow::selectedChannelCount() const {
    int count = m_channel_combo ? m_channel_combo->currentData().toInt() : 64;
    return std::clamp(count, 2, 128);
}

int MainWindow::selectedMeterStartChannel() const {
    int start = m_meter_bank_combo ? m_meter_bank_combo->currentData().toInt() : 0;
    return std::clamp(start, 0, 127);
}

void MainWindow::updateMeterBanks() {
    if (!m_meter_bank_combo)
        return;

    int channel_count = selectedChannelCount();
    int previous_start = selectedMeterStartChannel();
    bool blocked = m_meter_bank_combo->blockSignals(true);
    m_meter_bank_combo->clear();

    for (int start = 0; start < channel_count; start += kMeterCount) {
        int end = std::min(channel_count, start + kMeterCount);
        m_meter_bank_combo->addItem(QString("IN %1-%2")
                                        .arg(start + 1, 2, 10, QChar('0'))
                                        .arg(end, 2, 10, QChar('0')),
                                    start);
    }

    int index = m_meter_bank_combo->findData(previous_start);
    if (index < 0)
        index = 0;
    m_meter_bank_combo->setCurrentIndex(index);
    m_meter_bank_combo->blockSignals(blocked);
    updateMeterDisplay({}, channel_count);
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

void MainWindow::populateInterfaces() {
    m_interface_combo->clear();
    auto ifaces = pcap_enumerate_interfaces();
    if (ifaces.empty()) {
        m_interface_combo->addItem("No interfaces found (Npcap installed?)", QString());
        return;
    }
    for (const auto& iface : ifaces) {
        QString label = QString::fromStdString(iface.description);
        QString value = QString::fromStdString(iface.name);
        m_interface_combo->addItem(label, value);
    }
}

void MainWindow::refreshStats() {
    if (!m_engine) return;
    updateStatsDisplay();
}

static QString handshakeStateLabel(AvantisHandshakeState s) {
    switch (s) {
    case AvantisHandshakeState::Idle:       return "Handshake: idle";
    case AvantisHandshakeState::Announcing: return "Handshake: announcing";
    case AvantisHandshakeState::Connected:  return "Handshake: connected";
    case AvantisHandshakeState::Lost:       return "Handshake: lost";
    }
    return "Handshake: unknown";
}

void MainWindow::updateStatsDisplay() {
    auto stats = m_engine->snapshotStatistics();

    m_frames_rx_label->setText(QString::number(stats.frames_received));
    m_frames_ok_label->setText(QString::number(stats.frames_decoded));
    m_drops_label->setText(QString::number(stats.counter_drops));
    m_channels_label->setText(QString::number(stats.active_channels));
    m_buffered_label->setText(QString::number(m_engine->bufferedFrames()));

    // Handshake status
    auto hs = m_engine->handshakeState();
    QString hslabel = handshakeStateLabel(hs);
    if (hs == AvantisHandshakeState::Connected) {
        QString mac = QString::fromStdString(m_engine->consoleMacStr());
        if (!mac.isEmpty())
            hslabel += " (" + mac + ")";
    }
    m_handshake_label->setText(hslabel);
    QString hscolor = "#9aa4ad";
    if (hs == AvantisHandshakeState::Connected)  hscolor = "#4fce7b";
    else if (hs == AvantisHandshakeState::Announcing) hscolor = "#e6b450";
    else if (hs == AvantisHandshakeState::Lost)   hscolor = "#ff6670";
    m_handshake_label->setStyleSheet(QString("color: %1;").arg(hscolor));

    int meter_start = selectedMeterStartChannel();
    int channel_count = (stats.active_channels > 0) ? stats.active_channels : selectedChannelCount();
    auto levels = m_engine->latestLevels(kMeterCount, meter_start);
    updateMeterDisplay(levels, channel_count);
}

void MainWindow::updateMeterDisplay(const std::vector<float>& levels, int channel_count) {
    int meter_start = selectedMeterStartChannel();
    channel_count = std::clamp(channel_count, 0, 128);
    if ((int)m_meter_peak_db.size() < kMeterCount)
        m_meter_peak_db.assign(kMeterCount, kMeterFloorDb);

    for (int i = 0; i < kMeterCount; ++i) {
        int absolute_channel = meter_start + i;
        bool visible_channel = absolute_channel < channel_count;
        auto* chlbl = (i < (int)m_meter_channel_labels.size()) ? m_meter_channel_labels[i] : nullptr;
        auto* bar = (i < (int)m_meter_bars.size()) ? m_meter_bars[i] : nullptr;
        auto* dblbl = (i < (int)m_meter_db_labels.size()) ? m_meter_db_labels[i] : nullptr;
        auto* peaklbl = (i < (int)m_meter_peak_labels.size()) ? m_meter_peak_labels[i] : nullptr;
        float level = (visible_channel && i < (int)levels.size()) ? levels[i] : 0.0f;
        float dbfs = levelToDb(level);
        m_meter_peak_db[i] = std::max(dbfs, m_meter_peak_db[i] - 0.8f);

        if (chlbl) {
            chlbl->setText(QString("IN %1").arg(absolute_channel + 1, 2, 10, QChar('0')));
            chlbl->setEnabled(visible_channel);
        }
        if (bar) {
            bar->setValue(dbToMeterValue(dbfs));
            bar->setStyleSheet(meterStyle(dbfs));
            bar->setEnabled(visible_channel);
        }
        if (dblbl) {
            dblbl->setText(visible_channel ? dbText(dbfs) : "");
            dblbl->setEnabled(visible_channel);
        }
        if (peaklbl) {
            peaklbl->setText(visible_channel ? dbText(m_meter_peak_db[i]) : "");
            peaklbl->setEnabled(visible_channel);
        }
    }
}
