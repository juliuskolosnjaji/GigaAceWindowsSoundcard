#include "engine.h"
#include "ace_decoder.h"
#include <QDebug>
#include <QString>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <thread>
#include <windows.h>

static const uint8_t kGx4816TxMac[6] = {0x00, 0x04, 0xc4, 0x06, 0xcf, 0xe8};
static const uint8_t kGigaAceCardTxMac[6] = {0x00, 0x04, 0xc4, 0x09, 0xba, 0xc4};

GigaACEEngine::GigaACEEngine(const GigaACEConfig& config)
    : m_config(config) {
    qInfo() << "[Engine] Initializing:"
            << "mode=" << (config.capture_mode == GIGAACE_CAPTURE_MODE_DEMO ? "demo" : "pcap")
            << "channels=" << config.channels
            << "rate=" << config.sample_rate
            << "ring=" << config.ring_buffer_frames << "frames"
            << "shm=" << config.shared_memory_name;

    m_buffer = std::make_unique<PCMRingBuffer>(config.channels, config.ring_buffer_frames);

    m_shared_ring = std::make_unique<GigaACESharedRing>();
    int shm_rc = gigaace_shared_ring_create(
        config.shared_memory_name,
        config.channels,
        config.shared_memory_frames,
        config.sample_rate,
        m_shared_ring.get()
    );
    if (shm_rc == 0)
        qInfo() << "[Engine] Shared memory ring created:" << config.shared_memory_name;
    else
        qWarning() << "[Engine] Failed to create shared memory ring:" << config.shared_memory_name;

    if (config.capture_mode == GIGAACE_CAPTURE_MODE_DEMO) {
        m_demo_source = std::make_unique<DemoFrameSource>(config.channels, config.sample_rate);
        qInfo() << "[Engine] Demo source ready";
    } else {
        qInfo() << "[Engine] Pcap source interface:" << config.interface_name;
        m_pcap_source = std::make_unique<PcapFrameSource>(config.interface_name, 0x04ee);
        if (config.tx_probe_enabled || config.tx_stagebox_advertise_enabled) {
            m_tx_sender = std::make_unique<PcapPacketSender>(config.interface_name);
            loadTxWavFile();
        }

        AvantisHandshakeConfig hcfg;
        hcfg.interface_name = config.interface_name;
        hcfg.channel_count  = (uint16_t)config.channels;
        hcfg.sample_rate    = config.sample_rate;
        m_handshake = std::make_unique<AvantisHandshake>(hcfg);
    }
}

GigaACEEngine::~GigaACEEngine() {
    stop();
    if (m_shared_ring)
        gigaace_shared_ring_close(m_shared_ring.get());
}

bool GigaACEEngine::start() {
    if (m_running) return false;

    qInfo() << "[Engine] Starting...";
    auto handler = [this](const std::vector<uint8_t>& data) {
        handleFrame(data);
    };

    if (m_config.capture_mode == GIGAACE_CAPTURE_MODE_DEMO) {
        if (!m_demo_source || !m_demo_source->start(handler)) {
            qCritical() << "[Engine] Failed to start demo source";
            return false;
        }
        qInfo() << "[Engine] Demo source started";
    } else {
        if (!m_pcap_source || !m_pcap_source->start(handler)) {
            qCritical() << "[Engine] Failed to start pcap source:" << m_pcap_source->lastError().c_str();
            return false;
        }
        qInfo() << "[Engine] Pcap capture started";
        if (m_handshake)
            m_handshake->start();
        if (m_tx_sender) {
            if (m_tx_sender->open()) {
                qInfo() << "[Engine] TX probe sender ready";
                startTxThread();
            } else {
                qWarning() << "[Engine] TX probe sender failed:" << m_tx_sender->lastError().c_str();
            }
        }
    }

    m_running = true;
    gigaace_shared_ring_set_stream_state(m_shared_ring.get(), GIGAACE_STREAM_STATE_RUNNING);
    qInfo() << "[Engine] Running";
    return true;
}

void GigaACEEngine::stop() {
    if (!m_running) return;
    qInfo() << "[Engine] Stopping...";
    m_running = false;

    if (m_demo_source)
        m_demo_source->stop();
    if (m_pcap_source)
        m_pcap_source->stop();
    if (m_handshake)
        m_handshake->stop();
    stopTxThread();
    if (m_tx_sender)
        m_tx_sender->close();

    gigaace_shared_ring_set_stream_state(m_shared_ring.get(), GIGAACE_STREAM_STATE_STOPPED);

    auto stats = snapshotStatistics();
    qInfo() << "[Engine] Stopped. frames_rx=" << stats.frames_received
            << "frames_ok=" << stats.frames_decoded
            << "rejected=" << stats.frames_rejected
            << "counter_drops=" << stats.counter_drops;
}

GigaACEStatistics GigaACEEngine::snapshotStatistics() const {
    std::lock_guard<std::mutex> lock(m_stats_lock);
    return m_stats;
}

std::vector<float> GigaACEEngine::latestLevels(int count, int start_channel) const {
    return m_buffer->latestLevels(count, start_channel);
}

std::vector<float> GigaACEEngine::consumeInterleaved(int frame_count, const std::vector<int>& channels) {
    return m_buffer->consume(channels, frame_count);
}

void GigaACEEngine::consumeStereo(int frame_count, int left_ch, int right_ch, float* left, float* right) {
    m_buffer->consumeStereo(frame_count, left_ch, right_ch, left, right);
}

int GigaACEEngine::bufferedFrames() const {
    return m_buffer->bufferedFrameCount();
}

bool GigaACEEngine::sharedBridgeReady() const {
    return m_shared_ring && m_shared_ring->layout && m_shared_ring->samples;
}

std::string GigaACEEngine::consoleMacStr() const {
    if (!m_handshake) return {};
    uint8_t mac[6] = {};
    if (!m_handshake->consoleMac(mac)) return {};
    char buf[20];
    std::snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return buf;
}

static void encodeTxSample24(float sample, GigaACETxEncoding encoding, uint8_t packed[3]);
static size_t txSlotForChannel(int channel, GigaACETxLayout layout);

void GigaACEEngine::handleFrame(const std::vector<uint8_t>& data) {
    if (m_config.tx_probe_enabled && data.size() >= 12) {
        if (std::memcmp(data.data() + 6, kGx4816TxMac, sizeof(kGx4816TxMac)) == 0 ||
            std::memcmp(data.data() + 6, kGigaAceCardTxMac, sizeof(kGigaAceCardTxMac)) == 0) {
            return;
        }
    }

    uint64_t rx;
    {
        std::lock_guard<std::mutex> lock(m_stats_lock);
        rx = ++m_stats.frames_received;
        m_stats.last_frame_size = data.size();
        if (m_stats.min_frame_size == 0 || data.size() < m_stats.min_frame_size)
            m_stats.min_frame_size = data.size();
        if (data.size() > m_stats.max_frame_size)
            m_stats.max_frame_size = data.size();
    }
    if (rx == 1)
        qInfo() << "[Engine] First frame received, size=" << (int)data.size() << "bytes";

    // Notify handshake of every arriving frame so it can track the console MAC
    // and update its Connected/Lost state.
    if (m_handshake && data.size() >= 12)
        m_handshake->notifyAudioFrame(data.data(), data.size());

    GigaACEFrame frame;
    if (GigaACEDecoder::decode(data.data(), data.size(), m_config.channels, frame)) {
        if (frame.stream_type != 0x02 || frame.channel_count == 0) {
            uint64_t rej;
            {
                std::lock_guard<std::mutex> lock(m_stats_lock);
                rej = ++m_stats.frames_rejected;
                m_stats.frames_rejected_non_audio++;
            }
            if (rej == 1 || rej % 1000 == 0)
                qInfo() << "[Engine] Ignored non-audio GigaACE frame, total ignored="
                        << rej << "stream_type=0x"
                        << QString::number(frame.stream_type, 16).toUpper();
            return;
        }

        // Size to m_config.channels so gigaace_shared_ring_write_interleaved never overreads.
        std::vector<float> floats(m_config.channels, 0.0f);
        for (size_t i = 0; i < frame.channel_count; ++i)
            floats[i] = gigaace_pcm24_to_float(frame.pcm24[i]);

        if (m_have_audio_counter) {
            uint8_t delta = (frame.counter - m_last_audio_counter) & 0x1f;
            if (delta > 1 && delta < 16 && !m_last_audio_samples.empty()) {
                uint8_t missing = delta - 1;
                for (uint8_t n = 1; n <= missing; ++n) {
                    float t = static_cast<float>(n) / static_cast<float>(missing + 1);
                    std::vector<float> concealed(m_config.channels, 0.0f);
                    for (int ch = 0; ch < m_config.channels; ++ch) {
                        float prev = (ch < (int)m_last_audio_samples.size()) ? m_last_audio_samples[ch] : 0.0f;
                        concealed[ch] = prev + (floats[ch] - prev) * t;
                    }
                    appendAudioSamples(concealed);
                }
                {
                    std::lock_guard<std::mutex> lock(m_stats_lock);
                    m_stats.concealed_frames += missing;
                }
            } else if (delta == 0) {
                std::lock_guard<std::mutex> lock(m_stats_lock);
                m_stats.duplicate_counters++;
                m_stats.last_counter_delta = 0;
                m_stats.has_last_counter_delta = 1;
                return;
            }
        }

        appendAudioSamples(floats);
        if (m_tx_sender && m_tx_sender->isOpen()) {
            {
                std::lock_guard<std::mutex> tx_lock(m_tx_clock_lock);
                ++m_tx_clock_frames;
            }
            m_tx_clock_cv.notify_one();
        }

        m_last_audio_samples = floats;
        m_last_audio_counter = frame.counter;
        m_have_audio_counter = true;

        if (rx % 96000 == 0) {
            auto s = snapshotStatistics();
            qInfo() << "[Engine] frames_rx=" << s.frames_received
                    << "frames_ok=" << s.frames_decoded
                    << "drops=" << s.counter_drops
                    << "channels=" << s.active_channels;
        }
        updateStats(frame);
    } else {
        uint64_t rej;
        {
            std::lock_guard<std::mutex> lock(m_stats_lock);
            rej = ++m_stats.frames_rejected;
            m_stats.frames_rejected_decode++;
        }
        if (rej == 1 || rej % 1000 == 0)
            qWarning() << "[Engine] Decode failed, total rejected=" << rej << "frame_size=" << (int)data.size();
    }
}

void GigaACEEngine::startTxThread() {
    if (!m_tx_sender || !m_tx_sender->isOpen() || m_tx_running.exchange(true))
        return;

    {
        std::lock_guard<std::mutex> lock(m_tx_clock_lock);
        m_tx_clock_frames = 0;
    }
    m_tx_thread = std::make_unique<std::thread>(&GigaACEEngine::txLoop, this);
}

void GigaACEEngine::stopTxThread() {
    m_tx_running = false;
    m_tx_clock_cv.notify_all();
    if (m_tx_thread && m_tx_thread->joinable())
        m_tx_thread->join();
    m_tx_thread.reset();
}

void GigaACEEngine::txLoop() {
  try {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
    qInfo() << "[Engine] TX stream thread started (RX-clocked)"
            << "packet_format=" << m_config.tx_probe_packet_format;
    if (m_config.tx_stagebox_advertise_enabled && m_config.tx_probe_source == GIGAACE_TX_SOURCE_SILENCE)
        qInfo() << "[Engine] Stagebox advertise enabled: sending silent GX4816 stream";

    // GX4816/SLink sends roughly one stagebox TX frame per incoming console frame.
    // GigaACE card mode sends 0x00E1 at 96 k packets/s while the console side is
    // roughly 48 k packets/s. The real card starts this stream before a console
    // stream is visible, so it must free-run instead of waiting for RX clock.
    static constexpr size_t kMaxBatch  = 192;   // max frames per sendQueue call
    static constexpr size_t kCatchupCap = 768;  // never send more than this per wakeup
    const bool gigaace_card_mode = m_config.tx_probe_packet_format == GIGAACE_TX_PACKET_GIGAACE_CARD;
    const bool free_run_tx = gigaace_card_mode || m_config.tx_stagebox_advertise_enabled;
    const uint64_t tx_frames_per_rx = gigaace_card_mode ? 2 : 1;
    const double rate = gigaace_card_mode ? 96000.0 : 48000.0;

    uint64_t tx_sent = 0;  // local shadow of how many we have sent

    if (free_run_tx) {
        while (m_tx_running.load()) {
            std::vector<std::vector<uint8_t>> batch;
            batch.reserve(kMaxBatch);
            for (size_t i = 0; i < kMaxBatch && m_tx_running.load(); ++i)
                batch.push_back(makeTxProbeFrame());

            if (batch.empty())
                break;

            if (m_tx_sender->sendQueue(batch, rate)) {
                tx_sent += batch.size();
                m_tx_frames_sent += batch.size();
            } else {
                qWarning() << "[Engine] TX send queue failed:" << m_tx_sender->lastError().c_str();
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }

            if (tx_sent > 0 && tx_sent % 96000 < kMaxBatch)
                qInfo() << "[Engine] TX frames sent=" << (qulonglong)tx_sent;
        }

        qInfo() << "[Engine] TX stream thread stopped, sent=" << (qulonglong)tx_sent;
        return;
    }

    while (m_tx_running.load()) {
        // Wait until new RX frames arrive (or stop/timeout).
        uint64_t target;
        {
            std::unique_lock<std::mutex> lock(m_tx_clock_lock);
            m_tx_clock_cv.wait_for(lock, std::chrono::milliseconds(200), [&] {
                return !m_tx_running.load() || (m_tx_clock_frames * tx_frames_per_rx) > tx_sent;
            });
            target = m_tx_clock_frames * tx_frames_per_rx;
        }

        if (!m_tx_running.load())
            break;

        if (target <= tx_sent)
            continue; // just a timeout, no new RX frames yet

        // Cap catchup so we never flood the wire if we fall behind.
        uint64_t to_send = target - tx_sent;
        if (to_send > kCatchupCap) {
            qWarning() << "[Engine] TX falling behind by" << (qulonglong)to_send
                       << "frames, capping to" << (qulonglong)kCatchupCap;
            to_send = kCatchupCap;
            // Skip the gap so we don't keep catching up forever.
            tx_sent = target - kCatchupCap;
        }

        // Send in batches for efficient pcap sendqueue scheduling.
        while (to_send > 0 && m_tx_running.load()) {
            size_t batch_size = (to_send > kMaxBatch) ? kMaxBatch : (size_t)to_send;
            std::vector<std::vector<uint8_t>> batch;
            batch.reserve(batch_size);
            for (size_t i = 0; i < batch_size; ++i)
                batch.push_back(makeTxProbeFrame());

            if (m_tx_sender->sendQueue(batch, rate)) {
                tx_sent += batch_size;
                m_tx_frames_sent += batch_size;
                to_send -= batch_size;
            } else {
                qWarning() << "[Engine] TX send queue failed:" << m_tx_sender->lastError().c_str();
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                break;
            }
        }

        if (tx_sent > 0 && tx_sent % 96000 < kMaxBatch)
            qInfo() << "[Engine] TX frames sent=" << (qulonglong)tx_sent;
    }

    qInfo() << "[Engine] TX stream thread stopped, sent=" << (qulonglong)tx_sent;
  } catch (const std::exception& e) {
    m_tx_running = false;
    qCritical() << "[Engine] TX thread exception:" << e.what();
  } catch (...) {
    m_tx_running = false;
    qCritical() << "[Engine] TX thread unknown exception";
  }
}

std::vector<uint8_t> GigaACEEngine::makeTxProbeFrame() {
    static constexpr size_t kGx4816FrameLen = 1276;
    const int channels = std::clamp(m_config.channels, 1, GIGAACE_MAX_CHANNELS);
    std::vector<uint8_t> packet(kGx4816FrameLen, 0);

    static const uint8_t broadcast_mac[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    std::memcpy(packet.data(), broadcast_mac, 6);
    const bool gigaace_card_mode = m_config.tx_probe_packet_format == GIGAACE_TX_PACKET_GIGAACE_CARD;
    if (gigaace_card_mode) {
        std::memcpy(packet.data() + 6, kGigaAceCardTxMac, 6);
        packet[12] = 0x00;
        packet[13] = 0xe1;
        packet[14] = 0x00;
        packet[15] = 0x00;
        packet[16] = 0x00;
        packet[17] = 0x00;
        packet[18] = 0x00;
    } else {
        std::memcpy(packet.data() + 6, kGx4816TxMac, 6);
        packet[12] = 0x04;
        packet[13] = 0xee;
        packet[14] = 0x00;
        packet[15] = 0x0a;
        packet[16] = 0x04;
        packet[17] = 0xea;
        packet[18] = 0x00;
    }
    uint8_t counter = m_tx_counter++ & 0x1f;
    packet[GIGAACE_COUNTER_OFFSET] = counter;
    packet[GIGAACE_STREAM_TYPE_OFFSET] = gigaace_card_mode ? 0x01 : 0x02;

    size_t target_slot = txSlotForChannel(0, m_config.tx_probe_layout);
    if (m_config.tx_probe_source != GIGAACE_TX_SOURCE_SILENCE) {
        int ch = std::clamp(m_config.tx_probe_channel, 0, channels - 1);
        target_slot = txSlotForChannel(ch, m_config.tx_probe_layout);
    }

    size_t slot_count = std::max((size_t)channels, target_slot + 1);
    if (m_config.tx_probe_layout == GIGAACE_TX_LAYOUT_BANKED_8_WITH_SYNC)
        slot_count = std::max(slot_count, (size_t)channels + 1);
    else if (m_config.tx_probe_layout == GIGAACE_TX_LAYOUT_GX4816_LINEAR_48)
        slot_count = 48;
    else if (m_config.tx_probe_layout == GIGAACE_TX_LAYOUT_GIGAACE_CARD_PAIRED)
        slot_count = std::max(slot_count, target_slot + 3);
    size_t max_slots = (packet.size() > GIGAACE_AUDIO_BASE_OFFSET)
        ? (packet.size() - GIGAACE_AUDIO_BASE_OFFSET) / GIGAACE_BYTES_PER_SAMPLE
        : 0;
    slot_count = std::min(slot_count, max_slots);

    if (m_config.tx_probe_layout == GIGAACE_TX_LAYOUT_BANKED_8_WITH_SYNC && slot_count > 0) {
        uint8_t sync = (uint8_t)(0x40 + ((counter & 0x0f) * 4));
        size_t off = GIGAACE_AUDIO_BASE_OFFSET;
        packet[off] = 0x00;
        packet[off + 1] = 0x00;
        packet[off + 2] = sync;
    }

    const bool paired_card_layout = m_config.tx_probe_layout == GIGAACE_TX_LAYOUT_GIGAACE_CARD_PAIRED;
    const size_t mirror_slot = paired_card_layout ? target_slot + 2 : target_slot;
    float active_sample = 0.0f;
    if (m_config.tx_probe_source != GIGAACE_TX_SOURCE_SILENCE) {
        float gain = std::clamp(m_config.tx_probe_gain, 0.0f, 0.25f);
        active_sample = gain * nextTxSample();
    }

    for (size_t slot = 0; slot < slot_count; ++slot) {
        if (m_config.tx_probe_layout == GIGAACE_TX_LAYOUT_BANKED_8_WITH_SYNC && slot == 0)
            continue;
        float sample = 0.0f;
        if (m_config.tx_probe_source != GIGAACE_TX_SOURCE_SILENCE) {
            if (slot == target_slot || (paired_card_layout && slot == mirror_slot))
                sample = active_sample;
        }

        uint8_t packed[3];
        encodeTxSample24(sample, m_config.tx_probe_encoding, packed);
        size_t off = GIGAACE_AUDIO_BASE_OFFSET + slot * GIGAACE_BYTES_PER_SAMPLE;
        if (off + 2 >= packet.size())
            break;
        packet[off] = packed[0];
        packet[off + 1] = packed[1];
        packet[off + 2] = packed[2];
    }

    return packet;
}

void GigaACEEngine::sendTxProbeFrame() {
    if (!m_tx_sender)
        return;

    std::vector<uint8_t> packet = makeTxProbeFrame();
    if (m_tx_sender->send(packet.data(), packet.size())) {
        if (++m_tx_frames_sent == 1)
            qInfo() << "[Engine] First TX probe frame sent, len=" << (int)packet.size();
    } else if (m_tx_frames_sent == 0) {
        qWarning() << "[Engine] TX probe send failed:" << m_tx_sender->lastError().c_str();
    }
}

static uint16_t rd16(const uint8_t* p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t rd32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint32_t swizzle24(uint32_t sample) {
    sample &= 0x00ffffffu;
    uint32_t swapped_nibbles = ((sample & 0xf0f0f0u) >> 4) |
                               ((sample & 0x0f0f0fu) << 4);
    return (((swapped_nibbles & 0xff0000u) >> 16) |
            ( swapped_nibbles & 0x00ff00u) |
            ((swapped_nibbles & 0x0000ffu) << 16)) & 0x00ffffffu;
}

static size_t txSlotForChannel(int channel, GigaACETxLayout layout) {
    channel = std::max(0, channel);
    if (layout == GIGAACE_TX_LAYOUT_GX4816_LINEAR_48)
        return (size_t)std::min(channel, 47);
    if (layout == GIGAACE_TX_LAYOUT_GIGAACE_CARD_PAIRED)
        return (size_t)channel;
    if (layout == GIGAACE_TX_LAYOUT_RAW_SLOT)
        return (size_t)channel;
    if (layout == GIGAACE_TX_LAYOUT_BANKED_8_WITH_SYNC) {
        size_t bank = (size_t)(channel % 8);
        size_t index_in_bank = (size_t)(channel / 8);
        return 1 + bank * 8 + index_in_bank;
    }
    return (size_t)channel;
}

static void encodeTxSample24(float sample, GigaACETxEncoding encoding, uint8_t packed[3]) {
    uint32_t raw = (uint32_t)gigaace_float_to_pcm24(sample) & 0x00ffffffu;
    uint32_t wire = raw;
    switch (encoding) {
    case GIGAACE_TX_ENCODING_LE_RAW:
        wire = ((raw & 0xff0000u) >> 16) |
               ( raw & 0x00ff00u) |
               ((raw & 0x0000ffu) << 16);
        break;
    case GIGAACE_TX_ENCODING_NIBBLE:
        wire = ((raw & 0xf0f0f0u) >> 4) |
               ((raw & 0x0f0f0fu) << 4);
        break;
    case GIGAACE_TX_ENCODING_ACE_SWIZZLE:
    case GIGAACE_TX_ENCODING_NIBBLE_BYTE:
        wire = swizzle24(raw);
        break;
    case GIGAACE_TX_ENCODING_BE_RAW:
    default:
        wire = raw;
        break;
    }
    packed[0] = (uint8_t)(wire >> 16);
    packed[1] = (uint8_t)(wire >> 8);
    packed[2] = (uint8_t)wire;
}

void GigaACEEngine::loadTxWavFile() {
    m_tx_file_samples.clear();
    m_tx_file_rate = 0.0;
    m_tx_file_pos = 0.0;

    if (m_config.tx_probe_source != GIGAACE_TX_SOURCE_WAV || !m_config.tx_probe_file_path[0])
        return;

    std::ifstream in(m_config.tx_probe_file_path, std::ios::binary);
    if (!in) {
        qWarning() << "[Engine] TX WAV open failed:" << m_config.tx_probe_file_path;
        return;
    }

    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (bytes.size() < 44 || std::memcmp(bytes.data(), "RIFF", 4) != 0 || std::memcmp(bytes.data() + 8, "WAVE", 4) != 0) {
        qWarning() << "[Engine] TX WAV invalid RIFF/WAVE file";
        return;
    }

    size_t pos = 12;
    uint16_t format = 0;
    uint16_t channels = 0;
    uint32_t sample_rate = 0;
    uint16_t bits = 0;
    const uint8_t* data = nullptr;
    uint32_t data_size = 0;

    while (pos + 8 <= bytes.size()) {
        const uint8_t* chunk = bytes.data() + pos;
        uint32_t size = rd32(chunk + 4);
        size_t next = pos + 8 + size + (size & 1);
        if (next > bytes.size() + 1)
            break;

        if (std::memcmp(chunk, "fmt ", 4) == 0 && size >= 16) {
            format = rd16(chunk + 8);
            channels = rd16(chunk + 10);
            sample_rate = rd32(chunk + 12);
            bits = rd16(chunk + 22);
        } else if (std::memcmp(chunk, "data", 4) == 0) {
            data = chunk + 8;
            data_size = size;
        }
        pos = next;
    }

    if (!data || !channels || !sample_rate || !(format == 1 || format == 3) || !(bits == 16 || bits == 24 || bits == 32)) {
        qWarning() << "[Engine] TX WAV unsupported. Use PCM 16/24/32-bit or float32 WAV.";
        return;
    }

    size_t bytes_per_sample = bits / 8;
    size_t frame_bytes = bytes_per_sample * channels;
    size_t frames = data_size / frame_bytes;
    m_tx_file_samples.reserve(frames);

    for (size_t i = 0; i < frames; ++i) {
        const uint8_t* p = data + i * frame_bytes;
        double acc = 0.0;
        for (uint16_t ch = 0; ch < channels; ++ch) {
            const uint8_t* s = p + ch * bytes_per_sample;
            float v = 0.0f;
            if (format == 3 && bits == 32) {
                std::memcpy(&v, s, sizeof(float));
            } else if (bits == 16) {
                int16_t x = (int16_t)rd16(s);
                v = (float)x / 32768.0f;
            } else if (bits == 24) {
                int32_t x = (int32_t)s[0] | ((int32_t)s[1] << 8) | ((int32_t)s[2] << 16);
                if (x & 0x800000)
                    x |= ~0xFFFFFF;
                v = (float)x / 8388608.0f;
            } else if (bits == 32) {
                int32_t x = (int32_t)rd32(s);
                v = (float)((double)x / 2147483648.0);
            }
            acc += v;
        }
        m_tx_file_samples.push_back((float)(acc / channels));
    }

    m_tx_file_rate = (double)sample_rate;
    qInfo() << "[Engine] TX WAV loaded:" << m_config.tx_probe_file_path
            << "frames=" << (int)m_tx_file_samples.size()
            << "rate=" << sample_rate
            << "channels=" << channels
            << "bits=" << bits;
}

float GigaACEEngine::nextTxSample() {
    if (m_config.tx_probe_source == GIGAACE_TX_SOURCE_WAV) {
        if (m_tx_file_samples.empty())
            return 0.0f;
        size_t idx = std::min((size_t)m_tx_file_pos, m_tx_file_samples.size() - 1);
        float value = m_tx_file_samples[idx];
        double step = (m_tx_file_rate > 1.0) ? (m_tx_file_rate / m_config.sample_rate) : 1.0;
        m_tx_file_pos += step;
        if (m_tx_file_pos >= (double)m_tx_file_samples.size()) {
            if (m_config.tx_probe_file_loop)
                m_tx_file_pos = std::fmod(m_tx_file_pos, (double)m_tx_file_samples.size());
            else
                m_tx_file_pos = (double)m_tx_file_samples.size() - 1.0;
        }
        return value;
    }

    float value = (float)std::sin(m_tx_tone_phase);
    m_tx_tone_phase += 2.0 * 3.14159265358979323846 * m_config.tx_probe_frequency / m_config.sample_rate;
    if (m_tx_tone_phase >= 2.0 * 3.14159265358979323846)
        m_tx_tone_phase = std::fmod(m_tx_tone_phase, 2.0 * 3.14159265358979323846);
    return value;
}

void GigaACEEngine::appendAudioSamples(const std::vector<float>& samples) {
    m_buffer->append(samples);

    if (m_shared_ring && m_shared_ring->layout)
        gigaace_shared_ring_write_interleaved(m_shared_ring.get(), samples.data(), 1);
}

void GigaACEEngine::updateStats(const GigaACEFrame& frame) {
    std::lock_guard<std::mutex> lock(m_stats_lock);

    m_stats.frames_decoded++;
    m_stats.active_channels = (int)frame.channel_count;
    m_stats.stream_type = frame.stream_type;

    if (m_stats.has_last_counter) {
        uint8_t delta = (frame.counter - m_stats.last_counter) & 0x1f;
        m_stats.last_counter_delta = delta;
        m_stats.has_last_counter_delta = 1;
        if (delta != 1) {
            if (delta > 1 && delta < 16)
                m_stats.counter_drops += delta - 1;
            else
                m_stats.counter_drops++;
        }
    } else {
        m_stats.last_counter_delta = 0;
        m_stats.has_last_counter_delta = 0;
    }

    m_stats.last_counter = frame.counter;
    m_stats.has_last_counter = 1;
}
