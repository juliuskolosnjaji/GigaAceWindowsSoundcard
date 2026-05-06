#ifndef GIGAACE_PCAP_SOURCE_H
#define GIGAACE_PCAP_SOURCE_H

#include "demo_source.h"
#include <atomic>
#include <memory>
#include <string>
#include <thread>

class PcapFrameSource {
public:
    using FrameHandler = DemoFrameSource::FrameHandler;

    PcapFrameSource(std::string interface_name, uint16_t ethertype);
    ~PcapFrameSource();

    bool start(FrameHandler handler);
    void stop();
    std::string lastError() const;

private:
    void run();

    std::string m_interface_name;
    uint16_t m_ethertype;
    FrameHandler m_handler;
    std::unique_ptr<std::thread> m_thread;
    std::atomic<bool> m_running{false};
    std::string m_last_error;
};

#endif
