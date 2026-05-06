#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <objbase.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static constexpr const wchar_t* kAppName = L"GigaACE Virtual Sound Card";
static constexpr const wchar_t* kAsioName = L"GigaACE ASIO Driver";
static constexpr const wchar_t* kAsioClsid = L"{7D874A81-989A-457A-9EE8-7E182DDD8F37}";

static bool is_admin() {
    BOOL isAdmin = FALSE;
    PSID adminGroup = nullptr;
    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(&ntAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID,
                                 DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0,
                                 &adminGroup)) {
        CheckTokenMembership(nullptr, adminGroup, &isAdmin);
        FreeSid(adminGroup);
    }
    return isAdmin == TRUE;
}

static fs::path module_path() {
    std::vector<wchar_t> buffer(MAX_PATH);
    DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    while (length == buffer.size()) {
        buffer.resize(buffer.size() * 2);
        length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    }
    return fs::path(std::wstring(buffer.data(), length));
}

static int relaunch_elevated() {
    fs::path self = module_path();
    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.lpVerb = L"runas";
    sei.lpFile = self.c_str();
    sei.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&sei)) {
        std::wcerr << L"Elevation was cancelled or failed.\n";
        return 1;
    }
    return 0;
}

static void copy_tree(const fs::path& source, const fs::path& target) {
    fs::create_directories(target);
    for (const auto& entry : fs::recursive_directory_iterator(source)) {
        fs::path rel = fs::relative(entry.path(), source);
        fs::path dest = target / rel;
        if (entry.is_directory()) {
            fs::create_directories(dest);
        } else if (entry.is_regular_file()) {
            fs::create_directories(dest.parent_path());
            fs::copy_file(entry.path(), dest, fs::copy_options::overwrite_existing);
        }
    }
}

static void set_reg_string(HKEY root, const std::wstring& path, const wchar_t* name, const std::wstring& value) {
    HKEY key = nullptr;
    LONG result = RegCreateKeyExW(root, path.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr);
    if (result != ERROR_SUCCESS)
        throw std::runtime_error("RegCreateKeyExW failed");
    result = RegSetValueExW(key, name, 0, REG_SZ,
                            reinterpret_cast<const BYTE*>(value.c_str()),
                            static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    if (result != ERROR_SUCCESS)
        throw std::runtime_error("RegSetValueExW failed");
}

static void register_asio(const fs::path& install_dir) {
    fs::path dll = install_dir / L"GigaAceASIO.dll";
    std::wstring clsid_path = std::wstring(L"SOFTWARE\\Classes\\CLSID\\") + kAsioClsid;
    std::wstring inproc_path = clsid_path + L"\\InprocServer32";
    std::wstring asio_path = std::wstring(L"SOFTWARE\\ASIO\\") + kAsioName;

    set_reg_string(HKEY_LOCAL_MACHINE, clsid_path, nullptr, kAsioName);
    set_reg_string(HKEY_LOCAL_MACHINE, inproc_path, nullptr, dll.wstring());
    set_reg_string(HKEY_LOCAL_MACHINE, inproc_path, L"ThreadingModel", L"Both");
    set_reg_string(HKEY_LOCAL_MACHINE, asio_path, L"CLSID", kAsioClsid);
    set_reg_string(HKEY_LOCAL_MACHINE, asio_path, L"Description", kAsioName);
}

static fs::path known_folder(REFKNOWNFOLDERID id) {
    PWSTR raw = nullptr;
    if (FAILED(SHGetKnownFolderPath(id, 0, nullptr, &raw)))
        return {};
    fs::path path(raw);
    CoTaskMemFree(raw);
    return path;
}

static void create_shortcut(const fs::path& link_path, const fs::path& target, const fs::path& working_dir) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    IShellLinkW* link = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, reinterpret_cast<void**>(&link)))) {
        link->SetPath(target.c_str());
        link->SetWorkingDirectory(working_dir.c_str());
        link->SetDescription(kAppName);
        IPersistFile* file = nullptr;
        if (SUCCEEDED(link->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&file)))) {
            fs::create_directories(link_path.parent_path());
            file->Save(link_path.c_str(), TRUE);
            file->Release();
        }
        link->Release();
    }
    CoUninitialize();
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    try {
        if (!is_admin())
            return relaunch_elevated();

        fs::path source_dir = module_path().parent_path();
        wchar_t programFiles[MAX_PATH]{};
        DWORD len = GetEnvironmentVariableW(L"ProgramFiles", programFiles, MAX_PATH);
        if (len == 0 || len >= MAX_PATH)
            throw std::runtime_error("ProgramFiles not found");

        fs::path install_dir = fs::path(programFiles) / kAppName;
        copy_tree(source_dir, install_dir);
        register_asio(install_dir);

        fs::path app = install_dir / L"GigaAceVirtualSoundCard.exe";
        fs::path desktop = known_folder(FOLDERID_Desktop);
        if (!desktop.empty())
            create_shortcut(desktop / L"GigaACE Virtual Sound Card.lnk", app, install_dir);

        fs::path startMenu = known_folder(FOLDERID_CommonPrograms);
        if (!startMenu.empty())
            create_shortcut(startMenu / L"GigaACE Virtual Sound Card.lnk", app, install_dir);

        ShellExecuteW(nullptr, L"open", app.c_str(), nullptr, install_dir.c_str(), SW_SHOWNORMAL);
        MessageBoxW(nullptr,
                    L"GigaACE Virtual Sound Card was installed and the ASIO driver was registered.\n\nRestart REAPER before selecting the ASIO driver.",
                    kAppName,
                    MB_OK | MB_ICONINFORMATION);
        return 0;
    } catch (const std::exception& ex) {
        MessageBoxA(nullptr, ex.what(), "GigaACE setup failed", MB_OK | MB_ICONERROR);
        return 1;
    }
}
