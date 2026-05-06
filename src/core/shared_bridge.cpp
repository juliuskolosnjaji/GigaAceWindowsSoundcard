#include "shared_bridge.h"
#include <windows.h>
#include <cstring>

static void reset_ring(GigaACESharedRing* ring) {
    if (!ring) return;
    ring->mapping = nullptr;
    ring->map_size = 0;
    ring->layout = nullptr;
    ring->samples = nullptr;
    ring->hMapFile = nullptr;
}

size_t gigaace_shared_ring_calculate_map_size(uint32_t channel_count, uint32_t capacity_frames) {
    size_t sample_count = (size_t)channel_count * capacity_frames;
    return sizeof(GigaACESharedRingLayout) + sample_count * sizeof(float);
}

int gigaace_shared_ring_create(const char* shm_name, uint32_t channel_count,
                                uint32_t capacity_frames, double sample_rate,
                                GigaACESharedRing* out_ring) {
    if (!shm_name || !out_ring || channel_count == 0 || capacity_frames == 0)
        return -1;

    reset_ring(out_ring);

    size_t map_size = gigaace_shared_ring_calculate_map_size(channel_count, capacity_frames);

    wchar_t wname[512];
    MultiByteToWideChar(CP_UTF8, 0, shm_name, -1, wname, 512);

    HANDLE hMap = CreateFileMappingW(
        INVALID_HANDLE_VALUE,
        nullptr,
        PAGE_READWRITE,
        (DWORD)(map_size >> 32),
        (DWORD)(map_size & 0xFFFFFFFF),
        wname
    );

    if (!hMap)
        return -1;

    void* mapping = MapViewOfFile(hMap, FILE_MAP_ALL_ACCESS, 0, 0, map_size);
    if (!mapping) {
        CloseHandle(hMap);
        return -1;
    }

    std::memset(mapping, 0, map_size);

    GigaACESharedRingLayout* layout = (GigaACESharedRingLayout*)mapping;
    layout->magic = GIGAACE_SHARED_RING_MAGIC;
    layout->version = GIGAACE_SHARED_RING_VERSION;
    layout->channel_count = channel_count;
    layout->capacity_frames = capacity_frames;
    layout->bytes_per_sample = sizeof(float);
    layout->sample_rate = sample_rate;
    layout->data_offset = (uint32_t)sizeof(GigaACESharedRingLayout);
    layout->write_index = 0;
    layout->overrun_count = 0;
    layout->stream_state = GIGAACE_STREAM_STATE_STOPPED;
    layout->generation = 1;

    out_ring->mapping = mapping;
    out_ring->map_size = map_size;
    out_ring->layout = layout;
    out_ring->samples = (float*)((uint8_t*)mapping + layout->data_offset);
    out_ring->hMapFile = hMap;

    return 0;
}

int gigaace_shared_ring_open(const char* shm_name, GigaACESharedRing* out_ring) {
    if (!shm_name || !out_ring)
        return -1;

    reset_ring(out_ring);

    wchar_t wname[512];
    MultiByteToWideChar(CP_UTF8, 0, shm_name, -1, wname, 512);

    HANDLE hMap = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, wname);
    if (!hMap)
        return -1;

    void* mapping = MapViewOfFile(hMap, FILE_MAP_ALL_ACCESS, 0, 0, 0);
    if (!mapping) {
        CloseHandle(hMap);
        return -1;
    }

    GigaACESharedRingLayout* layout = (GigaACESharedRingLayout*)mapping;
    if (layout->magic != GIGAACE_SHARED_RING_MAGIC ||
        layout->version != GIGAACE_SHARED_RING_VERSION) {
        UnmapViewOfFile(mapping);
        CloseHandle(hMap);
        return -1;
    }

    out_ring->mapping = mapping;
    out_ring->map_size = gigaace_shared_ring_calculate_map_size(
        layout->channel_count,
        layout->capacity_frames
    );
    out_ring->layout = layout;
    out_ring->samples = (float*)((uint8_t*)mapping + layout->data_offset);
    out_ring->hMapFile = hMap;

    return 0;
}

void gigaace_shared_ring_close(GigaACESharedRing* ring) {
    if (!ring) return;

    if (ring->mapping) {
        UnmapViewOfFile(ring->mapping);
        ring->mapping = nullptr;
    }
    if (ring->hMapFile) {
        CloseHandle((HANDLE)ring->hMapFile);
        ring->hMapFile = nullptr;
    }

    reset_ring(ring);
}

void gigaace_shared_ring_set_stream_state(GigaACESharedRing* ring, uint32_t state) {
    if (!ring || !ring->layout) return;
    ring->layout->stream_state = state;
}

void gigaace_shared_ring_write_interleaved(GigaACESharedRing* ring, const float* input, uint32_t frame_count) {
    if (!ring || !ring->layout || !ring->samples || !input || frame_count == 0)
        return;

    uint32_t channels = ring->layout->channel_count;
    uint32_t capacity = ring->layout->capacity_frames;
    uint64_t write_index = ring->layout->write_index;
    uint64_t newest_index = write_index + frame_count;

    if (frame_count > capacity) {
        input += (frame_count - capacity) * channels;
        frame_count = capacity;
        newest_index = write_index + frame_count;
        ring->layout->overrun_count++;
    }

    for (uint32_t frame = 0; frame < frame_count; ++frame) {
        uint32_t slot = (uint32_t)((write_index + frame) % capacity);
        std::memcpy(&ring->samples[(size_t)slot * channels],
                     &input[(size_t)frame * channels],
                     channels * sizeof(float));
    }

    ring->layout->write_index = newest_index;
}

uint32_t gigaace_shared_ring_read_interleaved(GigaACESharedRing* ring, uint64_t* io_read_index,
                                               float* output, uint32_t frame_count) {
    if (!ring || !ring->layout || !ring->samples || !io_read_index || !output || frame_count == 0)
        return 0;

    uint32_t channels = ring->layout->channel_count;
    uint32_t capacity = ring->layout->capacity_frames;
    uint64_t write_index = ring->layout->write_index;
    uint64_t read_index = *io_read_index;

    if (write_index > read_index + capacity) {
        read_index = write_index - capacity;
        ring->layout->overrun_count++;
    }

    uint32_t available = (write_index > read_index) ? (uint32_t)(write_index - read_index) : 0;
    uint32_t frames_to_copy = (available < frame_count) ? available : frame_count;

    for (uint32_t frame = 0; frame < frames_to_copy; ++frame) {
        uint32_t slot = (uint32_t)((read_index + frame) % capacity);
        std::memcpy(&output[(size_t)frame * channels],
                     &ring->samples[(size_t)slot * channels],
                     channels * sizeof(float));
    }

    if (frames_to_copy < frame_count) {
        size_t remaining = (size_t)(frame_count - frames_to_copy) * channels;
        std::memset(&output[(size_t)frames_to_copy * channels], 0, remaining * sizeof(float));
    }

    *io_read_index = read_index + frames_to_copy;
    return frames_to_copy;
}
