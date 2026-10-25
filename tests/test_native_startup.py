"""Native setup, including synthetic font data; no game or headset sessions."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest
import test_menu_font

ROOT = Path(__file__).resolve().parents[1]
BUILD = Path(os.environ.get('HOTD2_TEST_BUILD_DIR', ROOT / 'build'))
DRIVER = BUILD / 'test_native_startup.exe'


def run(*args):
    return subprocess.run([str(DRIVER), *map(str, args)], capture_output=True, text=True,
                          timeout=10, creationflags=subprocess.CREATE_NO_WINDOW)


class NativeFontTest(test_menu_font.MenuFontTest):
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
        self.assertEqual(self.run_extractor().returncode, 0)
        self.assertEqual(self.output.read_bytes(), before)

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
