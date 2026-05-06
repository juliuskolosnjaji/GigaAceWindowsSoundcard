#ifndef GIGAACE_WASAPI_OUTPUT_H
#define GIGAACE_WASAPI_OUTPUT_H

#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <audiopolicy.h>
#include <atomic>
#include <thread>
#include <mutex>
#include <vector>
#include <functional>
#include <string>

struct WASAPIRenderDevice {
    std::wstring id;
    std::wstring name;
};

class WASAPIOutput {
public:
    using SampleCallback = std::function<void(int, float*, float*)>;

    WASAPIOutput(double sample_rate = 96000.0, std::wstring endpoint_id = {});
    ~WASAPIOutput();

    static std::vector<WASAPIRenderDevice> enumerateRenderDevices();

    bool start(SampleCallback callback);
    void stop();
    bool isRunning() const { return m_running.load(); }
    double sampleRate() const { return m_sample_rate; }

private:
    void renderLoop();

    double m_sample_rate;
    std::atomic<bool> m_running{false};
    std::unique_ptr<std::thread> m_thread;
    SampleCallback m_callback;
    std::wstring m_endpoint_id;

    IAudioClient* m_audio_client = nullptr;
    IAudioRenderClient* m_render_client = nullptr;
    HANDLE m_event = nullptr;
    UINT32 m_buffer_frame_count = 0;
    UINT16 m_output_channels = 2;
    bool m_initialized = false;

    bool initialize();
    void cleanup();
};

#endif
