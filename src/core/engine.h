#ifndef GIGAACE_ENGINE_H
#define GIGAACE_ENGINE_H

#include "frame.h"
#include "shared_bridge.h"
#include "pcm_buffer.h"
#include "demo_source.h"
#include "pcap_source.h"
#include "pcap_tx.h"
#include "decoder.h"
#include "avantis_handshake.h"
#include <memory>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

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
    void startTxThread();
    void stopTxThread();
    void txLoop();
    std::vector<uint8_t> makeTxProbeFrame();
    void sendTxProbeFrame();
    void loadTxWavFile();
    float nextTxSample();
    void updateStats(const GigaACEFrame& frame);

    GigaACEConfig m_config;
    std::unique_ptr<PCMRingBuffer> m_buffer;
    std::unique_ptr<DemoFrameSource> m_demo_source;
    std::unique_ptr<PcapFrameSource> m_pcap_source;
    std::unique_ptr<PcapPacketSender> m_tx_sender;
    std::unique_ptr<GigaACESharedRing> m_shared_ring;
    std::unique_ptr<AvantisHandshake> m_handshake;

    mutable std::mutex m_stats_lock;
    GigaACEStatistics m_stats{};
    std::atomic<bool> m_running{false};

    bool m_have_audio_counter = false;
    uint8_t m_last_audio_counter = 0;
    std::vector<float> m_last_audio_samples;
    std::atomic<bool> m_tx_running{false};
    std::unique_ptr<std::thread> m_tx_thread;
    std::mutex m_tx_clock_lock;
    std::condition_variable m_tx_clock_cv;
    uint64_t m_tx_clock_frames = 0;
    uint8_t m_tx_counter = 0;
    double m_tx_tone_phase = 0.0;
    uint64_t m_tx_frames_sent = 0;
    std::vector<float> m_tx_file_samples;
    double m_tx_file_rate = 0.0;
    double m_tx_file_pos = 0.0;
};

#endif
