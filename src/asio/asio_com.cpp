#include "gigaace_asio.h"
#include <shlwapi.h>
#include <strsafe.h>

static std::atomic<long> g_lockCount{0};
static std::atomic<long> g_objectCount{0};
static HMODULE g_module = nullptr;

class GigaAceClassFactory final : public IClassFactory {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override {
        if (!ppvObject) return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IClassFactory)) {
            *ppvObject = static_cast<IClassFactory*>(this);
            AddRef();
            return S_OK;
        }
        *ppvObject = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return ++m_refCount;
    }

    ULONG STDMETHODCALLTYPE Release() override {
        ULONG ref = --m_refCount;
        if (ref == 0)
            delete this;
        return ref;
    }

    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer, REFIID riid, void** ppvObject) override {
        if (outer)
            return CLASS_E_NOAGGREGATION;
        auto* driver = new GigaAceAsioDriver();
        g_objectCount++;
        HRESULT hr = driver->QueryInterface(riid, ppvObject);
        driver->Release();
        return hr;
    }

    HRESULT STDMETHODCALLTYPE LockServer(BOOL lock) override {
        if (lock)
            ++g_lockCount;
        else
            --g_lockCount;
        return S_OK;
    }

private:
    std::atomic<ULONG> m_refCount{1};
};

static HRESULT setStringValue(HKEY root, const wchar_t* path, const wchar_t* name, const wchar_t* value) {
    HKEY key = nullptr;
    LONG result = RegCreateKeyExW(root, path, 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr);
    if (result != ERROR_SUCCESS)
        return HRESULT_FROM_WIN32(result);
    result = RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value),
                            static_cast<DWORD>((wcslen(value) + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    return HRESULT_FROM_WIN32(result);
}

static HRESULT deleteTree(HKEY root, const wchar_t* path) {
    LONG result = SHDeleteKeyW(root, path);
    return (result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND) ? S_OK : HRESULT_FROM_WIN32(result);
}

static bool modulePath(wchar_t* output, DWORD count) {
    return GetModuleFileNameW(g_module, output, count) > 0;
}

extern "C" BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = instance;
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}

STDAPI DllCanUnloadNow() {
    return (g_lockCount == 0 && g_objectCount == 0) ? S_OK : S_FALSE;
}

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppvObject) {
    if (rclsid != CLSID_GigaAceAsioDriver)
        return CLASS_E_CLASSNOTAVAILABLE;
    auto* factory = new GigaAceClassFactory();
    HRESULT hr = factory->QueryInterface(riid, ppvObject);
    factory->Release();
    return hr;
}

STDAPI DllRegisterServer() {
    wchar_t path[MAX_PATH]{};
    if (!modulePath(path, MAX_PATH))
        return HRESULT_FROM_WIN32(GetLastError());

    const wchar_t* clsid = L"{7D874A81-989A-457A-9EE8-7E182DDD8F37}";
    wchar_t clsidKey[256]{};
    StringCchPrintfW(clsidKey, 256, L"CLSID\\%s", clsid);

    HRESULT hr = setStringValue(HKEY_CLASSES_ROOT, clsidKey, nullptr, L"GigaACE ASIO Driver");
    if (FAILED(hr)) return hr;

    wchar_t inprocKey[300]{};
    StringCchPrintfW(inprocKey, 300, L"%s\\InprocServer32", clsidKey);
    hr = setStringValue(HKEY_CLASSES_ROOT, inprocKey, nullptr, path);
    if (FAILED(hr)) return hr;
    hr = setStringValue(HKEY_CLASSES_ROOT, inprocKey, L"ThreadingModel", L"Both");
    if (FAILED(hr)) return hr;

    hr = setStringValue(HKEY_LOCAL_MACHINE, L"SOFTWARE\\ASIO\\GigaACE ASIO Driver", L"CLSID", clsid);
    if (FAILED(hr)) return hr;
    hr = setStringValue(HKEY_LOCAL_MACHINE, L"SOFTWARE\\ASIO\\GigaACE ASIO Driver", L"Description", L"GigaACE ASIO Driver");
    if (FAILED(hr)) return hr;

    return S_OK;
}

STDAPI DllUnregisterServer() {
    deleteTree(HKEY_LOCAL_MACHINE, L"SOFTWARE\\ASIO\\GigaACE ASIO Driver");
    deleteTree(HKEY_CLASSES_ROOT, L"CLSID\\{7D874A81-989A-457A-9EE8-7E182DDD8F37}");
    return S_OK;
}
