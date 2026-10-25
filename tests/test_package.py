"""Release contents, architecture and loader integrity without network or game files."""
from pathlib import Path
import hashlib
import os
import shutil
import struct
import subprocess
import tempfile
import unittest
import zipfile


ROOT = Path(__file__).resolve().parents[1]
LOADER_HASH = '05638C526F07BA3A5F1084BBC3308D654D5167F844FFA1E35515325CB72E0C34'


def pe(machine):
    image = bytearray(128)
    image[:2] = b'MZ'
    struct.pack_into('<I', image, 60, 64)
    image[64:68] = b'PE\x00\x00'
    struct.pack_into('<H', image, 68, machine)
    return image


class PackageTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='hotd2-package-')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        for name in ('HOTD2VR.ini', 'README.md', 'LICENSE', 'start.bat',
                     'THIRD-PARTY.md',
                     'docs/technical-reference.md', 'docs/culling-review.md',
                     'docs/vr-menu.md', 'docs/cheats.md', 'docs/pause.md', 'docs/findings.md',
                     'docs/screenshots/gameplay-vr-animated.png', 'docs/screenshots/vr-settings.png'):
            target = self.root / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / name, target)
        self.build = self.root / 'built files'
        self.build.mkdir()
        for name, machine in (('injector.exe', 0x14C), ('HOTD2Bridge.dll', 0x14C), ('HOTD2VR.exe', 0x8664)):
            (self.build / name).write_bytes(pe(machine))
        for name in ('gun.obj', 'gun.mtl', 'gun.bmp'):
            shutil.copyfile(ROOT / 'assets/gun' / name, self.build / name)
        for name in ('menu-font.rgba', 'test_startup.exe', 'private.log', 'HOTD2VR.ini'):
            (self.build / name).write_bytes(b'local data must not ship')
        self.loader = self.root / 'loader.nupkg'
        with zipfile.ZipFile(self.loader, 'w') as archive:
            archive.writestr('native/x64/release/bin/openxr_loader.dll', pe(0x8664))
        digest = hashlib.sha256(self.loader.read_bytes()).hexdigest().upper()
        self.script = self.root / 'package.ps1'
        self.script.write_text((ROOT / 'package.ps1').read_text().replace(LOADER_HASH, digest))
        self.output = self.root / 'release output'

    def run_package(self, version='v1.2.3', defaults=False):
        env = dict(os.environ)
        env.pop('PSMODULEPATH', None)
        command = ['powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass',
                   '-File', str(self.script), '-Version', version]
        if not defaults:
            command += ['-BuildDirectory', str(self.build), '-OutputDirectory', str(self.output),
                        '-LoaderPackage', str(self.loader)]
        return subprocess.run(command,
                              env=env, capture_output=True, text=True, timeout=30)

    def test_default_paths_work_in_windows_powershell(self):
        shutil.copytree(self.build, self.root / 'build')
        cache = self.root / 'build-artifacts/release-deps/OpenXR.Loader.1.1.65.nupkg'
        cache.parent.mkdir(parents=True)
        shutil.copyfile(self.loader, cache)
        result = self.run_package(defaults=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue((self.root / 'build-artifacts/releases/HOTD2-PC2VR-v1.2.3-windows-x64.zip').is_file())

    def test_release_allowlist_and_checksum(self):
        result = self.run_package()
        self.assertEqual(result.returncode, 0, result.stderr)
        archive = self.output / 'HOTD2-PC2VR-v1.2.3-windows-x64.zip'
        with zipfile.ZipFile(archive) as package:
            paths = {name.replace('\\', '/') for name in package.namelist() if not name.endswith('/')}
            self.assertTrue(paths)
            self.assertTrue(all(name.startswith('vr/') for name in paths), paths)
            files = {name[3:] for name in paths}
            self.assertFalse(any(name.startswith(('vr/', 'build/', 'third_party/')) or name.endswith('.ps1') or
                                 (name.endswith('.bat') and name != 'start.bat') for name in files), files)
            self.assertEqual(files, {
                'README.md', 'LICENSE', 'THIRD-PARTY.md', 'start.bat',
                'docs/technical-reference.md', 'docs/culling-review.md',
                'docs/vr-menu.md', 'docs/cheats.md', 'docs/pause.md',
                'docs/findings.md', 'injector.exe', 'HOTD2Bridge.dll',
                'HOTD2VR.exe', 'HOTD2VR.ini', 'openxr_loader.dll',
                'gun.obj', 'gun.mtl', 'gun.bmp',
                'docs/screenshots/gameplay-vr-animated.png', 'docs/screenshots/vr-settings.png',
            })
            self.assertEqual(package.read('vr/HOTD2VR.ini'), (ROOT / 'HOTD2VR.ini').read_bytes())
            notices = package.read('vr/THIRD-PARTY.md').decode('utf-8').replace('\r\n', '\n')
            self.assertIn((ROOT / 'third_party/openxr/LICENSE').read_text(encoding='utf-8'), notices)
            self.assertNotIn('third_party/openxr/LICENSE', notices)
            for image in ('gameplay-vr-animated.png', 'vr-settings.png'):
                name = 'docs/screenshots/' + image
                self.assertEqual(package.read('vr/' + name), (ROOT / name).read_bytes())
        checksum = archive.with_suffix('.zip.sha256').read_text().strip()
        self.assertEqual(checksum.lower(), f'{hashlib.sha256(archive.read_bytes()).hexdigest()}  {archive.name}'.lower())

    def test_wrong_architecture_rejected(self):
        (self.build / 'HOTD2VR.exe').write_bytes(pe(0x14C))
        result = self.run_package()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Wrong PE architecture', result.stderr)
        self.assertFalse(list(self.output.glob('*.zip')))

    def test_loader_integrity_rejected(self):
        self.loader.write_bytes(b'tampered')
        result = self.run_package()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('OpenXR loader package SHA256 mismatch', result.stderr)
        self.assertFalse(list(self.output.glob('*.zip')))

    def test_unsafe_version_rejected(self):
        result = self.run_package('../escape')
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(self.output.exists())

    def test_missing_runtime_rejected(self):
        (self.build / 'HOTD2Bridge.dll').unlink()
        result = self.run_package()
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(list(self.output.glob('*.zip')))


if __name__ == '__main__':
    unittest.main()
