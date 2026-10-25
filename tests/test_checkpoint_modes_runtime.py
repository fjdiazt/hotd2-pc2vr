"""Opt-in Arcade/Original native-only test; no renderer or OpenXR session.

Run after build.ps1: python tests/test_checkpoint_modes_runtime.py path-to-Hod2.exe
Uses isolated mod settings/saves, chooses no Original items, restores any native
save-file changes, and closes its own game and command processes.
"""
import ctypes as c
from ctypes import wintypes as w
import hashlib
import json
import mmap
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
SIZE = 88614108
MODE = 88614072
REQUEST = 87385016
NATIVE_SAVES = ('pol/bg_adv19.bin', 'tex/scr_tod_itm_itamidome2.bin', 'pol/komono_0.bin', 'pol/tv2.bin', 'Hod2.ini')


def main():
    game = Path(sys.argv[1]).resolve()
    kernel = c.WinDLL('kernel32', use_last_error=True)
    kernel.OpenFileMappingW.argtypes = [w.DWORD, w.BOOL, w.LPCWSTR]
    kernel.OpenFileMappingW.restype = w.HANDLE
    kernel.OpenProcess.argtypes = [w.DWORD, w.BOOL, w.DWORD]
    kernel.OpenProcess.restype = w.HANDLE
    kernel.ReadProcessMemory.argtypes = [w.HANDLE, c.c_void_p, c.c_void_p, c.c_size_t, c.c_void_p]
    kernel.TerminateProcess.argtypes = [w.HANDLE, w.UINT]
    kernel.WaitForSingleObject.argtypes = [w.HANDLE, w.DWORD]
    kernel.CloseHandle.argtypes = [w.HANDLE]
    user = c.WinDLL('user32', use_last_error=True)
    enum_callback = c.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)
    user.EnumWindows.argtypes = [enum_callback, w.LPARAM]
    user.EnumChildWindows.argtypes = [w.HWND, enum_callback, w.LPARAM]
    user.GetWindowThreadProcessId.argtypes = [w.HWND, c.POINTER(w.DWORD)]
    user.GetWindowTextW.argtypes = [w.HWND, w.LPWSTR, c.c_int]
    user.IsWindowVisible.argtypes = [w.HWND]
    user.PostMessageW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]
    user.SetForegroundWindow.argtypes = [w.HWND]
    user.GetForegroundWindow.restype = w.HWND
    user.ShowWindow.argtypes = [w.HWND, c.c_int]
    user.BringWindowToTop.argtypes = [w.HWND]
    user.AttachThreadInput.argtypes = [w.DWORD, w.DWORD, w.BOOL]
    user.keybd_event.argtypes = [c.c_ubyte, c.c_ubyte, w.DWORD, c.c_size_t]
    name = r'Local\HOTD2_PC2VR_Frame'
    existing = kernel.OpenFileMappingW(4, False, name)
    if existing:
        kernel.CloseHandle(existing)
        raise SystemExit('Existing bridge; no test launched.')
    stage = Path(tempfile.mkdtemp(prefix='mode-runtime-', dir=ROOT / 'build-artifacts'))
    print('Evidence:', stage, flush=True)
    for filename in ('HOTD2Launch.exe', 'HOTD2Bridge.dll', 'HOTD2VR.exe', 'openxr_loader.dll'):
        shutil.copy2(ROOT / 'build' / filename, stage / filename)
    (stage / 'HOTD2VR.ini').write_text('[bridge]\nlaunchfix=1\nskip_splash=1\nwindowed=1\n'
                                      '[vr]\nwindow=0\nvrwindow=0\ncontinue_game=0\nakimbo=0\n'
                                      'master_volume=0\nmusic_volume=0\neffects_volume=0\n')
    original = {}
    for filename in NATIVE_SAVES:
        path = game.parent / filename
        original[path] = path.read_bytes() if path.exists() else None
        if original[path] is not None:
            backup = stage / 'native-save-backups' / filename
            backup.parent.mkdir(parents=True, exist_ok=True)
            backup.write_bytes(original[path])
    game_hash = hashlib.sha256(game.read_bytes()).hexdigest()
    process = None
    memory = None
    children = []
    report = []
    try:
        launch = subprocess.run([str(stage / 'HOTD2Launch.exe'), str(game), '--no-graphics-hooks', '--no-continue-game'],
                                capture_output=True, text=True, check=True, timeout=25)
        pid = int(re.search(r'game PID (\d+)', launch.stdout).group(1))
        process = kernel.OpenProcess(0x100011, False, pid)
        assert process
        memory = mmap.mmap(-1, SIZE, tagname=name, access=mmap.ACCESS_WRITE)
        assert memory[4:12] == struct.pack('<2I', 0x32544F48, 25)

        def text(window):
            value = c.create_unicode_buffer(1024)
            user.GetWindowTextW(window, value, len(value))
            return value.value

        @enum_callback
        def renderer_prompt(window, unused):
            owner = w.DWORD()
            user.GetWindowThreadProcessId(window, c.byref(owner))
            if owner.value == pid and text(window) == 'Attention':
                content = []

                @enum_callback
                def child_text(child, unused):
                    content.append(text(child))
                    return True

                user.EnumChildWindows(window, child_text, 0)
                if any(value.endswith(' will be used. \n') for value in content):
                    user.PostMessageW(window, 0x111, 6, 0)  # Native renderer question: Yes.
            return True

        def read(address, fmt='<I'):
            data = c.create_string_buffer(struct.calcsize(fmt))
            assert kernel.ReadProcessMemory(process, address, data, len(data), None), c.get_last_error()
            return struct.unpack(fmt, data.raw)[0]

        def owns_focus():
            owner = w.DWORD()
            user.GetWindowThreadProcessId(user.GetForegroundWindow(), c.byref(owner))
            return owner.value == pid

        @enum_callback
        def focus_window(window, unused):
            owner = w.DWORD()
            user.GetWindowThreadProcessId(window, c.byref(owner))
            if owner.value != pid or not user.IsWindowVisible(window) or text(window) == 'Attention':
                return True
            current = kernel.GetCurrentThreadId()
            foreground = user.GetWindowThreadProcessId(user.GetForegroundWindow(), None)
            attached = current != foreground and user.AttachThreadInput(current, foreground, True)
            try:
                user.ShowWindow(window, 9)
                user.BringWindowToTop(window)
                user.SetForegroundWindow(window)
            finally:
                if attached:
                    user.AttachThreadInput(current, foreground, False)
            return False

        def focus_native_game():
            user.EnumWindows(focus_window, 0)

        def tick(button=0):
            user.EnumWindows(renderer_prompt, 0)
            if struct.unpack_from('<I', memory, MODE + 4)[0] < 3 and not owns_focus():
                focus_native_game()
            struct.pack_into('<I', memory, 28, 0)
            struct.pack_into('<2fI', memory, 64, 320, 240, button)
            struct.pack_into('<2fI', memory, 36, 320, 240, 0)
            struct.pack_into('<I', memory, 60, struct.unpack_from('<I', memory, 60)[0] + 1)
            struct.pack_into('<3I', memory, 87384220, 512 | 8192 | 16384, kernel.GetTickCount() & 0xFFFFFFFF, 4)
            struct.pack_into('<I', memory, 87385004, 0)

        def wait(predicate, seconds=25):
            until = time.monotonic() + seconds
            while not predicate():
                assert kernel.WaitForSingleObject(process, 0) == 258, 'Owned game exited'
                assert time.monotonic() < until, ('Timed out', read(0x9C8E98), read(0x9C90A4), read(0x9CA08C), read(0x9A2BB0, '<h'))
                tick()
                time.sleep(.02)

        def cli(*args, expected=0):
            result = subprocess.run([str(stage / 'HOTD2VR.exe'), *args], capture_output=True, text=True,
                                    timeout=35, creationflags=subprocess.CREATE_NO_WINDOW)
            assert result.returncode == expected, (args, result.returncode, result.stdout, result.stderr)
            report.append({'command': args, 'result': result.returncode, 'output': result.stdout.strip()})
            print(args, '->', result.returncode, flush=True)
            return result

        def native_key(key):
            if not owns_focus():
                focus_native_game()
                wait(owns_focus, seconds=3)
            owner = w.DWORD()
            user.GetWindowThreadProcessId(user.GetForegroundWindow(), c.byref(owner))
            assert owner.value == pid, 'Native test game must own keyboard focus'
            scan = user.MapVirtualKeyW(key, 0)
            extended = 1 if key in (39, 40) else 0
            user.keybd_event(key, scan, extended, 0)
            try:
                until = time.monotonic() + .12
                while time.monotonic() < until:
                    tick()
                    time.sleep(.02)
            finally:
                user.keybd_event(key, scan, extended | 2, 0)
            until = time.monotonic() + .12
            while time.monotonic() < until:
                tick()
                time.sleep(.02)

        def original_command(*args):
            child = subprocess.Popen([str(stage / 'HOTD2VR.exe'), '--game-mode', 'original', *args],
                                     stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                                     creationflags=subprocess.CREATE_NO_WINDOW)
            children.append(child)
            wait(lambda: struct.unpack_from('<I', memory, MODE + 16)[0] == 1)
            assert read(0x9CA08C) == 1 and read(0x9A1A08, '<h') == 0
            wait(lambda: read(0x9C90A4) == 2 and read(0x9A2BB0, '<h') == 5)
            # Wait through the native case animation, then choose OK with empty slots.
            until = time.monotonic() + 4
            while time.monotonic() < until:
                tick()
                time.sleep(.02)
            focus_native_game()
            wait(owns_focus, seconds=3)
            for _ in range(12):
                if not struct.unpack_from('<I', memory, MODE + 16)[0]:
                    break
                native_key(39)  # Right: equipment-slot column.
                native_key(40)
                native_key(40)  # Two Down presses: OK, without selecting inventory.
                native_key(13)  # Native Start confirms OK.
            wait(lambda: child.poll() is not None)
            stdout, stderr = child.communicate(timeout=5)
            assert child.returncode == 0, (child.returncode, stdout, stderr)
            expected_stage = int(args[args.index('--start-stage') + 1]) if '--start-stage' in args else 2
            assert read(0x9CA08C) == 1 and read(0x9A1A08, '<h') == expected_stage - 1
            assert read(0x9A1A00, '<h') == 0
            assert read(0x9A2240, '<b') == -1 and read(0x9A2241, '<b') == -1
            report.append({'command': ('--game-mode', 'original', *args), 'result': 0, 'output': stdout.strip()})
            print(args, '-> Original native item selection and destination passed', flush=True)

        wait(lambda: 3 <= struct.unpack_from('<I', memory, MODE + 4)[0] <= 12)
        cli('--start-stage', '2', '--start-scene', '0')
        assert read(0x9CA08C) == 0 and read(0x9A1A08, '<h') == 1
        cli('--save')
        arcade = (stage / 'HOTD2VR.checkpoint.ini').read_bytes()
        original_command('--start-stage', '2', '--start-scene', '0')
        cli('--game-mode', 'original', '--save')
        original_save = (stage / 'HOTD2VR.original.checkpoint.ini').read_bytes()
        assert (stage / 'HOTD2VR.checkpoint.ini').read_bytes() == arcade
        cli('--save', expected=1)
        assert (stage / 'HOTD2VR.checkpoint.ini').read_bytes() == arcade
        cli('--load')
        assert read(0x9CA08C) == 0
        original_command('--load')
        assert (stage / 'HOTD2VR.original.checkpoint.ini').read_bytes() == original_save
        wait(lambda: read(0x9A2BB0, '<h') == 1 and read(0x9A5C62, '<h') == 5)
        native_key(13)
        wait(lambda: read(0x9A1A00, '<h') != 0)
        original_command('--load')
        assert (stage / 'HOTD2VR.original.checkpoint.ini').read_bytes() == original_save
        original_command('--start-stage', '1', '--start-scene', '4')
        request = struct.unpack_from('<I', memory, REQUEST)[0]
        cli('--game-mode', 'training', '--load', expected=2)
        assert struct.unpack_from('<I', memory, REQUEST)[0] == request
        cli('--game-mode', 'original', '--start-stage', '2', '--start-scene', '255', expected=1)
        assert read(0x9CA08C) == 1 and read(0x9A1A08, '<h') == 0
        assert (stage / 'HOTD2VR.original.checkpoint.ini').read_bytes() == original_save
        log = (stage / 'HOTD2Bridge.log').read_text()
        assert 'graphics hooks=0 framebuffer readback=0' in log
        (stage / 'results.json').write_text(json.dumps(report, indent=2))
        print('Arcade/Original checkpoint modes passed; graphics hooks remained disabled.', flush=True)
    finally:
        for child in children:
            if child.poll() is None:
                child.terminate()
                child.wait(timeout=5)
        if memory:
            memory.close()
        if process:
            kernel.TerminateProcess(process, 0)
            kernel.WaitForSingleObject(process, 5000)
            kernel.CloseHandle(process)
        for path, data in original.items():
            if data is None:
                path.unlink(missing_ok=True)
            elif not path.exists() or path.read_bytes() != data:
                path.write_bytes(data)
        assert hashlib.sha256(game.read_bytes()).hexdigest() == game_hash
        print('Owned processes closed; native game/save files preserved.', flush=True)


if __name__ == '__main__':
    main()
