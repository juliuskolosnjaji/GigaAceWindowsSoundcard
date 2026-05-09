#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTimer>
#include <QProgressBar>
#include <QLabel>
#include <QCheckBox>
#include <QPushButton>
#include <QComboBox>
#include <QGroupBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFrame>
#include <QString>
#include <QElapsedTimer>
#include <QScrollArea>

#include <cstdint>
#include "engine.h"
#include "wasapi_output.h"
#include "diagnosticswindow.h"
#include "txdebugwindow.h"
#include <memory>
#include <vector>

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

private slots:
    void onStartClicked();
    void onStopClicked();
    void onMonitorToggled(bool checked);
    void refreshStats();

private:
    void setupUI();
    void populateOutputDevices();
    void populateInterfaces();
    void updateMeterBanks();
    void updateStatsDisplay();
    void updateMeterDisplay(const std::vector<float>& levels, int channel_count);
    QString runtimeText() const;
    QLabel* makeMetricLabel(const QString& text);
    void setStatus(const QString& text, const QString& state);
    void resetMeters();
    int selectedChannelCount() const;
    int selectedMeterStartChannel() const;
    TxDebugSettings currentTxSettings() const;
    void setTxSettings(const TxDebugSettings& settings);

    std::unique_ptr<GigaACEEngine> m_engine;
    std::unique_ptr<WASAPIOutput> m_monitor;

    QComboBox* m_interface_combo;
    QComboBox* m_channel_combo;
    QCheckBox* m_tx_probe_check;
    QCheckBox* m_tx_tone_check;
    QPushButton* m_start_btn;
    QPushButton* m_stop_btn;
    QPushButton* m_monitor_btn;
    QPushButton* m_diagnostics_btn;
    QPushButton* m_tx_debug_btn;
    QComboBox* m_output_combo;
    QLabel* m_status_label;
    QLabel* m_mode_label;
    QLabel* m_asio_label;
    QLabel* m_handshake_label;
    QLabel* m_frames_rx_label;
    QLabel* m_frames_ok_label;
    QLabel* m_drops_label;
    QLabel* m_channels_label;
    QLabel* m_buffered_label;
    QLabel* m_runtime_label;
    QComboBox* m_meter_bank_combo;
    std::vector<QLabel*> m_meter_channel_labels;
    std::vector<QProgressBar*> m_meter_bars;
    std::vector<QLabel*> m_meter_db_labels;
    std::vector<QLabel*> m_meter_peak_labels;
    std::vector<float> m_meter_peak_db;

    QTimer* m_refresh_timer;
    bool m_monitor_running = false;
    bool m_runtime_active = false;
    QElapsedTimer m_runtime_timer;
    uint64_t m_last_rate_frames = 0;
    qint64 m_last_rate_ms = 0;
    double m_current_frame_rate = 0.0;
    std::unique_ptr<DiagnosticsWindow> m_diagnostics_window;
    std::unique_ptr<TxDebugWindow> m_tx_debug_window;
    TxDebugSettings m_tx_settings;

    std::vector<float> m_left_buf;
    std::vector<float> m_right_buf;
    std::vector<float> m_monitor_source_left;
    std::vector<float> m_monitor_source_right;
    double m_monitor_resample_phase = 0.0;
};

#endif
