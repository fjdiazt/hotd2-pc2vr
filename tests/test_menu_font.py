"""PowerShell font extraction with synthetic owned-game layouts; no game needed."""
from pathlib import Path
import hashlib
import os
import struct
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
IDENTITY = 'C6B4116788B7F68C56860FB9CC8A94BF984E620907031E4BC3DB43623DBE579A'
SHELL = 'powershell.exe'


class MenuFontTest(unittest.TestCase):
    magic = b'H2PF'
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='hotd2-font-')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.game = self.root / 'owned game' / 'Hod2.exe'
        self.game.parent.mkdir()
        (self.game.parent / 'tex').mkdir()
        exe = bytearray(0x80200)
        struct.pack_into('<I', exe, 60, 0x80)
        struct.pack_into('<H', exe, 0x86, 1)
        struct.pack_into('<8sIIII', exe, 0x98, b'.data', 0x80000, 0x100000, 0x80000, 0x200)

        def put(address, fmt, value):
            struct.pack_into(fmt, exe, address - 0x500000 + 0x200, value)

        put(0x55B9B8 + 0x15E * 4, '<I', 0x510000)
        put(0x55C0A8 + 0x15E * 2, '<h', 1)
        for character in 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789':
            put(0x56AED0 + ord(character) * 2, '<h', 1)
        put(0x57A5BC + 4, '<I', 0x15E)
        put(0x57D448 + 4, '<I', 7)
        struct.pack_into('<hhIII', exe, 0x10200, 2, 2, 0x902, 0, 7)
        self.game.write_bytes(exe)
        self.identity = hashlib.sha256(exe).hexdigest().upper()
        self.extractor = self.root / 'extract.ps1'
        self.extractor.write_text((ROOT / 'tools/extract_menu_font.ps1').read_text().replace(IDENTITY, self.identity))
        pixels = struct.pack('<4H', 0xF123, 0x8456, 0x0789, 0xCABC) + bytes(64512 - 8)
        compressed = b''.join(b'\xff' + pixels[i:i+8] for i in range(0, len(pixels), 8))
        self.texture = self.game.parent / 'tex/scr_opt_moji05.bin'
        self.texture.write_bytes(struct.pack('<I', 64512) + compressed + b'\x02\x00\x00')
        self.output = self.root / 'new output/menu-font.rgba'

    def run_extractor(self):
        env = dict(os.environ)
        env.pop('PSMODULEPATH', None)
        return subprocess.run([SHELL, '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
                               str(self.extractor), str(self.game), str(self.output)],
                              env=env, capture_output=True, text=True, timeout=45)

    def test_rgba_layout_and_vertical_flip(self):
        result = self.run_extractor()
        self.assertEqual(result.returncode, 0, result.stderr)
        expected = bytearray(self.magic + bytes(128 * 32 * 32 * 4))
        for character in 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789':
            base = 4 + ord(character) * 32 * 32 * 4
            expected[base:base+8] = bytes((119, 136, 153, 0, 170, 187, 204, 204))
            expected[base+128:base+136] = bytes((17, 34, 51, 255, 68, 85, 102, 136))
        self.assertEqual(self.output.read_bytes(), expected)

    def test_unsupported_executable_preserves_existing_output(self):
        self.game.write_bytes(b'unsupported')
        self.output.parent.mkdir()
        self.output.write_bytes(b'existing font')
        result = self.run_extractor()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Unsupported HOTD2 executable', result.stderr)
        self.assertEqual(self.output.read_bytes(), b'existing font')

    def test_corrupt_texture_writes_nothing(self):
        for compressed in (b'', b'\x02\x00\x00', b'\x00\xff'):
            with self.subTest(compressed=compressed):
                self.texture.write_bytes(struct.pack('<I', 64512) + compressed)
                result = self.run_extractor()
                self.assertNotEqual(result.returncode, 0)
                self.assertFalse(self.output.exists())

    def test_overlapping_back_reference(self):
        # One literal, then a short overlapping copy, then end marker.
        self.texture.write_bytes(struct.pack('<I', 64512) + b'\x41\x7f\xff\x00\x00')
        result = self.run_extractor()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Unexpected decompressed font size', result.stderr)
        self.assertFalse(self.output.exists())

    def test_long_overlapping_back_references(self):
        compressed = bytearray()
        left = 0
        control = 0

        def bit(value):
            nonlocal left, control
            if not left:
                control = len(compressed)
                compressed.append(0)
                left = 8
            compressed[control] |= value << (8 - left)
            left -= 1

        bit(1)
        compressed.append(0)
        remaining = 64511
        while remaining:
            count = min(256, remaining)
            bit(0)
            bit(1)
            compressed.extend(struct.pack('<HB', 0xFFF8, count - 1))
            remaining -= count
        bit(0)
        bit(1)
        compressed.extend(b'\x00\x00')
        self.texture.write_bytes(struct.pack('<I', 64512) + compressed)
        result = self.run_extractor()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.output.read_bytes(), self.magic + bytes(128 * 32 * 32 * 4))


if __name__ == '__main__':
    unittest.main()
