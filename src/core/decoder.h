#ifndef GIGAACE_DECODER_H
#define GIGAACE_DECODER_H

#include "frame.h"
#include <vector>
#include <cstdint>

class GigaACEDecoder {
public:
    static bool decode(const uint8_t* data, size_t len, int max_channels, GigaACEFrame& out);
    static float pcm24ToFloat(int32_t sample);
    static int32_t floatToPCM24(float sample);
    static uint32_t unswizzle24(uint32_t sample);
    static uint32_t swizzle24(uint32_t sample);
};

class GigaACEDemoFrameFactory {
public:
    static std::vector<uint8_t> makeFrame(uint8_t counter, int channel_count, double phase);
};

#endif
