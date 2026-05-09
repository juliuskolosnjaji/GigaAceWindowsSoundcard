#include "diagnosticswindow.h"
#include <QFormLayout>
#include <QVBoxLayout>
#include <QGroupBox>
#include <QDialogButtonBox>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QStandardPaths>
#include <QTextStream>

DiagnosticsWindow::DiagnosticsWindow(QWidget* parent)
    : QDialog(parent) {
    setWindowTitle("GigaACE Diagnostics");
    resize(760, 720);
    setMinimumSize(640, 560);
    setModal(false);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(10);

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    root->addWidget(scroll);

    auto* body = new QWidget();
    scroll->setWidget(body);

    auto* body_layout = new QVBoxLayout(body);
    body_layout->setContentsMargins(0, 0, 0, 0);
    body_layout->setSpacing(10);

    auto* stream_group = new QGroupBox("Stream");
    auto* stream = new QFormLayout(stream_group);
    stream->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    stream->setLabelAlignment(Qt::AlignLeft);
    stream->setFormAlignment(Qt::AlignTop);
    stream->setHorizontalSpacing(28);
    stream->setVerticalSpacing(7);
    auto add_to = [this, stream](const QString& key, const QString& label) {
        auto* value = addRow(key, label);
        stream->addRow(label, value);
    };
    add_to("runtime", "Run time");
    add_to("interface", "Interface");
    add_to("sample_rate", "Sample rate");
    add_to("configured_channels", "Configured channels");
    add_to("active_channels", "Active channels");
    add_to("frame_rate", "Frame rate");
    add_to("buffered", "Buffered frames");
    add_to("bridge", "ASIO bridge");
    body_layout->addWidget(stream_group);

    auto* packet_group = new QGroupBox("Packets");
    auto* packets = new QFormLayout(packet_group);
    packets->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    packets->setLabelAlignment(Qt::AlignLeft);
    packets->setFormAlignment(Qt::AlignTop);
    packets->setHorizontalSpacing(28);
    packets->setVerticalSpacing(7);
    auto add_packet = [this, packets](const QString& key, const QString& label) {
        auto* value = addRow(key, label);
        packets->addRow(label, value);
    };
    add_packet("rx", "Frames RX");
    add_packet("ok", "Frames OK");
    add_packet("rejected", "Frames rejected");
    add_packet("rejected_decode", "Rejected: decode");
    add_packet("rejected_non_audio", "Rejected: non-audio");
    add_packet("drops", "Counter drops");
    add_packet("duplicates", "Duplicate counters");
    add_packet("concealed", "Concealed frames");
    add_packet("last_counter", "Last counter");
    add_packet("counter_delta", "Counter delta");
    add_packet("stream_type", "Stream type");
    add_packet("frame_size", "Frame size");
    body_layout->addWidget(packet_group);

    auto* link_group = new QGroupBox("Link");
    auto* link = new QFormLayout(link_group);
    link->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    link->setLabelAlignment(Qt::AlignLeft);
    link->setFormAlignment(Qt::AlignTop);
    link->setHorizontalSpacing(28);
    link->setVerticalSpacing(7);
    auto add_link = [this, link](const QString& key, const QString& label) {
        auto* value = addRow(key, label);
        link->addRow(label, value);
    };
    add_link("handshake", "Handshake");
    add_link("console_mac", "Console MAC");
    add_link("monitor", "Monitor");
    add_link("tx", "Send to console");
    body_layout->addWidget(link_group);
    body_layout->addStretch();

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    auto* save_button = buttons->addButton("Save Snapshot", QDialogButtonBox::ActionRole);
    connect(save_button, &QPushButton::clicked, this, &DiagnosticsWindow::saveSnapshot);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::hide);
    root->addWidget(buttons);
}

QLabel* DiagnosticsWindow::addRow(const QString& key, const QString&) {
    auto* value = new QLabel("-");
    value->setTextInteractionFlags(Qt::TextSelectableByMouse);
    value->setMinimumWidth(360);
    value->setWordWrap(false);
    m_values.insert(key, value);
    return value;
}

void DiagnosticsWindow::setValue(const QString& key, const QString& value) {
    if (auto* label = m_values.value(key, nullptr))
        label->setText(value);
}

void DiagnosticsWindow::updateSnapshot(const GigaACEStatistics& stats,
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
                                       double frame_rate) {
    setValue("runtime", runtime);
    setValue("interface", interface_name.isEmpty() ? "-" : interface_name);
    setValue("sample_rate", QString::number(sample_rate, 'f', 0) + " Hz");
    setValue("configured_channels", QString::number(configured_channels));
    setValue("active_channels", QString::number(stats.active_channels));
    setValue("frame_rate", QString::number(frame_rate, 'f', 1) + " fps");
    setValue("buffered", QString::number(buffered_frames));
    setValue("bridge", bridge_ready ? "ready" : "unavailable");

    setValue("rx", QString::number(stats.frames_received));
    setValue("ok", QString::number(stats.frames_decoded));
    setValue("rejected", QString::number(stats.frames_rejected));
    setValue("rejected_decode", QString::number(stats.frames_rejected_decode));
    setValue("rejected_non_audio", QString::number(stats.frames_rejected_non_audio));
    setValue("drops", QString::number(stats.counter_drops));
    setValue("duplicates", QString::number(stats.duplicate_counters));
    setValue("concealed", QString::number(stats.concealed_frames));
    setValue("last_counter", stats.has_last_counter ? QString::number(stats.last_counter) : "-");
    setValue("counter_delta", stats.has_last_counter_delta ? QString::number(stats.last_counter_delta) : "-");
    setValue("stream_type", QString("0x%1").arg(stats.stream_type, 2, 16, QChar('0')).toUpper());
    setValue("frame_size", QString("last %1 / min %2 / max %3")
                               .arg(stats.last_frame_size)
                               .arg(stats.min_frame_size)
                               .arg(stats.max_frame_size));

    setValue("handshake", handshake);
    setValue("console_mac", console_mac.isEmpty() ? "-" : console_mac);
    setValue("monitor", monitor_running ? "running" : "stopped");
    setValue("tx", tx_enabled ? "enabled" : "disabled");

    m_last_snapshot = buildSnapshotText();
}

QString DiagnosticsWindow::buildSnapshotText() const {
    QString text;
    QTextStream out(&text);
    out << "GigaACE Diagnostics Snapshot\n";
    out << "Captured: " << QDateTime::currentDateTime().toString(Qt::ISODate) << "\n\n";
    for (auto it = m_values.constBegin(); it != m_values.constEnd(); ++it)
        out << it.key() << ": " << it.value()->text() << "\n";
    return text;
}

void DiagnosticsWindow::saveSnapshot() {
    QString base = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (base.isEmpty())
        base = QDir::homePath();
    QString suggested = base + "/gigaace-diagnostics-" +
        QDateTime::currentDateTime().toString("yyyyMMdd-hhmmss") + ".txt";
    QString path = QFileDialog::getSaveFileName(this, "Save Diagnostics Snapshot", suggested, "Text files (*.txt)");
    if (path.isEmpty())
        return;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "Diagnostics", "Could not write diagnostics snapshot.");
        return;
    }

    QTextStream out(&file);
    out << (m_last_snapshot.isEmpty() ? buildSnapshotText() : m_last_snapshot);
}
