#include "engine.h"
#include "ace_decoder.h"
#include <QDebug>
#include <QString>
#include <algorithm>
#include <cstdio>

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

void GigaACEEngine::handleFrame(const std::vector<uint8_t>& data) {
    uint64_t rx;
    {
        std::lock_guard<std::mutex> lock(m_stats_lock);
        rx = ++m_stats.frames_received;
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
            } else if (delta == 0) {
                return;
            }
        }

        appendAudioSamples(floats);
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
        }
        if (rej == 1 || rej % 1000 == 0)
            qWarning() << "[Engine] Decode failed, total rejected=" << rej << "frame_size=" << (int)data.size();
    }
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
        uint8_t expected = (m_stats.last_counter + 1) & 0x1f;
        if (expected != frame.counter)
            m_stats.counter_drops++;
    }

    m_stats.last_counter = frame.counter;
    m_stats.has_last_counter = 1;
}
