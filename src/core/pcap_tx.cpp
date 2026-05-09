#include "pcap_tx.h"
#include <windows.h>
#include <algorithm>
#include <cctype>
#include <cstring>

struct pcap;
using pcap_t = pcap;

struct pcap_if {
    pcap_if* next;
    char* name;
    char* description;
    void* addresses;
    unsigned int flags;
};

using pcap_if_t = pcap_if;
using pcap_findalldevs_fn = int(__cdecl*)(pcap_if_t**, char*);
using pcap_freealldevs_fn = void(__cdecl*)(pcap_if_t*);
using pcap_open_live_fn = pcap_t*(__cdecl*)(const char*, int, int, int, char*);
using pcap_sendpacket_fn = int(__cdecl*)(pcap_t*, const unsigned char*, int);
using pcap_close_fn = void(__cdecl*)(pcap_t*);

struct pcap_timeval {
    long tv_sec;
    long tv_usec;
};

struct pcap_pkthdr_local {
    pcap_timeval ts;
    unsigned int caplen;
    unsigned int len;
};

struct pcap_send_queue {
    unsigned int maxlen;
    unsigned int len;
    char* buffer;
};

using pcap_sendqueue_alloc_fn = pcap_send_queue*(__cdecl*)(unsigned int);
using pcap_sendqueue_destroy_fn = void(__cdecl*)(pcap_send_queue*);
using pcap_sendqueue_queue_fn = int(__cdecl*)(pcap_send_queue*, const pcap_pkthdr_local*, const unsigned char*);
using pcap_sendqueue_transmit_fn = unsigned int(__cdecl*)(pcap_t*, pcap_send_queue*, int);

struct PcapTxRuntime {
    HMODULE module = nullptr;
    pcap_findalldevs_fn findalldevs = nullptr;
    pcap_freealldevs_fn freealldevs = nullptr;
    pcap_open_live_fn open_live = nullptr;
    pcap_sendpacket_fn sendpacket = nullptr;
    pcap_close_fn close = nullptr;
    pcap_sendqueue_alloc_fn sendqueue_alloc = nullptr;
    pcap_sendqueue_destroy_fn sendqueue_destroy = nullptr;
    pcap_sendqueue_queue_fn sendqueue_queue = nullptr;
    pcap_sendqueue_transmit_fn sendqueue_transmit = nullptr;

    bool load() {
        module = LoadLibraryA("wpcap.dll");
        if (!module)
            module = LoadLibraryA("Npcap\\wpcap.dll");
        if (!module)
            return false;

        findalldevs = (pcap_findalldevs_fn)GetProcAddress(module, "pcap_findalldevs");
        freealldevs = (pcap_freealldevs_fn)GetProcAddress(module, "pcap_freealldevs");
        open_live = (pcap_open_live_fn)GetProcAddress(module, "pcap_open_live");
        sendpacket = (pcap_sendpacket_fn)GetProcAddress(module, "pcap_sendpacket");
        close = (pcap_close_fn)GetProcAddress(module, "pcap_close");
        sendqueue_alloc = (pcap_sendqueue_alloc_fn)GetProcAddress(module, "pcap_sendqueue_alloc");
        sendqueue_destroy = (pcap_sendqueue_destroy_fn)GetProcAddress(module, "pcap_sendqueue_destroy");
        sendqueue_queue = (pcap_sendqueue_queue_fn)GetProcAddress(module, "pcap_sendqueue_queue");
        sendqueue_transmit = (pcap_sendqueue_transmit_fn)GetProcAddress(module, "pcap_sendqueue_transmit");
        return findalldevs && freealldevs && open_live && sendpacket && close;
    }

    void unload() {
        if (module) {
            FreeLibrary(module);
            module = nullptr;
        }
    }
};

static std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return (char)std::tolower(c);
    });
    return text;
}

PcapPacketSender::PcapPacketSender(std::string interface_name)
    : m_interface_name(std::move(interface_name)) {}

PcapPacketSender::~PcapPacketSender() {
    close();
}

bool PcapPacketSender::open() {
    if (m_handle)
        return true;

    auto* runtime = new PcapTxRuntime();
    if (!runtime->load()) {
        delete runtime;
        m_last_error = "Npcap/wpcap.dll not found or missing pcap_sendpacket";
        return false;
    }

    char errbuf[256] = {};
    pcap_if_t* devices = nullptr;
    if (runtime->findalldevs(&devices, errbuf) != 0 || !devices) {
        m_last_error = errbuf[0] ? errbuf : "pcap_findalldevs failed";
        runtime->unload();
        delete runtime;
        return false;
    }

    std::string wanted = lower(m_interface_name);
    const char* selected = nullptr;
    for (pcap_if_t* dev = devices; dev; dev = dev->next) {
        std::string name = dev->name ? dev->name : "";
        std::string desc = dev->description ? dev->description : "";
        if (wanted.empty() || lower(name + " " + desc).find(wanted) != std::string::npos) {
            selected = dev->name;
            break;
        }
    }
    if (!selected && devices)
        selected = devices->name;

    if (!selected) {
        runtime->freealldevs(devices);
        runtime->unload();
        delete runtime;
        m_last_error = "No Npcap interface available for TX";
        return false;
    }

    pcap_t* handle = runtime->open_live(selected, 2048, 1, 1, errbuf);
    runtime->freealldevs(devices);
    if (!handle) {
        m_last_error = errbuf[0] ? errbuf : "pcap_open_live failed for TX";
        runtime->unload();
        delete runtime;
        return false;
    }

    m_runtime = runtime;
    m_handle = handle;
    return true;
}

void PcapPacketSender::close() {
    auto* runtime = static_cast<PcapTxRuntime*>(m_runtime);
    auto* handle = static_cast<pcap_t*>(m_handle);
    if (runtime && handle)
        runtime->close(handle);
    if (runtime) {
        runtime->unload();
        delete runtime;
    }
    m_runtime = nullptr;
    m_handle = nullptr;
}

bool PcapPacketSender::send(const uint8_t* data, size_t len) {
    if (!data || len == 0)
        return false;
    if (!m_handle && !open())
        return false;

    auto* runtime = static_cast<PcapTxRuntime*>(m_runtime);
    auto* handle = static_cast<pcap_t*>(m_handle);
    if (!runtime || !handle)
        return false;

    int rc = runtime->sendpacket(handle, data, static_cast<int>(len));
    if (rc != 0) {
        m_last_error = "pcap_sendpacket failed";
        return false;
    }
    return true;
}

bool PcapPacketSender::sendQueue(const std::vector<std::vector<uint8_t>>& packets, double packet_rate) {
    if (packets.empty())
        return true;
    if (!m_handle && !open())
        return false;

    auto* runtime = static_cast<PcapTxRuntime*>(m_runtime);
    auto* handle = static_cast<pcap_t*>(m_handle);
    if (!runtime || !handle)
        return false;

    if (!runtime->sendqueue_alloc || !runtime->sendqueue_destroy ||
        !runtime->sendqueue_queue || !runtime->sendqueue_transmit) {
        for (const auto& packet : packets) {
            if (!send(packet.data(), packet.size()))
                return false;
        }
        return true;
    }

    size_t bytes = 0;
    for (const auto& packet : packets)
        bytes += sizeof(pcap_pkthdr_local) + packet.size();

    auto* queue = runtime->sendqueue_alloc((unsigned int)bytes);
    if (!queue) {
        m_last_error = "pcap_sendqueue_alloc failed";
        return false;
    }

    const double rate = packet_rate > 1.0 ? packet_rate : 96000.0;
    bool ok = true;
    for (size_t i = 0; i < packets.size(); ++i) {
        const auto& packet = packets[i];
        double usec = (double)i * 1000000.0 / rate;
        pcap_pkthdr_local hdr{};
        hdr.ts.tv_sec = (long)(usec / 1000000.0);
        hdr.ts.tv_usec = (long)(usec - (double)hdr.ts.tv_sec * 1000000.0 + 0.5);
        hdr.caplen = (unsigned int)packet.size();
        hdr.len = (unsigned int)packet.size();
        if (runtime->sendqueue_queue(queue, &hdr, packet.data()) != 0) {
            m_last_error = "pcap_sendqueue_queue failed";
            ok = false;
            break;
        }
    }

    if (ok) {
        unsigned int sent = runtime->sendqueue_transmit(handle, queue, 1);
        if (sent < queue->len) {
            m_last_error = "pcap_sendqueue_transmit sent incomplete queue";
            ok = false;
        }
    }

    runtime->sendqueue_destroy(queue);
    return ok;
}
