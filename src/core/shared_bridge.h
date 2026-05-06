#ifndef GIGAACE_SHARED_BRIDGE_H
#define GIGAACE_SHARED_BRIDGE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GIGAACE_SHARED_RING_MAGIC 0x47414345u
#define GIGAACE_SHARED_RING_VERSION 1u
#define GIGAACE_SHARED_RING_NAME_MAX 256

#define GIGAACE_STREAM_STATE_STOPPED 0
#define GIGAACE_STREAM_STATE_RUNNING 1

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t channel_count;
    uint32_t capacity_frames;
    uint32_t bytes_per_sample;
    uint32_t reserved0;
    double sample_rate;
    volatile uint64_t write_index;
    volatile uint64_t overrun_count;
    volatile uint32_t stream_state;
    volatile uint32_t generation;
    uint32_t data_offset;
    uint32_t reserved1;
} GigaACESharedRingLayout;

typedef struct {
    void* mapping;
    size_t map_size;
    GigaACESharedRingLayout* layout;
    float* samples;
    void* hMapFile;
} GigaACESharedRing;

size_t gigaace_shared_ring_calculate_map_size(uint32_t channel_count, uint32_t capacity_frames);
int gigaace_shared_ring_create(const char* shm_name, uint32_t channel_count, uint32_t capacity_frames, double sample_rate, GigaACESharedRing* out_ring);
int gigaace_shared_ring_open(const char* shm_name, GigaACESharedRing* out_ring);
void gigaace_shared_ring_close(GigaACESharedRing* ring);
void gigaace_shared_ring_set_stream_state(GigaACESharedRing* ring, uint32_t state);
void gigaace_shared_ring_write_interleaved(GigaACESharedRing* ring, const float* input, uint32_t frame_count);
uint32_t gigaace_shared_ring_read_interleaved(GigaACESharedRing* ring, uint64_t* io_read_index, float* output, uint32_t frame_count);

#ifdef __cplusplus
}
#endif

#endif
