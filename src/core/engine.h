#ifndef GIGAACE_ENGINE_H
#define GIGAACE_ENGINE_H

#include "frame.h"
#include "shared_bridge.h"
#include "pcm_buffer.h"
#include "demo_source.h"
#include "pcap_source.h"
#include "decoder.h"
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
    std::vector<float> latestLevels(int count) const;
    std::vector<float> consumeInterleaved(int frame_count, const std::vector<int>& channels);
    void consumeStereo(int frame_count, int left_ch, int right_ch, float* left, float* right);
    int bufferedFrames() const;
    bool sharedBridgeReady() const;

    bool isRunning() const { return m_running.load(); }
    const GigaACEConfig& config() const { return m_config; }

private:
    void handleFrame(const std::vector<uint8_t>& data);
    void updateStats(const GigaACEFrame& frame);

    GigaACEConfig m_config;
    std::unique_ptr<PCMRingBuffer> m_buffer;
    std::unique_ptr<DemoFrameSource> m_demo_source;
    std::unique_ptr<PcapFrameSource> m_pcap_source;
    std::unique_ptr<GigaACESharedRing> m_shared_ring;

    mutable std::mutex m_stats_lock;
    GigaACEStatistics m_stats{};
    std::atomic<bool> m_running{false};
};

#endif
