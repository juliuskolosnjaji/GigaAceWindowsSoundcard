#include "demo_source.h"
#include "decoder.h"
#include "frame.h"
#include <cmath>

DemoFrameSource::DemoFrameSource(int channels, double sample_rate)
    : m_channels(channels), m_sample_rate(sample_rate) {}

DemoFrameSource::~DemoFrameSource() {
    stop();
}

bool DemoFrameSource::start(FrameHandler handler) {
    if (m_running) return false;
    m_handler = std::move(handler);
    m_running = true;
    m_thread = std::make_unique<std::thread>(&DemoFrameSource::run, this);
    return true;
}

void DemoFrameSource::stop() {
    if (!m_running) return;
    m_running = false;
    if (m_thread && m_thread->joinable())
        m_thread->join();
    m_thread.reset();
}

void DemoFrameSource::run() {
    const int batch_frames = 96;
    double sleep_seconds = (double)batch_frames / m_sample_rate;

    uint8_t frame_counter = 0;
    double phase = 0.0;
    double phase_step = 1.0 / m_sample_rate;

    while (m_running) {
        for (int i = 0; i < batch_frames && m_running; ++i) {
            auto frame = GigaACEDemoFrameFactory::makeFrame(frame_counter, m_channels, phase);
            if (m_handler)
                m_handler(frame);
            frame_counter = (frame_counter + 1) & 0x1f;
            phase += phase_step;
        }

        auto start = std::chrono::steady_clock::now();
        while (m_running) {
            auto now = std::chrono::steady_clock::now();
            double elapsed = std::chrono::duration<double>(now - start).count();
            if (elapsed >= sleep_seconds)
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}
