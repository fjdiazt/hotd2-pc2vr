"""Portable and protected-install storage; no game or headset sessions."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
BUILD = Path(os.environ.get('HOTD2_TEST_BUILD_DIR', ROOT / 'build-artifacts'))


class DataPathsTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='hotd2 data ')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.mod = self.root / 'game' / 'vr'
        self.mod.mkdir(parents=True)
        self.local = self.root / 'local'
        self.local.mkdir()
        self.env = dict(os.environ, LOCALAPPDATA=str(self.local))

    def resolve(self, module='HOTD2VR.exe', blocked=True, success=True, driver='test_native_startup.exe'):
        result = subprocess.run([str(BUILD / driver), 'data', str(self.mod / module),
                                 'blocked' if blocked else 'writable'], env=self.env,
                                capture_output=True, text=True, encoding='utf-8', timeout=10)
        if success:
            self.assertEqual(result.returncode, 0, result.stderr)
            return Path(result.stdout.strip())
        self.assertNotEqual(result.returncode, 0)
        return result

    def test_writable_install_stays_portable(self):
        self.assertEqual(self.resolve(blocked=False), self.mod / 'HOTD2VR.ini')
        self.assertEqual(list(self.local.iterdir()), [])
        self.assertEqual(list(self.mod.iterdir()), [])

    def test_fallback_preserves_existing_settings_and_both_modes(self):
        files = {'HOTD2VR.ini': b'[vr]\r\nbrightness=120\r\n',
                 'HOTD2VR.checkpoint.ini': b'[checkpoint]\r\nstage=4\r\n',
                 'HOTD2VR.original.checkpoint.ini': b'[checkpoint]\r\nstage=2\r\n'}
        for name, content in files.items():
            (self.mod / name).write_bytes(content)
        target = self.resolve()
        self.assertEqual(target.parent.parent, self.local / 'HOTD2VR')
        for name, content in files.items():
            self.assertEqual((target.parent / name).read_bytes(), content)
            self.assertEqual((self.mod / name).read_bytes(), content)
        target.write_bytes(b'updated user settings')
        (self.mod / 'HOTD2VR.ini').write_bytes(b'new packaged defaults')
        self.assertEqual(self.resolve(blocked=False), target)
        self.assertEqual(target.read_bytes(), b'updated user settings')

    def test_deleted_user_files_are_not_reimported(self):
        for name in ('HOTD2VR.ini', 'HOTD2VR.checkpoint.ini', 'HOTD2VR.original.checkpoint.ini'):
            (self.mod / name).write_bytes(b'old portable data')
        target = self.resolve()
        for file in target.parent.glob('*.ini'):
            file.unlink()
        self.assertEqual(self.resolve(), target)
        self.assertEqual(list(target.parent.glob('*.ini')), [])

    def test_all_modules_and_architectures_share_the_directory(self):
        target = self.resolve()
        for module in ('injector.exe', 'HOTD2Bridge.dll'):
            self.assertEqual(self.resolve(module=module), target)
        self.assertEqual(self.resolve(module='hotd2vr.EXE'), target)
        wide_driver = BUILD / 'test_native_startup64.exe'
        if wide_driver.exists():
            self.assertEqual(self.resolve(driver=wide_driver.name), target)

    def test_different_installs_keep_separate_data(self):
        first = self.resolve()
        self.mod = self.root / 'other' / 'vr'
        self.mod.mkdir(parents=True)
        self.assertNotEqual(self.resolve().parent, first.parent)

    def test_unavailable_local_app_data_fails_without_touching_sources(self):
        self.env.pop('LOCALAPPDATA')
        result = self.resolve(success=False)
        self.assertIn('Windows error', result.stderr)
        self.assertEqual(list(self.mod.iterdir()), [])

    def test_blocked_fallback_fails_without_using_stale_portable_data(self):
        target = self.resolve()
        target.write_bytes(b'existing user settings')
        self.env['LOCALAPPDATA'] = str(self.root / 'missing-parent' / 'local')
        self.resolve(success=False)
        self.assertEqual(target.read_bytes(), b'existing user settings')

    def test_existing_readonly_ini_is_copied_as_writable(self):
        import stat
        source = self.mod / 'HOTD2VR.ini'
        source.write_bytes(b'original settings')
        source.chmod(stat.S_IREAD)
        self.addCleanup(source.chmod, stat.S_IWRITE)
        target = self.resolve()
        target.write_bytes(b'new settings')
        self.assertEqual(source.read_bytes(), b'original settings')

    def test_real_denied_directory_uses_same_fallback_in_x86_and_x64(self):
        import csv
        identity = subprocess.run(['whoami', '/user', '/fo', 'csv', '/nh'],
                                  capture_output=True, text=True, check=True, timeout=10)
        sid = next(csv.reader(identity.stdout.strip().splitlines()))[1]
        rule = subprocess.run(['icacls', str(self.mod), '/deny', f'*{sid}:(WD,DC)'],
                              capture_output=True, timeout=10)
        self.assertEqual(rule.returncode, 0, rule.stderr)
        try:
            target = self.resolve(blocked=False)
            self.assertEqual(target.parent.parent, self.local / 'HOTD2VR')
            wide_driver = BUILD / 'test_native_startup64.exe'
            if wide_driver.exists():
                self.assertEqual(self.resolve(blocked=False, driver=wide_driver.name), target)
        finally:
            restored = subprocess.run(['icacls', str(self.mod), '/remove:d', '*' + sid],
                                     capture_output=True, timeout=10)
            self.assertEqual(restored.returncode, 0, restored.stderr)


if __name__ == '__main__':
    unittest.main()
