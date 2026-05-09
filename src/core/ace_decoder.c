#include "ace_decoder.h"
#include <string.h>

static const uint8_t kBroadcastMAC[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

static int32_t sign_extend24(uint32_t sample24)
{
    sample24 &= 0x00ffffffu;
    if (sample24 & 0x00800000u)
        sample24 |= 0xff000000u;
    return (int32_t)sample24;
}

static int32_t decode_sample24(const uint8_t packed[3])
{
    uint32_t raw = ((uint32_t)packed[0] << 16) |
                   ((uint32_t)packed[1] << 8)  |
                   ((uint32_t)packed[2]);
    return sign_extend24(raw);
}

int gigaace_decode_frame(const uint8_t *frame, size_t frame_len, int max_channels,
                         uint8_t dst_mac[6], uint8_t src_mac[6],
                         uint16_t *ethertype, uint8_t *counter,
                         uint8_t *stream_type, size_t *channel_count,
                         int32_t *pcm24_out)
{
    if (!frame || !dst_mac || !src_mac || !ethertype || !counter ||
        !stream_type || !channel_count || !pcm24_out)
        return -1;

    if (frame_len < GIGAACE_MIN_FRAME_SIZE)
        return -1;

    memcpy(dst_mac, frame, 6);
    memcpy(src_mac, frame + 6, 6);
    *ethertype = (uint16_t)((frame[12] << 8) | frame[13]);
    *counter = frame[GIGAACE_COUNTER_OFFSET] & 0x1f;
    *stream_type = frame[GIGAACE_STREAM_TYPE_OFFSET];

    if (memcmp(dst_mac, kBroadcastMAC, 6) != 0)
        return -1;

    if (frame_len <= GIGAACE_AUDIO_BASE_OFFSET) {
        *channel_count = 0;
        return 0;
    }

    size_t available = frame_len - GIGAACE_AUDIO_BASE_OFFSET;
    size_t channels = available / GIGAACE_BYTES_PER_SAMPLE;
    if ((int)channels > max_channels)
        channels = (size_t)max_channels;

    *channel_count = channels;

    for (size_t i = 0; i < channels; ++i) {
        size_t off = GIGAACE_AUDIO_BASE_OFFSET + i * GIGAACE_BYTES_PER_SAMPLE;
        pcm24_out[i] = decode_sample24(frame + off);
    }

    return 0;
}

bool gigaace_decode_channel_sample(const uint8_t *frame, size_t frame_len,
                                   size_t channel_index, int32_t *out_pcm24)
{
    if (!frame || !out_pcm24)
        return false;

    size_t off = GIGAACE_AUDIO_BASE_OFFSET + channel_index * GIGAACE_BYTES_PER_SAMPLE;
    if (frame_len < off + GIGAACE_BYTES_PER_SAMPLE)
        return false;

    *out_pcm24 = decode_sample24(frame + off);
    return true;
}

float gigaace_pcm24_to_float(int32_t pcm24)
{
    return (float)pcm24 / 8388608.0f;
}

int32_t gigaace_float_to_pcm24(float sample)
{
    float clipped = sample;
    if (clipped > 0.9999999f) clipped = 0.9999999f;
    if (clipped < -1.0f) clipped = -1.0f;
    return (int32_t)(clipped * 8388608.0f);
}

void gigaace_encode_pcm24(int32_t pcm24, uint8_t packed[3])
{
    uint32_t raw = (uint32_t)pcm24 & 0x00ffffffu;
    packed[0] = (uint8_t)(raw >> 16);
    packed[1] = (uint8_t)(raw >> 8);
    packed[2] = (uint8_t)raw;
}
