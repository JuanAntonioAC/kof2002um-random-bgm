/* No se compila. El mod se construye con los archivos .c y build_native.bat. */
#include "Config.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

namespace {

HMODULE g_module = nullptr;
HANDLE g_log = INVALID_HANDLE_VALUE;
std::mutex g_mutex;
PathRouter* g_router = nullptr;
std::wstring g_dir;
std::atomic<int> g_seq{0};
bool g_sequential = false;

using CreateFileW_t = HANDLE(WINAPI*)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
using GetFileAttributesW_t = DWORD(WINAPI*)(LPCWSTR);
using GetFileAttributesExW_t = BOOL(WINAPI*)(LPCWSTR, GET_FILEEX_INFO_LEVELS, LPVOID);

CreateFileW_t g_create_file_w = nullptr;
GetFileAttributesW_t g_get_file_attributes_w = nullptr;
GetFileAttributesExW_t g_get_file_attributes_ex_w = nullptr;

thread_local int g_depth = 0;

std::wstring ModuleDir() {
    wchar_t buffer[MAX_PATH];
    DWORD n = GetModuleFileNameW(g_module, buffer, MAX_PATH);
    std::wstring path(buffer, n);
    size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return L"";
    return path.substr(0, slash + 1);
}

void LogLine(const std::wstring& text) {
    if (g_log == INVALID_HANDLE_VALUE) return;
    SYSTEMTIME time{};
    GetLocalTime(&time);
    wchar_t prefix[40];
    swprintf_s(prefix, L"%04u-%02u-%02u %02u:%02u:%02u ",
        time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond);
    std::wstring line = prefix + text + L"\r\n";
    int bytes = WideCharToMultiByte(CP_UTF8, 0, line.c_str(), static_cast<int>(line.size()), nullptr, 0, nullptr, nullptr);
    if (bytes <= 0) return;
    std::string utf8(static_cast<size_t>(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, line.c_str(), static_cast<int>(line.size()), utf8.data(), bytes, nullptr, nullptr);
    DWORD written = 0;
    WriteFile(g_log, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
}

const wchar_t* ActionName(RouteAction action) {
    switch (action) {
    case RouteAction::Boundary: return L"BOUNDARY";
    case RouteAction::Lock: return L"LOCK";
    case RouteAction::Hold: return L"HOLD";
    case RouteAction::Passthrough: return L"PASS";
    default: return L"IGNORE";
    }
}

bool ApplyRoute(const wchar_t* incoming, std::wstring& storage) {
    if (!incoming || !g_router) return false;
    std::lock_guard<std::mutex> lock(g_mutex);
    RouteResult result = g_router->Route(incoming);
    if (result.action == RouteAction::Ignore) return false;
    if (result.action == RouteAction::Lock || result.action == RouteAction::Hold || result.action == RouteAction::Boundary || result.action == RouteAction::Passthrough) {
        std::wstring message = ActionName(result.action);
        message += L" ";
        message += result.bank;
        message += L" ";
        message += result.requested_id;
        if (result.requested_loop) message += L" LOOP";
        if (!result.chosen_id.empty()) {
            message += L" -> ";
            message += result.chosen_id;
        }
        LogLine(message);
    }
    if (result.path == incoming) return false;
    storage = result.path;
    if ((result.action == RouteAction::Lock) && !g_sequential) {
        std::wstring state_path = g_dir + L"last_track.txt";
        HANDLE file = g_create_file_w(
            state_path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE) {
            std::wstring body = result.bank + L" " + result.chosen_id + L"\r\n";
            std::string ascii(body.begin(), body.end());
            DWORD written = 0;
            WriteFile(file, ascii.data(), static_cast<DWORD>(ascii.size()), &written, nullptr);
            CloseHandle(file);
        }
    }
    return true;
}

HANDLE WINAPI HookCreateFileW(LPCWSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD disp, DWORD flags, HANDLE templ) {
    if (g_depth > 0 || !g_create_file_w) return g_create_file_w(name, access, share, sa, disp, flags, templ);
    ++g_depth;
    std::wstring storage;
    const wchar_t* used = name;
    if (ApplyRoute(name, storage)) used = storage.c_str();
    HANDLE handle = g_create_file_w(used, access, share, sa, disp, flags, templ);
    --g_depth;
    return handle;
}

DWORD WINAPI HookGetFileAttributesW(LPCWSTR name) {
    if (g_depth > 0 || !g_get_file_attributes_w) return g_get_file_attributes_w(name);
    ++g_depth;
    std::wstring storage;
    const wchar_t* used = name;
    if (ApplyRoute(name, storage)) used = storage.c_str();
    DWORD attr = g_get_file_attributes_w(used);
    --g_depth;
    return attr;
}

BOOL WINAPI HookGetFileAttributesExW(LPCWSTR name, GET_FILEEX_INFO_LEVELS level, LPVOID info) {
    if (g_depth > 0 || !g_get_file_attributes_ex_w) return g_get_file_attributes_ex_w(name, level, info);
    ++g_depth;
    std::wstring storage;
    const wchar_t* used = name;
    if (ApplyRoute(name, storage)) used = storage.c_str();
    BOOL ok = g_get_file_attributes_ex_w(used, level, info);
    --g_depth;
    return ok;
}

bool PatchImport(HMODULE module, const char* function, void* detour, void** original) {
    auto base = reinterpret_cast<uint8_t*>(module);
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;
    auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return false;
    auto desc = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress);
    bool patched = false;
    for (; desc->Name; ++desc) {
        auto thunk = reinterpret_cast<IMAGE_THUNK_DATA*>(base + desc->FirstThunk);
        auto orig = reinterpret_cast<IMAGE_THUNK_DATA*>(base + (desc->OriginalFirstThunk ? desc->OriginalFirstThunk : desc->FirstThunk));
        for (; orig->u1.AddressOfData; ++orig, ++thunk) {
            if (IMAGE_SNAP_BY_ORDINAL(orig->u1.Ordinal)) continue;
            auto imp = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + orig->u1.AddressOfData);
            if (strcmp(imp->Name, function) != 0) continue;
            DWORD old = 0;
            if (!VirtualProtect(&thunk->u1.Function, sizeof(void*), PAGE_READWRITE, &old)) continue;
            if (original && !*original) *original = reinterpret_cast<void*>(thunk->u1.Function);
            thunk->u1.Function = reinterpret_cast<ULONG_PTR>(detour);
            VirtualProtect(&thunk->u1.Function, sizeof(void*), old, &old);
            patched = true;
        }
    }
    return patched;
}

int SequentialRng(int n) {
    if (n <= 1) return 0;
    int value = g_seq.fetch_add(1);
    if (value < 0) value = -value;
    return value % n;
}

int RandomRng(int n) {
    if (n <= 1) return 0;
    static std::atomic<uint32_t> state{static_cast<uint32_t>(GetTickCount()) ^ 0xA5A5u};
    uint32_t x = state.load();
    uint32_t next;
    do {
        next = x * 1664525u + 1013904223u;
    } while (!state.compare_exchange_weak(x, next));
    return static_cast<int>(next % static_cast<uint32_t>(n));
}

void LoadLastTrack(Settings& settings) {
    if (g_sequential || !g_router) return;
    std::wstring path = g_dir + L"last_track.txt";
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    char buffer[128] = {};
    DWORD read = 0;
    ReadFile(file, buffer, sizeof(buffer) - 1, &read, nullptr);
    CloseHandle(file);
    std::string text(buffer, buffer + read);
    auto space = text.find(' ');
    if (space == std::string::npos) return;
    std::wstring bank = AsciiToWide(Trim(text.substr(0, space)));
    std::wstring id = AsciiToWide(Trim(text.substr(space + 1)));
    if (!id.empty()) g_router->set_last(bank, id);
    (void)settings;
}

void Install() {
    g_dir = ModuleDir();
    wchar_t mode[32] = {};
    if (GetEnvironmentVariableW(L"KOF_BGM_RNG", mode, 32) > 0 && wcscmp(mode, L"sequential") == 0)
        g_sequential = true;

    std::wstring log_path = g_dir + L"bgm.log";
    g_log = CreateFileW(log_path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

    Settings settings;
    std::string error;
    std::wstring cfg = g_dir + L"tracks.cfg";
    if (!LoadSettings(cfg, settings, error)) {
        LogLine(L"CONFIG ERROR");
        return;
    }
    if (!settings.enabled) {
        LogLine(L"DISABLED");
        return;
    }

    g_router = new PathRouter(settings, g_sequential ? SequentialRng : RandomRng);
    LoadLastTrack(settings);

    HMODULE exe = GetModuleHandleW(nullptr);
    bool hooked = false;
    if (PatchImport(exe, "CreateFileW", reinterpret_cast<void*>(HookCreateFileW), reinterpret_cast<void**>(&g_create_file_w))) {
        LogLine(L"HOOK CreateFileW");
        hooked = true;
    }
    if (PatchImport(exe, "GetFileAttributesW", reinterpret_cast<void*>(HookGetFileAttributesW), reinterpret_cast<void**>(&g_get_file_attributes_w)))
        LogLine(L"HOOK GetFileAttributesW");
    if (PatchImport(exe, "GetFileAttributesExW", reinterpret_cast<void*>(HookGetFileAttributesExW), reinterpret_cast<void**>(&g_get_file_attributes_ex_w)))
        LogLine(L"HOOK GetFileAttributesExW");
    if (!hooked) LogLine(L"HOOK FAILED CreateFileW");
    else LogLine(L"READY");
}

}  // namespace

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = instance;
        DisableThreadLibraryCalls(instance);
        Install();
    }
    return TRUE;
}
