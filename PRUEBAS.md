# Informe de pruebas

Fecha: 17 de octubre de 2026. Ningún archivo de la instalación del juego fue modificado.

## Hechas y superadas

`build_native.bat` compiló `RouterTests.exe`, `dist\RandomBgm.dll` y `dist\HookTest.exe`, y las dos suites terminaron con código 0.

### Router

Generador secuencial y `config\tracks.cfg` real:

- El primer combate arrange fija `BGM15` y un `LOOP` de otra pista sigue en `BGM15`.
- Una voz no cambia ni suelta la pista.
- `BGM41` (Select) no se sustituye y suelta la pista.
- El combate siguiente elige `BGM16` y conserva las barras de la ruta.
- `BGM04LOOP` es límite.
- El banco original no se mezcla con el arrange. El tercer sorteo secuencial del pool de prueba es `2A`, y su `LOOP` apunta al `.ogg` completo.
- `dummy` y `joined.wav` no entran.
- El pool arrange real tiene 25 temas, de `BGM15` a `BGM39`.
- `BGM12` se intercepta y no está en el pool.
- `BGM41` y `BGM03` quedan fuera.
- El pool original real tiene 24 temas.

### Hook sobre archivos reales

`HookTest.exe` cargó `RandomBgm.dll` con `KOF_BGM_RNG=sequential` y abrió OGG de:

`D:\SteamLibrary\steamapps\common\The King of Fighters 2002 Unlimited Match\Data\BGM`

- `BGM17.ogg` se abrió como `BGM15.ogg`.
- `BGM12LOOP.ogg` siguió en `BGM15.ogg`.
- `GetFileAttributesExW` devolvió el tamaño de la pista fijada.
- `BGM41.ogg` se quedó en `BGM41.ogg`.
- El combate nuevo abrió `BGM16.ogg`.
- `joined.wav` no se redirigió.
- El tercer sorteo del banco original abrió `25.ogg`.

## Inyección en el juego

Steam ya estaba abierto. `dist\KofRandomBgm.exe` inició `KingOfFighters2002UM_x64.exe` (pid 1124) sin copiar nada a la carpeta del juego. `dist\bgm.log` anotó, a las 14:13:46:

```
HOOK CreateFileW
READY
```

El proceso llegó a unos 600 MB. `dumpbin /imports` del ejecutable muestra `CreateFileW` en `KERNEL32.dll`, y también `PlaySoundA` y `DSOUND.dll`. No importa `GetFileAttributesW` ni `GetFileAttributesExW`, así que el log no tiene esas dos líneas.

A las 14:15:18, todavía dentro de esa misma sesión, el juego pidió `BGM19` y el registro anotó:

```
LOCK arrange BGM19 -> BGM27
HOLD arrange BGM19 -> BGM27
```

La segunda apertura de la misma pista reutilizó `BGM27`. No hay constancia de qué pantalla o personajes estaban en juego, así que esto no cubre las pruebas 1 a 6. El proceso se cerró desde aquí al terminar la comprobación.

## Pendientes

Estas pruebas no se ejecutaron. No están superadas:

1. Combate Kyo contra Iori.
2. Combate K' contra Terry.
3. Combate Ryo contra Kula.
4. Diez combates seguidos para comprobar que la pista no depende de los personajes.
5. La misma canción durante todos los rounds de una pelea, oído en el juego.
6. Música de menús intacta, oída en el juego.

Para comprobarlas: abre Steam, ejecuta `Activar.bat`, juega en local y revisa `dist\bgm.log`. En un combate debe aparecer un `LOCK` y, en los rounds siguientes, `HOLD` hacia el mismo identificador. En selección y victoria deben aparecer `BOUNDARY` sin `->`.
