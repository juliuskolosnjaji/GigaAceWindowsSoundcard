#ifndef GIGAACE_PCAP_TX_H
#define GIGAACE_PCAP_TX_H

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

class PcapPacketSender {
public:
    explicit PcapPacketSender(std::string interface_name);
    ~PcapPacketSender();

    bool open();
    void close();
    bool send(const uint8_t* data, size_t len);
    bool sendQueue(const std::vector<std::vector<uint8_t>>& packets, double packet_rate);
    const std::string& lastError() const { return m_last_error; }
    bool isOpen() const { return m_handle != nullptr; }

private:
    std::string m_interface_name;
    std::string m_last_error;
    void* m_runtime = nullptr;
    void* m_handle = nullptr;
};

#endif
