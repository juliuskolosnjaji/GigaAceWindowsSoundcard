#include "engine.h"
#include "ace_decoder.h"
#include <algorithm>

GigaACEEngine::GigaACEEngine(const GigaACEConfig& config)
    : m_config(config) {
    m_buffer = std::make_unique<PCMRingBuffer>(config.channels, config.ring_buffer_frames);

    m_shared_ring = std::make_unique<GigaACESharedRing>();
    gigaace_shared_ring_create(
        config.shared_memory_name,
        config.channels,
        config.shared_memory_frames,
        config.sample_rate,
        m_shared_ring.get()
    );

    if (config.capture_mode == GIGAACE_CAPTURE_MODE_DEMO)
        m_demo_source = std::make_unique<DemoFrameSource>(config.channels, config.sample_rate);
    else
        m_pcap_source = std::make_unique<PcapFrameSource>(config.interface_name, 0x04ee);
}

GigaACEEngine::~GigaACEEngine() {
    stop();
    if (m_shared_ring)
        gigaace_shared_ring_close(m_shared_ring.get());
}

bool GigaACEEngine::start() {
    if (m_running) return false;

    auto handler = [this](const std::vector<uint8_t>& data) {
        handleFrame(data);
    };

    if (m_config.capture_mode == GIGAACE_CAPTURE_MODE_DEMO) {
        if (!m_demo_source || !m_demo_source->start(handler))
            return false;
    } else {
        if (!m_pcap_source || !m_pcap_source->start(handler))
            return false;
    }

    m_running = true;
    gigaace_shared_ring_set_stream_state(m_shared_ring.get(), GIGAACE_STREAM_STATE_RUNNING);
    return true;
}

void GigaACEEngine::stop() {
    if (!m_running) return;
    m_running = false;

    if (m_demo_source)
        m_demo_source->stop();
    if (m_pcap_source)
        m_pcap_source->stop();

    gigaace_shared_ring_set_stream_state(m_shared_ring.get(), GIGAACE_STREAM_STATE_STOPPED);
}

GigaACEStatistics GigaACEEngine::snapshotStatistics() const {
    std::lock_guard<std::mutex> lock(m_stats_lock);
    return m_stats;
}

std::vector<float> GigaACEEngine::latestLevels(int count) const {
    return m_buffer->latestLevels(count);
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

void GigaACEEngine::handleFrame(const std::vector<uint8_t>& data) {
    {
        std::lock_guard<std::mutex> lock(m_stats_lock);
        m_stats.frames_received++;
    }

    GigaACEFrame frame;
    if (GigaACEDecoder::decode(data.data(), data.size(), m_config.channels, frame)) {
        std::vector<float> floats;
        floats.reserve(frame.channel_count);
        for (size_t i = 0; i < frame.channel_count; ++i)
            floats.push_back(gigaace_pcm24_to_float(frame.pcm24[i]));

        m_buffer->append(floats);

        if (m_shared_ring && m_shared_ring->layout)
            gigaace_shared_ring_write_interleaved(m_shared_ring.get(), floats.data(), 1);

        updateStats(frame);
    } else {
        std::lock_guard<std::mutex> lock(m_stats_lock);
        m_stats.frames_rejected++;
    }
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
