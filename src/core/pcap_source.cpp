#include "pcap_source.h"
#include "frame.h"

#include <windows.h>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <vector>

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

struct pcap_timeval {
    long tv_sec;
    long tv_usec;
};

struct pcap_pkthdr {
    pcap_timeval ts;
    unsigned int caplen;
    unsigned int len;
};

using pcap_findalldevs_fn = int(__cdecl*)(pcap_if_t**, char*);
using pcap_freealldevs_fn = void(__cdecl*)(pcap_if_t*);
using pcap_open_live_fn = pcap_t*(__cdecl*)(const char*, int, int, int, char*);
using pcap_next_ex_fn = int(__cdecl*)(pcap_t*, pcap_pkthdr**, const unsigned char**);
using pcap_close_fn = void(__cdecl*)(pcap_t*);
using pcap_geterr_fn = char*(__cdecl*)(pcap_t*);

static std::string lowercase(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
        return (char)std::tolower(ch);
    });
    return text;
}

class PcapRuntime {
public:
    PcapRuntime() {
        module = LoadLibraryA("wpcap.dll");
        if (!module)
            module = LoadLibraryA("Npcap\\wpcap.dll");
        if (!module)
            return;

        findalldevs = (pcap_findalldevs_fn)GetProcAddress(module, "pcap_findalldevs");
        freealldevs = (pcap_freealldevs_fn)GetProcAddress(module, "pcap_freealldevs");
        open_live = (pcap_open_live_fn)GetProcAddress(module, "pcap_open_live");
        next_ex = (pcap_next_ex_fn)GetProcAddress(module, "pcap_next_ex");
        close = (pcap_close_fn)GetProcAddress(module, "pcap_close");
        geterr = (pcap_geterr_fn)GetProcAddress(module, "pcap_geterr");
    }

    ~PcapRuntime() {
        if (module)
            FreeLibrary(module);
    }

    bool valid() const {
        return module && findalldevs && freealldevs && open_live && next_ex && close && geterr;
    }

    HMODULE module = nullptr;
    pcap_findalldevs_fn findalldevs = nullptr;
    pcap_freealldevs_fn freealldevs = nullptr;
    pcap_open_live_fn open_live = nullptr;
    pcap_next_ex_fn next_ex = nullptr;
    pcap_close_fn close = nullptr;
    pcap_geterr_fn geterr = nullptr;
};

static bool is_gigaace_candidate(const unsigned char* data, unsigned int len, uint16_t ethertype) {
    if (!data || len < GIGAACE_MIN_FRAME_SIZE)
        return false;

    static const unsigned char broadcast[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    if (std::memcmp(data, broadcast, sizeof(broadcast)) != 0)
        return false;

    uint16_t frame_type = (uint16_t)(((uint16_t)data[12] << 8) | data[13]);
    return frame_type == ethertype;
}

PcapFrameSource::PcapFrameSource(std::string interface_name, uint16_t ethertype)
    : m_interface_name(std::move(interface_name)), m_ethertype(ethertype) {}

PcapFrameSource::~PcapFrameSource() {
    stop();
}

bool PcapFrameSource::start(FrameHandler handler) {
    if (m_running)
        return false;

    m_handler = std::move(handler);
    m_last_error.clear();
    m_running = true;
    m_thread = std::make_unique<std::thread>(&PcapFrameSource::run, this);
    return true;
}

void PcapFrameSource::stop() {
    if (!m_running)
        return;

    m_running = false;
    if (m_thread && m_thread->joinable())
        m_thread->join();
    m_thread.reset();
}

std::string PcapFrameSource::lastError() const {
    return m_last_error;
}

void PcapFrameSource::run() {
    PcapRuntime pcap;
    if (!pcap.valid()) {
        m_last_error = "Npcap/wpcap.dll not found. Install Npcap with WinPcap-compatible API.";
        m_running = false;
        return;
    }

    char errbuf[256] = {};
    pcap_if_t* devices = nullptr;
    if (pcap.findalldevs(&devices, errbuf) != 0 || !devices) {
        m_last_error = errbuf[0] ? errbuf : "No packet capture devices found.";
        m_running = false;
        return;
    }

    std::string wanted = lowercase(m_interface_name);
    const char* selected = nullptr;
    for (pcap_if_t* dev = devices; dev; dev = dev->next) {
        std::string name = dev->name ? dev->name : "";
        std::string desc = dev->description ? dev->description : "";
        std::string haystack = lowercase(name + " " + desc);
        if (wanted.empty() || haystack.find(wanted) != std::string::npos) {
            selected = dev->name;
            break;
        }
    }

    if (!selected && devices)
        selected = devices->name;

    if (!selected) {
        pcap.freealldevs(devices);
        m_last_error = "No selectable packet capture interface found.";
        m_running = false;
        return;
    }

    pcap_t* handle = pcap.open_live(selected, 2048, 1, 10, errbuf);
    pcap.freealldevs(devices);

    if (!handle) {
        m_last_error = errbuf[0] ? errbuf : "Failed to open packet capture interface.";
        m_running = false;
        return;
    }

    while (m_running) {
        pcap_pkthdr* header = nullptr;
        const unsigned char* payload = nullptr;
        int rc = pcap.next_ex(handle, &header, &payload);

        if (rc == 0)
            continue;
        if (rc < 0) {
            char* err = pcap.geterr(handle);
            m_last_error = err ? err : "Packet capture failed.";
            break;
        }

        if (header && payload && is_gigaace_candidate(payload, header->caplen, m_ethertype)) {
            std::vector<uint8_t> frame(payload, payload + header->caplen);
            if (m_handler)
                m_handler(frame);
        }
    }

    pcap.close(handle);
    m_running = false;
}
