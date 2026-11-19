typedef unsigned short Wchar;
typedef unsigned long ulong;

__declspec(dllimport) void __stdcall ExitProcess(ulong code);
__declspec(dllimport) void* __stdcall GetStdHandle(ulong n);
__declspec(dllimport) int __stdcall WriteFile(void* file, const void* buf, ulong n, ulong* written, void* ov);
__declspec(dllimport) void* __stdcall CreateFileW(const Wchar* name, ulong access, ulong share, void* sec, ulong disp, ulong flags, void* templ);
__declspec(dllimport) int __stdcall CloseHandle(void* handle);
__declspec(dllimport) int __stdcall GetFileAttributesExW(const Wchar* name, int level, void* info);
__declspec(dllimport) ulong __stdcall GetFinalPathNameByHandleW(void* file, Wchar* buf, ulong cap, ulong flags);
__declspec(dllimport) ulong __stdcall GetModuleFileNameW(void* module, Wchar* buf, ulong n);
__declspec(dllimport) void* __stdcall LoadLibraryW(const Wchar* name);
__declspec(dllimport) int __stdcall DeleteFileW(const Wchar* name);
__declspec(dllimport) int __stdcall SetEnvironmentVariableW(const Wchar* name, const Wchar* value);
__declspec(dllimport) ulong __stdcall GetLastError(void);

static int g_failures;

static void print(const char* text) {
    ulong written = 0;
    int n = 0;
    while (text[n]) ++n;
    WriteFile(GetStdHandle((ulong)-11), text, (ulong)n, &written, 0);
}

static void print_wide(const Wchar* text) {
    char buf[200];
    int i = 0;
    while (text && text[i] && i + 1 < 200) {
        buf[i] = (text[i] < 128) ? (char)text[i] : '?';
        ++i;
    }
    buf[i++] = '\r';
    buf[i++] = '\n';
    buf[i] = 0;
    print(buf);
}

static int weq(const Wchar* a, const Wchar* b) {
    int i = 0;
    while (a[i] && b[i]) {
        if (a[i] != b[i]) return 0;
        ++i;
    }
    return a[i] == 0 && b[i] == 0;
}

static void expect(int condition, const char* name) {
    print(condition ? "OK  " : "FAIL ");
    print(name);
    print("\r\n");
    if (!condition) ++g_failures;
}

static int wlen(const Wchar* text) {
    int n = 0;
    while (text[n]) ++n;
    return n;
}

static void final_name(void* handle, Wchar* dest, int cap) {
    Wchar buf[400];
    ulong n;
    int i;
    int slash = -1;
    int k = 0;
    for (i = 0; i < cap; ++i) dest[i] = 0;
    if (!handle || handle == (void*)(long long)-1) return;
    n = GetFinalPathNameByHandleW(handle, buf, 400, 0);
    if (n == 0 || n >= 400) return;
    for (i = 0; i < (int)n; ++i) if (buf[i] == '\\' || buf[i] == '/') slash = i;
    i = slash + 1;
    while (i < (int)n && k + 1 < cap) dest[k++] = buf[i++];
    dest[k] = 0;
}

static void join(Wchar* dest, int cap, const Wchar* a, const Wchar* b) {
    int i = 0;
    int j = 0;
    while (a[i] && i + 1 < cap) {
        dest[i] = a[i];
        ++i;
    }
    while (b[j] && i + 1 < cap) {
        dest[i++] = b[j++];
    }
    dest[i] = 0;
}

typedef struct AttrData {
    ulong attrs;
    ulong c_low, c_high, a_low, a_high, w_low, w_high;
    ulong size_high;
    ulong size_low;
} AttrData;

void Main(void) {
    static Wchar self[300];
    static Wchar dir[300];
    static Wchar dll[300];
    static Wchar log_path[300];
    static Wchar state_path[300];
    static Wchar path[300];
    static Wchar name[80];
    const Wchar* arrange = L"D:\\SteamLibrary\\steamapps\\common\\The King of Fighters 2002 Unlimited Match\\Data\\BGM\\UM48000Hz\\";
    const Wchar* original = L"D:\\SteamLibrary\\steamapps\\common\\The King of Fighters 2002 Unlimited Match\\Data\\BGM\\48000Hz\\";
    void* module;
    void* file;
    AttrData info;
    AttrData real;
    ulong n;
    int i;
    int slash = -1;

    n = GetModuleFileNameW(0, self, 300);
    for (i = 0; i < (int)n; ++i) if (self[i] == '\\' || self[i] == '/') slash = i;
    for (i = 0; i < 300; ++i) dir[i] = dll[i] = log_path[i] = state_path[i] = 0;
    for (i = 0; i < slash + 1 && i < 300; ++i) dir[i] = self[i];
    join(log_path, 300, dir, L"bgm.log");
    join(state_path, 300, dir, L"last_track.txt");
    join(dll, 300, dir, L"RandomBgm.dll");
    DeleteFileW(log_path);
    DeleteFileW(state_path);
    SetEnvironmentVariableW(L"KOF_BGM_RNG", L"sequential");

    module = LoadLibraryW(dll);
    if (!module) {
        print("FAIL no se cargo RandomBgm.dll\r\n");
        ExitProcess(1);
    }

    join(path, 300, arrange, L"BGM17.ogg");
    file = CreateFileW(path, 0x80000000u, 1u, 0, 3u, 0x80u, 0);
    final_name(file, name, 80);
    if (file != (void*)(long long)-1) CloseHandle(file);
    expect(weq(name, L"BGM15.ogg"), "Iori/Destiny abre la pista sorteada BGM15");

    join(path, 300, arrange, L"BGM12LOOP.ogg");
    file = CreateFileW(path, 0x80000000u, 1u, 0, 3u, 0x80u, 0);
    final_name(file, name, 80);
    if (file != (void*)(long long)-1) CloseHandle(file);
    expect(weq(name, L"BGM15.ogg"), "el LOOP de otro personaje sigue en BGM15");

    join(path, 300, arrange, L"BGM22.ogg");
    info.size_low = 0;
    real.size_low = 1;
    GetFileAttributesExW(path, 0, &info);
    join(path, 300, arrange, L"BGM15.ogg");
    GetFileAttributesExW(path, 0, &real);
    expect(info.size_low == real.size_low && info.size_high == real.size_high, "el tamano consultado es el de la pista fijada");

    join(path, 300, arrange, L"BGM41.ogg");
    file = CreateFileW(path, 0x80000000u, 1u, 0, 3u, 0x80u, 0);
    final_name(file, name, 80);
    if (file != (void*)(long long)-1) CloseHandle(file);
    expect(weq(name, L"BGM41.ogg"), "Select se abre sin sustituir");

    join(path, 300, arrange, L"BGM25.ogg");
    file = CreateFileW(path, 0x80000000u, 1u, 0, 3u, 0x80u, 0);
    final_name(file, name, 80);
    if (file != (void*)(long long)-1) CloseHandle(file);
    expect(weq(name, L"BGM16.ogg"), "el combate nuevo no repite BGM15");

    file = CreateFileW(L"D:\\SteamLibrary\\steamapps\\common\\The King of Fighters 2002 Unlimited Match\\Data\\joined.wav", 0x80000000u, 1u, 0, 3u, 0x80u, 0);
    final_name(file, name, 80);
    if (file != (void*)(long long)-1) CloseHandle(file);
    expect(weq(name, L"joined.wav"), "joined.wav no se redirige");

    join(path, 300, original, L"23.ogg");
    file = CreateFileW(path, 0x80000000u, 1u, 0, 3u, 0x80u, 0);
    final_name(file, name, 80);
    if (file != (void*)(long long)-1) CloseHandle(file);
    if (!weq(name, L"25.ogg")) print_wide(name);
    expect(weq(name, L"25.ogg"), "el tercer sorteo original es 25.ogg");

    if (g_failures) {
        print("pruebas del hook fallaron\r\n");
        ExitProcess(1);
    }
    print("Todas las pruebas del hook pasaron\r\n");
    ExitProcess(0);
}
