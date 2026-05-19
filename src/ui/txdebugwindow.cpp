#include "txdebugwindow.h"

#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QVBoxLayout>

TxDebugWindow::TxDebugWindow(QWidget* parent)
    : QDialog(parent) {
    setWindowTitle("GigaACE TX Debug");
    resize(560, 360);
    setMinimumSize(500, 320);
    setModal(false);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(14, 14, 14, 14);
    root->setSpacing(10);

    auto* form = new QFormLayout();
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    form->setHorizontalSpacing(18);
    form->setVerticalSpacing(8);
    root->addLayout(form);

    m_enabled_check = new QCheckBox("Enable send to console");
    form->addRow("TX", m_enabled_check);

    m_source_combo = new QComboBox();
    m_source_combo->addItem("Silence", 0);
    m_source_combo->addItem("Tone generator", 1);
    m_source_combo->addItem("WAV file", 2);
    form->addRow("Source", m_source_combo);

    m_channel_spin = new QSpinBox();
    m_channel_spin->setRange(1, 128);
    m_channel_spin->setValue(1);
    form->addRow("Target channel", m_channel_spin);

    m_gain_spin = new QDoubleSpinBox();
    m_gain_spin->setRange(0.0, 0.25);
    m_gain_spin->setDecimals(3);
    m_gain_spin->setSingleStep(0.005);
    m_gain_spin->setValue(0.03);
    form->addRow("Gain", m_gain_spin);

    m_frequency_spin = new QDoubleSpinBox();
    m_frequency_spin->setRange(20.0, 20000.0);
    m_frequency_spin->setDecimals(1);
    m_frequency_spin->setSingleStep(10.0);
    m_frequency_spin->setValue(440.0);
    m_frequency_spin->setSuffix(" Hz");
    form->addRow("Tone frequency", m_frequency_spin);

    m_packet_format_combo = new QComboBox();
    m_packet_format_combo->addItem("GigaACE card / ME-U port (0x00E1)", 1);
    m_packet_format_combo->addItem("GX4816 / SLink stagebox (0x04EE)", 0);
    form->addRow("Packet format", m_packet_format_combo);

    m_layout_combo = new QComboBox();
    m_layout_combo->addItem("GigaACE card paired slots", 4);
    m_layout_combo->addItem("Linear slots", 0);
    m_layout_combo->addItem("GX4816 linear 48", 3);
    m_layout_combo->addItem("ACE banked 8 + sync", 1);
    m_layout_combo->addItem("Raw 3-byte slot", 2);
    form->addRow("Channel layout", m_layout_combo);

    m_encoding_combo = new QComboBox();
    m_encoding_combo->addItem("ACE swizzle (byte + nibble)", 3);
    m_encoding_combo->addItem("Big-endian raw", 0);
    m_encoding_combo->addItem("Little-endian raw", 1);
    m_encoding_combo->addItem("Nibble swap only", 2);
    m_encoding_combo->addItem("Nibble then byte", 4);
    form->addRow("Wire encoding", m_encoding_combo);

    auto* file_row = new QWidget();
    auto* file_layout = new QHBoxLayout(file_row);
    file_layout->setContentsMargins(0, 0, 0, 0);
    file_layout->setSpacing(6);
    m_file_edit = new QLineEdit();
    m_file_edit->setPlaceholderText("PCM WAV file");
    m_browse_btn = new QPushButton("Browse...");
    file_layout->addWidget(m_file_edit, 1);
    file_layout->addWidget(m_browse_btn);
    form->addRow("WAV file", file_row);

    m_loop_check = new QCheckBox("Loop WAV file");
    m_loop_check->setChecked(true);
    form->addRow("", m_loop_check);

    m_note_label = new QLabel("Changes are used on the next Start/Restart Audio. GigaACE card mode matches the 0x00E1 capture: Big-endian 24-bit, 96k TX packets, channel 1 mirrored to slots 1 and 3.");
    m_note_label->setWordWrap(true);
    m_note_label->setStyleSheet("color: #4b5563; font-size: 11px;");
    root->addWidget(m_note_label);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::hide);
    root->addWidget(buttons);

    connect(m_enabled_check, &QCheckBox::toggled, this, &TxDebugWindow::emitSettingsChanged);
    connect(m_source_combo, &QComboBox::currentIndexChanged, this, [this]() {
        updateControlState();
        emitSettingsChanged();
    });
    connect(m_channel_spin, &QSpinBox::valueChanged, this, [this](int) { emitSettingsChanged(); });
    connect(m_gain_spin, &QDoubleSpinBox::valueChanged, this, [this](double) { emitSettingsChanged(); });
    connect(m_frequency_spin, &QDoubleSpinBox::valueChanged, this, [this](double) { emitSettingsChanged(); });
    connect(m_packet_format_combo, &QComboBox::currentIndexChanged, this, [this]() { emitSettingsChanged(); });
    connect(m_layout_combo, &QComboBox::currentIndexChanged, this, [this]() { emitSettingsChanged(); });
    connect(m_encoding_combo, &QComboBox::currentIndexChanged, this, [this]() { emitSettingsChanged(); });
    connect(m_file_edit, &QLineEdit::textChanged, this, &TxDebugWindow::emitSettingsChanged);
    connect(m_loop_check, &QCheckBox::toggled, this, &TxDebugWindow::emitSettingsChanged);
    connect(m_browse_btn, &QPushButton::clicked, this, &TxDebugWindow::browseFile);

    updateControlState();
}

TxDebugSettings TxDebugWindow::settings() const {
    TxDebugSettings out;
    out.enabled = m_enabled_check->isChecked();
    out.source = m_source_combo->currentData().toInt();
    out.channel = m_channel_spin->value() - 1;
    out.gain = m_gain_spin->value();
    out.frequency = m_frequency_spin->value();
    out.file_path = m_file_edit->text();
    out.loop_file = m_loop_check->isChecked();
    out.encoding = m_encoding_combo->currentData().toInt();
    out.layout = m_layout_combo->currentData().toInt();
    out.packet_format = m_packet_format_combo->currentData().toInt();
    return out;
}

void TxDebugWindow::setSettings(const TxDebugSettings& settings) {
    bool blocked = blockSignals(true);
    m_enabled_check->setChecked(settings.enabled);
    int index = m_source_combo->findData(settings.source);
    m_source_combo->setCurrentIndex(index >= 0 ? index : 0);
    m_channel_spin->setValue(settings.channel + 1);
    m_gain_spin->setValue(settings.gain);
    m_frequency_spin->setValue(settings.frequency);
    int packet_index = m_packet_format_combo->findData(settings.packet_format);
    m_packet_format_combo->setCurrentIndex(packet_index >= 0 ? packet_index : 0);
    int layout_index = m_layout_combo->findData(settings.layout);
    m_layout_combo->setCurrentIndex(layout_index >= 0 ? layout_index : 0);
    int encoding_index = m_encoding_combo->findData(settings.encoding);
    m_encoding_combo->setCurrentIndex(encoding_index >= 0 ? encoding_index : 0);
    m_file_edit->setText(settings.file_path);
    m_loop_check->setChecked(settings.loop_file);
    blockSignals(blocked);
    updateControlState();
}

void TxDebugWindow::emitSettingsChanged() {
    emit settingsChanged(settings());
}

void TxDebugWindow::browseFile() {
    QString path = QFileDialog::getOpenFileName(this, "Select WAV File", QString(), "WAV files (*.wav)");
    if (!path.isEmpty())
        m_file_edit->setText(path);
}

void TxDebugWindow::updateControlState() {
    int source = m_source_combo->currentData().toInt();
    bool tone = source == 1;
    bool wav = source == 2;
    m_frequency_spin->setEnabled(tone);
    m_file_edit->setEnabled(wav);
    m_browse_btn->setEnabled(wav);
    m_loop_check->setEnabled(wav);
}
