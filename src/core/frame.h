#ifndef GIGAACE_FRAME_H
#define GIGAACE_FRAME_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GIGAACE_MAX_CHANNELS 128
#define GIGAACE_COUNTER_OFFSET 19
#define GIGAACE_STREAM_TYPE_OFFSET 20
#define GIGAACE_AUDIO_BASE_OFFSET 24
#define GIGAACE_BYTES_PER_SAMPLE 3
#define GIGAACE_MIN_FRAME_SIZE 27

typedef struct {
    uint8_t dst_mac[6];
    uint8_t src_mac[6];
    uint16_t ethertype;
    uint8_t counter;
    uint8_t stream_type;
    size_t channel_count;
    int32_t pcm24[GIGAACE_MAX_CHANNELS];
} GigaACEFrame;

typedef struct {
    uint64_t frames_received;
    uint64_t frames_decoded;
    uint64_t frames_rejected;
    uint8_t last_counter;
    int has_last_counter;
    uint64_t counter_drops;
    int active_channels;
    uint8_t stream_type;
} GigaACEStatistics;

typedef enum {
    GIGAACE_CAPTURE_MODE_DEMO,
    GIGAACE_CAPTURE_MODE_PCAP
} GigaACECaptureMode;

typedef struct {
    int channels;
    double sample_rate;
    int ring_buffer_frames;
    GigaACECaptureMode capture_mode;
    char interface_name[256];
    const char* shared_memory_name;
    int shared_memory_frames;
} GigaACEConfig;

#ifdef __cplusplus
}
#endif

#endif
