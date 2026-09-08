param(
    [Parameter(Mandatory)][string]$BuildDir,
    [Parameter(Mandatory)][string]$QtRoot,
    [Parameter(Mandatory)][string]$VcpkgRoot,
    [Parameter(Mandatory)][string]$Version,
    [Parameter(Mandatory)][string]$Tag
)
$ErrorActionPreference = 'Stop'
if ($Version -notmatch '^\d+\.\d+\.\d+$' -or $Tag -ne "v$Version") { throw 'Invalid release version' }
$stage = Join-Path $BuildDir 'stage'
cmake --install $BuildDir --config Release --prefix $stage
if ($LASTEXITCODE -ne 0) { throw 'Installation failed' }
$bin = Join-Path $stage 'bin'
& "$QtRoot/bin/windeployqt.exe" --release --compiler-runtime --qmldir qml --dir $bin "$bin/tnuxmusic.exe"
if ($LASTEXITCODE -ne 0) { throw 'Qt deployment failed' }
# windeployqt does not deploy the application's OpenSSL dependency.
$crypto = @(Get-ChildItem "$VcpkgRoot/installed/x64-windows/bin/libcrypto*.dll")
if ($crypto.Count -eq 0) { throw 'OpenSSL runtime DLL missing' }
$crypto | Copy-Item -Destination $bin
# Avoid finding build-time DLLs through Qt/vcpkg PATH entries during verification.
$savedPath = $env:PATH
$savedPlatform = $env:QT_QPA_PLATFORM
try {
    $env:PATH = "$env:SystemRoot/system32;$env:SystemRoot"
    $env:QT_QPA_PLATFORM = 'offscreen'
    $reportedVersion = & "$bin/tnuxmusic.exe" --version
    if ($LASTEXITCODE -ne 0 -or "$reportedVersion".Trim() -ne "tnuxmusic $Version") {
        throw "Packaged executable failed version check: $reportedVersion"
    }
    $smokeDir = Join-Path $BuildDir 'package-smoke'
    New-Item -ItemType Directory -Force $smokeDir | Out-Null
    '{"schema":"tnuxmusic.library.v1","tracks":[]}' | Set-Content "$smokeDir/source.json" -Encoding utf8
    & "$bin/tnuxmusic.exe" --merge-library "$smokeDir/source.json" --library "$smokeDir/library.json"
    if ($LASTEXITCODE -ne 0 -or !(Test-Path "$smokeDir/library.json")) { throw 'Packaged CLI smoke test failed' }
} finally {
    $env:PATH = $savedPath
    $env:QT_QPA_PLATFORM = $savedPlatform
}
New-Item -ItemType Directory -Force dist | Out-Null
Compress-Archive -Path "$stage/*" -DestinationPath "dist/tnuxmusic-$Tag-windows-x64.zip" -Force
& 'C:/Program Files (x86)/Inno Setup 6/ISCC.exe' "/DAppVersion=$Version" "/DReleaseTag=$Tag" "/DStageBin=$((Resolve-Path $bin).Path)" assets/windows/installer.iss
if ($LASTEXITCODE -ne 0) { throw 'Installer compilation failed' }
