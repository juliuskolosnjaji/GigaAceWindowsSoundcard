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
    uint64_t frames_rejected_decode;
    uint64_t frames_rejected_non_audio;
    uint64_t duplicate_counters;
    uint64_t concealed_frames;
    uint8_t last_counter;
    int has_last_counter;
    uint8_t last_counter_delta;
    int has_last_counter_delta;
    uint64_t counter_drops;
    int active_channels;
    uint8_t stream_type;
    size_t last_frame_size;
    size_t min_frame_size;
    size_t max_frame_size;
} GigaACEStatistics;

typedef enum {
    GIGAACE_CAPTURE_MODE_DEMO,
    GIGAACE_CAPTURE_MODE_PCAP
} GigaACECaptureMode;

typedef enum {
    GIGAACE_TX_SOURCE_SILENCE = 0,
    GIGAACE_TX_SOURCE_TONE = 1,
    GIGAACE_TX_SOURCE_WAV = 2
} GigaACETxSource;

typedef enum {
    GIGAACE_TX_ENCODING_BE_RAW = 0,
    GIGAACE_TX_ENCODING_LE_RAW = 1,
    GIGAACE_TX_ENCODING_NIBBLE = 2,
    GIGAACE_TX_ENCODING_ACE_SWIZZLE = 3,
    GIGAACE_TX_ENCODING_NIBBLE_BYTE = 4
} GigaACETxEncoding;

typedef enum {
    GIGAACE_TX_LAYOUT_LINEAR = 0,
    GIGAACE_TX_LAYOUT_BANKED_8_WITH_SYNC = 1,
    GIGAACE_TX_LAYOUT_RAW_SLOT = 2,
    GIGAACE_TX_LAYOUT_GX4816_LINEAR_48 = 3,
    GIGAACE_TX_LAYOUT_GIGAACE_CARD_PAIRED = 4
} GigaACETxLayout;

typedef enum {
    GIGAACE_TX_PACKET_GX4816_SLINK = 0,
    GIGAACE_TX_PACKET_GIGAACE_CARD = 1
} GigaACETxPacketFormat;

typedef struct {
    int channels;
    double sample_rate;
    int ring_buffer_frames;
    GigaACECaptureMode capture_mode;
    char interface_name[256];
    const char* shared_memory_name;
    int shared_memory_frames;
    int tx_probe_enabled;
    int tx_stagebox_advertise_enabled;
    int tx_probe_tone_enabled;
    GigaACETxSource tx_probe_source;
    int tx_probe_channel;
    float tx_probe_gain;
    double tx_probe_frequency;
    char tx_probe_file_path[512];
    int tx_probe_file_loop;
    GigaACETxEncoding tx_probe_encoding;
    GigaACETxLayout tx_probe_layout;
    GigaACETxPacketFormat tx_probe_packet_format;
} GigaACEConfig;

#ifdef __cplusplus
}
#endif

#endif
