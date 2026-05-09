#include "captureanalyzerwindow.h"

#include <QApplication>
#include <QCheckBox>
#include <QCoreApplication>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QSpinBox>
#include <QTextCursor>
#include <QVBoxLayout>

CaptureAnalyzerWindow::CaptureAnalyzerWindow(QWidget* parent)
    : QWidget(parent) {
    setWindowFlag(Qt::Window, true);
    setWindowTitle("GigaACE Capture Analyzer");
    resize(980, 720);
    setMinimumSize(760, 520);

    m_path_edit = new QLineEdit();
    m_path_edit->setPlaceholderText("Select a .pcapng capture");
    m_browse_btn = new QPushButton("Browse");
    m_run_btn = new QPushButton("Analyze");
    m_stop_btn = new QPushButton("Stop");
    m_stop_btn->setEnabled(false);
    m_status_label = new QLabel("Ready");
    m_hint_label = new QLabel("Select one of the known GX4816/GigaACE captures to get scenario-specific hints.");
    m_hint_label->setWordWrap(true);
    m_hint_label->setStyleSheet("color: #4b5563;");

    m_tone_spin = new QDoubleSpinBox();
    m_tone_spin->setRange(1.0, 40000.0);
    m_tone_spin->setDecimals(1);
    m_tone_spin->setValue(440.0);
    m_tone_spin->setSuffix(" Hz");

    m_rate_spin = new QDoubleSpinBox();
    m_rate_spin->setRange(8000.0, 384000.0);
    m_rate_spin->setDecimals(1);
    m_rate_spin->setValue(96000.0);
    m_rate_spin->setSuffix(" Hz");

    m_slots_spin = new QSpinBox();
    m_slots_spin->setRange(1, 416);
    m_slots_spin->setValue(128);

    m_frames_spin = new QSpinBox();
    m_frames_spin->setRange(100, 1000000);
    m_frames_spin->setSingleStep(1000);
    m_frames_spin->setValue(96000);

    m_top_spin = new QSpinBox();
    m_top_spin->setRange(1, 100);
    m_top_spin->setValue(30);

    m_byte_scan_check = new QCheckBox("Byte scan");
    m_byte_scan_check->setToolTip("Scan every possible byte offset instead of only 3-byte aligned sample slots. Slower, but useful when lane alignment is unknown.");

    m_output = new QPlainTextEdit();
    m_output->setReadOnly(true);
    m_output->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_output->setStyleSheet("font-family: Consolas, 'Cascadia Mono', monospace; font-size: 11px;");

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(14, 14, 14, 14);
    root->setSpacing(10);

    auto* file_group = new QGroupBox("Capture");
    auto* file_layout = new QGridLayout(file_group);
    file_layout->addWidget(new QLabel("File"), 0, 0);
    file_layout->addWidget(m_path_edit, 0, 1);
    file_layout->addWidget(m_browse_btn, 0, 2);
    file_layout->addWidget(new QLabel("Expected"), 1, 0);
    file_layout->addWidget(m_hint_label, 1, 1, 1, 2);
    file_layout->setColumnStretch(1, 1);
    root->addWidget(file_group);

    auto* options_group = new QGroupBox("Analysis");
    auto* options = new QGridLayout(options_group);
    options->addWidget(new QLabel("Tone"), 0, 0);
    options->addWidget(m_tone_spin, 0, 1);
    options->addWidget(new QLabel("Rate"), 0, 2);
    options->addWidget(m_rate_spin, 0, 3);
    options->addWidget(new QLabel("Slots"), 0, 4);
    options->addWidget(m_slots_spin, 0, 5);
    options->addWidget(new QLabel("Max frames"), 1, 0);
    options->addWidget(m_frames_spin, 1, 1);
    options->addWidget(new QLabel("Top rows"), 1, 2);
    options->addWidget(m_top_spin, 1, 3);
    options->addWidget(m_byte_scan_check, 1, 4, 1, 2);
    root->addWidget(options_group);

    auto* actions = new QHBoxLayout();
    actions->addWidget(m_run_btn);
    actions->addWidget(m_stop_btn);
    actions->addStretch();
    actions->addWidget(m_status_label);
    root->addLayout(actions);
    root->addWidget(m_output, 1);

    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::MergedChannels);

    connect(m_browse_btn, &QPushButton::clicked, this, &CaptureAnalyzerWindow::browseCapture);
    connect(m_path_edit, &QLineEdit::textChanged, this, &CaptureAnalyzerWindow::updateCaptureHint);
    connect(m_run_btn, &QPushButton::clicked, this, &CaptureAnalyzerWindow::startAnalysis);
    connect(m_stop_btn, &QPushButton::clicked, this, &CaptureAnalyzerWindow::stopAnalysis);
    connect(m_process, &QProcess::readyReadStandardOutput, this, &CaptureAnalyzerWindow::readOutput);
    connect(m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, [this](int exit_code, QProcess::ExitStatus) {
        processFinished(exit_code);
    });
}

CaptureAnalyzerWindow::~CaptureAnalyzerWindow() {
    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_process->kill();
        m_process->waitForFinished(1000);
    }
}

void CaptureAnalyzerWindow::browseCapture() {
    QString file = QFileDialog::getOpenFileName(
        this,
        "Open packet capture",
        QDir::homePath(),
        "Packet captures (*.pcapng *.pcap *.cap *.pca);;All files (*.*)"
    );
    if (!file.isEmpty())
        m_path_edit->setText(QDir::toNativeSeparators(file));
}

QString CaptureAnalyzerWindow::inferScenario(const QString& path) const {
    QString name = QFileInfo(path).fileName().toLower();
    if (name.contains("connection without endpoint identifier")) {
        return "GX4816 connection/startup capture. Expect GX4816 0x04EE frames from 00:04:c4:06:cf:e8. Useful for identity/startup headers; often no useful tone candidates.";
    }
    if (name.contains("gigaace connection to slink")) {
        return "GigaACE card -> SLink receive-channel test. Expect 0x00E1 from 00:04:c4:09:ba:c4 and 440 Hz on receive channel/slot 1. Good reference for GigaACE-card emulation.";
    }
    if (name.contains("gx4816 connection to avantis") && name.contains("recieve channel 1")) {
        return "GX4816 -> Avantis receive-channel test. Expect 0x04EE GX4816 frames and 440 Hz on Avantis receive channel 1. Good reference for stagebox-to-console TX.";
    }
    if (name.contains("gx4816 connection to avantis") && name.contains("send channel 1")) {
        return "Avantis send-channel test through GX4816 link. Expect Avantis-origin 0x04EE frames with 440 Hz on send channel 1. Good reference for console-to-stagebox RX.";
    }
    return "Unknown capture scenario. The analyzer will still classify EtherTypes, MAC directions, headers and tone candidates.";
}

void CaptureAnalyzerWindow::updateCaptureHint() {
    m_hint_label->setText(inferScenario(m_path_edit->text().trimmed()));
}

QString CaptureAnalyzerWindow::analyzerPath() const {
    QString exe = QDir(QCoreApplication::applicationDirPath()).filePath("GigaAceAnalyze.exe");
    if (QFileInfo::exists(exe))
        return exe;
    return "GigaAceAnalyze.exe";
}

void CaptureAnalyzerWindow::startAnalysis() {
    if (m_process->state() != QProcess::NotRunning)
        return;

    QString capture = m_path_edit->text().trimmed();
    if (capture.isEmpty() || !QFileInfo::exists(capture)) {
        QMessageBox::warning(this, "Capture Analyzer", "Please select an existing capture file.");
        return;
    }

    QString exe = analyzerPath();
    if (!QFileInfo::exists(exe) && exe.contains(QDir::separator())) {
        QMessageBox::warning(this, "Capture Analyzer", "GigaAceAnalyze.exe was not found next to the application.");
        return;
    }

    QStringList args;
    args << "--input" << capture;
    args << "--tone" << QString::number(m_tone_spin->value(), 'f', 1);
    args << "--rate" << QString::number(m_rate_spin->value(), 'f', 1);
    args << "--slots" << QString::number(m_slots_spin->value());
    args << "--max-frames" << QString::number(m_frames_spin->value());
    args << "--top" << QString::number(m_top_spin->value());
    if (m_byte_scan_check->isChecked())
        args << "--byte-scan";

    m_output->clear();
    m_output->appendPlainText("Running: " + exe + " " + args.join(' '));
    m_output->appendPlainText("");
    setRunning(true);
    m_process->start(exe, args);
    if (!m_process->waitForStarted(3000)) {
        m_output->appendPlainText("Failed to start analyzer: " + m_process->errorString());
        setRunning(false);
    }
}

void CaptureAnalyzerWindow::stopAnalysis() {
    if (m_process->state() != QProcess::NotRunning) {
        m_process->terminate();
        if (!m_process->waitForFinished(1500))
            m_process->kill();
    }
}

void CaptureAnalyzerWindow::readOutput() {
    QByteArray data = m_process->readAllStandardOutput();
    if (!data.isEmpty())
        m_output->moveCursor(QTextCursor::End);
    m_output->insertPlainText(QString::fromLocal8Bit(data));
    m_output->moveCursor(QTextCursor::End);
}

void CaptureAnalyzerWindow::processFinished(int exit_code) {
    readOutput();
    m_output->appendPlainText("");
    m_output->appendPlainText(QString("Analyzer finished with exit code %1.").arg(exit_code));
    setRunning(false);
}

void CaptureAnalyzerWindow::setRunning(bool running) {
    m_run_btn->setEnabled(!running);
    m_browse_btn->setEnabled(!running);
    m_stop_btn->setEnabled(running);
    m_status_label->setText(running ? "Running" : "Ready");
}
