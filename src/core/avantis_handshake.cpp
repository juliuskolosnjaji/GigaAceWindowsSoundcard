#include "avantis_handshake.h"
#include <windows.h>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdio>

// ---------------------------------------------------------------------------
// Minimal pcap type wiring (dynamic DLL, no pcap headers needed)
// ---------------------------------------------------------------------------

struct PcapIf {
    PcapIf*      next;
    char*        name;
    char*        description;
    void*        addresses;
    unsigned int flags;
};

using fn_findalldevs = int  (__cdecl*)(PcapIf**, char*);
using fn_freealldevs = void (__cdecl*)(PcapIf*);
using fn_open_live   = void*(__cdecl*)(const char*, int, int, int, char*);
using fn_sendpacket  = int  (__cdecl*)(void*, const unsigned char*, int);
using fn_close       = void (__cdecl*)(void*);

struct PcapTx {
    HMODULE      mod         = nullptr;
    fn_findalldevs findall   = nullptr;
    fn_freealldevs freeall   = nullptr;
    fn_open_live   open_live = nullptr;
    fn_sendpacket  sendpkt   = nullptr;
    fn_close       close     = nullptr;

    bool load() {
        mod = LoadLibraryA("wpcap.dll");
        if (!mod) mod = LoadLibraryA("Npcap\\wpcap.dll");
        if (!mod) return false;
        findall   = (fn_findalldevs)GetProcAddress(mod, "pcap_findalldevs");
        freeall   = (fn_freealldevs)GetProcAddress(mod, "pcap_freealldevs");
        open_live = (fn_open_live)  GetProcAddress(mod, "pcap_open_live");
        sendpkt   = (fn_sendpacket) GetProcAddress(mod, "pcap_sendpacket");
        close     = (fn_close)      GetProcAddress(mod, "pcap_close");
        return findall && freeall && open_live && sendpkt && close;
    }

    void unload() {
        if (mod) { FreeLibrary(mod); mod = nullptr; }
    }
};

static std::string str_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c){ return (char)std::tolower(c); });
    return s;
}

// ---------------------------------------------------------------------------
// Announcement frame constants
// ---------------------------------------------------------------------------

static constexpr uint8_t kStreamAnnounce = 0xF0; // first contact
static constexpr uint8_t kStreamHeart    = 0xF1; // already connected

// ---------------------------------------------------------------------------
// AvantisHandshake
// ---------------------------------------------------------------------------

AvantisHandshake::AvantisHandshake(AvantisHandshakeConfig cfg)
    : m_cfg(std::move(cfg)) {}

AvantisHandshake::~AvantisHandshake() { stop(); }

bool AvantisHandshake::start(StateCallback cb) {
    if (m_running.load()) return false;
    m_callback   = std::move(cb);
    m_audio_seen = false;
    m_running    = true;
    m_thread     = std::thread(&AvantisHandshake::run, this);
    return true;
}

void AvantisHandshake::stop() {
    if (!m_running.exchange(false)) return;
    if (m_thread.joinable()) m_thread.join();
    setState(AvantisHandshakeState::Idle);
}

void AvantisHandshake::notifyAudioFrame(const uint8_t* frame_bytes, size_t frame_len) {
    if (!frame_bytes || frame_len < 12) return;
    // bytes 6-11 = src_mac of sender (the Avantis console)
    std::lock_guard<std::mutex> lk(m_audio_mutex);
    if (!m_audio_seen)
        std::memcpy(m_console_mac, frame_bytes + 6, 6);
    m_audio_seen   = true;
    m_last_audio_tp = std::chrono::steady_clock::now();
}

bool AvantisHandshake::consoleMac(uint8_t out[6]) const {
    std::lock_guard<std::mutex> lk(m_audio_mutex);
    if (!m_audio_seen) return false;
    std::memcpy(out, m_console_mac, 6);
    return true;
}

void AvantisHandshake::setState(AvantisHandshakeState s) {
    AvantisHandshakeState prev = m_state.exchange(s);
    if (prev != s && m_callback) {
        uint8_t mac[6] = {};
        consoleMac(mac);
        m_callback(s, mac);
    }
}

// Builds a GigaACE device-announcement frame into buf.
// Returns the number of bytes written (52), or 0 on failure.
size_t AvantisHandshake::buildFrame(uint8_t* buf, size_t buf_size,
                                     uint8_t counter, bool heartbeat) {
    static constexpr size_t kFrameLen = 52;
    if (!buf || buf_size < kFrameLen) return 0;
    std::memset(buf, 0, kFrameLen);

    // Ethernet header
    std::memset(buf, 0xFF, 6);                      // dst: broadcast
    std::memcpy(buf + 6, m_cfg.local_mac, 6);       // src: virtual device
    buf[12] = 0x04; buf[13] = 0xEE;                 // EtherType 0x04EE

    // GigaACE control header (bytes 14-23)
    buf[19] = counter & 0x1F;                       // sequence (5-bit)
    buf[20] = heartbeat ? kStreamHeart : kStreamAnnounce;

    // Payload (bytes 24-51): GACE TLV announcement
    buf[24] = 0x47; buf[25] = 0x41;                 // 'G','A'
    buf[26] = 0x43; buf[27] = 0x45;                 // 'C','E'
    buf[28] = heartbeat ? 0x02u : 0x01u;            // opcode
    buf[29] = 0x10;                                 // device_type: virtual PC rx

    // channel_count (uint16_t big-endian)
    uint16_t ch = m_cfg.channel_count;
    buf[30] = (uint8_t)(ch >> 8);
    buf[31] = (uint8_t)(ch & 0xFF);

    // sample_rate (uint32_t big-endian)
    auto sr = (uint32_t)m_cfg.sample_rate;
    buf[32] = (uint8_t)(sr >> 24);
    buf[33] = (uint8_t)(sr >> 16);
    buf[34] = (uint8_t)(sr >> 8);
    buf[35] = (uint8_t)(sr & 0xFF);

    // device name (null-terminated, 16 bytes)
    std::strncpy(reinterpret_cast<char*>(buf + 36), "GigaACE PC", 15);
    buf[51] = 0;

    return kFrameLen;
}

void AvantisHandshake::run() {
  try {
    PcapTx pcap;
    if (!pcap.load()) {
        m_running = false;
        return;
    }

    // Locate the right pcap interface by name/description match
    char errbuf[256] = {};
    PcapIf* devs = nullptr;
    if (pcap.findall(&devs, errbuf) != 0 || !devs) {
        pcap.unload();
        m_running = false;
        return;
    }

    std::string wanted = str_lower(m_cfg.interface_name);
    const char* selected = nullptr;
    for (PcapIf* d = devs; d; d = d->next) {
        std::string name = d->name ? d->name : "";
        std::string desc = d->description ? d->description : "";
        if (wanted.empty() || str_lower(name + " " + desc).find(wanted) != std::string::npos) {
            selected = d->name;
            break;
        }
    }
    if (!selected && devs)
        selected = devs->name;

    if (!selected) {
        pcap.freeall(devs);
        pcap.unload();
        m_running = false;
        return;
    }

    // Open with a short read timeout (1 ms); we only need TX on this handle
    void* handle = pcap.open_live(selected, 64, 1, 1, errbuf);
    pcap.freeall(devs);

    if (!handle) {
        pcap.unload();
        m_running = false;
        return;
    }

    setState(AvantisHandshakeState::Announcing);

    uint8_t counter = 0;
    uint8_t frame_buf[64];
    auto    interval = std::chrono::milliseconds(m_cfg.announce_interval_ms);
    auto    timeout  = std::chrono::milliseconds(m_cfg.timeout_ms);

    while (m_running.load()) {
        // Send announcement or heartbeat
        bool hb      = (m_state.load() == AvantisHandshakeState::Connected);
        size_t flen  = buildFrame(frame_buf, sizeof(frame_buf), counter++, hb);
        if (flen > 0)
            pcap.sendpkt(handle, frame_buf, (int)flen);

        // Update connection state based on audio frame activity
        {
            std::lock_guard<std::mutex> lk(m_audio_mutex);
            bool live = m_audio_seen &&
                (std::chrono::steady_clock::now() - m_last_audio_tp) < timeout;
            auto cur = m_state.load();
            if (live && cur != AvantisHandshakeState::Connected)
                setState(AvantisHandshakeState::Connected);
            else if (!live && cur == AvantisHandshakeState::Connected)
                setState(AvantisHandshakeState::Lost);
        }

        std::this_thread::sleep_for(interval);
    }

    pcap.close(handle);
    pcap.unload();
  } catch (...) {
    m_running = false;
    setState(AvantisHandshakeState::Lost);
  }
}
