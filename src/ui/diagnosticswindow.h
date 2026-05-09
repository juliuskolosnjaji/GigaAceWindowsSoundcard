#ifndef DIAGNOSTICSWINDOW_H
#define DIAGNOSTICSWINDOW_H

#include "frame.h"
#include <QDialog>
#include <QLabel>
#include <QString>
#include <QMap>

class DiagnosticsWindow : public QDialog {
    Q_OBJECT

public:
    explicit DiagnosticsWindow(QWidget* parent = nullptr);

    void updateSnapshot(const GigaACEStatistics& stats,
                        int buffered_frames,
                        const QString& runtime,
                        const QString& handshake,
                        const QString& console_mac,
                        const QString& interface_name,
                        bool bridge_ready,
                        bool monitor_running,
                        bool tx_enabled,
                        int configured_channels,
                        double sample_rate,
                        double frame_rate);

private:
    QLabel* addRow(const QString& key, const QString& label);
    void setValue(const QString& key, const QString& value);
    void saveSnapshot();
    QString buildSnapshotText() const;

    QMap<QString, QLabel*> m_values;
    QString m_last_snapshot;
};

#endif
