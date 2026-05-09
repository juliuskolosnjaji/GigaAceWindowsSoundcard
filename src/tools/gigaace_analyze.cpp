#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>
#include <windows.h>

enum DecodeMode { DM_BE_RAW = 0, DM_LE_RAW = 1, DM_NIBBLE = 2, DM_SWIZZLE = 3, DM_NIB_BYTE = 4 };
static const char* kDecodeModeNames[] = { "be-raw", "le-raw", "nibble", "swizzle", "nib-byte" };

struct Options {
    std::wstring input_path;
    double tone_hz = 440.0;
    double sample_rate = 96000.0;
    int slots = 128;
    int top = 20;
    uint64_t max_audio_frames = 96000;
    bool byte_scan = false;
};

struct Packet {
    std::vector<uint8_t> bytes;
    uint64_t timestamp_us = 0;
};

struct ToneStat {
    uint64_t n = 0;
    double sum = 0.0;
    double sum2 = 0.0;
    double sin_sum = 0.0;
    double cos_sum = 0.0;
    double abs_peak = 0.0;
};

struct Candidate {
    int offset = 0;
    int slot = 0;
    int mode = 0;
    double score = 0.0;
    double rms = 0.0;
    double peak = 0.0;
    uint64_t n = 0;
};

static uint32_t read_u32_buf(const uint8_t* p, bool le) {
    if (le) return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static uint32_t byte_rev24(uint32_t v) {
    return ((v >> 16) & 0xFF) | (v & 0xFF00u) | ((v & 0xFFu) << 16);
}

static uint32_t nibble_swap24(uint32_t v) {
    return ((v & 0xF0F0F0u) >> 4) | ((v & 0x0F0F0Fu) << 4);
}

static int32_t sign_ext24(uint32_t v) {
    v &= 0x00FFFFFFu;
    if (v & 0x800000u) v |= 0xFF000000u;
    return (int32_t)v;
}

static double decode_sample_mode(const uint8_t* p, int mode) {
    uint32_t v = ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | (uint32_t)p[2];
    switch (mode) {
    case DM_LE_RAW:   v = byte_rev24(v); break;
    case DM_NIBBLE:   v = nibble_swap24(v); break;
    case DM_SWIZZLE:  v = nibble_swap24(byte_rev24(v)); break;
    case DM_NIB_BYTE: v = byte_rev24(nibble_swap24(v)); break;
    default: break;
    }
    return (double)sign_ext24(v) / 8388608.0;
}

static std::string mac_string(const uint8_t* p) {
    std::ostringstream os;
    os << std::hex << std::setfill('0');
    for (int i = 0; i < 6; ++i) {
        if (i) os << ':';
        os << std::setw(2) << (unsigned)p[i];
    }
    return os.str();
}

static std::string hex16(uint16_t v) {
    std::ostringstream os;
    os << "0x" << std::hex << std::setw(4) << std::setfill('0') << v;
    return os.str();
}

static std::string hex_bytes(const uint8_t* p, size_t n) {
    std::ostringstream os;
    os << std::hex << std::setfill('0');
    for (size_t i = 0; i < n; ++i) {
        if (i) os << ' ';
        os << std::setw(2) << (unsigned)p[i];
    }
    return os.str();
}

static std::string narrow(const std::wstring& text) {
    if (text.empty()) return {};
    int needed = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return {};
    std::string out((size_t)needed - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, out.data(), needed, nullptr, nullptr);
    return out;
}

static std::filesystem::path long_windows_path(const std::wstring& text) {
    if (text.rfind(LR"(\\?\)", 0) == 0)
        return std::filesystem::path(text);
    if (text.rfind(LR"(\\)", 0) == 0)
        return std::filesystem::path(LR"(\\?\UNC\)" + text.substr(2));
    if (text.size() >= 3 && text[1] == L':' && (text[2] == L'\\' || text[2] == L'/'))
        return std::filesystem::path(LR"(\\?\)" + text);
    return std::filesystem::path(text);
}

static bool read_exact(std::ifstream& f, uint8_t* dst, size_t n) {
    f.read((char*)dst, (std::streamsize)n);
    return (size_t)f.gcount() == n;
}

static bool looks_like_gigaace(uint16_t et) {
    return et == 0x04ee || et == 0x00e1;
}

class Analyzer {
public:
    explicit Analyzer(Options options) : opt(std::move(options)) {
        int count = opt.byte_scan ? (opt.slots * 3) : opt.slots;
        stats.resize((size_t)count * 5);
    }

    bool run() {
        std::ifstream f(long_windows_path(opt.input_path), std::ios::binary);
        if (!f) {
            std::cerr << "Cannot open: " << narrow(opt.input_path) << "\n";
            return false;
        }

        uint8_t first4[4] = {};
        if (!read_exact(f, first4, 4)) return false;
        f.seekg(0);

        if (first4[0] == 0x0a && first4[1] == 0x0d && first4[2] == 0x0d && first4[3] == 0x0a)
            return run_pcapng(f);

        std::cerr << "Only pcapng is supported by this analyzer build.\n";
        return false;
    }

    void print() {
        std::cout << "Capture: " << narrow(opt.input_path) << "\n";
        std::cout << "Packets: " << packets_total << "\n";
        std::cout << "GigaACE-like packets: " << gigaace_total << "\n";
        if (first_ts && last_ts > first_ts) {
            double seconds = (double)(last_ts - first_ts) / 1000000.0;
            std::cout << "Timestamp span: " << std::fixed << std::setprecision(3) << seconds << " s\n";
            std::cout << "GigaACE frame rate estimate: " << std::setprecision(1) << ((double)(gigaace_total - 1) / seconds) << " fps\n";
        }

        print_map("EtherTypes", ethertype_counts, true);
        print_map("Directions", direction_counts, false);
        print_map("Header signatures", header_counts, false);

        if (!first_frames.empty()) {
            std::cout << "\nFirst frames:\n";
            for (const auto& line : first_frames)
                std::cout << "  " << line << "\n";
        }

        print_tone_candidates();
    }

private:
    bool run_pcapng(std::ifstream& f) {
        bool le = true;
        while (f) {
            uint8_t hdr[8] = {};
            if (!read_exact(f, hdr, sizeof(hdr))) break;
            uint32_t type = read_u32_buf(hdr, le);
            uint32_t blen = read_u32_buf(hdr + 4, le);
            if (blen < 12) break;

            std::vector<uint8_t> rest(blen - 8);
            if (!read_exact(f, rest.data(), rest.size())) break;
            const uint8_t* blk = rest.data();

            if (type == 0x0a0d0d0a && blen >= 28) {
                if (read_u32_buf(blk, true) == 0x1a2b3c4d) le = true;
                else if (read_u32_buf(blk, false) == 0x1a2b3c4d) le = false;
            } else if (type == 0x00000006 && blen >= 32) {
                uint32_t ts_hi = read_u32_buf(blk + 4, le);
                uint32_t ts_lo = read_u32_buf(blk + 8, le);
                uint32_t cap_len = read_u32_buf(blk + 12, le);
                if (cap_len && 20 + cap_len <= rest.size()) {
                    Packet p;
                    p.timestamp_us = ((uint64_t)ts_hi << 32) | ts_lo;
                    p.bytes.assign(rest.begin() + 20, rest.begin() + 20 + cap_len);
                    process_packet(p);
                }
            }
        }
        return true;
    }

    void process_packet(const Packet& pkt) {
        ++packets_total;
        const auto& b = pkt.bytes;
        if (b.size() < 14) return;

        uint16_t et = (uint16_t)(((uint16_t)b[12] << 8) | b[13]);
        ethertype_counts[hex16(et)]++;
        std::string src = mac_string(b.data() + 6);
        std::string dst = mac_string(b.data());
        direction_counts[src + " -> " + dst]++;

        if (first_frames.size() < 24) {
            std::ostringstream os;
            os << "#" << packets_total << " len=" << b.size()
               << " " << src << " -> " << dst
               << " et=" << hex16(et)
               << " head=" << hex_bytes(b.data(), std::min<size_t>(48, b.size()));
            first_frames.push_back(os.str());
        }

        if (!looks_like_gigaace(et) || b.size() < 27)
            return;

        ++gigaace_total;
        if (pkt.timestamp_us) {
            if (!first_ts) first_ts = pkt.timestamp_us;
            last_ts = pkt.timestamp_us;
        }

        std::string sig = hex16(et) + " " + src + " " + hex_bytes(b.data() + 14, std::min<size_t>(12, b.size() - 14));
        header_counts[sig]++;

        if (audio_frames_scanned >= opt.max_audio_frames)
            return;

        scan_tone(b);
        ++audio_frames_scanned;
    }

    void scan_tone(const std::vector<uint8_t>& b) {
        double phase = 2.0 * 3.14159265358979323846 * opt.tone_hz * (double)audio_frames_scanned / opt.sample_rate;
        double s = std::sin(phase);
        double c = std::cos(phase);
        int scan_count = opt.byte_scan ? (opt.slots * 3) : opt.slots;
        for (int i = 0; i < scan_count; ++i) {
            int off = 24 + (opt.byte_scan ? i : i * 3);
            if (off + 3 > (int)b.size()) break;
            for (int mode = 0; mode < 5; ++mode) {
                ToneStat& st = stats[(size_t)i * 5 + mode];
                double v = decode_sample_mode(b.data() + off, mode);
                st.n++;
                st.sum += v;
                st.sum2 += v * v;
                st.sin_sum += v * s;
                st.cos_sum += v * c;
                st.abs_peak = std::max(st.abs_peak, std::abs(v));
            }
        }
    }

    void print_map(const char* title, const std::map<std::string, uint64_t>& m, bool already_hex) {
        (void)already_hex;
        std::vector<std::pair<std::string, uint64_t>> rows(m.begin(), m.end());
        std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) { return b.second < a.second; });
        std::cout << "\n" << title << ":\n";
        for (size_t i = 0; i < rows.size() && i < 20; ++i)
            std::cout << "  " << std::setw(8) << rows[i].second << "  " << rows[i].first << "\n";
    }

    void print_tone_candidates() {
        if (!audio_frames_scanned) return;

        std::vector<Candidate> candidates;
        int scan_count = opt.byte_scan ? (opt.slots * 3) : opt.slots;
        for (int i = 0; i < scan_count; ++i) {
            int off = 24 + (opt.byte_scan ? i : i * 3);
            for (int mode = 0; mode < 5; ++mode) {
                const ToneStat& st = stats[(size_t)i * 5 + mode];
                if (st.n < 16) continue;
                double mean = st.sum / (double)st.n;
                double power = std::max(0.0, st.sum2 / (double)st.n - mean * mean);
                double rms = std::sqrt(power);
                double mag = 2.0 * std::sqrt(st.sin_sum * st.sin_sum + st.cos_sum * st.cos_sum) / (double)st.n;
                double score = rms > 1e-9 ? mag / rms : 0.0;
                if (rms > 1e-6 || st.abs_peak > 1e-5)
                    candidates.push_back({ off, opt.byte_scan ? i : i + 1, mode, score, rms, st.abs_peak, st.n });
            }
        }

        std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
            if (a.score != b.score) return a.score > b.score;
            return a.rms > b.rms;
        });

        std::cout << "\nTone candidates near " << opt.tone_hz << " Hz"
                  << " (scanned " << audio_frames_scanned << " frames at " << opt.sample_rate << " Hz):\n";
        std::cout << "  rank  slot/byte  offset  mode       score    rms       peak\n";
        for (int i = 0; i < (int)candidates.size() && i < opt.top; ++i) {
            const auto& c = candidates[(size_t)i];
            std::cout << "  " << std::setw(4) << (i + 1)
                      << "  " << std::setw(8) << c.slot
                      << "  " << std::setw(6) << c.offset
                      << "  " << std::setw(9) << kDecodeModeNames[c.mode]
                      << "  " << std::setw(7) << std::fixed << std::setprecision(3) << c.score
                      << "  " << std::setw(8) << std::setprecision(5) << c.rms
                      << "  " << std::setw(8) << std::setprecision(5) << c.peak
                      << "\n";
        }
    }

    Options opt;
    uint64_t packets_total = 0;
    uint64_t gigaace_total = 0;
    uint64_t audio_frames_scanned = 0;
    uint64_t first_ts = 0;
    uint64_t last_ts = 0;
    std::map<std::string, uint64_t> ethertype_counts;
    std::map<std::string, uint64_t> direction_counts;
    std::map<std::string, uint64_t> header_counts;
    std::vector<std::string> first_frames;
    std::vector<ToneStat> stats;
};

static void usage() {
    std::cout <<
        "GigaAceAnalyze - inspect GigaACE/SLink captures\n\n"
        "Usage:\n"
        "  GigaAceAnalyze.exe --input capture.pcapng [options]\n\n"
        "Options:\n"
        "  --tone HZ          Tone to correlate against (default 440)\n"
        "  --rate HZ          Sample/frame rate for tone correlation (default 96000)\n"
        "  --slots N          Linear 24-bit slots to scan from byte 24 (default 128)\n"
        "  --byte-scan        Scan every byte offset instead of 3-byte aligned slots\n"
        "  --max-frames N     Audio frames to scan for tone (default 96000)\n"
        "  --top N            Candidate rows to print (default 20)\n";
}

static bool parse_args(int argc, wchar_t** argv, Options& o) {
    auto next = [&](int& i, const wchar_t* name) -> const wchar_t* {
        if (i + 1 >= argc) {
            std::cerr << narrow(name) << " needs a value.\n";
            return nullptr;
        }
        return argv[++i];
    };

    for (int i = 1; i < argc; ++i) {
        std::wstring arg = argv[i];
        if (arg == L"--input" || arg == L"-i") {
            auto v = next(i, arg.c_str()); if (!v) return false;
            o.input_path = v;
        } else if (arg == L"--tone") {
            auto v = next(i, arg.c_str()); if (!v) return false;
            o.tone_hz = std::wcstod(v, nullptr);
        } else if (arg == L"--rate") {
            auto v = next(i, arg.c_str()); if (!v) return false;
            o.sample_rate = std::wcstod(v, nullptr);
        } else if (arg == L"--slots") {
            auto v = next(i, arg.c_str()); if (!v) return false;
            o.slots = std::max(1, std::min(416, (int)std::wcstol(v, nullptr, 10)));
        } else if (arg == L"--max-frames") {
            auto v = next(i, arg.c_str()); if (!v) return false;
            o.max_audio_frames = std::max<uint64_t>(1, (uint64_t)std::wcstoull(v, nullptr, 10));
        } else if (arg == L"--top") {
            auto v = next(i, arg.c_str()); if (!v) return false;
            o.top = std::max(1, std::min(100, (int)std::wcstol(v, nullptr, 10)));
        } else if (arg == L"--byte-scan") {
            o.byte_scan = true;
        } else if (arg == L"--help" || arg == L"-h" || arg == L"/?") {
            usage();
            return false;
        } else {
            std::cerr << "Unknown argument: " << narrow(arg) << "\n";
            return false;
        }
    }

    if (o.input_path.empty()) {
        usage();
        return false;
    }
    return true;
}

int wmain(int argc, wchar_t** argv) {
    Options options;
    if (!parse_args(argc, argv, options))
        return 2;

    Analyzer analyzer(options);
    if (!analyzer.run())
        return 1;
    analyzer.print();
    return 0;
}
