"""Opt-in native desktop smoke test. Launches/closes only its own injected game; never VR.

Run after build.ps1: python tests/test_checkpoint_runtime.py path-to-Hod2.exe
Preserves the existing save file and INI. Requires no game/viewer session running.
"""
import ctypes as c
from ctypes import wintypes as w
import mmap
from pathlib import Path
import re
import struct
import subprocess
import sys
import time

build = Path(__file__).resolve().parents[1] / "build"
save = build / "HOTD2VR.checkpoint.ini"
kernel = c.WinDLL("kernel32", use_last_error=True)
kernel.OpenProcess.argtypes = [w.DWORD, w.BOOL, w.DWORD]
kernel.OpenProcess.restype = w.HANDLE
kernel.ReadProcessMemory.argtypes = [w.HANDLE, c.c_void_p, c.c_void_p, c.c_size_t, c.c_void_p]
kernel.TerminateProcess.argtypes = [w.HANDLE, w.UINT]
kernel.WaitForSingleObject.argtypes = [w.HANDLE, w.DWORD]
kernel.CloseHandle.argtypes = [w.HANDLE]
kernel.OpenFileMappingW.argtypes = [w.DWORD, w.BOOL, w.LPCWSTR]
kernel.OpenFileMappingW.restype = w.HANDLE
name = r"Local\HOTD2_PC2VR_Frame"
old = kernel.OpenFileMappingW(4, False, name)
if old:
    kernel.CloseHandle(old)
    raise SystemExit("Existing game/viewer session; no test launched")
original = save.read_bytes() if save.exists() else None
ini = (build / "HOTD2VR.ini").read_bytes()


def session(from_menu=False):
    command = [str(build / "injector.exe"), str(Path(sys.argv[1]).resolve()), "--no-continue-game"]
    if not from_menu:
        command += ["--start-stage", "1", "--start-scene", "4"]
    launched = subprocess.run(command, cwd=build, text=True, capture_output=True, check=True)
    pid = int(re.search(r"game PID (\d+)", launched.stdout).group(1))
    process = kernel.OpenProcess(0x100011, False, pid)
    assert process
    memory = None
    try:
        memory = mmap.mmap(-1, 88614108, tagname=name, access=mmap.ACCESS_WRITE)
        assert struct.unpack_from("<2I", memory, 4) == (0x32544F48, 25)

        def read(address, fmt="<I"):
            data = c.create_string_buffer(struct.calcsize(fmt))
            assert kernel.ReadProcessMemory(process, address, data, len(data), None), c.get_last_error()
            return struct.unpack(fmt, data.raw)[0]

        def tick(button=0):
            struct.pack_into("<I", memory, 28, 1)
            struct.pack_into("<2fI", memory, 64, 320, 240, button | 64 if button else 0)
            struct.pack_into("<2fI", memory, 36, 320, 240, 0)
            struct.pack_into("<I", memory, 60, struct.unpack_from("<I", memory, 60)[0] + 1)
            struct.pack_into("<3I", memory, 87384220, 256 | 512 | 1024 | 8192 | 16384,
                             kernel.GetTickCount() & 0xFFFFFFFF, 10)
            struct.pack_into("<I", memory, 87385004, 0)  # Muted; leave configuration untouched.

        def wait(predicate, seconds=35):
            until = time.monotonic() + seconds
            while not predicate():
                assert time.monotonic() < until, ("Timed out", read(0x9C8E98), read(0x9C90A4))
                tick()
                time.sleep(.02)

        def command(action, stage=0, scene=0):
            request = struct.unpack_from("<I", memory, 87385016)[0] + 1
            struct.pack_into("<2I", memory, 87385060, stage, scene)
            struct.pack_into("<I", memory, 87385036, action)
            struct.pack_into("<I", memory, 87385016, request)
            wait(lambda: struct.unpack_from("<I", memory, 87385020)[0] == request)
            result = struct.unpack_from("<3I", memory, 87385024)
            print("menu" if from_menu else "gameplay", "action", action, "result", result, flush=True)
            return result

        def write(stage, scene):
            save.write_text(f"[checkpoint]\nversion=1\nstage={stage}\nscene={scene}\n")

        wait(lambda: read(0x9C8E98) in (3, 4, 5))
        for stage in range(1, 7):
            catalog = struct.unpack_from("<8I", memory, 87385068 + (stage - 1) * 32)
            entrance = 7 if stage == 3 else 0
            assert catalog[entrance // 32] & (1 << (entrance % 32))
        if from_menu:
            before = save.read_bytes()
            assert command(0)[0] == 2 and save.read_bytes() == before
            wait(lambda: struct.unpack_from("<2I", memory, 87385052) == (2, 0))
            assert struct.unpack_from("<I", memory, 87385044)[0] == 0
            assert command(1) == (1, 2, 0)  # Load works without autosave, including from title.
        else:
            start = time.monotonic()
            while read(0x9C8E98) != 6:
                assert time.monotonic() - start < 25
                tick(2 if int((time.monotonic() - start) * 2) % 4 == 0 else 0)
                time.sleep(.02)
            wait(lambda: read(0x9C90A4) == 2 and read(0x9A2224, "<h") > 0)
            for _ in range(15):
                tick()
                time.sleep(.02)
            for _ in range(10):
                tick(128)
                time.sleep(.02)
            tick()
            wait(lambda: read(0x9A1A00, "<h") != 0)
            point = (read(0x9A1A08, "<h") + 1, read(0x9A2BC0, "<h"))
            wait(lambda: struct.unpack_from("<2I", memory, 87385044) == point)
            assert command(0) == (1, *point)
            wait(lambda: struct.unpack_from("<2I", memory, 87385052) == point)
            assert command(1) == (1, *point)  # Native resume and restart from paused gameplay.
            assert read(0x9A1A00, "<h") == 0
            write(2, 0)
            assert command(1) == (1, 2, 0)  # Cross-chapter unload/reinitialization.
            before = save.read_bytes()
            assert command(4, 1, 4) == (1, 1, 4) and save.read_bytes() == before
            assert command(4, 20, 0)[0] == 4
            assert command(4, 1, 255)[0] == 4
            assert command(4, 2, 0) == (1, 2, 0) and save.read_bytes() == before
            before = save.read_bytes()
            assert command(2) == (1, 1, 0) and save.read_bytes() == before
            write(2, 255)
            assert command(1)[0] == 4 and read(0x9A1A08, "<h") == 0
            struct.pack_into("<I", memory, 87385040, 2)
            wait(lambda: "stage=1" in save.read_text())
            struct.pack_into("<I", memory, 87385040, 1)
            write(2, 0)
            before = save.read_bytes()
            assert command(2) == (1, 1, 0) and save.read_bytes() == before
        wait(lambda: read(0x9A2224, "<h") > 0 and struct.unpack_from("<I", memory, 48)[0] > 0)
        print("native section", read(0x9A2224, "<h"), "geometry present", flush=True)
    finally:
        if memory:
            memory.close()
        kernel.TerminateProcess(process, 0)
        kernel.WaitForSingleObject(process, 5000)
        kernel.CloseHandle(process)
        time.sleep(.3)


try:
    session()
    session(from_menu=True)
finally:
    if original is None:
        save.unlink(missing_ok=True)
    else:
        save.write_bytes(original)
    assert (build / "HOTD2VR.ini").read_bytes() == ini
print("Native save/load/stage select/new game, paused load, live autosave, invalid targets and title load passed")
