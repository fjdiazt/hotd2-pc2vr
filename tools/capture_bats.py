"""Build and run isolated, desktop-only chapter-4/scene-0 bat comparisons."""
import argparse
import configparser
import ctypes as c
from ctypes import wintypes as w
import csv
import hashlib
import io
import json
import mmap
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]
SIZE = 88614072
EXPECTED = 'c6b4116788b7f68c56860fb9cc8a94bf984e620907031e4bc3db43623dbe579a'
VR_NAMES = {'vrserver.exe', 'vrcompositor.exe', 'vrmonitor.exe', 'vrwebhelper.exe', 'vrdashboard.exe', 'vrstartup.exe'}


def processes():
    rows = csv.reader(io.StringIO(subprocess.check_output(['tasklist', '/fo', 'csv', '/nh'], text=True)))
    return [(name.lower(), int(pid)) for name, pid, *_ in rows if pid.isdigit()]


def actor_report(data):
    if len(data) < 16:
        raise ValueError('Incomplete actor snapshot header')
    magic, version, frame, count = struct.unpack_from('<4I', data)
    if (magic, version) != (0x54434142, 1) or count > 32:
        raise ValueError('Unsupported actor snapshot')
    offset, actors = 16, []
    for _ in range(count):
        if offset + 16 > len(data):
            raise ValueError('Incomplete actor record header')
        address, fn, serial, size = struct.unpack_from('<4I', data, offset)
        offset += 16
        if size != 0x13D8 or offset + size > len(data):
            raise ValueError('Incomplete actor snapshot')
        raw = data[offset:offset + size]
        offset += size
        nodes = []
        for slot in range(7):
            at = 0x20C + slot * 0x90
            model = struct.unpack_from('<I', raw, at)[0]
            if 0x1B01 <= model <= 0x1B09:
                nodes.append({'slot': slot, 'model': hex(model),
                              'model_view': struct.unpack_from('<16f', raw, at + 0x28)})
        actors.append({'address': hex(address), 'function': hex(fn), 'serial': serial,
                       'kind': struct.unpack_from('<h', raw, 0x1F4)[0],
                       'scale': struct.unpack_from('<f', raw, 0x1300)[0], 'nodes': nodes})
    if offset != len(data):
        raise ValueError('Trailing actor snapshot data')
    return {'boot_frame': frame, 'actors': actors}


def build(stage, re_source):
    source = re_source / 'src'
    autoplay = (source / 'hook/auto_play.cpp').read_text()
    old = '*reinterpret_cast<const T*>(a)'
    if autoplay.count(old) != 1:
        raise ValueError('Autoplay memory-reader changed; inspect before rebuilding')
    (stage / 'bat_autoplay.inc').write_text(autoplay.replace(old, '*reinterpret_cast<const T*>(bot_address(a))'))
    shooting = (source / 'game/shooting.cpp').read_text()
    start = shooting.index('int GunCalibrate(')
    calibration = shooting[start:shooting.index('// 0x0040CD37', start)].strip()
    if calibration.count('{') != calibration.count('}') or not calibration.endswith('}'):
        raise ValueError('Calibration function boundary changed')
    (stage / 'calibration.cpp').write_text('#include <cstdint>\nnamespace game {\n' + calibration + '\n}\n')
    common = ['-std=c++17', '-O2', '-Wall', '-Wextra', '-static-libgcc', '-static-libstdc++',
              '-I', str(stage), '-I', str(source / 'hook'), '-I', str(source)]
    subprocess.run(['g++', *common, '-o', str(stage / 'test_bat_bot.exe'),
                    str(ROOT / 'tests/test_bat_bot.cpp'), str(ROOT / 'tools/bat_bot.cpp'),
                    str(stage / 'calibration.cpp')], check=True)
    subprocess.run([str(stage / 'test_bat_bot.exe'), str(stage / 'test.ini')], check=True)
    subprocess.run(['gcc', '-std=c11', '-O2', '-Wall', '-Wextra', '-DHOTD_BAT_BOT', '-c',
                    str(ROOT / 'src/bridge.c'), '-o', str(stage / 'bridge.o')], check=True)
    subprocess.run(['g++', *common, '-shared', '-Wl,--kill-at', '-o', str(stage / 'HOTD2Bridge.dll'),
                    str(stage / 'bridge.o'), str(ROOT / 'tools/bat_bot.cpp'), str(stage / 'calibration.cpp'),
                    '-ldxguid', '-luser32', '-lkernel32'], check=True)
    subprocess.run(['gcc', '-std=c11', '-O2', '-Wall', '-Wextra', '-municode', '-static-libgcc',
                    '-o', str(stage / 'HOTD2Launch.exe'), str(ROOT / 'src/launch.c')], check=True)
    manifest = {'autoplay_sha256': hashlib.sha256(autoplay.encode()).hexdigest(),
                'calibration_sha256': hashlib.sha256(calibration.encode()).hexdigest()}
    (stage / 'build-inputs.json').write_text(json.dumps(manifest, indent=2))


def run_case(mode, stage, game, runtime):
    from PIL import Image, ImageGrab
    folder = stage / mode
    folder.mkdir()
    for name in ('HOTD2Bridge.dll', 'HOTD2Launch.exe'):
        shutil.copy2(stage / name, folder / name)
    for path in runtime.iterdir():
        if path.is_file() and path.name in ('HOTD2VR.exe', 'openxr_loader.dll', 'gun.obj', 'gun.mtl', 'gun.bmp', 'menu-font.rgba'):
            shutil.copy2(path, folder / path.name)
    cfg = configparser.ConfigParser()
    cfg.read(runtime / 'HOTD2VR.ini')
    for section in ('vr', 'bridge'):
        if not cfg.has_section(section):
            cfg.add_section(section)
    cfg['vr'].update(window='0', vrwindow='0', akimbo='0', continue_game='0', native_lighting='1', expand_world='1')
    cfg['bridge'].update(skip_splash='1', launchfix='1', windowed='1', background='1', trace_bats='1')
    with (folder / 'HOTD2VR.ini').open('w') as f:
        cfg.write(f)
    (folder / 'HOTD2VR.checkpoint.ini').write_text('[checkpoint]\nversion=1\nstage=4\nscene=0\n')
    env = {k: v for k, v in os.environ.items() if not k.startswith('HOTD2_INPUT_AUTO')}
    env['HOTD2_INPUT_AUTO_PERIOD'] = '6'
    flags = ['--window' if mode == 'converted' else '--no-window', '--no-vrwindow', '--start-stage', '4', '--start-scene', '0', '--no-continue-game']
    if mode == 'native':
        flags.append('--no-graphics-hooks')
    launched = subprocess.run([str(folder / 'HOTD2Launch.exe'), str(game), *flags], env=env,
                              capture_output=True, text=True, timeout=25)
    (folder / 'launch.log').write_text(launched.stdout + launched.stderr)
    launched.check_returncode()
    match = re.search(r'game PID (\d+)', launched.stdout)
    if not match:
        raise RuntimeError('Launcher did not identify its owned game')
    pid = int(match[1])
    kernel, user = c.WinDLL('kernel32', use_last_error=True), c.WinDLL('user32', use_last_error=True)
    kernel.OpenProcess.argtypes = [w.DWORD, w.BOOL, w.DWORD]
    kernel.OpenProcess.restype = w.HANDLE
    kernel.CloseHandle.argtypes = [w.HANDLE]
    kernel.TerminateProcess.argtypes = [w.HANDLE, w.UINT]
    kernel.GetExitCodeProcess.argtypes = [w.HANDLE, c.POINTER(w.DWORD)]
    kernel.WaitForSingleObject.argtypes = [w.HANDLE, w.DWORD]
    kernel.OpenFileMappingW.argtypes = [w.DWORD, w.BOOL, w.LPCWSTR]
    kernel.OpenFileMappingW.restype = w.HANDLE
    handle = kernel.OpenProcess(0x101001, False, pid)
    if not handle:
        raise c.WinError(c.get_last_error())
    callback = c.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)
    user.GetWindowThreadProcessId.argtypes = [w.HWND, c.POINTER(w.DWORD)]
    user.GetWindowTextW.argtypes = [w.HWND, w.LPWSTR, c.c_int]
    user.GetDlgCtrlID.argtypes = [w.HWND]
    user.IsWindowVisible.argtypes = [w.HWND]
    user.SendMessageTimeoutW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM, w.UINT, w.UINT, c.POINTER(c.c_size_t)]

    def windows(owner):
        found = []
        @callback
        def visit(hwnd, _):
            proc, text = w.DWORD(), c.create_unicode_buffer(512)
            user.GetWindowThreadProcessId(hwnd, c.byref(proc))
            if proc.value == owner:
                user.GetWindowTextW(hwnd, text, len(text))
                found.append((hwnd, text.value))
            return True
        user.EnumWindows(visit, 0)
        return found

    def device_prompt():
        for hwnd, title in windows(pid):
            if title not in ('Attention', 'Error'):
                continue
            children = []
            @callback
            def visit(child, _):
                text = c.create_unicode_buffer(512)
                user.GetWindowTextW(child, text, len(text))
                children.append((child, user.GetDlgCtrlID(child), text.value))
                return True
            user.EnumChildWindows(hwnd, visit, 0)
            if title == 'Error':
                raise RuntimeError('Native startup error: ' + ' '.join(text for _, _, text in children if text and '&' not in text))
            if any(text.endswith(' will be used. \n') for _, _, text in children):
                for child, cid, _ in children:
                    if cid == 6:
                        result = c.c_size_t()
                        user.SendMessageTimeoutW(child, 0xF5, 0, 0, 2, 100, c.byref(result))

    children, logs, memory = [], [], None
    try:
        service_log = (folder / 'service.log').open('w')
        logs.append(service_log)
        children.append(subprocess.Popen([str(folder / 'HOTD2VR.exe'), '--no-window', '--no-vrwindow',
                         '--infinite-ammo', '--infinite-lives', '--invincibility', '--infinite-continues', '--master-volume', '0'],
                         cwd=folder, stdout=service_log, stderr=subprocess.STDOUT, env=env))
        deadline, last_request = time.monotonic() + 180, 0
        while time.monotonic() < deadline:
            code = w.DWORD()
            if not kernel.GetExitCodeProcess(handle, c.byref(code)):
                raise c.WinError(c.get_last_error())
            if code.value != 259:
                raise RuntimeError('Owned game exited before capture')
            device_prompt()
            if memory is None:
                mapping = kernel.OpenFileMappingW(2, False, r'Local\HOTD2_PC2VR_Frame')
                if mapping:
                    kernel.CloseHandle(mapping)
                    memory = mmap.mmap(-1, SIZE, tagname=r'Local\HOTD2_PC2VR_Frame', access=mmap.ACCESS_WRITE)
                    if struct.unpack_from('<2I', memory, 4) != (0x32544F48, 24) or struct.unpack_from('<I', memory, 124)[0] != pid:
                        raise RuntimeError('Unexpected bridge owner/protocol')
            if memory is not None:
                stage_scene = struct.unpack_from('<2I', memory, 87385044)
                request, response = struct.unpack_from('<2I', memory, 87385016)
                if stage_scene != (4, 0) and request == response and struct.unpack_from('<I', memory, 12)[0] > 120 and time.monotonic() - last_request > 1:
                    struct.pack_into('<I', memory, 87385036, 1)
                    struct.pack_into('<I', memory, 87385016, (request + 1) & 0xFFFFFFFF or 1)
                    last_request = time.monotonic()
            bridge_log = folder / 'HOTD2Bridge.log'
            if bridge_log.exists() and 'batbot frame saved:' in bridge_log.read_text(errors='replace'):
                break
            time.sleep(.1)
        else:
            raise TimeoutError(f'{mode}: bat capture not reached within 180 seconds; inspect {folder}')
        if mode == 'native':
            game_windows = [hwnd for hwnd, title in windows(pid) if title == 'The House of the Dead 2' and user.IsWindowVisible(hwnd)]
            if len(game_windows) != 1:
                raise RuntimeError('Native game window unavailable for external screenshot')
            # External desktop capture only; never call the game's D3D device or pause renderer.
            ImageGrab.grab(window=game_windows[0]).save(folder / 'native-frame.png')
        raw = (folder / 'BatBotFrame.bin').read_bytes()
        if len(raw) != SIZE or struct.unpack_from('<2I', raw, 4) != (0x32544F48, 24):
            raise ValueError('Incomplete or incompatible frame snapshot')
        width, height = struct.unpack_from('<2I', raw, 88)
        if mode == 'converted':
            if not 0 < width <= 1280 or not 0 < height <= 960:
                raise ValueError('Invalid framebuffer dimensions')
            Image.frombytes('RGBA', (width, height), raw[15360156:15360156 + width * height * 4], 'raw', 'BGRA').save(folder / 'native-frame.png')
        elif width or height or struct.unpack_from('<I', raw, 16)[0]:
            raise ValueError('Graphics data unexpectedly published in native mode')
        actors = actor_report((folder / 'BatBotActors.bin').read_bytes())
        (folder / 'actors.json').write_text(json.dumps(actors, indent=2, allow_nan=False))
        summary = {'mode': mode, 'game_pid': pid, 'frame': struct.unpack_from('<I', raw, 12)[0],
                   'vertices': struct.unpack_from('<I', raw, 16)[0], 'actors': len(actors['actors']),
                   'graphics_hooks': mode == 'converted',
                   'screenshot_source': 'external desktop window' if mode == 'native' else 'native D3D7 framebuffer',
                   'models': sorted({n['model'] for a in actors['actors'] for n in a['nodes']})}
        if mode == 'converted':
            Image.frombytes('RGBA', (4096, 4096), raw[20275356:87384220], 'raw', 'BGRA').save(folder / 'atlas.png')
            preview_log = (folder / 'preview.log').open('w')
            logs.append(preview_log)
            preview = subprocess.Popen([str(folder / 'HOTD2VR.exe'), '--preview-only', '--window', '--no-vrwindow'],
                                       cwd=folder, stdout=preview_log, stderr=subprocess.STDOUT)
            children.append(preview)
            time.sleep(2)
            visible = [hwnd for hwnd, _ in windows(preview.pid) if user.IsWindowVisible(hwnd)]
            if not visible:
                raise RuntimeError('Converted desktop preview did not open')
            ImageGrab.grab(window=visible[0]).save(folder / 'converted-preview.png')
        (folder / 'summary.json').write_text(json.dumps(summary, indent=2))
        print(json.dumps(summary), flush=True)
        return summary
    finally:
        if memory is not None:
            memory.close()
        try:
            for child in reversed(children):
                if child.poll() is None:
                    child.terminate()
                    child.wait(timeout=5)
        finally:
            # Only this freshly launched test game; never an existing session.
            kernel.TerminateProcess(handle, 0)
            kernel.WaitForSingleObject(handle, 5000)
            kernel.CloseHandle(handle)
            for log in logs:
                log.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game', type=Path, default=Path(r'N:\games\systems\pcvr\bin\TheHouseOfTheDead2\Hod2.exe'))
    parser.add_argument('--re-source', type=Path, default=Path(r'C:\src\gaming\hotd2-re'))
    parser.add_argument('--runtime', type=Path, default=ROOT / 'build/bat-trace-stage/build')
    parser.add_argument('--mode', choices=('both', 'native', 'converted'), default='both')
    args = parser.parse_args()
    live = processes()
    if any(name in {'hod2.exe', 'hotd2vr.exe'} or name in VR_NAMES or 'psvr' in name for name, _ in live):
        raise RuntimeError('Existing game/viewer/VR runtime found; preserved. Close before diagnostic run.')
    if hashlib.sha256(args.game.read_bytes()).hexdigest() != EXPECTED:
        raise ValueError('Unsupported game executable')
    stage = ROOT / 'build' / time.strftime('bat-bot-%Y%m%d-%H%M%S')
    stage.mkdir()
    print(f'Desktop-only evidence: {stage}', flush=True)
    build(stage, args.re_source)
    # Preserve the supplied mapped drive instead of converting it to UNC.
    results = [run_case(mode, stage, args.game.absolute(), args.runtime) for mode in
               (('native', 'converted') if args.mode == 'both' else (args.mode,))]
    remaining = [(name, pid) for name, pid in processes() if name in VR_NAMES or 'psvr' in name or name in {'hod2.exe', 'hotd2vr.exe'}]
    (stage / 'result.json').write_text(json.dumps({'captures': results, 'remaining_processes': remaining}, indent=2))
    if remaining:
        raise RuntimeError(f'Test cleanup incomplete: {remaining}')
    print('Captures saved; game/viewers closed; no VR runtime started.', flush=True)


if __name__ == '__main__':
    main()
