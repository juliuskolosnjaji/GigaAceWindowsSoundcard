#ifndef CAPTUREANALYZERWINDOW_H
#define CAPTUREANALYZERWINDOW_H

#include <QWidget>

class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProcess;
class QPushButton;
class QSpinBox;

class CaptureAnalyzerWindow : public QWidget {
    Q_OBJECT

public:
    explicit CaptureAnalyzerWindow(QWidget* parent = nullptr);
    ~CaptureAnalyzerWindow() override;

private slots:
    void browseCapture();
    void startAnalysis();
    void stopAnalysis();
    void readOutput();
    void processFinished(int exit_code);

private:
    QString analyzerPath() const;
    void setRunning(bool running);

    QLineEdit* m_path_edit = nullptr;
    QPushButton* m_browse_btn = nullptr;
    QPushButton* m_run_btn = nullptr;
    QPushButton* m_stop_btn = nullptr;
    QDoubleSpinBox* m_tone_spin = nullptr;
    QDoubleSpinBox* m_rate_spin = nullptr;
    QSpinBox* m_slots_spin = nullptr;
    QSpinBox* m_frames_spin = nullptr;
    QSpinBox* m_top_spin = nullptr;
    QCheckBox* m_byte_scan_check = nullptr;
    QLabel* m_status_label = nullptr;
    QPlainTextEdit* m_output = nullptr;
    QProcess* m_process = nullptr;
};

#endif
