#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTimer>
#include <QProgressBar>
#include <QLabel>
#include <QLineEdit>
#include <QCheckBox>
#include <QSpinBox>
#include <QPushButton>
#include <QComboBox>
#include <QGroupBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFrame>
#include <QString>

#include "engine.h"
#include "wasapi_output.h"
#include <memory>

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
    void updateStatsDisplay();
    QLabel* makeMetricLabel(const QString& text);
    void setStatus(const QString& text, const QString& state);

    std::unique_ptr<GigaACEEngine> m_engine;
    std::unique_ptr<WASAPIOutput> m_monitor;

    QCheckBox* m_demo_check;
    QLineEdit* m_interface_edit;
    QSpinBox* m_channel_spin;
    QPushButton* m_start_btn;
    QPushButton* m_stop_btn;
    QPushButton* m_monitor_btn;
    QComboBox* m_output_combo;
    QLabel* m_status_label;
    QLabel* m_mode_label;
    QLabel* m_asio_label;
    QLabel* m_frames_rx_label;
    QLabel* m_frames_ok_label;
    QLabel* m_drops_label;
    QLabel* m_channels_label;
    QLabel* m_buffered_label;

    QTimer* m_refresh_timer;
    bool m_monitor_running = false;

    std::vector<float> m_left_buf;
    std::vector<float> m_right_buf;
};

#endif
