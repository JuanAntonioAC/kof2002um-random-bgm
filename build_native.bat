@echo off
setlocal
set "BIN=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64"
set "PATH=%BIN%;%PATH%"
set "ROOT=D:\KOF2002UM-RandomBGM"
set "SRC=%ROOT%\src\RandomBgm"
if not exist "%ROOT%\build" mkdir "%ROOT%\build"
if not exist "%ROOT%\dist" mkdir "%ROOT%\dist"
copy /Y "%ROOT%\config\tracks.cfg" "%ROOT%\dist\tracks.cfg" >nul
copy /Y "%ROOT%\config\launcher.cfg" "%ROOT%\dist\launcher.cfg" >nul

"%BIN%\lib.exe" /nologo /machine:x64 /def:"%SRC%\kernel32.def" /out:"%ROOT%\build\kernel32.lib"
if errorlevel 1 exit /b 1

"%BIN%\cl.exe" /nologo /c /TC /Od /GS- /Zl /W3 /Fo"%ROOT%\build\crtstub.obj" "%SRC%\crtstub.c"
if errorlevel 1 exit /b 1
"%BIN%\cl.exe" /nologo /c /TC /O2 /GS- /Zl /W3 /I"%SRC%" /Fo"%ROOT%\build\router.obj" "%SRC%\router.c"
if errorlevel 1 exit /b 1
"%BIN%\cl.exe" /nologo /c /TC /O2 /GS- /Zl /W3 /I"%SRC%" /Fo"%ROOT%\build\selftest.obj" "%SRC%\selftest.c"
if errorlevel 1 exit /b 1
"%BIN%\cl.exe" /nologo /c /TC /O2 /GS- /Zl /W3 /I"%SRC%" /Fo"%ROOT%\build\hook.obj" "%SRC%\hook.c"
if errorlevel 1 exit /b 1
"%BIN%\cl.exe" /nologo /c /TC /O2 /GS- /Zl /W3 /Fo"%ROOT%\build\hooktest.obj" "%SRC%\hooktest.c"
if errorlevel 1 exit /b 1
"%BIN%\ml64.exe" /nologo /c /Fo"%ROOT%\build\chkstk.obj" "%SRC%\chkstk.asm"
if errorlevel 1 exit /b 1

"%BIN%\link.exe" /nologo /NODEFAULTLIB /SUBSYSTEM:CONSOLE /ENTRY:Main /OUT:"%ROOT%\build\RouterTests.exe" "%ROOT%\build\selftest.obj" "%ROOT%\build\router.obj" "%ROOT%\build\crtstub.obj" "%ROOT%\build\chkstk.obj" "%ROOT%\build\kernel32.lib"
if errorlevel 1 exit /b 1
"%BIN%\link.exe" /nologo /DLL /NODEFAULTLIB /SUBSYSTEM:WINDOWS /ENTRY:DllMain /OUT:"%ROOT%\dist\RandomBgm.dll" "%ROOT%\build\hook.obj" "%ROOT%\build\router.obj" "%ROOT%\build\crtstub.obj" "%ROOT%\build\chkstk.obj" "%ROOT%\build\kernel32.lib"
if errorlevel 1 exit /b 1
"%BIN%\link.exe" /nologo /NODEFAULTLIB /SUBSYSTEM:CONSOLE /ENTRY:Main /OUT:"%ROOT%\dist\HookTest.exe" "%ROOT%\build\hooktest.obj" "%ROOT%\build\crtstub.obj" "%ROOT%\build\chkstk.obj" "%ROOT%\build\kernel32.lib"
if errorlevel 1 exit /b 1

"%ROOT%\build\RouterTests.exe"
if errorlevel 1 exit /b 1
"%ROOT%\dist\HookTest.exe"
exit /b %ERRORLEVEL%
