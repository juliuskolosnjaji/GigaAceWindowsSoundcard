#include "wasapi_output.h"
#include <functiondiscoverykeys_devpkey.h>
#include <stdexcept>
#include <propvarutil.h>

WASAPIOutput::WASAPIOutput(double sample_rate, std::wstring endpoint_id)
    : m_sample_rate(sample_rate), m_endpoint_id(std::move(endpoint_id)) {}

WASAPIOutput::~WASAPIOutput() {
    stop();
    cleanup();
}

bool WASAPIOutput::initialize() {
    if (m_initialized) return true;

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE)
        return false;

    IMMDeviceEnumerator* enumerator = nullptr;
    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                          CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                          (void**)&enumerator);
    if (FAILED(hr)) return false;

    IMMDevice* device = nullptr;
    if (!m_endpoint_id.empty()) {
        hr = enumerator->GetDevice(m_endpoint_id.c_str(), &device);
    } else {
        hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
    }
    enumerator->Release();
    if (FAILED(hr)) return false;

    hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&m_audio_client);
    device->Release();
    if (FAILED(hr)) return false;

    WAVEFORMATEX* mix_format = nullptr;
    hr = m_audio_client->GetMixFormat(&mix_format);
    if (FAILED(hr)) return false;

    m_sample_rate = mix_format->nSamplesPerSec;
    m_output_channels = mix_format->nChannels;
    if (m_output_channels == 0)
        m_output_channels = 2;

    hr = m_audio_client->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
        0,
        0,
        mix_format,
        nullptr
    );

    CoTaskMemFree(mix_format);
    if (FAILED(hr)) return false;

    hr = m_audio_client->GetBufferSize(&m_buffer_frame_count);
    if (FAILED(hr)) return false;

    hr = m_audio_client->GetService(__uuidof(IAudioRenderClient), (void**)&m_render_client);
    if (FAILED(hr)) return false;

    m_event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!m_event) return false;

    hr = m_audio_client->SetEventHandle(m_event);
    if (FAILED(hr)) return false;

    m_initialized = true;
    return true;
}

void WASAPIOutput::cleanup() {
    if (m_render_client) {
        m_render_client->Release();
        m_render_client = nullptr;
    }
    if (m_audio_client) {
        m_audio_client->Release();
        m_audio_client = nullptr;
    }
    if (m_event) {
        CloseHandle(m_event);
        m_event = nullptr;
    }
    m_initialized = false;
}

bool WASAPIOutput::start(SampleCallback callback) {
    if (m_running) return false;

    if (!initialize())
        return false;

    m_callback = std::move(callback);
    m_running = true;

    HRESULT hr = m_audio_client->Start();
    if (FAILED(hr)) {
        m_running = false;
        return false;
    }

    m_thread = std::make_unique<std::thread>(&WASAPIOutput::renderLoop, this);
    return true;
}

void WASAPIOutput::stop() {
    if (!m_running) return;
    m_running = false;

    if (m_audio_client)
        m_audio_client->Stop();

    if (m_event)
        SetEvent(m_event);

    if (m_thread && m_thread->joinable())
        m_thread->join();
    m_thread.reset();
}

void WASAPIOutput::renderLoop() {
    BYTE* buffer = nullptr;

    while (m_running) {
        DWORD wait_result = WaitForSingleObject(m_event, 2000);
        if (!m_running || wait_result != WAIT_OBJECT_0)
            break;

        UINT32 padding = 0;
        m_audio_client->GetCurrentPadding(&padding);

        UINT32 frames_available = m_buffer_frame_count - padding;
        if (frames_available == 0)
            continue;

        HRESULT hr = m_render_client->GetBuffer(frames_available, &buffer);
        if (FAILED(hr)) break;

        float* float_buffer = (float*)buffer;
        int total_samples = frames_available * m_output_channels;

        std::vector<float> left(frames_available);
        std::vector<float> right(frames_available);

        if (m_callback)
            m_callback((int)frames_available, left.data(), right.data());

        for (int i = 0; i < total_samples; ++i)
            float_buffer[i] = 0.0f;

        for (UINT32 i = 0; i < frames_available; ++i) {
            float_buffer[i * m_output_channels] = left[i];
            if (m_output_channels > 1)
                float_buffer[i * m_output_channels + 1] = right[i];
        }

        m_render_client->ReleaseBuffer(frames_available, 0);
    }
}

std::vector<WASAPIRenderDevice> WASAPIOutput::enumerateRenderDevices() {
    std::vector<WASAPIRenderDevice> devices;

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE)
        return devices;

    IMMDeviceEnumerator* enumerator = nullptr;
    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                          CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                          (void**)&enumerator);
    if (FAILED(hr))
        return devices;

    IMMDeviceCollection* collection = nullptr;
    hr = enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &collection);
    enumerator->Release();
    if (FAILED(hr))
        return devices;

    UINT count = 0;
    collection->GetCount(&count);

    for (UINT i = 0; i < count; ++i) {
        IMMDevice* device = nullptr;
        if (FAILED(collection->Item(i, &device)) || !device)
            continue;

        LPWSTR id = nullptr;
        std::wstring device_id;
        if (SUCCEEDED(device->GetId(&id)) && id) {
            device_id = id;
            CoTaskMemFree(id);
        }

        std::wstring name = L"Unknown output";
        IPropertyStore* props = nullptr;
        if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &props)) && props) {
            PROPVARIANT value;
            PropVariantInit(&value);
            if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &value)) && value.vt == VT_LPWSTR)
                name = value.pwszVal;
            PropVariantClear(&value);
            props->Release();
        }

        if (!device_id.empty())
            devices.push_back({device_id, name});

        device->Release();
    }

    collection->Release();
    return devices;
}
