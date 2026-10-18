param()
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$out = Join-Path $root 'build'
New-Item -ItemType Directory -Path $out -Force | Out-Null
$gcc = (Get-Command gcc -ErrorAction Stop).Source
if ((& $gcc -dumpmachine) -ne 'i686-w64-mingw32') { throw '32-bit MinGW GCC required for game bridge.' }
& $gcc -std=c11 -O2 -Wall -Wextra -shared -static-libgcc '-Wl,--kill-at' -o "$out\HOTD2Bridge.dll" "$root\src\bridge.c" -ldxguid -luser32 -lkernel32
if ($LASTEXITCODE) { throw 'Bridge build failed.' }
& $gcc -std=c11 -O2 -Wall -Wextra -municode -static-libgcc -o "$out\HOTD2Launch.exe" "$root\src\launch.c"
if ($LASTEXITCODE) { throw 'Launcher build failed.' }
& $gcc -std=c11 -O2 -Wall -Wextra -static-libgcc -o "$out\test_startup.exe" "$root\tests\test_startup.c"
if ($LASTEXITCODE) { throw 'Startup test build failed.' }
& "$out\test_startup.exe"
if ($LASTEXITCODE) { throw 'Startup test failed.' }
& $gcc -std=c11 -O2 -Wall -Wextra -static-libgcc -o "$out\test_checkpoint.exe" "$root\tests\test_checkpoint.c" -ldxguid -luser32 -lkernel32
if ($LASTEXITCODE) { throw 'Checkpoint test build failed.' }
& "$out\test_checkpoint.exe"
if ($LASTEXITCODE) { throw 'Checkpoint test failed.' }
& $gcc -std=c11 -O2 -Wall -Wextra -static-libgcc -o "$out\test_lighting.exe" "$root\tests\test_lighting.c"
if ($LASTEXITCODE) { throw 'Lighting test build failed.' }
& "$out\test_lighting.exe"
if ($LASTEXITCODE) { throw 'Lighting test failed.' }
& $gcc -std=c11 -O2 -Wall -Wextra -static-libgcc -o "$out\test_audio.exe" "$root\tests\test_audio.c" -ldxguid -luser32 -lkernel32
if ($LASTEXITCODE) { throw 'Audio test build failed.' }
& "$out\test_audio.exe"
if ($LASTEXITCODE) { throw 'Audio test failed.' }
& $gcc -std=c11 -Wall -Wextra -o "$out\test_math.exe" "$root\tests\test_math.c"
if ($LASTEXITCODE) { throw 'Math test build failed.' }
& "$out\test_math.exe"
if ($LASTEXITCODE) { throw 'Math test failed.' }
& $gcc -std=c11 -O2 -Wall -Wextra -static-libgcc -o "$out\test_capture_visibility.exe" "$root\tests\test_capture_visibility.c" -ldxguid -luser32 -lkernel32
if ($LASTEXITCODE) { throw 'Capture visibility test build failed.' }
& "$out\test_capture_visibility.exe"
if ($LASTEXITCODE) { throw 'Capture visibility test failed.' }
& $gcc -std=c11 -O2 -Wall -Wextra -static-libgcc -o "$out\test_crosshair_capture.exe" "$root\tests\test_crosshair_capture.c" -ldxguid -luser32 -lkernel32
if ($LASTEXITCODE) { throw 'Crosshair capture test build failed.' }
& "$out\test_crosshair_capture.exe"
if ($LASTEXITCODE) { throw 'Crosshair capture test failed.' }
& $gcc -std=c11 -O2 -Wall -Wextra -static-libgcc -o "$out\test_vertex_reuse.exe" "$root\tests\test_vertex_reuse.c" -ldxguid -luser32 -lkernel32
if ($LASTEXITCODE) { throw 'Vertex reuse test build failed.' }
& "$out\test_vertex_reuse.exe"
if ($LASTEXITCODE) { throw 'Vertex reuse test failed.' }
& $gcc -std=c11 -O2 -Wall -Wextra -static-libgcc -o "$out\test_texture_cache.exe" "$root\tests\test_texture_cache.c" -ldxguid -luser32 -lkernel32
if ($LASTEXITCODE) { throw 'Texture cache test build failed.' }
& "$out\test_texture_cache.exe"
if ($LASTEXITCODE) { throw 'Texture cache test failed.' }
& $gcc -std=c11 -O2 -Wall -Wextra -static-libgcc -o "$out\test_desktop_pause.exe" "$root\tests\test_desktop_pause.c" -lddraw -ldxguid -luser32 -lkernel32
if ($LASTEXITCODE) { throw 'Desktop pause test build failed.' }
& "$out\test_desktop_pause.exe"
if ($LASTEXITCODE) { throw 'Desktop pause test failed.' }
& $gcc -std=c11 -O2 -Wall -Wextra -static-libgcc -o "$out\test_reload.exe" "$root\tests\test_reload.c" -ldxguid -luser32 -lkernel32
if ($LASTEXITCODE) { throw 'Reload test build failed.' }
& "$out\test_reload.exe"
if ($LASTEXITCODE) { throw 'Reload test failed.' }
& $gcc -std=c11 -O2 -Wall -Wextra -static-libgcc -o "$out\test_protection.exe" "$root\tests\test_protection.c" -ldxguid -luser32 -lkernel32
if ($LASTEXITCODE) { throw 'Protection test build failed.' }
& "$out\test_protection.exe"
if ($LASTEXITCODE) { throw 'Protection test failed.' }
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -property installationPath
if (!$vs) { throw 'Visual Studio C++ tools required for x64 viewer.' }
$batch = @"
@call "$vs\VC\Auxiliary\Build\vcvars64.bat" >nul
@cl /nologo /std:c11 /O2 /MT /utf-8 /W3 /I"$root\third_party" "$root\src\hotd2vr.c" /Fo"$out\hotd2vr.obj" /Fe"$out\HOTD2VR.exe" /link d3d11.lib dxgi.lib d3dcompiler.lib dxguid.lib uuid.lib gdi32.lib user32.lib ole32.lib
@if errorlevel 1 exit /b 1
@cl /nologo /std:c11 /O2 /MT /utf-8 /W3 /I"$root\third_party" "$root\tests\test_frame_snapshot.c" /Fo"$out\test_frame_snapshot.obj" /Fe"$out\test_frame_snapshot.exe" /link d3d11.lib dxgi.lib d3dcompiler.lib dxguid.lib uuid.lib gdi32.lib user32.lib ole32.lib
@if errorlevel 1 exit /b 1
@cl /nologo /std:c11 /O2 /MT /utf-8 /W3 /I"$root\third_party" "$root\tests\test_world_scale.c" /Fo"$out\test_world_scale.obj" /Fe"$out\test_world_scale.exe" /link d3d11.lib dxgi.lib d3dcompiler.lib dxguid.lib uuid.lib gdi32.lib user32.lib ole32.lib
@if errorlevel 1 exit /b 1
@cl /nologo /std:c11 /O2 /MT /utf-8 /W3 /I"$root\third_party" "$root\tests\test_aim_indicators.c" /Fo"$out\test_aim_indicators.obj" /Fe"$out\test_aim_indicators.exe" /link d3d11.lib dxgi.lib d3dcompiler.lib dxguid.lib uuid.lib gdi32.lib user32.lib ole32.lib
@if errorlevel 1 exit /b 1
@cl /nologo /std:c11 /O2 /MT /utf-8 /W3 /I"$root\third_party" "$root\tests\test_effect_render.c" /Fo"$out\test_effect_render.obj" /Fe"$out\test_effect_render.exe" /link d3d11.lib dxgi.lib d3dcompiler.lib dxguid.lib uuid.lib gdi32.lib user32.lib ole32.lib
@if errorlevel 1 exit /b 1
@cl /nologo /std:c11 /O2 /MT /utf-8 /W3 /I"$root\third_party" "$root\tests\test_output_modes.c" /Fo"$out\test_output_modes.obj" /Fe"$out\test_output_modes.exe" /link d3d11.lib dxgi.lib d3dcompiler.lib dxguid.lib uuid.lib gdi32.lib user32.lib ole32.lib
"@
[IO.File]::WriteAllText("$out\compile-viewer.cmd", $batch)
& $env:ComSpec /d /c "$out\compile-viewer.cmd"
if ($LASTEXITCODE) { throw 'Viewer build failed.' }
& "$out\test_frame_snapshot.exe"
if ($LASTEXITCODE) { throw 'Frame snapshot test failed.' }
& "$out\test_world_scale.exe"
if ($LASTEXITCODE) { throw 'World scale test failed.' }
& "$out\test_aim_indicators.exe"
if ($LASTEXITCODE) { throw 'Aim indicator test failed.' }
& "$out\test_effect_render.exe"
if ($LASTEXITCODE) { throw 'Effect shader test failed.' }
& "$out\test_output_modes.exe"
if ($LASTEXITCODE) { throw 'Output mode test failed.' }
Copy-Item "$root\assets\gun\*" -Destination $out -Force
if (!(Test-Path "$out\HOTD2VR.ini")) { Copy-Item "$root\HOTD2VR.ini" $out }
$loader = 'N:\games\systems\pcvr\bin\VirtuaCop2VR\VR\openxr_loader.dll'
if (!(Test-Path "$out\openxr_loader.dll") -and (Test-Path $loader)) { Copy-Item $loader $out }
Write-Output "Build and offline checks passed: $out"
