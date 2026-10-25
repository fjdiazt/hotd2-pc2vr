"""Native setup, including synthetic font data; no game or headset sessions."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest
import test_menu_font

ROOT = Path(__file__).resolve().parents[1]
BUILD = Path(os.environ.get('HOTD2_TEST_BUILD_DIR', ROOT / 'build-artifacts'))
DRIVER = BUILD / 'test_native_startup.exe'


def run(*args):
    return subprocess.run([str(DRIVER), *map(str, args)], capture_output=True, text=True,
                          timeout=10, creationflags=subprocess.CREATE_NO_WINDOW)


class NativeFontTest(test_menu_font.MenuFontTest):
    magic = b'H2P2'

    def setUp(self):
        super().setUp()
        import hashlib
        import struct
        exe = bytearray(self.game.read_bytes())

        def put(address, fmt, *values):
            struct.pack_into(fmt, exe, address - 0x500000 + 0x200, *values)

        put(0x55B9B8 + 0x156 * 4, '<I', 0x510100)
        put(0x55C0A8 + 0x156 * 2, '<h', 1)
        put(0x57A5BC + 0x5F7 * 4, '<I', 0x156)
        put(0x57D448 + 0x5F7 * 4, '<I', 9)
        put(0x510100, '<hhIII', 32, 32, 0x902, 0, 9)
        self.game.write_bytes(exe)
        self.identity = hashlib.sha256(exe).hexdigest().upper()
        self.items_texture = self.game.parent / 'tex/scr_item.bin'
        self.write_items(bytes(811008))

    def write_items(self, pixels):
        import struct
        compressed = b''.join(b'\xff' + pixels[i:i+8] for i in range(0, len(pixels), 8))
        self.items_texture.write_bytes(struct.pack('<I', len(pixels)) + compressed + b'\x02\x00\x00')

    def run_extractor(self):
        self.output.parent.mkdir(parents=True, exist_ok=True)
        return run('font', self.game, self.output.parent / 'HOTD2VR.exe', self.output, self.identity)

    def test_corruption_preserves_previous_font(self):
        self.output.parent.mkdir()
        self.output.write_bytes(b'old font')
        self.texture.write_bytes(b'corrupt')
        self.assertNotEqual(self.run_extractor().returncode, 0)
        self.assertEqual(self.output.read_bytes(), b'old font')
        self.assertEqual(list(self.output.parent.iterdir()), [self.output])

    def test_valid_cache_needs_no_texture(self):
        self.assertEqual(self.run_extractor().returncode, 0)
        before = self.output.read_bytes()
        self.texture.unlink()
        self.items_texture.unlink()
        self.assertEqual(self.run_extractor().returncode, 0)
        self.assertEqual(self.output.read_bytes(), before)

    def test_native_arrow_and_vertical_flip(self):
        import struct
        pixels = bytearray(811008)
        struct.pack_into('<H', pixels, 0, 0xF123)
        struct.pack_into('<H', pixels, 31 * 32 * 2, 0x8456)
        self.write_items(pixels)
        result = self.run_extractor()
        self.assertEqual(result.returncode, 0, result.stderr)
        font = self.output.read_bytes()
        down, up = font[4+4096:4+8192], font[4+8192:4+12288]
        self.assertEqual(down[:4], bytes((68, 85, 102, 136)))
        self.assertEqual(up[:4], bytes((17, 34, 51, 255)))
        self.assertEqual(up, b''.join(down[row*128:(row+1)*128] for row in reversed(range(32))))

    def test_old_cache_upgrades_atomically(self):
        self.output.parent.mkdir()
        self.output.write_bytes(b'H2PF' + bytes(128 * 32 * 32 * 4))
        result = self.run_extractor()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.output.read_bytes()[:4], self.magic)
        self.assertEqual(list(self.output.parent.iterdir()), [self.output])

    def test_bad_arrow_keeps_old_cache(self):
        old = b'H2PF' + bytes(128 * 32 * 32 * 4)
        self.output.parent.mkdir()
        self.output.write_bytes(old)
        for payload in (b'corrupt', b'\x00\x60\x0c\x00\x00\xff'):
            with self.subTest(payload=payload):
                self.items_texture.write_bytes(payload)
                self.assertNotEqual(self.run_extractor().returncode, 0)
                self.assertEqual(self.output.read_bytes(), old)
                self.assertEqual(list(self.output.parent.iterdir()), [self.output])

    def test_arrow_descriptor_bounds(self):
        import hashlib
        import struct
        original = self.game.read_bytes()
        for width, height, fmt, offset in ((33, 32, 0x902, 0), (32, 32, 0x901, 0), (32, 32, 0x902, 811008)):
            with self.subTest(width=width, height=height, fmt=fmt, offset=offset):
                exe = bytearray(original)
                struct.pack_into('<hhIII', exe, 0x10300, width, height, fmt, offset, 9)
                self.game.write_bytes(exe)
                self.identity = hashlib.sha256(exe).hexdigest().upper()
                self.assertNotEqual(self.run_extractor().returncode, 0)
                self.assertFalse(self.output.exists())

    def test_damaged_pe_sections_rejected(self):
        data = bytearray(self.game.read_bytes())
        data[60:64] = b'\xff' * 4
        self.game.write_bytes(data)
        import hashlib
        self.identity = hashlib.sha256(data).hexdigest().upper()
        self.assertNotEqual(self.run_extractor().returncode, 0)
        self.assertFalse(self.output.exists())


class NativeStartupTest(unittest.TestCase):
    def test_nearest_game_and_explicit_override(self):
        with tempfile.TemporaryDirectory(prefix='hotd2 native & [1] ') as temporary:
            root = Path(temporary)
            mod = root / 'owned game/vr/nested'
            mod.mkdir(parents=True)
            viewer = mod / 'HOTD2VR.exe'
            self.assertNotEqual(run('find', viewer).returncode, 0)
            outer = root / 'Hod2.exe'
            inner = mod.parent / 'Hod2.exe'
            outer.touch()
            inner.touch()
            result = run('find', viewer)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(Path(result.stdout.strip()), inner)
            self.assertEqual(Path(run('find', viewer, outer).stdout.strip()), outer)
            self.assertNotEqual(run('find', viewer, root / 'missing.exe').returncode, 0)
            beside = mod / 'Hod2.exe'
            beside.touch()
            self.assertEqual(Path(run('find', viewer).stdout.strip()), beside)
            result = run('prepare', viewer, beside)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('SHA256 mismatch', result.stderr)
            self.assertFalse((mod / 'menu-font.rgba').exists())

    def test_native_helper_receives_literal_paths_and_flags(self):
        with tempfile.TemporaryDirectory(prefix='hotd2 native & [1] ') as temporary:
            root = Path(temporary)
            source = root / 'stub.c'
            source.write_text('''#include <stdio.h>
#include <wchar.h>
int wmain(int argc,wchar_t **argv) {
    FILE *out=_wfopen(L"args.txt",L"wb");
    if(!out)return 99;
    for(int i=1;i<argc;i++)fwprintf(out,L"%ls\\n",argv[i]);
    fclose(out);
    return argc>2 && !wcscmp(argv[2],L"--show-splash")?7:0;
}
''')
            subprocess.run(['gcc', '-municode', '-static-libgcc', '-o', str(root / 'HOTD2Launch.exe'),
                            str(source)], check=True, capture_output=True, timeout=30)
            # The helper inherits cwd; shell metacharacters must remain literal.
            game = root / 'game & [1].exe'
            options = '--no-window --no-vrwindow --no-graphics-hooks --start-stage 4 --start-scene 0 --no-continue-game'
            result = subprocess.run([str(DRIVER), 'start', str(root / 'HOTD2VR.exe'), str(game), options],
                                    cwd=root, capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual((root / 'args.txt').read_text(encoding='utf-16-le').splitlines(), [str(game), *options.split()])
            result = subprocess.run([str(DRIVER), 'start', str(root / 'HOTD2VR.exe'), str(game), '--show-splash'],
                                    cwd=root, capture_output=True, timeout=10)
            self.assertNotEqual(result.returncode, 0)

    def test_viewer_rejects_invalid_startup_options(self):
        for flags in (['--start-scene', '0'], ['--start-stage', '7'], ['--start-scene', '-1'],
                      ['--start-stage', '4x'], ['--game-exe'], ['--start-stage'], ['--game-mode'],
                      ['--game-mode', 'training'], ['--game-mode', '2'], ['--save', '--load'],
                      ['--save', '--start-stage', '2'], ['--load', '--start-stage', '2']):
            result = subprocess.run([str(BUILD / 'HOTD2VR.exe'), *flags], capture_output=True,
                                    timeout=5, creationflags=subprocess.CREATE_NO_WINDOW)
            self.assertEqual(result.returncode, 2, (flags, result.stdout, result.stderr))


if __name__ == '__main__':
    unittest.main()
