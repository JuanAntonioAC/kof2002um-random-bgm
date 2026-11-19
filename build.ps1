$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
cmd /c (Join-Path $root 'build_native.bat')
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
dotnet publish (Join-Path $root 'src\Launcher\KofRandomBgm.csproj') -c Release -o (Join-Path $root 'dist') --nologo
exit $LASTEXITCODE
