#ifndef GIGAACE_ENGINE_H
#define GIGAACE_ENGINE_H

#include "frame.h"
#include "shared_bridge.h"
#include "pcm_buffer.h"
#include "demo_source.h"
#include "pcap_source.h"
#include "decoder.h"
#include "avantis_handshake.h"
#include <memory>
#include <atomic>
#include <mutex>
#include <string>

class GigaACEEngine {
public:
    explicit GigaACEEngine(const GigaACEConfig& config);
    ~GigaACEEngine();

    bool start();
    void stop();

    GigaACEStatistics snapshotStatistics() const;
    std::vector<float> latestLevels(int count, int start_channel = 0) const;
    std::vector<float> consumeInterleaved(int frame_count, const std::vector<int>& channels);
    void consumeStereo(int frame_count, int left_ch, int right_ch, float* left, float* right);
    int bufferedFrames() const;
    bool sharedBridgeReady() const;

    bool isRunning() const { return m_running.load(); }
    const GigaACEConfig& config() const { return m_config; }

    AvantisHandshakeState handshakeState() const {
        return m_handshake ? m_handshake->state() : AvantisHandshakeState::Idle;
    }

    // Returns empty string if no console has been seen yet.
    std::string consoleMacStr() const;

private:
    void handleFrame(const std::vector<uint8_t>& data);
    void appendAudioSamples(const std::vector<float>& samples);
    void updateStats(const GigaACEFrame& frame);

    GigaACEConfig m_config;
    std::unique_ptr<PCMRingBuffer> m_buffer;
    std::unique_ptr<DemoFrameSource> m_demo_source;
    std::unique_ptr<PcapFrameSource> m_pcap_source;
    std::unique_ptr<GigaACESharedRing> m_shared_ring;
    std::unique_ptr<AvantisHandshake> m_handshake;

    mutable std::mutex m_stats_lock;
    GigaACEStatistics m_stats{};
    std::atomic<bool> m_running{false};

    bool m_have_audio_counter = false;
    uint8_t m_last_audio_counter = 0;
    std::vector<float> m_last_audio_samples;
};

#endif
