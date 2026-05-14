#include "pcap_tx.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <windows.h>

static constexpr uint8_t kBroadcastMac[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
static constexpr uint8_t kGx4816Mac[6] = {0x00, 0x04, 0xc4, 0x06, 0xcf, 0xe8};
static constexpr uint8_t kKnownAvantisMac[6] = {0x00, 0x04, 0xc4, 0x08, 0xc8, 0x54};

struct pcap;
using pcap_t = pcap;

struct pcap_if {
    pcap_if* next;
    char* name;
    char* description;
    void* addresses;
    unsigned int flags;
};

struct pcap_timeval {
    long tv_sec;
    long tv_usec;
};

struct pcap_pkthdr {
    pcap_timeval ts;
    unsigned int caplen;
    unsigned int len;
};

using pcap_findalldevs_fn = int(__cdecl*)(pcap_if**, char*);
using pcap_freealldevs_fn = void(__cdecl*)(pcap_if*);
using pcap_open_live_fn = pcap_t*(__cdecl*)(const char*, int, int, int, char*);
using pcap_next_ex_fn = int(__cdecl*)(pcap_t*, pcap_pkthdr**, const unsigned char**);
using pcap_close_fn = void(__cdecl*)(pcap_t*);

struct Variant {
    int id = 0;
    const char* name = "";
    const char* note = "";
    uint16_t ethertype = 0x04ee;
    uint8_t header14 = 0x00;
    uint8_t header15 = 0x0a;
    uint8_t header16 = 0x04;
    uint8_t header17 = 0xea;
    uint8_t header18 = 0x00;
    uint8_t stream_type = 0x02;
    size_t frame_len = 1276;
    double packet_rate = 48000.0;
    bool short_identity = false;
};

struct Options {
    std::string interface_name;
    int seconds = 4;
    int pause_ms = 1000;
    int variant = -1;
    bool all = false;
    bool send = false;
};

static std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
        return (char)std::tolower(ch);
    });
    return text;
}

static bool mac_eq(const uint8_t* a, const uint8_t* b) {
    return a && b && std::memcmp(a, b, 6) == 0;
}

static bool is_ah_oui(const uint8_t* mac) {
    return mac && mac[0] == 0x00 && mac[1] == 0x04 && mac[2] == 0xc4;
}

static std::string mac_string(const uint8_t* mac) {
    char buf[20] = {};
    if (!mac)
        return {};
    std::snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return buf;
}

static bool is_console_response(const uint8_t* frame, size_t len) {
    if (!frame || len < 24)
        return false;
    uint16_t ethertype = (uint16_t)(((uint16_t)frame[12] << 8) | frame[13]);
    if (ethertype != 0x04ee)
        return false;
    const uint8_t* src = frame + 6;
    if (mac_eq(src, kGx4816Mac))
        return false;
    bool known_avantis = mac_eq(src, kKnownAvantisMac);
    bool console_header = frame[14] == 0x00 && frame[15] == 0x00 &&
                          frame[16] == 0x04 && frame[17] == 0xea &&
                          frame[18] == 0x00;
    return known_avantis || (is_ah_oui(src) && console_header);
}

class ConsoleResponseMonitor {
public:
    explicit ConsoleResponseMonitor(std::string interface_name)
        : m_interface_name(std::move(interface_name)) {}

    bool start() {
        if (m_running)
            return false;
        m_running = true;
        m_thread = std::thread(&ConsoleResponseMonitor::run, this);
        return true;
    }

    void stop() {
        if (!m_running.exchange(false))
            return;
        if (m_thread.joinable())
            m_thread.join();
    }

    ~ConsoleResponseMonitor() {
        stop();
    }

    uint64_t count() const {
        return m_count.load();
    }

    std::string lastMac() const {
        std::lock_guard<std::mutex> lock(m_lock);
        return m_last_mac;
    }

    std::string lastError() const {
        std::lock_guard<std::mutex> lock(m_lock);
        return m_last_error;
    }

private:
    void setError(const std::string& error) {
        std::lock_guard<std::mutex> lock(m_lock);
        m_last_error = error;
    }

    void setLastMac(const uint8_t* mac) {
        std::lock_guard<std::mutex> lock(m_lock);
        m_last_mac = mac_string(mac);
    }

    void run() {
        HMODULE module = LoadLibraryA("wpcap.dll");
        if (!module)
            module = LoadLibraryA("Npcap\\wpcap.dll");
        if (!module) {
            setError("Npcap/wpcap.dll not found");
            m_running = false;
            return;
        }

        auto findalldevs = (pcap_findalldevs_fn)GetProcAddress(module, "pcap_findalldevs");
        auto freealldevs = (pcap_freealldevs_fn)GetProcAddress(module, "pcap_freealldevs");
        auto open_live = (pcap_open_live_fn)GetProcAddress(module, "pcap_open_live");
        auto next_ex = (pcap_next_ex_fn)GetProcAddress(module, "pcap_next_ex");
        auto close = (pcap_close_fn)GetProcAddress(module, "pcap_close");
        if (!findalldevs || !freealldevs || !open_live || !next_ex || !close) {
            FreeLibrary(module);
            setError("Npcap API missing pcap_next_ex");
            m_running = false;
            return;
        }

        char errbuf[256] = {};
        pcap_if* devices = nullptr;
        if (findalldevs(&devices, errbuf) != 0 || !devices) {
            FreeLibrary(module);
            setError(errbuf[0] ? errbuf : "pcap_findalldevs failed");
            m_running = false;
            return;
        }

        std::string wanted = lower(m_interface_name);
        const char* selected = nullptr;
        for (pcap_if* dev = devices; dev; dev = dev->next) {
            std::string name = dev->name ? dev->name : "";
            std::string desc = dev->description ? dev->description : "";
            if (wanted.empty() || lower(name + " " + desc).find(wanted) != std::string::npos) {
                selected = dev->name;
                break;
            }
        }
        if (!selected && devices)
            selected = devices->name;

        pcap_t* handle = selected ? open_live(selected, 2048, 1, 10, errbuf) : nullptr;
        freealldevs(devices);
        if (!handle) {
            FreeLibrary(module);
            setError(errbuf[0] ? errbuf : "pcap_open_live failed");
            m_running = false;
            return;
        }

        while (m_running.load()) {
            pcap_pkthdr* header = nullptr;
            const unsigned char* data = nullptr;
            int rc = next_ex(handle, &header, &data);
            if (rc == 0)
                continue;
            if (rc < 0)
                break;
            if (header && data && is_console_response(data, header->caplen)) {
                ++m_count;
                setLastMac(data + 6);
            }
        }

        close(handle);
        FreeLibrary(module);
        m_running = false;
    }

    std::string m_interface_name;
    std::atomic<bool> m_running{false};
    std::atomic<uint64_t> m_count{0};
    std::thread m_thread;
    mutable std::mutex m_lock;
    std::string m_last_mac;
    std::string m_last_error;
};

static const std::vector<Variant>& variants() {
    static const std::vector<Variant> v = {
        {0, "gx4816-slink-48k", "Observed GX4816/SLink shape: 0x04EE, 00 0A 04 EA 00, stream 0x02, 48k packets/s."},
        {1, "gx4816-slink-96k", "Same GX4816 header, but free-running at 96k packets/s.", 0x04ee, 0x00, 0x0a, 0x04, 0xea, 0x00, 0x02, 1276, 96000.0},
        {2, "avantis-header-gx-mac", "Console-style 00 00 04 EA 00 header, but with GX4816 source MAC.", 0x04ee, 0x00, 0x00, 0x04, 0xea, 0x00, 0x02, 1276, 48000.0},
        {3, "gx4816-stream01", "GX4816 header with stream type 0x01 instead of 0x02.", 0x04ee, 0x00, 0x0a, 0x04, 0xea, 0x00, 0x01, 1276, 48000.0},
        {4, "gx4816-zero-prefix", "GX4816 MAC and stream 0x02, but bytes 14-18 zeroed.", 0x04ee, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 1276, 48000.0},
        {5, "short-gace-identity", "Short experimental identity frame with ASCII GACE/GX4816 payload.", 0x04ee, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf0, 64, 1000.0, true},
    };
    return v;
}

static void usage() {
    std::cout
        << "GigaAceIdentify - controlled stagebox identification probe\n\n"
        << "Usage:\n"
        << "  GigaAceIdentify.exe --interface \"\\\\Device\\\\NPF_{...}\" --send --all\n"
        << "  GigaAceIdentify.exe --interface \"Intel(R) Ethernet\" --send --variant 0 --seconds 10\n\n"
        << "Options:\n"
        << "  --interface NAME   Npcap interface name or description fragment\n"
        << "  --send             Actually transmit packets. Without this it only prints the plan\n"
        << "  --all              Run all variants sequentially\n"
        << "  --variant N        Run one variant\n"
        << "  --seconds N        Seconds per streaming variant, default 4\n"
        << "  --pause-ms N       Pause between variants, default 1000\n"
        << "  --list             Print known variants\n";
}

static void list_variants() {
    for (const auto& v : variants()) {
        std::cout << "  " << v.id << "  " << v.name
                  << "  rate=" << v.packet_rate
                  << "  stream=0x" << std::hex << (int)v.stream_type << std::dec
                  << "\n      " << v.note << "\n";
    }
}

static bool parse_args(int argc, char** argv, Options& o) {
    auto next_val = [&](int& i, const char* arg) -> const char* {
        if (i + 1 >= argc) {
            std::cerr << arg << " needs a value.\n";
            return nullptr;
        }
        return argv[++i];
    };

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--interface" || a == "-i") {
            const char* v = next_val(i, a.c_str());
            if (!v) return false;
            o.interface_name = v;
        } else if (a == "--seconds") {
            const char* v = next_val(i, a.c_str());
            if (!v) return false;
            o.seconds = std::max(1, std::atoi(v));
        } else if (a == "--pause-ms") {
            const char* v = next_val(i, a.c_str());
            if (!v) return false;
            o.pause_ms = std::max(0, std::atoi(v));
        } else if (a == "--variant") {
            const char* v = next_val(i, a.c_str());
            if (!v) return false;
            o.variant = std::atoi(v);
        } else if (a == "--all") {
            o.all = true;
        } else if (a == "--send") {
            o.send = true;
        } else if (a == "--list") {
            list_variants();
            return false;
        } else if (a == "--help" || a == "-h" || a == "/?") {
            usage();
            return false;
        } else {
            std::cerr << "Unknown argument: " << a << "\n";
            return false;
        }
    }

    if (!o.all && o.variant < 0)
        o.variant = 0;
    if (o.interface_name.empty() && o.send) {
        std::cerr << "--interface is required when --send is used.\n";
        return false;
    }
    return true;
}

static std::vector<uint8_t> make_frame(const Variant& v, uint8_t counter) {
    std::vector<uint8_t> frame(v.frame_len, 0);
    std::memcpy(frame.data(), kBroadcastMac, 6);
    std::memcpy(frame.data() + 6, kGx4816Mac, 6);
    frame[12] = (uint8_t)(v.ethertype >> 8);
    frame[13] = (uint8_t)(v.ethertype & 0xff);
    frame[14] = v.header14;
    frame[15] = v.header15;
    frame[16] = v.header16;
    frame[17] = v.header17;
    frame[18] = v.header18;
    frame[19] = counter & 0x1f;
    frame[20] = v.stream_type;

    if (v.short_identity && frame.size() >= 52) {
        frame[24] = 'G';
        frame[25] = 'A';
        frame[26] = 'C';
        frame[27] = 'E';
        frame[28] = 0x01;
        frame[29] = 0x48;
        frame[30] = 0x00;
        frame[31] = 0x40;
        frame[32] = 0x00;
        frame[33] = 0x01;
        frame[34] = 0x77;
        frame[35] = 0x00;
        std::memcpy(frame.data() + 36, "GX4816", 6);
    }

    return frame;
}

static bool send_variant(PcapPacketSender& sender, const Variant& v, int seconds) {
    std::cout << "\nVariant " << v.id << ": " << v.name << "\n"
              << "  " << v.note << "\n";

    if (v.short_identity) {
        for (int i = 0; i < seconds; ++i) {
            auto frame = make_frame(v, (uint8_t)i);
            if (!sender.send(frame.data(), frame.size()))
                return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }
        return true;
    }

    static constexpr size_t kBatchSize = 192;
    const int loops = std::max(1, (int)(seconds * v.packet_rate / (double)kBatchSize));
    uint64_t sent = 0;
    uint8_t counter = 0;

    for (int loop = 0; loop < loops; ++loop) {
        std::vector<std::vector<uint8_t>> batch;
        batch.reserve(kBatchSize);
        for (size_t i = 0; i < kBatchSize; ++i)
            batch.push_back(make_frame(v, counter++));

        if (!sender.sendQueue(batch, v.packet_rate))
            return false;

        sent += batch.size();
        if ((loop % 100) == 0)
            std::cout << "\r  sent " << sent << " frames" << std::flush;
    }

    std::cout << "\r  sent " << sent << " frames\n";
    return true;
}

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--list") {
            list_variants();
            return 0;
        }
        if (arg == "--help" || arg == "-h" || arg == "/?") {
            usage();
            return 0;
        }
    }

    Options options;
    if (!parse_args(argc, argv, options))
        return 2;

    std::vector<Variant> selected;
    if (options.all) {
        selected = variants();
    } else {
        auto it = std::find_if(variants().begin(), variants().end(), [&](const Variant& v) {
            return v.id == options.variant;
        });
        if (it == variants().end()) {
            std::cerr << "Unknown variant " << options.variant << ". Use --list.\n";
            return 2;
        }
        selected.push_back(*it);
    }

    std::cout << "Stagebox identity probe plan:\n";
    list_variants();
    std::cout << "\nSelected:";
    for (const auto& v : selected)
        std::cout << " " << v.id;
    std::cout << "\n";

    if (!options.send) {
        std::cout << "\nDry run only. Add --send and --interface to transmit.\n";
        return 0;
    }

    PcapPacketSender sender(options.interface_name);
    if (!sender.open()) {
        std::cerr << "TX open failed: " << sender.lastError() << "\n";
        return 1;
    }

    for (size_t i = 0; i < selected.size(); ++i) {
        ConsoleResponseMonitor monitor(options.interface_name);
        uint64_t before = 0;
        if (monitor.start()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            before = monitor.count();
        }

        if (!send_variant(sender, selected[i], options.seconds)) {
            std::cerr << "\nTX failed: " << sender.lastError() << "\n";
            monitor.stop();
            return 1;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        uint64_t after = monitor.count();
        std::string mac = monitor.lastMac();
        std::string error = monitor.lastError();
        monitor.stop();

        uint64_t responses = (after >= before) ? (after - before) : after;
        if (responses > 0) {
            std::cout << "  Console response detected: " << responses << " frames";
            if (!mac.empty())
                std::cout << " from " << mac;
            std::cout << "\n";
        } else {
            std::cout << "  No console response detected during this variant";
            if (!error.empty())
                std::cout << " (" << error << ")";
            std::cout << "\n";
        }

        if (i + 1 < selected.size() && options.pause_ms > 0) {
            std::cout << "Pause " << options.pause_ms << " ms. Watch the console state now.\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(options.pause_ms));
        }
    }

    std::cout << "\nDone.\n";
    return 0;
}
