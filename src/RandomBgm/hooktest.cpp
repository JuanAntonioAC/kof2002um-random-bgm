/* No se compila. El mod se construye con los archivos .c y build_native.bat. */
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <iostream>
#include <string>

namespace {

std::wstring FinalName(HANDLE handle) {
    wchar_t buffer[1024];
    DWORD n = GetFinalPathNameByHandleW(handle, buffer, 1024, FILE_NAME_NORMALIZED);
    if (n == 0 || n >= 1024) return L"";
    std::wstring path(buffer, n);
    size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

bool Expect(bool condition, const wchar_t* name) {
    std::wcout << (condition ? L"OK  " : L"FAIL ") << name << L"\n";
    return condition;
}

}  // namespace

int wmain() {
    const wchar_t* dir = LR"(D:\SteamLibrary\steamapps\common\The King of Fighters 2002 Unlimited Match\Data\BGM\)";
    std::wstring arrange = std::wstring(dir) + L"UM48000Hz\\";
    std::wstring original = std::wstring(dir) + L"48000Hz\\";
    std::wstring voice = LR"(D:\SteamLibrary\steamapps\common\The King of Fighters 2002 Unlimited Match\Data\VOICE)";

    wchar_t self[MAX_PATH];
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    std::wstring dll = self;
    size_t slash = dll.find_last_of(L"\\/");
    dll = dll.substr(0, slash + 1) + L"RandomBgm.dll";

    DeleteFileW((dll.substr(0, dll.find_last_of(L"\\/") + 1) + L"bgm.log").c_str());
    DeleteFileW((dll.substr(0, dll.find_last_of(L"\\/") + 1) + L"last_track.txt").c_str());
    SetEnvironmentVariableW(L"KOF_BGM_RNG", L"sequential");

    HMODULE module = LoadLibraryW(dll.c_str());
    if (!module) {
        std::wcout << L"FAIL no se cargo RandomBgm.dll, error " << GetLastError() << L"\n";
        return 1;
    }

    int failures = 0;
    HANDLE kyo = CreateFileW((arrange + L"BGM17.ogg").c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    std::wstring first = FinalName(kyo);
    if (kyo != INVALID_HANDLE_VALUE) CloseHandle(kyo);
    if (!Expect(first == L"BGM15.ogg", L"Iori/Destiny abre la pista sorteada BGM15")) ++failures;

    HANDLE loop = CreateFileW((arrange + L"BGM12LOOP.ogg").c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    std::wstring held = FinalName(loop);
    if (loop != INVALID_HANDLE_VALUE) CloseHandle(loop);
    if (!Expect(held == L"BGM15.ogg", L"el LOOP de otro personaje sigue en BGM15")) ++failures;

    WIN32_FILE_ATTRIBUTE_DATA info{};
    BOOL attr = GetFileAttributesExW((arrange + L"BGM22.ogg").c_str(), GetFileExInfoStandard, &info);
    WIN32_FILE_ATTRIBUTE_DATA real{};
    GetFileAttributesExW((arrange + L"BGM15.ogg").c_str(), GetFileExInfoStandard, &real);
    if (!Expect(attr && info.nFileSizeLow == real.nFileSizeLow && info.nFileSizeHigh == real.nFileSizeHigh,
            L"el tamano consultado es el de la pista fijada")) ++failures;

    HANDLE select = CreateFileW((arrange + L"BGM41.ogg").c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    std::wstring select_name = FinalName(select);
    if (select != INVALID_HANDLE_VALUE) CloseHandle(select);
    if (!Expect(select_name == L"BGM41.ogg", L"Select se abre sin sustituir")) ++failures;

    HANDLE next = CreateFileW((arrange + L"BGM25.ogg").c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    std::wstring second = FinalName(next);
    if (next != INVALID_HANDLE_VALUE) CloseHandle(next);
    if (!Expect(second == L"BGM16.ogg", L"el combate nuevo no repite BGM15")) ++failures;

    WIN32_FIND_DATAW found{};
    HANDLE search = FindFirstFileW((std::wstring(LR"(D:\SteamLibrary\steamapps\common\The King of Fighters 2002 Unlimited Match\Data\)") + L"joined.wav").c_str(), &found);
    if (search != INVALID_HANDLE_VALUE) FindClose(search);
    HANDLE joined = CreateFileW(LR"(D:\SteamLibrary\steamapps\common\The King of Fighters 2002 Unlimited Match\Data\joined.wav)", GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    std::wstring joined_name = FinalName(joined);
    if (joined != INVALID_HANDLE_VALUE) CloseHandle(joined);
    if (!Expect(joined_name == L"joined.wav", L"joined.wav no se redirige")) ++failures;
    (void)voice;
    (void)original;
    (void)found;

    HANDLE original_file = CreateFileW((original + L"23.ogg").c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    std::wstring original_name = FinalName(original_file);
    if (original_file != INVALID_HANDLE_VALUE) CloseHandle(original_file);
    if (!Expect(original_name == L"25.ogg", L"el tercer sorteo original es 25.ogg")) ++failures;

    if (failures) {
        std::wcout << failures << L" pruebas del hook fallaron\n";
        return 1;
    }
    std::wcout << L"Todas las pruebas del hook pasaron\n";
    return 0;
}
