"""Validate a running injected HOTD2 session; never launches or closes the game."""
import argparse
import ctypes as c
from ctypes import wintypes as w
import json
import math
import mmap
import struct
import time

parser = argparse.ArgumentParser()
parser.add_argument("--input", action="store_true", help="briefly own both gun axes, without firing")
args = parser.parse_args()
SIZE = 87385008
memory = mmap.mmap(-1, SIZE, tagname=r"Local\HOTD2_PC2VR_Frame", access=mmap.ACCESS_WRITE if args.input else mmap.ACCESS_READ)
assert struct.unpack_from("<2I", memory, 4) == (0x32544F48, 16), "HOTD2 bridge not running or protocol mismatch"

def snapshot():
    deadline = time.monotonic() + 3
    while time.monotonic() < deadline:
        before = struct.unpack_from("<I", memory)[0]
        if before & 1:
            continue
        n = struct.unpack_from("<I", memory, 16)[0]
        assert n <= 240000 and n % 3 == 0
        data = memory[:156 + n * 64]
        after = struct.unpack_from("<I", memory)[0]
        if before == after:
            return data
    raise AssertionError("No stable frame")

data = snapshot()
first = struct.unpack_from("<I", data, 12)[0]
time.sleep(0.25)
data = snapshot()
assert struct.unpack_from("<I", data, 12)[0] > first, "Game stopped publishing"
assert struct.unpack_from("<I", data, 152)[0], "No live projection"
fx, fy, cx, cy = struct.unpack_from("<4f", data, 108)
assert all(math.isfinite(v) for v in (fx, fy, cx, cy)) and fx > 0 and fy > 0
n = struct.unpack_from("<I", data, 16)[0]
assert n > 0, "No 3D scene: wait for attract gameplay or start a game"
for i in range(n):
    v = struct.unpack_from("<3fI7f4If", data, 156 + i * 64)
    assert all(math.isfinite(x) for x in v[:3] + v[4:11]), f"Nonfinite vertex {i}"
    assert 0 <= v[9] <= 1 and 0 <= v[10] <= 1, "Atlas tile outside bounds"
    assert math.isfinite(v[15]) and (v[14] >> 24) <= 3, "Invalid native fog"
    address = v[12] & ~0x100  # HOTD_GFILE presentation provenance is separate from native U/V modes.
    assert address <= 10 and (address & 3) <= 2 and ((address >> 2) & 3) <= 2, "Invalid texture address modes"
report = {"frame": struct.unpack_from("<I", data, 12)[0], "vertices": n,
          "lens": [fx, fy, cx, cy], "draw_calls": struct.unpack_from("<I", data, 140)[0],
          "dropped_triangles": struct.unpack_from("<I", data, 144)[0],
          "textures": struct.unpack_from("<I", data, 148)[0]}

if args.input:
    observed = struct.unpack_from("<I", memory, 60)[0]
    time.sleep(.1)
    assert struct.unpack_from("<I", memory, 60)[0] == observed, "Close the VR viewer before the input self-check"
    kernel = c.WinDLL("kernel32", use_last_error=True)
    kernel.OpenProcess.argtypes = [w.DWORD, w.BOOL, w.DWORD]
    kernel.OpenProcess.restype = w.HANDLE
    kernel.ReadProcessMemory.argtypes = [w.HANDLE, c.c_void_p, c.c_void_p, c.c_size_t, c.c_void_p]
    kernel.CloseHandle.argtypes = [w.HANDLE]
    pid = struct.unpack_from("<I", data, 124)[0]
    handle = kernel.OpenProcess(0x10, False, pid)
    assert handle, "Cannot read game process"

    def read(address, size):
        buffer = c.create_string_buffer(size)
        assert kernel.ReadProcessMemory(handle, address, buffer, size, None), c.WinError(c.get_last_error())
        return buffer.raw

    previous_akimbo = struct.unpack_from("<I", memory, 28)[0]
    try:
        struct.pack_into("<I", memory, 28, 1)
        for sx, sy in [(0, 0), (320, 240), (639, 479), (-100, -80), (740, 560)]:
            for _ in range(30):
                struct.pack_into("<2fI", memory, 64, sx, sy, 0)
                struct.pack_into("<2fI", memory, 36, 640-sx, 480-sy, 0)
                struct.pack_into("<I", memory, 60, struct.unpack_from("<I", memory, 60)[0]+1)
                time.sleep(.01)
            assert struct.unpack("<2h", read(0x9C8FC4, 4)) == (sx-320, 240-sy)
            assert struct.unpack("<2h", read(0x9C8FEC, 4)) == (320-sx, sy-240)
        time.sleep(.8)
        assert read(0x40D1AD, 4) == bytes.fromhex("66 89 46 04"), "X axis hook not released"
        assert read(0x40D1B7, 4) == bytes.fromhex("66 89 46 06"), "Y axis hook not released"
        report["input_axes_and_watchdog"] = "passed"
    finally:
        struct.pack_into("<I", memory, 72, 0)
        struct.pack_into("<I", memory, 44, 0)
        struct.pack_into("<I", memory, 28, previous_akimbo)
        kernel.CloseHandle(handle)
memory.close()
print(json.dumps(report, indent=2))
