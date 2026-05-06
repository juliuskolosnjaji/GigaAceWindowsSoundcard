#ifndef GIGAACE_PCM_BUFFER_H
#define GIGAACE_PCM_BUFFER_H

#include <vector>
#include <mutex>
#include <cstdint>

class PCMRingBuffer {
public:
    PCMRingBuffer(int channels, int capacity);
    void append(const std::vector<float>& frame_samples);
    std::vector<float> consume(const std::vector<int>& channel_map, int frame_count);
    void consumeStereo(int frame_count, int left_channel, int right_channel,
                       float* left_out, float* right_out);
    std::vector<float> latestLevels(int count, int start_channel = 0) const;
    int bufferedFrameCount() const;

private:
    int m_channels;
    int m_capacity;
    std::vector<std::vector<float>> m_storage;
    int m_write_index = 0;
    int m_read_index = 0;
    int m_available = 0;
    mutable std::mutex m_mutex;
};

#endif
