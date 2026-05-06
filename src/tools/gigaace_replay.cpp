#include "ace_decoder.h"
#include "shared_bridge.h"

#include <windows.h>
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

struct ReplayOptions {
    std::string input_path;
    std::string shared_memory_name = kDefaultSharedMemory;
    int channels = 64;
    double sample_rate = 48000.0;
    int ring_frames = 96000;
    bool loop = false;
    bool quiet = false;
};

struct Packet {
    std::vector<uint8_t> bytes;
};

static uint16_t read_u16(const uint8_t* p, bool le) {
    if (le)
        return static_cast<uint16_t>(p[0] | (p[1] << 8));
    return static_cast<uint16_t>((p[0] << 8) | p[1]);
}

static uint32_t read_u32(const uint8_t* p, bool le) {
    if (le)
        return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static bool read_file(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return false;
    file.seekg(0, std::ios::end);
    auto size = file.tellg();
    if (size < 0)
        return false;
    file.seekg(0, std::ios::beg);
    out.resize(static_cast<size_t>(size));
    if (!out.empty())
        file.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(out.size()));
    return true;
}

static bool parse_pcap(const std::vector<uint8_t>& data, std::vector<Packet>& packets) {
    if (data.size() < 24)
        return false;

    bool le = true;
    if ((data[0] == 0xa1 && data[1] == 0xb2 && data[2] == 0xc3 && data[3] == 0xd4) ||
        (data[0] == 0xa1 && data[1] == 0xb2 && data[2] == 0x3c && data[3] == 0x4d)) {
        le = false;
    } else if ((data[0] == 0xd4 && data[1] == 0xc3 && data[2] == 0xb2 && data[3] == 0xa1) ||
               (data[0] == 0x4d && data[1] == 0x3c && data[2] == 0xb2 && data[3] == 0xa1)) {
        le = true;
    } else {
        return false;
    }

    size_t offset = 24;
    while (offset + 16 <= data.size()) {
        uint32_t incl_len = read_u32(data.data() + offset + 8, le);
        offset += 16;
        if (incl_len == 0 || offset + incl_len > data.size())
            break;
        packets.push_back(Packet{std::vector<uint8_t>(data.begin() + offset, data.begin() + offset + incl_len)});
        offset += incl_len;
    }

    return !packets.empty();
}

static bool parse_pcapng(const std::vector<uint8_t>& data, std::vector<Packet>& packets) {
    if (data.size() < 12 || read_u32(data.data(), true) != 0x0a0d0d0a)
        return false;

    size_t offset = 0;
    bool le = true;
    while (offset + 12 <= data.size()) {
        uint32_t block_type = read_u32(data.data() + offset, le);
        uint32_t block_len = read_u32(data.data() + offset + 4, le);
        if (block_len < 12 || offset + block_len > data.size())
            break;

        const uint8_t* block = data.data() + offset;
        if (block_type == 0x0a0d0d0a && block_len >= 28) {
            uint32_t bom_le = read_u32(block + 8, true);
            uint32_t bom_be = read_u32(block + 8, false);
            if (bom_le == 0x1a2b3c4d)
                le = true;
            else if (bom_be == 0x1a2b3c4d)
                le = false;
        } else if (block_type == 0x00000006 && block_len >= 32) {
            uint32_t cap_len = read_u32(block + 20, le);
            size_t packet_offset = offset + 28;
            if (cap_len > 0 && packet_offset + cap_len <= offset + block_len - 4) {
                packets.push_back(Packet{std::vector<uint8_t>(data.begin() + packet_offset,
                                                              data.begin() + packet_offset + cap_len)});
            }
        } else if (block_type == 0x00000003 && block_len >= 20) {
            uint32_t cap_len = read_u32(block + 8, le);
            size_t packet_offset = offset + 12;
            if (cap_len > 0 && packet_offset + cap_len <= offset + block_len - 4) {
                packets.push_back(Packet{std::vector<uint8_t>(data.begin() + packet_offset,
                                                              data.begin() + packet_offset + cap_len)});
            }
        }

        offset += block_len;
    }

    return !packets.empty();
}

static int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool parse_hex_dump(const std::vector<uint8_t>& data, std::vector<Packet>& packets) {
    std::string text(data.begin(), data.end());
    std::vector<uint8_t> current;

    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos)
            end = text.size();
        std::string line = text.substr(pos, end - pos);
        pos = end + 1;

        size_t first_hex = line.find_first_of("0123456789abcdefABCDEF");
        if (first_hex == std::string::npos)
            continue;

        size_t scan = first_hex;
        if (line.size() >= scan + 4 && std::isxdigit((unsigned char)line[scan]) &&
            std::isxdigit((unsigned char)line[scan + 1]) &&
            std::isxdigit((unsigned char)line[scan + 2]) &&
            std::isxdigit((unsigned char)line[scan + 3])) {
            unsigned offset_value = 0;
            for (int i = 0; i < 4; ++i)
                offset_value = (offset_value << 4) | (unsigned)hex_value(line[scan + i]);
            scan += 4;
            if (offset_value == 0 && !current.empty()) {
                packets.push_back(Packet{current});
                current.clear();
            }
        }

        while (scan + 1 < line.size()) {
            while (scan < line.size() && !std::isxdigit((unsigned char)line[scan]))
                ++scan;
            if (scan + 1 >= line.size())
                break;
            int hi = hex_value(line[scan]);
            int lo = hex_value(line[scan + 1]);
            if (hi < 0 || lo < 0)
                break;
            current.push_back(static_cast<uint8_t>((hi << 4) | lo));
            scan += 2;
        }
    }

    if (!current.empty())
        packets.push_back(Packet{current});

    return !packets.empty();
}

static bool load_packets(const std::string& path, std::vector<Packet>& packets) {
    std::vector<uint8_t> data;
    if (!read_file(path, data))
        return false;

    if (parse_pcapng(data, packets))
        return true;
    packets.clear();
    if (parse_pcap(data, packets))
        return true;
    packets.clear();
    if (parse_hex_dump(data, packets))
        return true;
    packets.clear();
    return false;
}

static void usage() {
    std::cout
        << "GigaAceReplay - replay recorded GigaACE frames into the ASIO bridge\n\n"
        << "Usage:\n"
        << "  GigaAceReplay.exe --input capture.pcapng [--loop] [--channels 64]\n\n"
        << "Supported inputs: pcap, pcapng, Wireshark-style hex dump text.\n";
}

static bool parse_args(int argc, char** argv, ReplayOptions& options) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto require_value = [&](const char* name) -> const char* {
            if (i + 1 >= argc) {
                std::cerr << name << " needs a value.\n";
                return nullptr;
            }
            return argv[++i];
        };

        if (arg == "--input" || arg == "-i") {
            const char* value = require_value(arg.c_str());
            if (!value) return false;
            options.input_path = value;
        } else if (arg == "--channels" || arg == "-c") {
            const char* value = require_value(arg.c_str());
            if (!value) return false;
            options.channels = std::max(1, std::min(128, std::atoi(value)));
        } else if (arg == "--rate" || arg == "-r") {
            const char* value = require_value(arg.c_str());
            if (!value) return false;
            options.sample_rate = std::atof(value);
        } else if (arg == "--shared-memory") {
            const char* value = require_value(arg.c_str());
            if (!value) return false;
            options.shared_memory_name = value;
        } else if (arg == "--loop") {
            options.loop = true;
        } else if (arg == "--quiet" || arg == "-q") {
            options.quiet = true;
        } else if (arg == "--help" || arg == "-h" || arg == "/?") {
            usage();
            return false;
        } else {
            std::cerr << "Unknown argument: " << arg << "\n";
            return false;
        }
    }

    if (options.input_path.empty()) {
        usage();
        return false;
    }
    return true;
}

static bool decode_to_float(const Packet& packet, int channels, std::vector<float>& out) {
    GigaACEFrame frame{};
    int ret = gigaace_decode_frame(packet.bytes.data(), packet.bytes.size(), channels,
                                   frame.dst_mac, frame.src_mac, &frame.ethertype,
                                   &frame.counter, &frame.stream_type, &frame.channel_count,
                                   frame.pcm24);
    if (ret < 0) {
        return false;
    }

    out.assign(static_cast<size_t>(channels), 0.0f);
    size_t count = std::min<size_t>(frame.channel_count, static_cast<size_t>(channels));
    for (size_t ch = 0; ch < count; ++ch)
        out[ch] = gigaace_pcm24_to_float(frame.pcm24[ch]);
    return true;
}

int main(int argc, char** argv) {
    ReplayOptions options;
    if (!parse_args(argc, argv, options))
        return 2;

    std::vector<Packet> packets;
    if (!load_packets(options.input_path, packets)) {
        std::cerr << "Could not read any packets from: " << options.input_path << "\n";
        return 1;
    }

    GigaACESharedRing ring{};
    if (gigaace_shared_ring_create(options.shared_memory_name.c_str(),
                                   static_cast<uint32_t>(options.channels),
                                   static_cast<uint32_t>(options.ring_frames),
                                   options.sample_rate,
                                   &ring) != 0) {
        std::cerr << "Could not create shared memory ring: " << options.shared_memory_name << "\n";
        return 1;
    }

    gigaace_shared_ring_set_stream_state(&ring, GIGAACE_STREAM_STATE_RUNNING);

    if (!options.quiet) {
        std::cout << "Loaded packets: " << packets.size() << "\n"
                  << "Shared memory: " << options.shared_memory_name << "\n"
                  << "Channels: " << options.channels << ", sample rate: " << options.sample_rate << "\n"
                  << "Press Ctrl+C to stop.\n";
    }

    uint64_t decoded = 0;
    uint64_t rejected = 0;
    std::vector<float> frame_samples;
    const int batch_frames = 96;
    const auto batch_sleep = std::chrono::duration<double>((double)batch_frames / options.sample_rate);

    do {
        int batch_count = 0;
        for (const auto& packet : packets) {
            if (decode_to_float(packet, options.channels, frame_samples)) {
                gigaace_shared_ring_write_interleaved(&ring, frame_samples.data(), 1);
                ++decoded;
                ++batch_count;
            } else {
                ++rejected;
            }

            if (batch_count >= batch_frames) {
                std::this_thread::sleep_for(batch_sleep);
                batch_count = 0;
            }
        }

        if (!options.quiet) {
            std::cout << "\rDecoded: " << decoded << "  Rejected: " << rejected << "       " << std::flush;
        }
    } while (options.loop);

    if (!options.quiet)
        std::cout << "\nDone.\n";

    gigaace_shared_ring_set_stream_state(&ring, GIGAACE_STREAM_STATE_STOPPED);
    gigaace_shared_ring_close(&ring);
    return 0;
}
