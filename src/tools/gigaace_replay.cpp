#include "shared_bridge.h"

#include <windows.h>
#include <mmsystem.h>
#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

static constexpr const char* kDefaultSharedMemory = "Local\\GigaACEVirtualDevice";

// ---------------------------------------------------------------------------
// Decode modes for raw 3-byte (24-bit) samples.
// Bytes are always assembled big-endian from the wire first:
//   raw = (bytes[0]<<16) | (bytes[1]<<8) | bytes[2]
// Then the selected transform is applied before sign extension.
//
//  0  be-raw     Plain big-endian PCM24, no transform (matches fixed decode)
//  1  le-raw     Little-endian: byte-reverse then sign-extend
//  2  nibble     Nibble-swap each byte, then sign-extend
//  3  swizzle    Byte-reverse then nibble-swap (the old incorrect default)
//  4  nib-byte   Nibble-swap then byte-reverse
// ---------------------------------------------------------------------------
enum DecodeMode { DM_BE_RAW = 0, DM_LE_RAW = 1, DM_NIBBLE = 2, DM_SWIZZLE = 3, DM_NIB_BYTE = 4 };

static const char* kDecodeModeNames[] = { "be-raw", "le-raw", "nibble", "swizzle", "nib-byte" };

static inline uint32_t byte_rev24(uint32_t v) {
    return ((v >> 16) & 0xFF) | (v & 0xFF00u) | ((v & 0xFFu) << 16);
}
static inline uint32_t nibble_swap24(uint32_t v) {
    return ((v & 0xF0F0F0u) >> 4) | ((v & 0x0F0F0Fu) << 4);
}
static inline int32_t sign_ext24(uint32_t v) {
    v &= 0x00FFFFFFu;
    if (v & 0x800000u) v |= 0xFF000000u;
    return (int32_t)v;
}

static float decode_sample_mode(const uint8_t p[3], int mode) {
    uint32_t v = ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | (uint32_t)p[2];
    switch (mode) {
    case DM_LE_RAW:   v = byte_rev24(v); break;
    case DM_NIBBLE:   v = nibble_swap24(v); break;
    case DM_SWIZZLE:  v = nibble_swap24(byte_rev24(v)); break;
    case DM_NIB_BYTE: v = byte_rev24(nibble_swap24(v)); break;
    default: break; // DM_BE_RAW
    }
    return (float)sign_ext24(v) / 8388608.0f;
}

// ---------------------------------------------------------------------------
// Options
// ---------------------------------------------------------------------------
struct ReplayOptions {
    std::string input_path;
    std::string shared_memory_name = kDefaultSharedMemory;
    int         channels           = 128;
    double      sample_rate        = 96000.0;
    int         ring_frames        = 192000;
    bool        loop               = false;
    bool        quiet              = false;
    bool        probe              = false;
    int         probe_frames       = 30;
    std::string wav_output;
    int         wav_channels       = 8;
    int         decode_mode        = DM_BE_RAW;
};

// ---------------------------------------------------------------------------
// Packet container
// ---------------------------------------------------------------------------
struct Packet {
    std::vector<uint8_t> bytes;
    uint64_t timestamp_us = 0; // microseconds from pcapng EPB, 0 if unknown
};

// ---------------------------------------------------------------------------
// File I/O helpers
// ---------------------------------------------------------------------------
static uint16_t read_u16(const uint8_t* p, bool le) {
    return le ? (uint16_t)(p[0] | (p[1] << 8))
              : (uint16_t)((p[0] << 8) | p[1]);
}
static uint32_t read_u32(const uint8_t* p, bool le) {
    if (le) return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
    return ((uint32_t)p[0]<<24) | ((uint32_t)p[1]<<16) | ((uint32_t)p[2]<<8) | (uint32_t)p[3];
}

static bool read_file(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    auto sz = f.tellg();
    if (sz < 0) return false;
    f.seekg(0);
    out.resize((size_t)sz);
    if (!out.empty()) f.read((char*)out.data(), (std::streamsize)out.size());
    return true;
}

// ---------------------------------------------------------------------------
// pcap / pcapng parsers
// ---------------------------------------------------------------------------
static bool parse_pcap(const std::vector<uint8_t>& data, std::vector<Packet>& packets) {
    if (data.size() < 24) return false;
    bool le = true;
    if      (data[0]==0xa1&&data[1]==0xb2&&data[2]==0xc3&&data[3]==0xd4) le=false;
    else if (data[0]==0xa1&&data[1]==0xb2&&data[2]==0x3c&&data[3]==0x4d) le=false;
    else if (data[0]==0xd4&&data[1]==0xc3&&data[2]==0xb2&&data[3]==0xa1) le=true;
    else if (data[0]==0x4d&&data[1]==0x3c&&data[2]==0xb2&&data[3]==0xa1) le=true;
    else return false;

    size_t off = 24;
    while (off + 16 <= data.size()) {
        uint32_t incl_len = read_u32(data.data() + off + 8, le);
        off += 16;
        if (!incl_len || off + incl_len > data.size()) break;
        packets.push_back({std::vector<uint8_t>(data.begin()+off, data.begin()+off+incl_len)});
        off += incl_len;
    }
    return !packets.empty();
}

static bool parse_pcapng(const std::vector<uint8_t>& data, std::vector<Packet>& packets) {
    if (data.size() < 12 || read_u32(data.data(), true) != 0x0a0d0d0a) return false;

    size_t off = 0;
    bool le = true;
    while (off + 12 <= data.size()) {
        uint32_t btype = read_u32(data.data() + off, le);
        uint32_t blen  = read_u32(data.data() + off + 4, le);
        if (blen < 12 || off + blen > data.size()) break;

        const uint8_t* blk = data.data() + off;
        if (btype == 0x0a0d0d0a && blen >= 28) {         // SHB: detect byte order
            if (read_u32(blk + 8, true)  == 0x1a2b3c4d) le = true;
            if (read_u32(blk + 8, false) == 0x1a2b3c4d) le = false;
        } else if (btype == 0x00000006 && blen >= 32) {  // EPB
            uint32_t ts_hi   = read_u32(blk + 12, le);
            uint32_t ts_lo   = read_u32(blk + 16, le);
            uint64_t ts_us   = ((uint64_t)ts_hi << 32) | ts_lo;
            uint32_t cap_len = read_u32(blk + 20, le);
            size_t   poff    = off + 28;
            if (cap_len && poff + cap_len <= off + blen - 4) {
                Packet pkt;
                pkt.bytes.assign(data.begin()+poff, data.begin()+poff+cap_len);
                pkt.timestamp_us = ts_us;
                packets.push_back(std::move(pkt));
            }
        } else if (btype == 0x00000003 && blen >= 20) {  // SPB
            uint32_t cap_len = read_u32(blk + 8, le);
            size_t   poff    = off + 12;
            if (cap_len && poff + cap_len <= off + blen - 4)
                packets.push_back({std::vector<uint8_t>(data.begin()+poff, data.begin()+poff+cap_len)});
        }
        off += blen;
    }
    return !packets.empty();
}

static bool parse_hex_dump(const std::vector<uint8_t>& data, std::vector<Packet>& packets) {
    auto hex_val = [](char c) -> int {
        if (c>='0'&&c<='9') return c-'0';
        if (c>='a'&&c<='f') return c-'a'+10;
        if (c>='A'&&c<='F') return c-'A'+10;
        return -1;
    };

    std::string txt(data.begin(), data.end());
    std::vector<uint8_t> cur;
    size_t pos = 0;
    while (pos < txt.size()) {
        size_t end = txt.find('\n', pos);
        if (end == std::string::npos) end = txt.size();
        std::string line = txt.substr(pos, end - pos);
        pos = end + 1;

        size_t scan = line.find_first_of("0123456789abcdefABCDEF");
        if (scan == std::string::npos) continue;

        if (line.size() >= scan + 4 &&
            std::isxdigit((unsigned char)line[scan]) &&
            std::isxdigit((unsigned char)line[scan+1]) &&
            std::isxdigit((unsigned char)line[scan+2]) &&
            std::isxdigit((unsigned char)line[scan+3])) {
            unsigned ov = 0;
            for (int i = 0; i < 4; ++i) ov = (ov << 4) | (unsigned)hex_val(line[scan+i]);
            if (ov == 0 && !cur.empty()) { packets.push_back({cur}); cur.clear(); }
            scan += 4;
        }
        while (scan + 1 < line.size()) {
            while (scan < line.size() && !std::isxdigit((unsigned char)line[scan])) ++scan;
            if (scan + 1 >= line.size()) break;
            int hi = hex_val(line[scan]), lo = hex_val(line[scan+1]);
            if (hi < 0 || lo < 0) break;
            cur.push_back((uint8_t)((hi << 4) | lo));
            scan += 2;
        }
    }
    if (!cur.empty()) packets.push_back({cur});
    return !packets.empty();
}

static bool load_packets(const std::string& path, std::vector<Packet>& packets) {
    std::vector<uint8_t> data;
    if (!read_file(path, data)) return false;
    if (parse_pcapng(data, packets)) return true;
    packets.clear();
    if (parse_pcap(data, packets)) return true;
    packets.clear();
    if (parse_hex_dump(data, packets)) return true;
    packets.clear();
    return false;
}

// ---------------------------------------------------------------------------
// GigaACE frame helpers
// ---------------------------------------------------------------------------
static bool is_gigaace(const std::vector<uint8_t>& b) {
    if (b.size() < 27) return false;
    uint16_t et = (uint16_t)((b[12] << 8) | b[13]);
    return et == 0x04EE;
}

static bool decode_to_float(const Packet& pkt, int channels, int mode,
                              std::vector<float>& out) {
    const auto& b = pkt.bytes;
    if (!is_gigaace(b)) return false;

    out.assign((size_t)channels, 0.0f);
    size_t max_ch = std::min<size_t>((b.size() - 24) / 3, (size_t)channels);
    for (size_t ch = 0; ch < max_ch; ++ch)
        out[ch] = decode_sample_mode(b.data() + 24 + ch * 3, mode);
    return true;
}

// ---------------------------------------------------------------------------
// Probe mode: print raw bytes + all decode interpretations
// ---------------------------------------------------------------------------
static void do_probe(const std::vector<Packet>& packets, int max_frames, int n_ch) {
    static const int kShowCh = 4;
    int shown = 0;

    for (const auto& pkt : packets) {
        const auto& b = pkt.bytes;
        if (!is_gigaace(b)) continue;

        bool broadcast = (b[0]==0xFF && b[1]==0xFF && b[2]==0xFF &&
                          b[3]==0xFF && b[4]==0xFF && b[5]==0xFF);

        std::printf("=== Frame %d ===%s\n", shown, broadcast ? "" : "  [non-broadcast dst]");
        std::printf("dst %02X:%02X:%02X:%02X:%02X:%02X  src %02X:%02X:%02X:%02X:%02X:%02X\n",
                    b[0],b[1],b[2],b[3],b[4],b[5],
                    b[6],b[7],b[8],b[9],b[10],b[11]);
        std::printf("counter %u  stream_type 0x%02X  payload_bytes %zu\n",
                    b[19]&0x1F, b[20], b.size()-24);

        size_t ch_avail = std::min<size_t>((b.size()-24)/3, (size_t)kShowCh);
        for (size_t ch = 0; ch < ch_avail; ++ch) {
            const uint8_t* p = b.data() + 24 + ch*3;
            std::printf("  ch%zu [%02X %02X %02X]", ch, p[0], p[1], p[2]);
            for (int m = 0; m <= 4; ++m)
                std::printf("  %s:%+.4f", kDecodeModeNames[m], decode_sample_mode(p, m));
            std::printf("\n");
        }
        std::printf("\n");

        if (++shown >= max_frames) break;
    }

    if (shown == 0)
        std::printf("No GigaACE frames (EtherType 0x04EE) found in capture.\n");
    else
        std::printf("Probed %d frames. Compare be-raw column values to expected 440 Hz sine amplitude.\n", shown);
}

// ---------------------------------------------------------------------------
// WAV output
// ---------------------------------------------------------------------------
static void write_u16_le(std::ofstream& f, uint16_t v) { f.write((char*)&v, 2); }
static void write_u32_le(std::ofstream& f, uint32_t v) { f.write((char*)&v, 4); }

static void do_write_wav(const std::vector<Packet>& packets, const std::string& path,
                          int channels, double sample_rate, int mode) {
    int num_ch = std::min(channels, 32);

    // Count valid (deduplicated) frames and detect rate from pcapng timestamps
    size_t n_frames = 0;
    uint64_t first_ts = 0, last_ts = 0;
    bool has_ts = false;
    int last_count_counter = -1;
    for (const auto& pkt : packets) {
        if (!is_gigaace(pkt.bytes)) continue;
        if (pkt.bytes.size() > 19) {
            int counter = pkt.bytes[19] & 0x1f;
            if (counter == last_count_counter) continue;
            last_count_counter = counter;
        }
        if (pkt.timestamp_us) {
            if (!has_ts) { first_ts = pkt.timestamp_us; has_ts = true; }
            last_ts = pkt.timestamp_us;
        }
        ++n_frames;
    }

    if (has_ts && last_ts > first_ts && n_frames > 1) {
        double span_sec = (double)(last_ts - first_ts) / 1e6;
        double detected = (double)(n_frames - 1) / span_sec;
        std::cout << "Detected frame rate: " << (int)std::round(detected) << " fps"
                  << "  (span=" << span_sec << "s over " << n_frames << " frames)\n";
        // Override only if detected rate differs significantly from declared rate
        if (std::abs(detected - sample_rate) / sample_rate > 0.01)
            sample_rate = detected;
    }

    uint32_t data_size = (uint32_t)(n_frames * num_ch * 3);
    uint32_t byte_rate = (uint32_t)(sample_rate * num_ch * 3);
    uint16_t block_align = (uint16_t)(num_ch * 3);

    std::ofstream f(path, std::ios::binary);
    if (!f) { std::cerr << "Cannot create: " << path << "\n"; return; }

    // RIFF header
    f.write("RIFF", 4);
    write_u32_le(f, 36 + data_size);
    f.write("WAVE", 4);

    // fmt chunk
    f.write("fmt ", 4);
    write_u32_le(f, 16);
    write_u16_le(f, 1);                            // PCM
    write_u16_le(f, (uint16_t)num_ch);
    write_u32_le(f, (uint32_t)sample_rate);
    write_u32_le(f, byte_rate);
    write_u16_le(f, block_align);
    write_u16_le(f, 24);                           // bits per sample

    // data chunk
    f.write("data", 4);
    write_u32_le(f, data_size);

    int last_wav_counter = -1;
    for (const auto& pkt : packets) {
        const auto& b = pkt.bytes;
        if (!is_gigaace(b)) continue;
        if (b.size() > 19) {
            int counter = b[19] & 0x1f;
            if (counter == last_wav_counter) continue;
            last_wav_counter = counter;
        }
        for (int ch = 0; ch < num_ch; ++ch) {
            size_t off = 24 + ch * 3;
            float v = (off + 3 <= b.size()) ? decode_sample_mode(b.data() + off, mode) : 0.0f;
            if (v >  0.9999f) v =  0.9999f;
            if (v < -1.0f)   v = -1.0f;
            int32_t pcm = (int32_t)(v * 8388608.0f);
            uint8_t s[3] = { (uint8_t)(pcm & 0xFF),
                              (uint8_t)((pcm >> 8) & 0xFF),
                              (uint8_t)((pcm >> 16) & 0xFF) };
            f.write((char*)s, 3);
        }
    }

    std::cout << "Wrote " << n_frames << " frames, " << num_ch << " ch, mode="
              << kDecodeModeNames[mode] << " -> " << path << "\n";
}

// ---------------------------------------------------------------------------
// CLI
// ---------------------------------------------------------------------------
static void usage() {
    std::cout <<
        "GigaAceReplay - replay recorded GigaACE frames into the ASIO bridge\n\n"
        "Usage:\n"
        "  GigaAceReplay.exe --input capture.pcapng [options]\n\n"
        "Decode:\n"
        "  --decode-mode N    0=be-raw(default) 1=le-raw 2=nibble 3=swizzle 4=nib-byte\n\n"
        "Diagnostic:\n"
        "  --probe [N]        Print raw bytes + all decode modes for first N frames (default 30)\n"
        "  --wav FILE         Write decoded audio to 24-bit PCM WAV\n"
        "  --wav-channels N   Channels in WAV (default 8, max 32)\n\n"
        "Replay:\n"
        "  --channels N       Channel count (default 128)\n"
        "  --rate R           Sample rate (default 96000)\n"
        "  --loop             Replay continuously\n"
        "  --quiet            Suppress progress output\n"
        "  --shared-memory S  Named mapping (default Local\\\\GigaACEVirtualDevice)\n\n"
        "Supported inputs: pcap, pcapng, Wireshark-style hex dump.\n";
}

static bool parse_args(int argc, char** argv, ReplayOptions& o) {
    auto next_val = [&](int& i, const char* name) -> const char* {
        if (i + 1 >= argc) { std::cerr << name << " needs a value.\n"; return nullptr; }
        return argv[++i];
    };
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--input" || arg == "-i") {
            auto v = next_val(i, arg.c_str()); if (!v) return false;
            o.input_path = v;
        } else if (arg == "--channels" || arg == "-c") {
            auto v = next_val(i, arg.c_str()); if (!v) return false;
            o.channels = std::max(1, std::min(128, std::atoi(v)));
        } else if (arg == "--rate" || arg == "-r") {
            auto v = next_val(i, arg.c_str()); if (!v) return false;
            o.sample_rate = std::atof(v);
        } else if (arg == "--shared-memory") {
            auto v = next_val(i, arg.c_str()); if (!v) return false;
            o.shared_memory_name = v;
        } else if (arg == "--decode-mode") {
            auto v = next_val(i, arg.c_str()); if (!v) return false;
            o.decode_mode = std::max(0, std::min(4, std::atoi(v)));
        } else if (arg == "--probe") {
            o.probe = true;
            if (i + 1 < argc && std::isdigit((unsigned char)argv[i+1][0]))
                o.probe_frames = std::atoi(argv[++i]);
        } else if (arg == "--wav") {
            auto v = next_val(i, arg.c_str()); if (!v) return false;
            o.wav_output = v;
        } else if (arg == "--wav-channels") {
            auto v = next_val(i, arg.c_str()); if (!v) return false;
            o.wav_channels = std::max(1, std::min(32, std::atoi(v)));
        } else if (arg == "--loop") {
            o.loop = true;
        } else if (arg == "--quiet" || arg == "-q") {
            o.quiet = true;
        } else if (arg == "--help" || arg == "-h" || arg == "/?") {
            usage(); return false;
        } else {
            std::cerr << "Unknown argument: " << arg << "\n"; return false;
        }
    }
    if (o.input_path.empty()) { usage(); return false; }
    return true;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main(int argc, char** argv) {
    ReplayOptions o;
    if (!parse_args(argc, argv, o)) return 2;

    std::vector<Packet> packets;
    if (!load_packets(o.input_path, packets)) {
        std::cerr << "Could not read any packets from: " << o.input_path << "\n";
        return 1;
    }

    // Diagnostic: probe
    if (o.probe) {
        do_probe(packets, o.probe_frames, o.channels);
        if (o.wav_output.empty() && !o.loop) return 0;
    }

    // Diagnostic: WAV output
    if (!o.wav_output.empty()) {
        do_write_wav(packets, o.wav_output, o.wav_channels, o.sample_rate, o.decode_mode);
        if (!o.loop) return 0;
    }

    // Normal replay into shared memory
    GigaACESharedRing ring{};
    if (gigaace_shared_ring_create(o.shared_memory_name.c_str(),
                                   (uint32_t)o.channels, (uint32_t)o.ring_frames,
                                   o.sample_rate, &ring) != 0) {
        std::cerr << "Could not create shared memory: " << o.shared_memory_name << "\n";
        return 1;
    }
    gigaace_shared_ring_set_stream_state(&ring, GIGAACE_STREAM_STATE_RUNNING);

    if (!o.quiet) {
        std::cout << "Packets: " << packets.size()
                  << "  SHM: " << o.shared_memory_name
                  << "  ch: " << o.channels
                  << "  rate: " << o.sample_rate
                  << "  mode: " << kDecodeModeNames[o.decode_mode]
                  << "\nPress Ctrl+C to stop.\n";
    }

    // Set 1ms Windows timer resolution so sleep_until is accurate enough for ~10µs frame spacing.
    timeBeginPeriod(1);

    uint64_t decoded = 0, rejected = 0;
    std::vector<float> frame_samples;
    using clock = std::chrono::steady_clock;

    do {
        // Anchor wall-clock time to the first packet's pcapng timestamp each loop iteration.
        bool ts_anchored = false;
        uint64_t first_ts_us = 0;
        clock::time_point wall_start;
        int last_counter = -1; // GigaACE 5-bit frame counter, -1 = none seen yet

        for (const auto& pkt : packets) {
            // Deduplicate: skip frames with the same 5-bit counter as the previous frame.
            if (pkt.bytes.size() > 19) {
                int counter = pkt.bytes[19] & 0x1f;
                if (counter == last_counter) { ++rejected; continue; }
                last_counter = counter;
            }

            if (!decode_to_float(pkt, o.channels, o.decode_mode, frame_samples)) {
                ++rejected;
                continue;
            }

            // Pace using the original capture timestamps.
            if (pkt.timestamp_us > 0) {
                if (!ts_anchored) {
                    first_ts_us = pkt.timestamp_us;
                    wall_start  = clock::now();
                    ts_anchored = true;
                } else {
                    auto target = wall_start +
                                  std::chrono::microseconds(pkt.timestamp_us - first_ts_us);
                    std::this_thread::sleep_until(target);
                }
            }

            gigaace_shared_ring_write_interleaved(&ring, frame_samples.data(), 1);
            ++decoded;
        }

        if (!o.quiet)
            std::cout << "\rDecoded: " << decoded << "  Rejected: " << rejected << "    " << std::flush;
    } while (o.loop);

    timeEndPeriod(1);

    if (!o.quiet) std::cout << "\nDone.\n";

    gigaace_shared_ring_set_stream_state(&ring, GIGAACE_STREAM_STATE_STOPPED);
    gigaace_shared_ring_close(&ring);
    return 0;
}
