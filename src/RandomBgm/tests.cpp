/* No se compila. El mod se construye con los archivos .c y build_native.bat. */
#include "Config.hpp"

#include <iostream>
#include <string>

namespace {

int g_failures = 0;
int g_next = 0;

int Sequential(int n) {
    if (n <= 0) return 0;
    return g_next++ % n;
}

void Expect(bool condition, const char* name) {
    if (condition) {
        std::cout << "OK  " << name << "\n";
        return;
    }
    std::cout << "FAIL " << name << "\n";
    ++g_failures;
}

Settings Sample() {
    Settings settings;
    settings.arrange_loop = {L"BGM12"};
    settings.arrange_boundary = {L"BGM01", L"BGM03", L"BGM04", L"BGM41", L"BGM42"};
    settings.arrange_source = {L"BGM12", L"BGM15", L"BGM16", L"BGM17"};
    settings.arrange_pool = {L"BGM15", L"BGM16", L"BGM17"};
    settings.original_boundary = {L"21", L"dummy"};
    settings.original_source = {L"23", L"24", L"2A"};
    settings.original_pool = {L"23", L"24", L"2A"};
    return settings;
}

}  // namespace

int main() {
    PathRouter router(Sample(), Sequential);

    auto first = router.Route(LR"(D:\game\Data\BGM\UM48000Hz\BGM17.ogg)");
    Expect(first.action == RouteAction::Lock, "primer combate fija una pista");
    Expect(first.path == LR"(D:\game\Data\BGM\UM48000Hz\BGM15.ogg)", "elige la primera del pool");
    Expect(first.chosen_id == L"BGM15", "identificador elegido BGM15");

    auto change = router.Route(LR"(D:\game\Data\BGM\UM48000Hz\BGM12LOOP.ogg)");
    Expect(change.action == RouteAction::Hold, "cambio de personaje mantiene la pista");
    Expect(change.path == LR"(D:\game\Data\BGM\UM48000Hz\BGM15.ogg)", "LOOP de ESAKA usa la pista fijada");

    auto voice = router.Route(LR"(D:\game\Data\VOICE\24000Hz\B_IOR_04.ogg)");
    Expect(voice.action == RouteAction::Ignore, "las voces no se tocan");
    Expect(voice.path == LR"(D:\game\Data\VOICE\24000Hz\B_IOR_04.ogg)", "la ruta de voz queda igual");
    Expect(router.locked_id() == L"BGM15", "una voz no suelta la pista del combate");

    auto select = router.Route(L"data/BGM/UM48000Hz/BGM41.ogg");
    Expect(select.action == RouteAction::Boundary, "la seleccion no se sustituye");
    Expect(select.path == L"data/BGM/UM48000Hz/BGM41.ogg", "ruta de seleccion intacta");
    Expect(router.locked_id().empty(), "la seleccion libera la pista");

    auto second = router.Route(L"data/BGM/UM48000Hz/bgm22.ogg");
    Expect(second.action == RouteAction::Lock, "el combate siguiente elige otra vez");
    Expect(second.chosen_id == L"BGM16", "no repite la pista anterior");
    Expect(second.path == L"data/BGM/UM48000Hz/BGM16.ogg", "conserva las barras de la ruta");

    auto winner = router.Route(L"data\\BGM\\UM48000Hz\\BGM04LOOP.ogg");
    Expect(winner.action == RouteAction::Boundary, "el tema corto de victoria es limite");
    Expect(router.locked_id().empty(), "la victoria libera la pista");

    auto original = router.Route(L"data\\BGM\\48000Hz\\23.ogg");
    Expect(original.bank == L"original", "el banco original no se confunde con UM");
    Expect(original.chosen_id == L"2A", "el tercer sorteo del generador secuencial es 2A");
    Expect(original.path == L"data\\BGM\\48000Hz\\2A.ogg", "la ruta original permanece en 48000Hz");

    auto original_hold = router.Route(L"data\\BGM\\48000Hz\\2ALOOP.ogg");
    Expect(original_hold.action == RouteAction::Hold, "otro tema original mantiene el sorteo");
    Expect(original_hold.path == L"data\\BGM\\48000Hz\\2A.ogg", "el LOOP original apunta al archivo completo");

    auto dummy = router.Route(L"data\\BGM\\48000Hz\\dummy.ogg");
    Expect(dummy.action == RouteAction::Boundary, "dummy no entra en el sorteo");

    std::wstring bank, id;
    bool loop = false;
    Expect(!PathRouter::Parse(L"data\\joined.wav", bank, id, loop), "joined.wav no es BGM");

    Settings loaded;
    std::string error;
    bool ok = LoadSettings(LR"(D:\KOF2002UM-RandomBGM\config\tracks.cfg)", loaded, error);
    Expect(ok, "tracks.cfg abre");
    Expect(loaded.enabled, "el mod arranca activo");
    Expect(loaded.arrange_pool.size() == 25, "pool arrange tiene 25 temas");
    Expect(loaded.arrange_pool.front() == L"BGM15" && loaded.arrange_pool.back() == L"BGM39", "el pool arrange va de BGM15 a BGM39");
    Expect(std::find(loaded.arrange_pool.begin(), loaded.arrange_pool.end(), L"BGM12") == loaded.arrange_pool.end(), "ESAKA no esta en el pool porque tiene LOOP");
    Expect(std::find(loaded.arrange_source.begin(), loaded.arrange_source.end(), L"BGM12") != loaded.arrange_source.end(), "ESAKA si se intercepta");
    Expect(std::find(loaded.arrange_boundary.begin(), loaded.arrange_boundary.end(), L"BGM41") != loaded.arrange_boundary.end(), "Select queda fuera");
    Expect(std::find(loaded.arrange_boundary.begin(), loaded.arrange_boundary.end(), L"BGM03") != loaded.arrange_boundary.end(), "Decision of Fate queda fuera");
    Expect(loaded.original_pool.size() == 24, "pool original tiene 24 temas");

    if (g_failures) {
        std::cout << g_failures << " pruebas fallaron\n";
        return 1;
    }
    std::cout << "Todas las pruebas del router pasaron\n";
    return 0;
}
