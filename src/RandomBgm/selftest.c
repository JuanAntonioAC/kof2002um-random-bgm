#include "router.h"

void* memcpy(void* dst, const void* src, unsigned long long n);
void* memset(void* dst, int value, unsigned long long n);

__declspec(dllimport) void __stdcall ExitProcess(unsigned long code);
__declspec(dllimport) void* __stdcall GetStdHandle(unsigned long n);
__declspec(dllimport) int __stdcall WriteFile(void* file, const void* buf, unsigned long n, unsigned long* written, void* ov);
__declspec(dllimport) void* __stdcall CreateFileW(const Wchar* name, unsigned long access, unsigned long share, void* sec, unsigned long disp, unsigned long flags, void* templ);
__declspec(dllimport) int __stdcall ReadFile(void* file, void* buf, unsigned long n, unsigned long* read, void* ov);
__declspec(dllimport) int __stdcall CloseHandle(void* handle);
__declspec(dllimport) unsigned long __stdcall GetFileSize(void* file, unsigned long* high);

static int g_failures;
static int g_next;

static int sequential(int n) {
    if (n <= 0) return 0;
    return g_next++ % n;
}

static void print(const char* text) {
    unsigned long written = 0;
    int n = 0;
    while (text[n]) ++n;
    WriteFile(GetStdHandle((unsigned long)-11), text, (unsigned long)n, &written, 0);
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
    if (condition) {
        print("OK  ");
        print(name);
        print("\r\n");
        return;
    }
    print("FAIL ");
    print(name);
    print("\r\n");
    ++g_failures;
}

static void fill_sample(Settings* settings) {
    const char* text =
        "enabled=1\r\n"
        "avoid_repeat=1\r\n"
        "[arrange_loop]\r\nBGM12\r\n"
        "[arrange_boundary]\r\nBGM01\r\nBGM03\r\nBGM04\r\nBGM41\r\nBGM42\r\n"
        "[arrange_source]\r\nBGM12\r\nBGM15\r\nBGM16\r\nBGM17\r\n"
        "[arrange_pool]\r\nBGM15\r\nBGM16\r\nBGM17\r\n"
        "[original_boundary]\r\n21\r\ndummy\r\n"
        "[original_source]\r\n23\r\n24\r\n2A\r\n"
        "[original_pool]\r\n23\r\n24\r\n2A\r\n";
    int n = 0;
    while (text[n]) ++n;
    parse_settings(text, n, settings);
}

static int read_all(const Wchar* path, char* buf, int cap) {
    void* file = CreateFileW(path, 0x80000000u, 1u, 0, 3u, 0x80u, 0);
    unsigned long size;
    unsigned long read = 0;
    if (file == (void*)(long long)-1) return -1;
    size = GetFileSize(file, 0);
    if (size >= (unsigned long)cap) size = (unsigned long)cap - 1;
    ReadFile(file, buf, size, &read, 0);
    CloseHandle(file);
    buf[read] = 0;
    return (int)read;
}

void Main(void) {
    static Settings settings;
    static Router router;
    static char file[32768];
    static Settings loaded;
    RouteResult result;
    int n;
    fill_sample(&settings);
    router_init(&router, &settings, sequential);

    router_route(&router, L"D:\\game\\Data\\BGM\\UM48000Hz\\BGM17.ogg", &result);
    expect(result.action == ROUTE_LOCK, "primer combate fija una pista");
    expect(weq(result.path, L"D:\\game\\Data\\BGM\\UM48000Hz\\BGM15.ogg"), "elige la primera del pool");
    expect(weq(result.chosen_id, L"BGM15"), "identificador elegido BGM15");

    router_route(&router, L"D:\\game\\Data\\BGM\\UM48000Hz\\BGM12LOOP.ogg", &result);
    expect(result.action == ROUTE_HOLD, "cambio de personaje mantiene la pista");
    expect(weq(result.path, L"D:\\game\\Data\\BGM\\UM48000Hz\\BGM15.ogg"), "LOOP de ESAKA usa la pista fijada");

    router_route(&router, L"D:\\game\\Data\\VOICE\\24000Hz\\B_IOR_04.ogg", &result);
    expect(result.action == ROUTE_IGNORE, "las voces no se tocan");
    expect(weq(router.locked_id, L"BGM15"), "una voz no suelta la pista del combate");

    router_route(&router, L"data/BGM/UM48000Hz/BGM41.ogg", &result);
    expect(result.action == ROUTE_BOUNDARY, "la seleccion no se sustituye");
    expect(weq(result.path, L"data/BGM/UM48000Hz/BGM41.ogg"), "ruta de seleccion intacta");
    expect(router.locked_id[0] == 0, "la seleccion libera la pista");

    router_route(&router, L"data/BGM/UM48000Hz/BGM17.ogg", &result);
    expect(result.action == ROUTE_LOCK, "el combate siguiente elige otra vez");
    expect(weq(result.chosen_id, L"BGM16"), "no repite la pista anterior");
    expect(weq(result.path, L"data/BGM/UM48000Hz/BGM16.ogg"), "conserva las barras de la ruta");

    router_route(&router, L"data\\BGM\\UM48000Hz\\BGM04LOOP.ogg", &result);
    expect(result.action == ROUTE_BOUNDARY, "el tema corto de victoria es limite");
    expect(router.locked_id[0] == 0, "la victoria libera la pista");

    router_route(&router, L"data\\BGM\\48000Hz\\23.ogg", &result);
    expect(weq(result.bank, L"original"), "el banco original no se confunde con UM");
    expect(weq(result.chosen_id, L"2A"), "el tercer sorteo del generador secuencial es 2A");
    expect(weq(result.path, L"data\\BGM\\48000Hz\\2A.ogg"), "la ruta original permanece en 48000Hz");

    router_route(&router, L"data\\BGM\\48000Hz\\2ALOOP.ogg", &result);
    expect(result.action == ROUTE_HOLD, "otro tema original mantiene el sorteo");
    expect(weq(result.path, L"data\\BGM\\48000Hz\\2A.ogg"), "el LOOP original apunta al archivo completo");

    router_route(&router, L"data\\BGM\\48000Hz\\dummy.ogg", &result);
    expect(result.action == ROUTE_BOUNDARY, "dummy no entra en el sorteo");

    router_route(&router, L"data\\joined.wav", &result);
    expect(result.action == ROUTE_IGNORE, "joined.wav no es BGM");

    n = read_all(L"D:\\KOF2002UM-RandomBGM\\config\\tracks.cfg", file, (int)sizeof(file));
    expect(n > 0, "tracks.cfg abre");
    parse_settings(file, n, &loaded);
    expect(loaded.enabled, "el mod arranca activo");
    expect(loaded.arrange_pool.count == 25, "pool arrange tiene 25 temas");
    expect(weq(loaded.arrange_pool.ids[0], L"BGM15") && weq(loaded.arrange_pool.ids[24], L"BGM39"), "el pool arrange va de BGM15 a BGM39");
    expect(!id_list_contains(&loaded.arrange_pool, L"BGM12"), "ESAKA no esta en el pool porque tiene LOOP");
    expect(id_list_contains(&loaded.arrange_source, L"BGM12"), "ESAKA si se intercepta");
    expect(id_list_contains(&loaded.arrange_boundary, L"BGM41"), "Select queda fuera");
    expect(id_list_contains(&loaded.arrange_boundary, L"BGM03"), "Decision of Fate queda fuera");
    expect(loaded.original_pool.count == 24, "pool original tiene 24 temas");

    if (g_failures) {
        print("pruebas fallaron\r\n");
        ExitProcess(1);
    }
    print("Todas las pruebas del router pasaron\r\n");
    ExitProcess(0);
}
