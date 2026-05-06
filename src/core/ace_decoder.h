#ifndef GIGAACE_ACE_DECODER_H
#define GIGAACE_ACE_DECODER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "frame.h"

#ifdef __cplusplus
extern "C" {
#endif

int gigaace_decode_frame(const uint8_t *frame, size_t frame_len, int max_channels,
                         uint8_t dst_mac[6], uint8_t src_mac[6],
                         uint16_t *ethertype, uint8_t *counter,
                         uint8_t *stream_type, size_t *channel_count,
                         int32_t *pcm24_out);

bool gigaace_decode_channel_sample(const uint8_t *frame, size_t frame_len,
                                   size_t channel_index, int32_t *out_pcm24);

float gigaace_pcm24_to_float(int32_t pcm24);

int32_t gigaace_float_to_pcm24(float sample);

#ifdef __cplusplus
}
#endif

#endif
