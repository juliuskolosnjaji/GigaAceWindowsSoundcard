#include "gigaace_asio.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

const CLSID CLSID_GigaAceAsioDriver =
    {0x7d874a81, 0x989a, 0x457a, {0x9e, 0xe8, 0x7e, 0x18, 0x2d, 0xdd, 0x8f, 0x37}};

static constexpr const char* kDriverName = "GigaACE ASIO Driver";
static constexpr const char* kSharedMemoryName = "Local\\GigaACEVirtualDevice";
static constexpr const char* kLegacySharedMemoryName = "Global\\GigaACEVirtualDevice";
static constexpr long kMinBufferSize = 64;
static constexpr long kMaxBufferSize = 2048;
static constexpr long kPreferredBufferSize = 256;
static constexpr int kDefaultInputChannels = 64;

GigaAceAsioDriver::GigaAceAsioDriver()
    : m_refCount(1),
      m_running(false),
      m_callbacks(nullptr),
      m_bufferInfos(nullptr),
      m_numBufferInfos(0),
      m_bufferSize(kPreferredBufferSize),
      m_sampleRate(48000.0),
      m_inputChannels(kDefaultInputChannels),
      m_outputChannels(2),
      m_readIndex(0),
      m_samplePosition(0) {
    std::memset(&m_ring, 0, sizeof(m_ring));
}

GigaAceAsioDriver::~GigaAceAsioDriver() {
    stop();
    disposeBuffers();
    gigaace_shared_ring_close(&m_ring);
}

HRESULT GigaAceAsioDriver::QueryInterface(REFIID riid, void** ppvObject) {
    if (!ppvObject) return E_POINTER;
    if (riid == __uuidof(IUnknown) || riid == __uuidof(IASIO) || riid == CLSID_GigaAceAsioDriver) {
        *ppvObject = static_cast<IASIO*>(this);
        AddRef();
        return S_OK;
    }
    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

ULONG GigaAceAsioDriver::AddRef() {
    return ++m_refCount;
}

ULONG GigaAceAsioDriver::Release() {
    ULONG ref = --m_refCount;
    if (ref == 0)
        delete this;
    return ref;
}

ASIOBool GigaAceAsioDriver::init(void*) {
    ensureSharedRing();
    return 1;
}

void GigaAceAsioDriver::getDriverName(char* name) {
    if (name)
        std::strncpy(name, kDriverName, 31);
}

long GigaAceAsioDriver::getDriverVersion() {
    return 1;
}

void GigaAceAsioDriver::getErrorMessage(char* string) {
    if (string)
        std::strncpy(string, "GigaACE ASIO bridge is running.", 124);
}

ASIOError GigaAceAsioDriver::start() {
    if (m_running)
        return ASE_OK;
    if (!m_bufferInfos || !m_callbacks)
        return ASE_InvalidMode;

    m_running = true;
    m_thread = std::thread(&GigaAceAsioDriver::audioThread, this);
    return ASE_OK;
}

ASIOError GigaAceAsioDriver::stop() {
    if (!m_running)
        return ASE_OK;
    m_running = false;
    if (m_thread.joinable())
        m_thread.join();
    return ASE_OK;
}

ASIOError GigaAceAsioDriver::getChannels(long* numInputChannels, long* numOutputChannels) {
    if (!numInputChannels || !numOutputChannels)
        return ASE_InvalidParameter;
    *numInputChannels = m_inputChannels;
    *numOutputChannels = m_outputChannels;
    return ASE_OK;
}

ASIOError GigaAceAsioDriver::getLatencies(long* inputLatency, long* outputLatency) {
    if (!inputLatency || !outputLatency)
        return ASE_InvalidParameter;
    *inputLatency = m_bufferSize * 2;
    *outputLatency = 0;
    return ASE_OK;
}

ASIOError GigaAceAsioDriver::getBufferSize(long* minSize, long* maxSize, long* preferredSize, long* granularity) {
    if (!minSize || !maxSize || !preferredSize || !granularity)
        return ASE_InvalidParameter;
    *minSize = kMinBufferSize;
    *maxSize = kMaxBufferSize;
    *preferredSize = kPreferredBufferSize;
    *granularity = 0;
    return ASE_OK;
}

ASIOError GigaAceAsioDriver::canSampleRate(ASIOSampleRate sampleRate) {
    return std::abs(sampleRate - 48000.0) < 1.0 ? ASE_OK : ASE_NoClock;
}

ASIOError GigaAceAsioDriver::getSampleRate(ASIOSampleRate* sampleRate) {
    if (!sampleRate)
        return ASE_InvalidParameter;
    *sampleRate = m_sampleRate;
    return ASE_OK;
}

ASIOError GigaAceAsioDriver::setSampleRate(ASIOSampleRate sampleRate) {
    if (canSampleRate(sampleRate) != ASE_OK)
        return ASE_NoClock;
    m_sampleRate = 48000.0;
    return ASE_OK;
}

ASIOError GigaAceAsioDriver::getClockSources(ASIOClockSource* clocks, long* numSources) {
    if (!numSources)
        return ASE_InvalidParameter;
    if (clocks && *numSources > 0) {
        clocks[0].index = 0;
        clocks[0].associatedChannel = -1;
        clocks[0].associatedGroup = -1;
        clocks[0].isCurrentSource = 1;
        std::strncpy(clocks[0].name, "GigaACE 48 kHz", sizeof(clocks[0].name) - 1);
    }
    *numSources = 1;
    return ASE_OK;
}

ASIOError GigaAceAsioDriver::setClockSource(long reference) {
    return reference == 0 ? ASE_OK : ASE_InvalidParameter;
}

ASIOError GigaAceAsioDriver::getSamplePosition(ASIOSamples* samplePosition, ASIOTimeStamp* timeStamp) {
    if (!samplePosition || !timeStamp)
        return ASE_InvalidParameter;
    *samplePosition = static_cast<ASIOSamples>(m_samplePosition);
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    *timeStamp = static_cast<ASIOTimeStamp>(std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
    return ASE_OK;
}

ASIOError GigaAceAsioDriver::getChannelInfo(ASIOChannelInfo* info) {
    if (!info)
        return ASE_InvalidParameter;
    const int limit = info->isInput ? m_inputChannels : m_outputChannels;
    if (info->channel < 0 || info->channel >= limit)
        return ASE_InvalidParameter;
    info->isActive = 1;
    info->channelGroup = 0;
    info->type = ASIOSTFloat32LSB;
    std::snprintf(info->name, sizeof(info->name),
                  info->isInput ? "GigaACE In %02ld" : "GigaACE Out %02ld",
                  info->channel + 1);
    return ASE_OK;
}

ASIOError GigaAceAsioDriver::createBuffers(ASIOBufferInfo* bufferInfos, long numChannels, long bufferSize, ASIOCallbacks* callbacks) {
    if (!bufferInfos || !callbacks || numChannels <= 0 || bufferSize < kMinBufferSize || bufferSize > kMaxBufferSize)
        return ASE_InvalidParameter;

    disposeBuffers();

    m_bufferInfos = bufferInfos;
    m_numBufferInfos = numChannels;
    m_bufferSize = bufferSize;
    m_callbacks = callbacks;

    for (long i = 0; i < numChannels; ++i) {
        const int limit = bufferInfos[i].isInput ? m_inputChannels : m_outputChannels;
        if (bufferInfos[i].channelNum < 0 || bufferInfos[i].channelNum >= limit)
            return ASE_InvalidParameter;
        bufferInfos[i].buffers[0] = new float[bufferSize]();
        bufferInfos[i].buffers[1] = new float[bufferSize]();
    }

    return ASE_OK;
}

ASIOError GigaAceAsioDriver::disposeBuffers() {
    stop();
    if (m_bufferInfos) {
        for (long i = 0; i < m_numBufferInfos; ++i) {
            delete[] static_cast<float*>(m_bufferInfos[i].buffers[0]);
            delete[] static_cast<float*>(m_bufferInfos[i].buffers[1]);
            m_bufferInfos[i].buffers[0] = nullptr;
            m_bufferInfos[i].buffers[1] = nullptr;
        }
    }
    m_bufferInfos = nullptr;
    m_numBufferInfos = 0;
    m_callbacks = nullptr;
    return ASE_OK;
}

ASIOError GigaAceAsioDriver::controlPanel() {
    MessageBoxW(nullptr,
                L"Start the GigaACE Virtual Sound Card app first. The ASIO driver reads Local\\GigaACEVirtualDevice.",
                L"GigaACE ASIO Driver",
                MB_OK | MB_ICONINFORMATION);
    return ASE_OK;
}

ASIOError GigaAceAsioDriver::future(long, void*) {
    return ASE_NotPresent;
}

ASIOError GigaAceAsioDriver::outputReady() {
    return ASE_OK;
}

bool GigaAceAsioDriver::ensureSharedRing() {
    if (m_ring.layout)
        return true;

    if (gigaace_shared_ring_open(kSharedMemoryName, &m_ring) != 0)
        gigaace_shared_ring_open(kLegacySharedMemoryName, &m_ring);

    if (m_ring.layout) {
        m_inputChannels = static_cast<int>(std::min<uint32_t>(m_ring.layout->channel_count, 64));
        m_sampleRate = m_ring.layout->sample_rate;
        m_readIndex = m_ring.layout->write_index;
        return true;
    }

    return false;
}

void GigaAceAsioDriver::audioThread() {
    using clock = std::chrono::steady_clock;
    const auto period = std::chrono::duration<double>(static_cast<double>(m_bufferSize) / m_sampleRate);
    long index = 0;
    auto next = clock::now();

    while (m_running) {
        next += std::chrono::duration_cast<clock::duration>(period);
        fillInputBuffers(index);

        if (m_callbacks) {
            if (m_callbacks->bufferSwitchTimeInfo) {
                ASIOTime time{};
                time.timeInfo.samplePosition = static_cast<ASIOSamples>(m_samplePosition);
                time.timeInfo.sampleRate = m_sampleRate;
                time.timeInfo.speed = 1.0;
                m_callbacks->bufferSwitchTimeInfo(&time, index, 0);
            } else if (m_callbacks->bufferSwitch) {
                m_callbacks->bufferSwitch(index, 0);
            }
        }

        m_samplePosition += static_cast<uint64_t>(m_bufferSize);
        index = 1 - index;
        std::this_thread::sleep_until(next);
    }
}

void GigaAceAsioDriver::fillInputBuffers(long doubleBufferIndex) {
    if (!m_bufferInfos || doubleBufferIndex < 0 || doubleBufferIndex > 1) {
        return;
    }

    if (!ensureSharedRing()) {
        clearBuffers(doubleBufferIndex);
        return;
    }

    const uint32_t ringChannels = m_ring.layout->channel_count;
    std::vector<float> interleaved(static_cast<size_t>(m_bufferSize) * ringChannels);
    gigaace_shared_ring_read_interleaved(&m_ring, &m_readIndex, interleaved.data(), static_cast<uint32_t>(m_bufferSize));

    for (long i = 0; i < m_numBufferInfos; ++i) {
        if (!m_bufferInfos[i].isInput) {
            continue;
        }
        float* dest = static_cast<float*>(m_bufferInfos[i].buffers[doubleBufferIndex]);
        const long ch = m_bufferInfos[i].channelNum;
        if (!dest || ch < 0 || static_cast<uint32_t>(ch) >= ringChannels) {
            continue;
        }
        for (long frame = 0; frame < m_bufferSize; ++frame)
            dest[frame] = interleaved[static_cast<size_t>(frame) * ringChannels + ch];
    }
}

void GigaAceAsioDriver::clearBuffers(long doubleBufferIndex) {
    if (!m_bufferInfos)
        return;
    for (long i = 0; i < m_numBufferInfos; ++i) {
        float* dest = static_cast<float*>(m_bufferInfos[i].buffers[doubleBufferIndex]);
        if (dest)
            std::memset(dest, 0, static_cast<size_t>(m_bufferSize) * sizeof(float));
    }
}
