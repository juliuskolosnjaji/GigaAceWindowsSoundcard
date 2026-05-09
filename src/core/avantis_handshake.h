#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

// Connection state with the Avantis console.
// Announcing: sending periodic device-presence frames, waiting for audio.
// Connected:  audio frames arriving from a known console MAC.
// Lost:       audio stopped; still announcing so the console can reconnect.
enum class AvantisHandshakeState : int {
    Idle       = 0,
    Announcing = 1,
    Connected  = 2,
    Lost       = 3
};

struct AvantisHandshakeConfig {
    std::string interface_name;
    // Observed GX4816 MAC from the reference capture.
    uint8_t  local_mac[6]         = {0x00, 0x04, 0xc4, 0x06, 0xcf, 0xe8};
    uint16_t channel_count        = 64;
    double   sample_rate          = 96000.0;
    unsigned announce_interval_ms = 1000;
    unsigned timeout_ms           = 2000;
    bool     send_announcement    = false;
};

// Tracks whether audio frames are arriving from a console. Optional announcement
// frames are disabled by default because real GX4816 captures only show the
// continuous 0x04EE/0x02 audio stream with GX MAC identity.
// Tracks whether audio frames are arriving from a console and maintains
// the Announcing→Connected→Lost state machine.
class AvantisHandshake {
public:
    // Called whenever the state changes. Second arg is the console src_mac
    // (zeroed when no console has been seen yet).
    using StateCallback = std::function<void(AvantisHandshakeState, const uint8_t*)>;

    explicit AvantisHandshake(AvantisHandshakeConfig cfg);
    ~AvantisHandshake();

    bool start(StateCallback cb = nullptr);
    void stop();

    // Call from the frame-receive path for every arriving GigaACE audio frame.
    // frame_bytes[6..11] = src_mac of the console.
    void notifyAudioFrame(const uint8_t* frame_bytes, size_t frame_len);

    AvantisHandshakeState state() const { return m_state.load(); }

    // Returns false if no console has been seen yet.
    bool consoleMac(uint8_t out[6]) const;

private:
    void   run();
    size_t buildFrame(uint8_t* buf, size_t buf_size, uint8_t counter, bool heartbeat);
    void   setState(AvantisHandshakeState s);

    AvantisHandshakeConfig              m_cfg;
    StateCallback                       m_callback;
    std::atomic<AvantisHandshakeState>  m_state{AvantisHandshakeState::Idle};
    std::atomic<bool>                   m_running{false};
    std::thread                         m_thread;

    mutable std::mutex                        m_audio_mutex;
    bool                                      m_audio_seen = false;
    uint8_t                                   m_console_mac[6] = {};
    std::chrono::steady_clock::time_point     m_last_audio_tp;
};
