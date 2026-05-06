#ifndef GIGAACE_ASIO_H
#define GIGAACE_ASIO_H

#include "asio_types.h"
#include "shared_bridge.h"
#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

class GigaAceAsioDriver final : public IASIO {
public:
    GigaAceAsioDriver();
    ~GigaAceAsioDriver();

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;

    ASIOBool init(void* sysHandle) override;
    void getDriverName(char* name) override;
    long getDriverVersion() override;
    void getErrorMessage(char* string) override;
    ASIOError start() override;
    ASIOError stop() override;
    ASIOError getChannels(long* numInputChannels, long* numOutputChannels) override;
    ASIOError getLatencies(long* inputLatency, long* outputLatency) override;
    ASIOError getBufferSize(long* minSize, long* maxSize, long* preferredSize, long* granularity) override;
    ASIOError canSampleRate(ASIOSampleRate sampleRate) override;
    ASIOError getSampleRate(ASIOSampleRate* sampleRate) override;
    ASIOError setSampleRate(ASIOSampleRate sampleRate) override;
    ASIOError getClockSources(ASIOClockSource* clocks, long* numSources) override;
    ASIOError setClockSource(long reference) override;
    ASIOError getSamplePosition(ASIOSamples* samplePosition, ASIOTimeStamp* timeStamp) override;
    ASIOError getChannelInfo(ASIOChannelInfo* info) override;
    ASIOError createBuffers(ASIOBufferInfo* bufferInfos, long numChannels, long bufferSize, ASIOCallbacks* callbacks) override;
    ASIOError disposeBuffers() override;
    ASIOError controlPanel() override;
    ASIOError future(long selector, void* opt) override;
    ASIOError outputReady() override;

private:
    void audioThread();
    bool ensureSharedRing();
    void fillInputBuffers(long doubleBufferIndex);
    void clearBuffers(long doubleBufferIndex);

    std::atomic<ULONG> m_refCount;
    std::atomic<bool> m_running;
    std::thread m_thread;
    std::mutex m_lock;

    ASIOCallbacks* m_callbacks;
    ASIOBufferInfo* m_bufferInfos;
    long m_numBufferInfos;
    long m_bufferSize;
    double m_sampleRate;
    int m_inputChannels;
    int m_outputChannels;
    uint64_t m_readIndex;
    uint64_t m_samplePosition;
    GigaACESharedRing m_ring;
};

extern const CLSID CLSID_GigaAceAsioDriver;

#endif
