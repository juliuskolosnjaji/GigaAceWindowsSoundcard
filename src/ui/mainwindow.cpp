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
static constexpr float kMeterCeilDb = 12.0f;
static constexpr float kMeterCalibrationDb = 12.0f;

static float levelToDb(float level) {
    if (level <= 0.00000025f)
        return kMeterFloorDb;
    return std::clamp(20.0f * std::log10(level) + kMeterCalibrationDb, kMeterFloorDb, kMeterCeilDb);
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
        "QProgressBar { border: 1px solid #8a8a8a; border-radius: 2px; height: 13px; text-align: center; }"
        "QProgressBar::chunk { background: %1; }"
    ).arg(color);
}

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent) {
    setWindowTitle("GigaACE Virtual Sound Card");
    resize(1180, 760);
    setMinimumSize(860, 560);

    m_interface_combo = new QComboBox();
    m_interface_combo->setMinimumWidth(260);
    m_interface_combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    m_channel_combo = new QComboBox();
    for (int count : {2, 8, 16, 24, 32, 48, 64, 96, 128})
        m_channel_combo->addItem(QString("%1 inputs").arg(count), count);
    m_channel_combo->setCurrentIndex(m_channel_combo->findData(64));
    m_channel_combo->setMinimumWidth(110);

    m_tx_probe_check = new QCheckBox("Send to console");
    m_tx_probe_check->setToolTip("Experimental: transmit synchronized test frames back to the selected GigaACE interface. Leave this off for receive-only ASIO use.");
    m_tx_tone_check = new QCheckBox("TX test tone CH 01");
    m_tx_tone_check->setToolTip("Only used when Send to console is enabled. Off sends silence; on sends a quiet 1 kHz probe tone on channel 1.");
    m_tx_tone_check->setChecked(false);
    m_tx_tone_check->setEnabled(false);

    m_start_btn = new QPushButton("Start Audio");
    m_stop_btn = new QPushButton("Stop");
    m_monitor_btn = new QPushButton("Start Monitor");
    m_monitor_btn->setCheckable(true);
    m_monitor_btn->setEnabled(false);
    m_diagnostics_btn = new QPushButton("Diagnostics");
    m_tx_debug_btn = new QPushButton("TX Debug");
    m_output_combo = new QComboBox();
    m_output_combo->setMinimumWidth(220);
    m_output_combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    m_status_label = new QLabel("Ready");
    m_mode_label = new QLabel("Npcap capture");
    m_asio_label = new QLabel("ASIO bridge idle");
    m_handshake_label = new QLabel("Handshake: idle");

    m_frames_rx_label = new QLabel("0");
    m_frames_ok_label = new QLabel("0");
    m_drops_label = new QLabel("0");
    m_channels_label = new QLabel("0");
    m_buffered_label = new QLabel("0");
    m_runtime_label = new QLabel("00:00:00");
    m_meter_bank_combo = new QComboBox();
    m_meter_bank_combo->setMinimumWidth(170);

    m_refresh_timer = new QTimer(this);
    connect(m_refresh_timer, &QTimer::timeout, this, &MainWindow::refreshStats);

    connect(m_start_btn, &QPushButton::clicked, this, &MainWindow::onStartClicked);
    connect(m_stop_btn, &QPushButton::clicked, this, &MainWindow::onStopClicked);
    connect(m_monitor_btn, &QPushButton::clicked, this, &MainWindow::onMonitorToggled);
    connect(m_diagnostics_btn, &QPushButton::clicked, [this]() {
        if (!m_diagnostics_window)
            m_diagnostics_window = std::make_unique<DiagnosticsWindow>(this);
        if (m_engine)
            updateStatsDisplay();
        m_diagnostics_window->show();
        m_diagnostics_window->raise();
        m_diagnostics_window->activateWindow();
    });
    connect(m_tx_debug_btn, &QPushButton::clicked, [this]() {
        if (!m_tx_debug_window) {
            m_tx_debug_window = std::make_unique<TxDebugWindow>(this);
            m_tx_debug_window->setSettings(currentTxSettings());
            connect(m_tx_debug_window.get(), &TxDebugWindow::settingsChanged, this, [this](const TxDebugSettings& settings) {
                setTxSettings(settings);
            });
        }
        m_tx_debug_window->show();
        m_tx_debug_window->raise();
        m_tx_debug_window->activateWindow();
    });
    connect(m_tx_probe_check, &QCheckBox::toggled, [this](bool checked) {
        m_tx_tone_check->setEnabled(checked);
        m_tx_settings.enabled = checked;
        m_tx_settings.source = (checked && m_tx_tone_check->isChecked()) ? GIGAACE_TX_SOURCE_TONE : GIGAACE_TX_SOURCE_SILENCE;
        if (m_tx_debug_window)
            m_tx_debug_window->setSettings(m_tx_settings);
    });
    connect(m_tx_tone_check, &QCheckBox::toggled, [this](bool checked) {
        m_tx_settings.source = (m_tx_probe_check->isChecked() && checked) ? GIGAACE_TX_SOURCE_TONE : GIGAACE_TX_SOURCE_SILENCE;
        if (m_tx_debug_window)
            m_tx_debug_window->setSettings(m_tx_settings);
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
    populateInterfaces();
    updateMeterBanks();
    updateMeterDisplay({}, selectedChannelCount());
    setStatus("Ready", "idle");
}

MainWindow::~MainWindow() {
    onStopClicked();
}

void MainWindow::setupUI() {
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    setCentralWidget(scroll);

    auto* central = new QWidget();
    central->setMinimumWidth(820);
    scroll->setWidget(central);

    central->setStyleSheet(
        "QWidget { font-size: 13px; }"
        "QLabel#Title { font-size: 24px; font-weight: 700; }"
        "QLabel#Subtitle { color: #4b5563; }"
        "QFrame#StatusBar { border: 1px solid #b8b8b8; background: #f5f5f5; }"
        "QPushButton#PrimaryButton { font-weight: 600; }"
        "QPushButton#DangerButton { font-weight: 600; }"
    );
    auto* main_layout = new QVBoxLayout(central);
    main_layout->setSpacing(12);
    main_layout->setContentsMargins(16, 16, 16, 16);

    auto* title = new QLabel("GigaACE Virtual Sound Card");
    title->setObjectName("Title");
    main_layout->addWidget(title);

    auto* subtitle = new QLabel("Decode GigaACE/SLink frames and expose them as a 64-channel ASIO input device.");
    subtitle->setObjectName("Subtitle");
    main_layout->addWidget(subtitle);

    auto* controls_group = new QGroupBox("Audio");
    auto* controls = new QGridLayout(controls_group);
    controls->setHorizontalSpacing(10);
    controls->setVerticalSpacing(10);
    controls->setContentsMargins(12, 12, 12, 12);

    m_start_btn->setObjectName("PrimaryButton");
    m_stop_btn->setObjectName("DangerButton");

    controls->addWidget(new QLabel("Interface"), 0, 0);
    controls->addWidget(m_interface_combo, 0, 1, 1, 3);
    controls->addWidget(new QLabel("Channels"), 0, 4);
    controls->addWidget(m_channel_combo, 0, 5);
    controls->addWidget(new QLabel("Monitor out"), 1, 0);
    controls->addWidget(m_output_combo, 1, 1);
    controls->addWidget(m_start_btn, 1, 2);
    controls->addWidget(m_monitor_btn, 1, 3);
    controls->addWidget(m_stop_btn, 1, 4);
    controls->addWidget(m_diagnostics_btn, 1, 5);
    controls->addWidget(m_tx_debug_btn, 2, 3);
    controls->addWidget(m_tx_probe_check, 2, 1);
    controls->addWidget(m_tx_tone_check, 2, 2);
    controls->setColumnStretch(1, 2);
    controls->setColumnStretch(3, 1);
    controls->setColumnStretch(6, 1);
    main_layout->addWidget(controls_group);

    auto* status_frame = new QFrame();
    status_frame->setObjectName("StatusBar");
    auto* status_layout = new QHBoxLayout(status_frame);
    status_layout->setContentsMargins(10, 8, 10, 8);
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
    stats_layout->setHorizontalSpacing(14);
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
    stats_layout->addWidget(makeMetricLabel("Run time"), 0, 5);
    stats_layout->addWidget(m_runtime_label, 1, 5);
    for (auto* label : {m_frames_rx_label, m_frames_ok_label, m_drops_label, m_channels_label, m_buffered_label, m_runtime_label})
        label->setStyleSheet("font-size: 18px; font-weight: 700;");
    main_layout->addWidget(stats);

    auto* meter_group = new QGroupBox("Input Metering");
    auto* meter_layout = new QGridLayout(meter_group);
    meter_layout->setHorizontalSpacing(10);
    meter_layout->setVerticalSpacing(8);

    auto* meter_header = new QHBoxLayout();
    auto* meter_hint = new QLabel("Visible bank");
    meter_hint->setMinimumWidth(130);
    meter_hint->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Preferred);
    meter_hint->setStyleSheet("font-weight: 600;");
    meter_header->addWidget(meter_hint);
    meter_header->addWidget(m_meter_bank_combo);
    meter_header->addStretch();
    auto* meter_legend = new QLabel("Calibrated +12 dB   Green < -18   Yellow -18..-6   Red > -6");
    meter_legend->setMinimumWidth(300);
    meter_legend->setWordWrap(true);
    meter_legend->setStyleSheet("color: #4b5563; font-size: 11px;");
    meter_header->addWidget(meter_legend);
    meter_layout->addLayout(meter_header, 0, 0, 1, 10);

    for (int col = 0; col < 2; ++col) {
        int base_col = col * 5;
        auto* scale = new QLabel("-60        -36        -12       0    +6  +12");
        scale->setMinimumWidth(200);
        scale->setStyleSheet("color: #4b5563; font-size: 10px;");
        meter_layout->addWidget(scale, 1, base_col + 1, 1, 3);
        auto* peak_hdr = new QLabel("Peak");
        peak_hdr->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        peak_hdr->setStyleSheet("color: #4b5563; font-size: 10px;");
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
        ch_label->setStyleSheet("font-weight: 600;");
        meter_layout->addWidget(ch_label, row, col);

        auto* bar = new QProgressBar();
        bar->setRange(0, 1000);
        bar->setValue(0);
        bar->setTextVisible(false);
        bar->setMinimumWidth(180);
        bar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        bar->setObjectName(QString("meter_%1").arg(i));
        bar->setStyleSheet(meterStyle(kMeterFloorDb));
        meter_layout->addWidget(bar, row, col + 1);

        auto* db_label = new QLabel("-inf");
        db_label->setFixedWidth(74);
        db_label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        db_label->setObjectName(QString("meter_db_%1").arg(i));
        db_label->setStyleSheet("font-family: Consolas, 'Cascadia Mono', monospace; font-size: 11px;");
        meter_layout->addWidget(db_label, row, col + 2);

        auto* peak_label = new QLabel("-inf");
        peak_label->setFixedWidth(74);
        peak_label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        peak_label->setStyleSheet("color: #4b5563; font-family: Consolas, 'Cascadia Mono', monospace; font-size: 11px;");
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
        "Start this app before opening the ASIO device in your DAW. Live capture uses Npcap; sending to the console is separate and off by default."
    );
    info->setWordWrap(true);
    info->setStyleSheet("color: gray; font-size: 11px;");
    main_layout->addWidget(info);
}

void MainWindow::onStartClicked() {
    qInfo() << "[UI] Start Audio clicked";
    onStopClicked();

    GigaACEConfig config{};
    config.channels = selectedChannelCount();
    config.sample_rate = 96000.0;
    config.ring_buffer_frames = 192000;
    config.capture_mode = GIGAACE_CAPTURE_MODE_PCAP;

    QString iface = m_interface_combo->currentData().toString();
    std::strncpy(config.interface_name, iface.toUtf8().constData(),
                 sizeof(config.interface_name) - 1);
    config.interface_name[sizeof(config.interface_name) - 1] = '\0';

    config.shared_memory_name = "Local\\GigaACEVirtualDevice";
    config.shared_memory_frames = 192000;
    TxDebugSettings tx = currentTxSettings();
    config.tx_probe_enabled = tx.enabled ? 1 : 0;
    config.tx_probe_source = static_cast<GigaACETxSource>(tx.source);
    config.tx_probe_tone_enabled = (tx.enabled && tx.source == GIGAACE_TX_SOURCE_TONE) ? 1 : 0;
    config.tx_probe_channel = tx.channel;
    config.tx_probe_gain = (float)tx.gain;
    config.tx_probe_frequency = tx.frequency;
    config.tx_probe_file_loop = tx.loop_file ? 1 : 0;
    config.tx_probe_encoding = static_cast<GigaACETxEncoding>(tx.encoding);
    config.tx_probe_layout = static_cast<GigaACETxLayout>(tx.layout);
    config.tx_probe_packet_format = static_cast<GigaACETxPacketFormat>(tx.packet_format);
    std::strncpy(config.tx_probe_file_path, tx.file_path.toUtf8().constData(),
                 sizeof(config.tx_probe_file_path) - 1);
    config.tx_probe_file_path[sizeof(config.tx_probe_file_path) - 1] = '\0';

    qInfo() << "[UI] Config: mode=pcap"
            << "channels=" << config.channels
            << "interface=" << iface
            << "send_to_console=" << config.tx_probe_enabled
            << "tx_source=" << config.tx_probe_source
            << "tx_channel=" << config.tx_probe_channel + 1
            << "tx_encoding=" << config.tx_probe_encoding
            << "tx_layout=" << config.tx_probe_layout
            << "tx_packet_format=" << config.tx_probe_packet_format;

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
        m_runtime_timer.restart();
        m_runtime_active = true;
        m_last_rate_frames = 0;
        m_last_rate_ms = 0;
        m_current_frame_rate = 0.0;
        m_runtime_label->setText(runtimeText());
        setStatus("Capturing", m_engine->sharedBridgeReady() ? "ok" : "warning");
        m_mode_label->setText("Npcap capture");
        m_asio_label->setText(m_engine->sharedBridgeReady() ? "ASIO bridge ready" : "ASIO bridge unavailable");
        m_handshake_label->setVisible(true);
        m_handshake_label->setText("Handshake: announcing");
        m_handshake_label->setStyleSheet("color: #e6b450;");
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
    m_runtime_active = false;
    m_last_rate_frames = 0;
    m_last_rate_ms = 0;
    m_current_frame_rate = 0.0;
    if (m_runtime_label)
        m_runtime_label->setText("00:00:00");
    m_mode_label->setText("Npcap capture");
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
            setStatus(QString("Monitoring: %1 (%2 Hz, ~%3 ms)")
                          .arg(m_output_combo->currentText())
                          .arg(static_cast<int>(m_monitor->sampleRate()))
                          .arg(m_monitor->latencyMs(), 0, 'f', 1),
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
    label->setStyleSheet("font-weight: 600;");
    return label;
}

void MainWindow::setStatus(const QString& text, const QString& state) {
    QString color = "#7d8790";
    if (state == "ok")
        color = "#0a7d2a";
    else if (state == "warning")
        color = "#946200";
    else if (state == "error")
        color = "#b00020";
    m_status_label->setText(text);
    m_status_label->setStyleSheet(QString("font-weight: 700; color: %1;").arg(color));
}

QString MainWindow::runtimeText() const {
    if (!m_runtime_active || !m_runtime_timer.isValid())
        return "00:00:00";

    qint64 seconds = m_runtime_timer.elapsed() / 1000;
    qint64 hours = seconds / 3600;
    seconds %= 3600;
    qint64 minutes = seconds / 60;
    seconds %= 60;
    return QString("%1:%2:%3")
        .arg(hours, 2, 10, QChar('0'))
        .arg(minutes, 2, 10, QChar('0'))
        .arg(seconds, 2, 10, QChar('0'));
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

TxDebugSettings MainWindow::currentTxSettings() const {
    TxDebugSettings settings = m_tx_settings;
    settings.enabled = m_tx_probe_check ? m_tx_probe_check->isChecked() : settings.enabled;
    if (m_tx_debug_window)
        settings = m_tx_debug_window->settings();
    else if (m_tx_tone_check && m_tx_tone_check->isChecked())
        settings.source = GIGAACE_TX_SOURCE_TONE;
    else if (!settings.enabled)
        settings.source = GIGAACE_TX_SOURCE_SILENCE;
    return settings;
}

void MainWindow::setTxSettings(const TxDebugSettings& settings) {
    m_tx_settings = settings;
    bool probe_blocked = m_tx_probe_check->blockSignals(true);
    bool tone_blocked = m_tx_tone_check->blockSignals(true);
    m_tx_probe_check->setChecked(settings.enabled);
    m_tx_tone_check->setChecked(settings.source == GIGAACE_TX_SOURCE_TONE);
    m_tx_tone_check->setEnabled(settings.enabled);
    m_tx_tone_check->blockSignals(tone_blocked);
    m_tx_probe_check->blockSignals(probe_blocked);
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
    qint64 now_ms = m_runtime_active && m_runtime_timer.isValid() ? m_runtime_timer.elapsed() : 0;
    if (m_last_rate_ms > 0 && now_ms > m_last_rate_ms) {
        qint64 elapsed_ms = now_ms - m_last_rate_ms;
        uint64_t frames_delta = stats.frames_received >= m_last_rate_frames
            ? stats.frames_received - m_last_rate_frames
            : 0;
        double instant_rate = static_cast<double>(frames_delta) * 1000.0 / static_cast<double>(elapsed_ms);
        m_current_frame_rate = (m_current_frame_rate <= 0.0)
            ? instant_rate
            : (m_current_frame_rate * 0.75 + instant_rate * 0.25);
    }
    m_last_rate_ms = now_ms;
    m_last_rate_frames = stats.frames_received;

    m_frames_rx_label->setText(QString::number(stats.frames_received));
    m_frames_ok_label->setText(QString::number(stats.frames_decoded));
    m_drops_label->setText(QString::number(stats.counter_drops));
    m_channels_label->setText(QString::number(stats.active_channels));
    m_buffered_label->setText(QString::number(m_engine->bufferedFrames()));
    m_runtime_label->setText(runtimeText());

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
    if (hs == AvantisHandshakeState::Connected)  hscolor = "#0a7d2a";
    else if (hs == AvantisHandshakeState::Announcing) hscolor = "#946200";
    else if (hs == AvantisHandshakeState::Lost)   hscolor = "#b00020";
    m_handshake_label->setStyleSheet(QString("color: %1;").arg(hscolor));

    int meter_start = selectedMeterStartChannel();
    int channel_count = (stats.active_channels > 0) ? stats.active_channels : selectedChannelCount();
    auto levels = m_engine->latestLevels(kMeterCount, meter_start);
    updateMeterDisplay(levels, channel_count);

    if (m_diagnostics_window && m_diagnostics_window->isVisible()) {
        m_diagnostics_window->updateSnapshot(
            stats,
            m_engine->bufferedFrames(),
            runtimeText(),
            hslabel,
            QString::fromStdString(m_engine->consoleMacStr()),
            m_interface_combo->currentText(),
            m_engine->sharedBridgeReady(),
            m_monitor_running,
            m_tx_probe_check->isChecked(),
            m_engine->config().channels,
            m_engine->config().sample_rate,
            m_current_frame_rate
        );
    }
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
