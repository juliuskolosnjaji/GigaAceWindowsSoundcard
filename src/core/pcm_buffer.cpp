#include "pcm_buffer.h"
#include <algorithm>
#include <cmath>

PCMRingBuffer::PCMRingBuffer(int channels, int capacity)
    : m_channels(std::max(1, channels))
    , m_capacity(std::max(256, capacity))
{
    m_storage.resize(m_channels, std::vector<float>(m_capacity, 0.0f));
}

void PCMRingBuffer::append(const std::vector<float>& frame_samples) {
    std::lock_guard<std::mutex> lock(m_mutex);

    for (int ch = 0; ch < m_channels; ++ch) {
        float sample = (ch < (int)frame_samples.size()) ? frame_samples[ch] : 0.0f;
        m_storage[ch][m_write_index] = sample;
    }

    m_write_index = (m_write_index + 1) % m_capacity;

    if (m_available < m_capacity)
        m_available++;
    else
        m_read_index = (m_read_index + 1) % m_capacity;
}

std::vector<float> PCMRingBuffer::consume(const std::vector<int>& channel_map, int frame_count) {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (frame_count <= 0 || channel_map.empty())
        return {};

    std::vector<float> result(frame_count * channel_map.size(), 0.0f);

    for (int frame = 0; frame < frame_count; ++frame) {
        bool have_data = frame < m_available;
        int src_index = have_data ? (m_read_index + frame) % m_capacity : m_read_index;

        for (size_t out_ch = 0; out_ch < channel_map.size(); ++out_ch) {
            int req_ch = channel_map[out_ch];
            int safe_ch = (req_ch >= 0 && req_ch < m_channels) ? req_ch : 0;
            result[frame * channel_map.size() + out_ch] = have_data ? m_storage[safe_ch][src_index] : 0.0f;
        }
    }

    int consumed = std::min(frame_count, m_available);
    m_read_index = (m_read_index + consumed) % m_capacity;
    m_available -= consumed;
    return result;
}

void PCMRingBuffer::consumeStereo(int frame_count, int left_ch, int right_ch,
                                   float* left_out, float* right_out) {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (frame_count <= 0) return;

    int safe_left = (left_ch >= 0 && left_ch < m_channels) ? left_ch : 0;
    int safe_right = (right_ch >= 0 && right_ch < m_channels) ? right_ch : 0;

    for (int frame = 0; frame < frame_count; ++frame) {
        bool have_data = frame < m_available;
        int src_index = have_data ? (m_read_index + frame) % m_capacity : m_read_index;

        left_out[frame] = have_data ? m_storage[safe_left][src_index] : 0.0f;
        right_out[frame] = have_data ? m_storage[safe_right][src_index] : 0.0f;
    }

    int consumed = std::min(frame_count, m_available);
    m_read_index = (m_read_index + consumed) % m_capacity;
    m_available -= consumed;
}

std::vector<float> PCMRingBuffer::latestLevels(int count) const {
    std::lock_guard<std::mutex> lock(m_mutex);

    int n = std::min(count, m_channels);
    std::vector<float> levels(n, 0.0f);

    if (m_available == 0)
        return levels;

    int latest = (m_write_index - 1 + m_capacity) % m_capacity;
    for (int i = 0; i < n; ++i)
        levels[i] = std::abs(m_storage[i][latest]);

    return levels;
}

int PCMRingBuffer::bufferedFrameCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_available;
}
