#ifndef TXDEBUGWINDOW_H
#define TXDEBUGWINDOW_H

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QString>

struct TxDebugSettings {
    bool enabled = false;
    int source = 0;
    int channel = 0;
    double gain = 0.03;
    double frequency = 440.0;
    QString file_path;
    bool loop_file = true;
    int encoding = 0;
    int layout = 4;
    int packet_format = 1;
};

class TxDebugWindow : public QDialog {
    Q_OBJECT

public:
    explicit TxDebugWindow(QWidget* parent = nullptr);
    TxDebugSettings settings() const;
    void setSettings(const TxDebugSettings& settings);

signals:
    void settingsChanged(const TxDebugSettings& settings);

private:
    void emitSettingsChanged();
    void browseFile();
    void updateControlState();

    QCheckBox* m_enabled_check;
    QComboBox* m_source_combo;
    QSpinBox* m_channel_spin;
    QDoubleSpinBox* m_gain_spin;
    QDoubleSpinBox* m_frequency_spin;
    QComboBox* m_encoding_combo;
    QComboBox* m_layout_combo;
    QComboBox* m_packet_format_combo;
    QLineEdit* m_file_edit;
    QPushButton* m_browse_btn;
    QCheckBox* m_loop_check;
    QLabel* m_note_label;
};

#endif
