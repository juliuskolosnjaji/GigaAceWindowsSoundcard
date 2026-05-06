#ifndef GIGAACE_DEMO_SOURCE_H
#define GIGAACE_DEMO_SOURCE_H

#include <functional>
#include <atomic>
#include <thread>
#include <vector>
#include <cstdint>
#include <memory>

class DemoFrameSource {
public:
    using FrameHandler = std::function<void(const std::vector<uint8_t>&)>;

    DemoFrameSource(int channels, double sample_rate);
    ~DemoFrameSource();

    bool start(FrameHandler handler);
    void stop();

private:
    void run();

    int m_channels;
    double m_sample_rate;
    std::atomic<bool> m_running{false};
    std::unique_ptr<std::thread> m_thread;
    FrameHandler m_handler;
};

#endif
