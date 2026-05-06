#include "decoder.h"
#include "ace_decoder.h"
#include <cmath>
#include <cstring>
#include <algorithm>

bool GigaACEDecoder::decode(const uint8_t* data, size_t len, int max_channels, GigaACEFrame& out) {
    uint16_t ethertype;
    uint8_t counter, stream_type;
    size_t channel_count;

    int ret = gigaace_decode_frame(data, len, max_channels,
                                   out.dst_mac, out.src_mac,
                                   &ethertype, &counter, &stream_type,
                                   &channel_count, out.pcm24);
    if (ret < 0)
        return false;

    out.ethertype = ethertype;
    out.counter = counter;
    out.stream_type = stream_type;
    out.channel_count = channel_count;
    return true;
}

float GigaACEDecoder::pcm24ToFloat(int32_t sample) {
    return gigaace_pcm24_to_float(sample);
}

int32_t GigaACEDecoder::floatToPCM24(float sample) {
    return gigaace_float_to_pcm24(sample);
}

uint32_t GigaACEDecoder::unswizzle24(uint32_t sample) {
    uint32_t swapped = ((sample & 0xff0000u) >> 16) |
                       ( sample & 0x00ff00u) |
                       ((sample & 0x0000ffu) << 16);
    return (((swapped & 0xf0f0f0u) >> 4) | ((swapped & 0x0f0f0fu) << 4)) & 0x00ffffffu;
}

uint32_t GigaACEDecoder::swizzle24(uint32_t sample) {
    uint32_t swapped = ((sample & 0xf0f0f0u) >> 4) |
                       ((sample & 0x0f0f0fu) << 4);
    return (((swapped & 0xff0000u) >> 16) |
            ( swapped & 0x00ff00u) |
            ((swapped & 0x0000ffu) << 16)) & 0x00ffffffu;
}

std::vector<uint8_t> GigaACEDemoFrameFactory::makeFrame(uint8_t counter, int channel_count, double phase) {
    int sanitized = std::max(1, channel_count);
    size_t total_size = GIGAACE_AUDIO_BASE_OFFSET + sanitized * GIGAACE_BYTES_PER_SAMPLE;
    std::vector<uint8_t> bytes(total_size, 0);

    for (int i = 0; i < 6; ++i)
        bytes[i] = 0xff;

    bytes[6] = 0x00; bytes[7] = 0x04; bytes[8] = 0xc4;
    bytes[9] = 0x0b; bytes[10] = 0xa5; bytes[11] = 0x28;
    bytes[12] = 0x04; bytes[13] = 0xee;
    bytes[GIGAACE_COUNTER_OFFSET] = counter & 0x1f;
    bytes[GIGAACE_STREAM_TYPE_OFFSET] = 0x02;

    for (int ch = 0; ch < sanitized; ++ch) {
        double amplitude = 0.15 + (ch % 8) * 0.04;
        double frequency = (double)((ch % 16) + 1);
        double value = std::sin((phase * frequency * 2.0 * 3.14159265358979) + ch * 0.12) * amplitude;

        uint32_t pcm24 = (uint32_t)(value * 8388608.0) & 0x00ffffffu;
        uint32_t packed = GigaACEDecoder::swizzle24(pcm24);

        size_t base = GIGAACE_AUDIO_BASE_OFFSET + ch * GIGAACE_BYTES_PER_SAMPLE;
        bytes[base]     = (uint8_t)((packed >> 16) & 0xff);
        bytes[base + 1] = (uint8_t)((packed >> 8) & 0xff);
        bytes[base + 2] = (uint8_t)(packed & 0xff);
    }

    return bytes;
}
