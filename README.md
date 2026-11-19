# KOF 2002 UM Random BGM

Mod para **The King of Fighters 2002 Unlimited Match** (Steam, appid 222440) que sustituye la música de combate por una pista aleatoria del soundtrack original. La instalación del juego no se modifica.

## Qué hace

Al abrir un tema de combate, el mod elige un identificador del banco que el juego acaba de pedir y lo mantiene hasta que aparece una pista de menú, selección, victoria o créditos. Un combate nuevo vuelve a sortear. No repite la pista anterior de ese banco.

- Banco arrange (`Data\BGM\UM48000Hz`): `BGM15`–`BGM39`. Son temas largos de un solo archivo.
- Banco original (`Data\BGM\48000Hz`): los 24 temas de un solo archivo listados en `config\tracks.cfg`.
- El sorteo no cruza bancos. Si el juego pide arrange, suena arrange.
- `BGM12` (ESAKA!!) se intercepta, pero no entra en el sorteo porque el juego la abre en dos partes (`BGM12` y `BGM12LOOP`). Si la pista elegida no tiene archivo `LOOP`, el mod sirve el `.ogg` completo también para esa petición.
- Voces, efectos y `joined.wav` no se redirigen.
- Intro, selección de personaje, victoria y créditos quedan fuera. La lista exacta está en las secciones `boundary` de `tracks.cfg`.

El ejecutable de 32 bits no está cubierto. El lanzador usa `KingOfFighters2002UM_x64.exe`.

## Cómo se engancha

`RandomBgm.dll` parchea, solo en memoria, las importaciones `CreateFileW`, `GetFileAttributesW` y `GetFileAttributesExW` del ejecutable. Cuando la ruta es un BGM de combate, cambia el nombre del archivo antes de que el juego lo abra. El juego sigue decodificando sus OGG, con su volumen y su loop.

No hay tabla de personaje a canción parcheada. El mod no necesita saber qué personaje pidió la pista: sustituye el archivo que el juego iba a abrir.

## Activar

1. Abre Steam.
2. Ejecuta `Activar.bat`, o `dist\KofRandomBgm.exe`.
3. Juega en local.

Hace falta el runtime de .NET 9. La DLL no lo usa.

`dist\bgm.log` anota cada decisión: `LOCK` (sorteo nuevo), `HOLD` (misma pista), `BOUNDARY` (menú o corte) y `PASS`. `dist\last_track.txt` guarda el último identificador para no repetirlo al reiniciar.

## Desactivar

Cierra el juego y ábrelo desde Steam. No queda ningún archivo copiado en la carpeta del juego.

Para dejar el lanzador instalado pero sin redirección, pon `enabled=0` en `dist\tracks.cfg` y vuelve a iniciar con `Activar.bat`. El registro dirá `DISABLED`.

## Configuración

`config\tracks.cfg` es la fuente. `build_native.bat` la copia a `dist\tracks.cfg`, que es la que lee la DLL.

- `enabled`: `1` activo, `0` apagado.
- `avoid_repeat`: `1` evita repetir la pista anterior del mismo banco.
- `arrange_pool` / `original_pool`: candidatas del sorteo.
- `arrange_source` / `original_source`: archivos que el juego pide en combate y que sí se sustituyen.
- `arrange_boundary` / `original_boundary`: archivos que no se sustituyen y que sueltan la pista fijada.
- `arrange_loop` / `original_loop`: temas partidos en intro y `LOOP`. No son destinos del sorteo.

`config\launcher.cfg` indica la carpeta del juego, el ejecutable y el appid. Cámbialo si mueves la biblioteca de Steam.

La variable de entorno `KOF_BGM_RNG=sequential` recorre el pool en orden. Sirve para las pruebas. Sin esa variable el sorteo es un generador lineal.

## Compilar

Desde la raíz del proyecto:

```bat
build_native.bat
dotnet publish src\Launcher\KofRandomBgm.csproj -c Release -o dist
```

`build.ps1` hace las dos cosas. Hace falta Visual Studio 2022 con `cl.exe` y `ml64.exe` en la ruta que fija `build_native.bat`, y el SDK de .NET 9 para el lanzador. El script no usa `vcvars` porque en esta máquina el SDK de Windows no está instalado: la DLL es C sin CRT y sin `windows.h`.

Los archivos `.cpp` y `.hpp` de `src\RandomBgm` no forman parte de la compilación.

## Límites

- No se jugaron combates reales en esta entrega. Las pruebas automáticas cubren el router y la apertura real de los OGG. Ver `PRUEBAS.md`.
- `BGM07`, `BGM09`, `BGM10`, `BGM14` y `BGM40` están fuera del sorteo a propósito. Si alguna suena en combate y quieres incluirla, muévela de `boundary` a `source` y a `pool`.
- El mod no altera Steam, el ejecutable en disco ni las partidas en línea. Úsalo en local.
