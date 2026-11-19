#include "router.h"

void* memcpy(void* dst, const void* src, unsigned long long n);
void* memset(void* dst, int value, unsigned long long n);

typedef unsigned long ulong;
typedef unsigned long long ulong64;

__declspec(dllimport) void* __stdcall GetModuleHandleW(const Wchar* name);
__declspec(dllimport) ulong __stdcall GetModuleFileNameW(void* module, Wchar* buf, ulong n);
__declspec(dllimport) void* __stdcall CreateFileW(const Wchar* name, ulong access, ulong share, void* sec, ulong disp, ulong flags, void* templ);
__declspec(dllimport) int __stdcall ReadFile(void* file, void* buf, ulong n, ulong* read, void* ov);
__declspec(dllimport) int __stdcall WriteFile(void* file, const void* buf, ulong n, ulong* written, void* ov);
__declspec(dllimport) int __stdcall CloseHandle(void* handle);
__declspec(dllimport) ulong __stdcall GetFileSize(void* file, ulong* high);
__declspec(dllimport) ulong __stdcall GetFileAttributesW(const Wchar* name);
__declspec(dllimport) int __stdcall GetFileAttributesExW(const Wchar* name, int level, void* info);
__declspec(dllimport) int __stdcall VirtualProtect(void* addr, ulong64 size, ulong neu, ulong* old);
__declspec(dllimport) ulong __stdcall GetEnvironmentVariableW(const Wchar* name, Wchar* buf, ulong n);
__declspec(dllimport) void __stdcall GetLocalTime(void* time);
__declspec(dllimport) void __stdcall DisableThreadLibraryCalls(void* module);
__declspec(dllimport) ulong __stdcall GetCurrentThreadId(void);

typedef void* (__stdcall *CreateFileW_fn)(const Wchar*, ulong, ulong, void*, ulong, ulong, void*);
typedef ulong (__stdcall *GetFileAttributesW_fn)(const Wchar*);
typedef int (__stdcall *GetFileAttributesExW_fn)(const Wchar*, int, void*);

static void* g_module;
static void* g_log;
static Router g_router;
static int g_ready;
static int g_sequential;
static int g_seq;
static Wchar g_dir[300];
static CreateFileW_fn g_create;
static GetFileAttributesW_fn g_getattr;
static GetFileAttributesExW_fn g_getattrex;

#define INVALID_FILE ((void*)(long long)-1)
#define GENERIC_READ 0x80000000u
#define GENERIC_WRITE 0x40000000u
#define FILE_SHARE_READ 1u
#define FILE_SHARE_WRITE 2u
#define OPEN_EXISTING 3u
#define OPEN_ALWAYS 4u
#define CREATE_ALWAYS 2u
#define FILE_ATTRIBUTE_NORMAL 0x80u
#define FILE_APPEND_DATA 4u
#define PAGE_READWRITE 4u

static int depth_of_this_thread(int delta) {
    static ulong tids[32];
    static int depths[32];
    ulong tid = GetCurrentThreadId();
    int i;
    int free_slot = -1;
    for (i = 0; i < 32; ++i) {
        if (tids[i] == tid) {
            depths[i] += delta;
            return depths[i];
        }
        if (tids[i] == 0 && free_slot < 0) free_slot = i;
    }
    if (free_slot >= 0) {
        tids[free_slot] = tid;
        depths[free_slot] = delta > 0 ? delta : 0;
        return depths[free_slot];
    }
    return 1;
}

static int wlen(const Wchar* text) {
    int n = 0;
    while (text && text[n]) ++n;
    return n;
}

static void wcopy(Wchar* dst, int cap, const Wchar* src) {
    int i = 0;
    if (cap <= 0) return;
    while (src && src[i] && i + 1 < cap) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = 0;
}

static void log_text(const char* text) {
    ulong written = 0;
    int n = 0;
    if (!g_log || g_log == INVALID_FILE || !text) return;
    while (text[n]) ++n;
    WriteFile(g_log, text, (ulong)n, &written, 0);
}

static void log_line(const char* text) {
    struct {
        unsigned short year, month, dow, day, hour, minute, second, ms;
    } time;
    char prefix[40];
    int n = 0;
    int i = 0;
    ulong written = 0;
    GetLocalTime(&time);
    prefix[n++] = '0' + (time.year / 1000) % 10;
    prefix[n++] = '0' + (time.year / 100) % 10;
    prefix[n++] = '0' + (time.year / 10) % 10;
    prefix[n++] = '0' + time.year % 10;
    prefix[n++] = '-';
    prefix[n++] = '0' + time.month / 10;
    prefix[n++] = '0' + time.month % 10;
    prefix[n++] = '-';
    prefix[n++] = '0' + time.day / 10;
    prefix[n++] = '0' + time.day % 10;
    prefix[n++] = ' ';
    prefix[n++] = '0' + time.hour / 10;
    prefix[n++] = '0' + time.hour % 10;
    prefix[n++] = ':';
    prefix[n++] = '0' + time.minute / 10;
    prefix[n++] = '0' + time.minute % 10;
    prefix[n++] = ':';
    prefix[n++] = '0' + time.second / 10;
    prefix[n++] = '0' + time.second % 10;
    prefix[n++] = ' ';
    prefix[n] = 0;
    log_text(prefix);
    log_text(text);
    log_text("\r\n");
    (void)i;
    (void)written;
}

static void wide_to_ascii(const Wchar* src, char* dst, int cap) {
    int i = 0;
    if (cap <= 0) return;
    while (src && src[i] && i + 1 < cap) {
        Wchar ch = src[i];
        dst[i] = (ch < 128) ? (char)ch : '?';
        ++i;
    }
    dst[i] = 0;
}

static int sequential_rng(int n) {
    int value;
    if (n <= 1) return 0;
    value = g_seq++;
    if (value < 0) value = -value;
    return value % n;
}

static int random_rng(int n) {
    static ulong state = 0x12345678u;
    if (n <= 1) return 0;
    state = state * 1664525u + 1013904223u;
    return (int)(state % (ulong)n);
}

static int read_file(const Wchar* path, char* buf, int cap, int* out_len) {
    void* file;
    ulong size;
    ulong read = 0;
    *out_len = 0;
    file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_FILE) return 0;
    size = GetFileSize(file, 0);
    if (size >= (ulong)cap) size = (ulong)cap - 1;
    if (!ReadFile(file, buf, size, &read, 0)) {
        CloseHandle(file);
        return 0;
    }
    CloseHandle(file);
    buf[read] = 0;
    *out_len = (int)read;
    return 1;
}

static void load_last(void) {
    char buf[128];
    int n = 0;
    int i = 0;
    int space = -1;
    Wchar bank[16];
    Wchar id[R_ID_CHARS];
    Wchar path[300];
    if (g_sequential) return;
    wcopy(path, 300, g_dir);
    wcopy(path + wlen(path), 300 - wlen(path), L"last_track.txt");
    if (!read_file(path, buf, 128, &n)) return;
    while (i < n && buf[i] != ' ' && buf[i] != '\r' && buf[i] != '\n') ++i;
    if (i >= n || buf[i] != ' ') return;
    space = i;
    {
        int j = 0;
        memset(bank, 0, sizeof(bank));
        memset(id, 0, sizeof(id));
        for (j = 0; j < space && j + 1 < 16; ++j) bank[j] = (Wchar)(unsigned char)buf[j];
        j = space + 1;
        i = 0;
        while (j < n && buf[j] != '\r' && buf[j] != '\n' && i + 1 < R_ID_CHARS) {
            id[i++] = (Wchar)(unsigned char)buf[j++];
        }
        if (id[0]) router_set_last(&g_router, bank, id);
    }
}

static int patch_import(void* module, const char* function, void* detour, void** original) {
    unsigned char* base = (unsigned char*)module;
    int lfanew;
    ulong import_rva;
    unsigned char* desc;
    int patched = 0;
    if (!base || *(unsigned short*)base != 0x5A4D) return 0;
    lfanew = *(int*)(base + 0x3C);
    if (*(ulong*)(base + lfanew) != 0x4550) return 0;
    import_rva = *(ulong*)(base + lfanew + 144);
    if (!import_rva) return 0;
    desc = base + import_rva;
    for (;;) {
        ulong orig_rva = *(ulong*)(desc + 0);
        ulong name_rva = *(ulong*)(desc + 12);
        ulong thunk_rva = *(ulong*)(desc + 16);
        unsigned char* orig;
        unsigned char* thunk;
        if (!name_rva) break;
        if (!orig_rva) orig_rva = thunk_rva;
        orig = base + orig_rva;
        thunk = base + thunk_rva;
        for (;;) {
            ulong64 entry = *(ulong64*)orig;
            if (!entry) break;
            if ((entry & 0x8000000000000000ull) == 0) {
                const char* imp = (const char*)(base + (ulong)entry + 2);
                int k = 0;
                int same = 1;
                while (function[k] || imp[k]) {
                    if (function[k] != imp[k]) { same = 0; break; }
                    ++k;
                }
                if (same) {
                    ulong old = 0;
                    if (VirtualProtect(thunk, sizeof(void*), PAGE_READWRITE, &old)) {
                        if (original && !*original) *original = *(void**)thunk;
                        *(void**)thunk = detour;
                        VirtualProtect(thunk, sizeof(void*), old, &old);
                        patched = 1;
                    }
                }
            }
            orig += 8;
            thunk += 8;
        }
        desc += 20;
    }
    return patched;
}

static int apply_route(const Wchar* incoming, Wchar* storage, int cap) {
    RouteResult result;
    char line[180];
    char id_a[16];
    char chosen_a[16];
    int pos = 0;
    const char* action;
    if (!incoming || !g_ready) return 0;
    router_route(&g_router, incoming, &result);
    if (result.action == ROUTE_IGNORE) return 0;
    if (result.action == ROUTE_BOUNDARY) action = "BOUNDARY ";
    else if (result.action == ROUTE_LOCK) action = "LOCK ";
    else if (result.action == ROUTE_HOLD) action = "HOLD ";
    else action = "PASS ";
    while (action[pos] && pos + 1 < 180) { line[pos] = action[pos]; ++pos; }
    wide_to_ascii(result.bank, id_a, 16);
    {
        int k = 0;
        while (id_a[k] && pos + 1 < 180) line[pos++] = id_a[k++];
    }
    line[pos++] = ' ';
    wide_to_ascii(result.requested_id, id_a, 16);
    {
        int k = 0;
        while (id_a[k] && pos + 1 < 180) line[pos++] = id_a[k++];
    }
    if (result.requested_loop && pos + 5 < 180) {
        line[pos++] = ' ';
        line[pos++] = 'L';
        line[pos++] = 'O';
        line[pos++] = 'O';
        line[pos++] = 'P';
    }
    if (result.chosen_id[0] && pos + 4 < 180) {
        int k = 0;
        line[pos++] = ' ';
        line[pos++] = '-';
        line[pos++] = '>';
        line[pos++] = ' ';
        wide_to_ascii(result.chosen_id, chosen_a, 16);
        while (chosen_a[k] && pos + 1 < 180) line[pos++] = chosen_a[k++];
    }
    line[pos] = 0;
    log_line(line);
    if (result.action == ROUTE_LOCK && !g_sequential && g_create) {
        Wchar state_path[300];
        char body[40];
        int b = 0;
        int k = 0;
        void* file;
        ulong written = 0;
        wcopy(state_path, 300, g_dir);
        wcopy(state_path + wlen(state_path), 300 - wlen(state_path), L"last_track.txt");
        wide_to_ascii(result.bank, body, 40);
        while (body[b]) ++b;
        body[b++] = ' ';
        wide_to_ascii(result.chosen_id, body + b, 40 - b);
        while (body[k]) ++k;
        body[k++] = '\r';
        body[k++] = '\n';
        body[k] = 0;
        file = g_create(state_path, GENERIC_WRITE, FILE_SHARE_READ, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
        if (file != INVALID_FILE) {
            WriteFile(file, body, (ulong)k, &written, 0);
            CloseHandle(file);
        }
    }
    if (result.path[0] == 0) return 0;
    {
        int i = 0;
        int same = 1;
        while (incoming[i] || result.path[i]) {
            if (incoming[i] != result.path[i]) { same = 0; break; }
            if (!incoming[i]) break;
            ++i;
        }
        if (same) return 0;
    }
    wcopy(storage, cap, result.path);
    return 1;
}

static void* __stdcall HookCreateFileW(const Wchar* name, ulong access, ulong share, void* sec, ulong disp, ulong flags, void* templ) {
    int depth;
    Wchar storage[R_PATH_CHARS];
    const Wchar* used = name;
    if (!g_create) return INVALID_FILE;
    depth = depth_of_this_thread(1);
    if (depth == 1 && apply_route(name, storage, R_PATH_CHARS)) used = storage;
    {
        void* handle = g_create(used, access, share, sec, disp, flags, templ);
        depth_of_this_thread(-1);
        return handle;
    }
}

static ulong __stdcall HookGetFileAttributesW(const Wchar* name) {
    int depth;
    Wchar storage[R_PATH_CHARS];
    const Wchar* used = name;
    if (!g_getattr) return 0xFFFFFFFFu;
    depth = depth_of_this_thread(1);
    if (depth == 1 && apply_route(name, storage, R_PATH_CHARS)) used = storage;
    {
        ulong attr = g_getattr(used);
        depth_of_this_thread(-1);
        return attr;
    }
}

static int __stdcall HookGetFileAttributesExW(const Wchar* name, int level, void* info) {
    int depth;
    Wchar storage[R_PATH_CHARS];
    const Wchar* used = name;
    if (!g_getattrex) return 0;
    depth = depth_of_this_thread(1);
    if (depth == 1 && apply_route(name, storage, R_PATH_CHARS)) used = storage;
    {
        int ok = g_getattrex(used, level, info);
        depth_of_this_thread(-1);
        return ok;
    }
}

static char g_cfg_text[32768];
static Settings g_settings;

static void install(void) {
    Wchar mode[32];
    Wchar log_path[300];
    Wchar cfg_path[300];
    int length = 0;
    int n;
    g_log = INVALID_FILE;
    n = (int)GetModuleFileNameW(g_module, g_dir, 300);
    while (n > 0 && g_dir[n - 1] != '\\' && g_dir[n - 1] != '/') --n;
    g_dir[n] = 0;
    memset(mode, 0, sizeof(mode));
    if (GetEnvironmentVariableW(L"KOF_BGM_RNG", mode, 32) > 0) {
        if ((mode[0] == 's' || mode[0] == 'S') && (mode[1] == 'e' || mode[1] == 'E')) g_sequential = 1;
    }
    wcopy(log_path, 300, g_dir);
    wcopy(log_path + wlen(log_path), 300 - wlen(log_path), L"bgm.log");
    g_log = CreateFileW(log_path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    wcopy(cfg_path, 300, g_dir);
    wcopy(cfg_path + wlen(cfg_path), 300 - wlen(cfg_path), L"tracks.cfg");
    if (!read_file(cfg_path, g_cfg_text, (int)sizeof(g_cfg_text), &length)) {
        log_line("CONFIG ERROR");
        return;
    }
    parse_settings(g_cfg_text, length, &g_settings);
    if (!g_settings.enabled) {
        log_line("DISABLED");
        return;
    }
    router_init(&g_router, &g_settings, g_sequential ? sequential_rng : random_rng);
    g_ready = 1;
    load_last();
    if (patch_import(GetModuleHandleW(0), "CreateFileW", (void*)HookCreateFileW, (void**)&g_create))
        log_line("HOOK CreateFileW");
    else log_line("HOOK FAILED CreateFileW");
    if (patch_import(GetModuleHandleW(0), "GetFileAttributesW", (void*)HookGetFileAttributesW, (void**)&g_getattr))
        log_line("HOOK GetFileAttributesW");
    if (patch_import(GetModuleHandleW(0), "GetFileAttributesExW", (void*)HookGetFileAttributesExW, (void**)&g_getattrex))
        log_line("HOOK GetFileAttributesExW");
    log_line("READY");
}

int __stdcall DllMain(void* instance, ulong reason, void* reserved) {
    (void)reserved;
    if (reason == 1) {
        g_module = instance;
        DisableThreadLibraryCalls(instance);
        install();
    }
    return 1;
}
