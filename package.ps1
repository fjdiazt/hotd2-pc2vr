param(
    [Parameter(Mandatory = $true)][ValidatePattern('^[A-Za-z0-9][A-Za-z0-9._-]{0,79}\z')][string]$Version,
    [string]$BuildDirectory,
    [string]$OutputDirectory,
    [string]$LoaderPackage
)
$ErrorActionPreference = 'Stop'
if (!$BuildDirectory) { $BuildDirectory = Join-Path $PSScriptRoot 'build' }
if (!$OutputDirectory) { $OutputDirectory = Join-Path $PSScriptRoot 'build\releases' }
if (!$LoaderPackage) { $LoaderPackage = Join-Path $PSScriptRoot 'build\release-deps\OpenXR.Loader.1.1.65.nupkg' }
Add-Type -AssemblyName System.IO.Compression.FileSystem
function Assert-PeMachine([string]$Path, [int]$Machine) {
    $image = [IO.File]::ReadAllBytes($Path)
    if ($image.Length -lt 64 -or $image[0] -ne 0x4D -or $image[1] -ne 0x5A) { throw "Invalid PE file: $Path" }
    $pe = [BitConverter]::ToUInt32($image, 60)
    if ([long]$pe + 6 -gt $image.Length -or [BitConverter]::ToUInt32($image, $pe) -ne 0x4550) { throw "Invalid PE header: $Path" }
    if ([BitConverter]::ToUInt16($image, $pe + 4) -ne $Machine) { throw "Wrong PE architecture: $Path" }
}
foreach ($name in 'HOTD2Launch.exe', 'HOTD2Bridge.dll') { Assert-PeMachine (Join-Path $BuildDirectory $name) 0x14C }
Assert-PeMachine (Join-Path $BuildDirectory 'HOTD2VR.exe') 0x8664
$runtime = @('HOTD2Launch.exe', 'HOTD2Bridge.dll', 'HOTD2VR.exe', 'gun.obj', 'gun.mtl', 'gun.bmp')
$sources = @('Start-HOTD2.ps1', 'tools\extract_menu_font.ps1', 'README.md', 'LICENSE', 'THIRD-PARTY.md',
    'third_party\openxr\LICENSE', 'docs\vr-menu.md', 'docs\cheats.md', 'docs\pause.md', 'docs\findings.md',
    'docs\screenshots\game-menu.png', 'docs\screenshots\vr-settings.png')
foreach ($name in $runtime) {
    if (!(Test-Path -LiteralPath (Join-Path $BuildDirectory $name) -PathType Leaf)) { throw "Missing runtime file: $name" }
}
foreach ($name in ($sources + 'HOTD2VR.ini')) {
    if (!(Test-Path -LiteralPath (Join-Path $PSScriptRoot $name) -PathType Leaf)) { throw "Missing package source: $name" }
}
$loaderHash = '05638C526F07BA3A5F1084BBC3308D654D5167F844FFA1E35515325CB72E0C34'
if (!(Test-Path -LiteralPath $LoaderPackage)) {
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($LoaderPackage))) | Out-Null
    Invoke-WebRequest -UseBasicParsing -Uri 'https://github.com/KhronosGroup/OpenXR-SDK/releases/download/release-1.1.65/OpenXR.Loader.1.1.65.nupkg' -OutFile $LoaderPackage
}
if ((Get-FileHash -LiteralPath $LoaderPackage -Algorithm SHA256).Hash -ne $loaderHash) { throw 'OpenXR loader package SHA256 mismatch.' }
$outputPath = [IO.Path]::GetFullPath($OutputDirectory)
$archivePath = Join-Path $outputPath "HOTD2-PC2VR-$Version-windows-x64.zip"
if ((Test-Path -LiteralPath $archivePath) -or (Test-Path -LiteralPath "$archivePath.sha256")) { throw "Release output already exists: $archivePath" }
$tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$stage = [IO.Path]::GetFullPath((Join-Path $tempRoot ('hotd2-package-' + [Guid]::NewGuid().ToString('N'))))
if ([IO.Path]::GetDirectoryName($stage).TrimEnd('\') -ne $tempRoot.TrimEnd('\')) { throw 'Invalid package staging directory.' }
[IO.Directory]::CreateDirectory((Join-Path $stage 'build')) | Out-Null
try {
    foreach ($name in $runtime) { Copy-Item -LiteralPath (Join-Path $BuildDirectory $name) -Destination (Join-Path $stage 'build') }
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'HOTD2VR.ini') -Destination (Join-Path $stage 'build')
    foreach ($name in $sources) {
        $target = Join-Path $stage $name
        [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($target)) | Out-Null
        Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination $target
    }
    $loader = [IO.Compression.ZipFile]::OpenRead([IO.Path]::GetFullPath($LoaderPackage))
    try {
        $entry = $loader.GetEntry('native/x64/release/bin/openxr_loader.dll')
        if (!$entry) { throw 'Official OpenXR package has no x64 loader.' }
        [IO.Compression.ZipFileExtensions]::ExtractToFile($entry, (Join-Path $stage 'build\openxr_loader.dll'))
    } finally { $loader.Dispose() }
    Assert-PeMachine (Join-Path $stage 'build\openxr_loader.dll') 0x8664
    [IO.Directory]::CreateDirectory($outputPath) | Out-Null
    [IO.Compression.ZipFile]::CreateFromDirectory($stage, $archivePath)
    $checksum = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant()
    [IO.File]::WriteAllText("$archivePath.sha256", "$checksum  $([IO.Path]::GetFileName($archivePath))`n", [Text.Encoding]::ASCII)
    Write-Output "Release package: $archivePath"
} finally {
    if ([IO.Path]::GetDirectoryName($stage).TrimEnd('\') -ne $tempRoot.TrimEnd('\') -or
        ((Get-Item -LiteralPath $stage).Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'Unsafe package cleanup path.' }
    Remove-Item -LiteralPath $stage -Recurse -Force
}
