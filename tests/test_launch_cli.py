"""Launcher option validation without starting a game."""
from pathlib import Path
import hashlib
import os
import shutil
import subprocess
import tempfile
import unittest


class LaunchOptionsTest(unittest.TestCase):
    def test_splash_flags_reach_game_validation(self):
        launcher = Path(os.environ.get("HOTD2_TEST_BUILD_DIR", Path(__file__).resolve().parents[1] / "build")) / "injector.exe"
        for flags in (("--skip-splash",), ("--show-splash",),
                      ("--wait", "--skip-splash"),
                      ("--native-window", "--show-splash"),
                      ("--no-graphics-hooks", "--start-stage", "4", "--start-scene", "0"),
                      ("--no-window", "--no-vrwindow", "--start-stage", "4", "--start-scene", "0"),
                      ("--no-window", "--no-vrwindow", "--continue-game")):
            with self.subTest(flags=flags):
                result = subprocess.run(
                    [str(launcher), "__missing_hotd2__.exe", *flags],
                    capture_output=True, text=True, timeout=5)
                self.assertEqual(result.returncode, 2)
                self.assertIn("Game executable missing.", result.stderr)

    def test_output_flags_reach_both_bridge_and_service_process(self):
        root = Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory(prefix="hotd2-launch-") as temporary:
            fixture = Path(temporary).resolve()  # GitHub's TEMP can use an 8.3 alias.
            build = fixture / "build"
            build.mkdir()
            game = fixture / "game & file [1].exe"
            game.write_bytes(b"Test identity; never executed")
            script = (root / "Start-HOTD2.ps1").read_text()
            script = script.replace(
                "C6B4116788B7F68C56860FB9CC8A94BF984E620907031E4BC3DB43623DBE579A",
                hashlib.sha256(game.read_bytes()).hexdigest().upper())
            (fixture / "Start-HOTD2.ps1").write_text(script)
            (build / "HOTD2Bridge.dll").touch()
            (build / "menu-font.rgba").touch()
            shim = fixture / "launch.c"
            shim.write_text('''#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
int wmain(int argc,wchar_t **argv) {
    int viewer=wcsstr(argv[0],L"HOTD2VR.exe")!=NULL;
    FILE *out=fopen(getenv(viewer?"HOTD2_TEST_VIEWER_ARGS":"HOTD2_TEST_LAUNCH_ARGS"),"w");
    if(!out)return 99;
    for(int i=1;i<argc;i++)fprintf(out,"%ls\\n",argv[i]);
    fclose(out);
    const char *failure=getenv("HOTD2_TEST_LAUNCH_EXIT");
    return !viewer && failure?atoi(failure):0;
}
''')
            subprocess.run(["gcc", "-municode", "-static-libgcc", "-o",
                            str(build / "injector.exe"), str(shim)], check=True,
                           capture_output=True, text=True, timeout=30)
            shutil.copyfile(build / "injector.exe", build / "HOTD2VR.exe")
            log = fixture / "args.txt"
            viewer_log = fixture / "viewer-args.txt"
            env = dict(os.environ, HOTD2_TEST_LAUNCH_ARGS=str(log), HOTD2_TEST_VIEWER_ARGS=str(viewer_log))
            command = [shutil.which("pwsh") or "powershell", "-NoProfile", "-File",
                       str(fixture / "Start-HOTD2.ps1"), "-GameExe", str(game),
                       "-NoWindow", "-NoVrWindow", "-StartStage", "4", "-StartScene", "0",
                       "-NoContinueGame", "-InfiniteAmmo", "-Invincibility", "-ProtectRescueNpcs", "-MasterVolume", "25"]
            result = subprocess.run(command, env=env, capture_output=True, text=True, timeout=15)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(log.read_text().splitlines(), [str(game), "--start-stage", "4",
                             "--start-scene", "0", "--no-continue-game", "--no-window", "--no-vrwindow"])
            self.assertEqual(viewer_log.read_text().splitlines(), ["--no-window", "--no-vrwindow",
                             "--infinite-ammo", "--invincibility", "--protect-rescue-npcs", "--master-volume", "25"])
            # Model an extracted release several levels below its game, launched from another CWD.
            mod = fixture / "game folder & spaces [1]" / "vr"
            mod.mkdir(parents=True)
            shutil.copyfile(fixture / "Start-HOTD2.ps1", mod / "Start-HOTD2.ps1")
            shutil.copyfile(root / "Start-HOTD2.bat", mod / "Start-HOTD2.bat")
            shutil.copytree(build, mod / "build")
            automatic = command[:3] + [str(mod / "Start-HOTD2.ps1")]
            detected = fixture / "Hod2.exe"
            detected.write_bytes(game.read_bytes())
            result = subprocess.run(automatic, cwd=root, env=env, capture_output=True,
                                    text=True, timeout=15)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(log.read_text().splitlines(), [str(detected)])
            nearby = mod.parent / "Hod2.exe"
            nearby.write_bytes(game.read_bytes())
            result = subprocess.run(automatic, cwd=root, env=env, capture_output=True,
                                    text=True, timeout=15)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(log.read_text().splitlines(), [str(nearby)])
            colocated = mod / "Hod2.exe"
            colocated.write_bytes(game.read_bytes())
            result = subprocess.run(automatic, cwd=root, env=env, capture_output=True,
                                    text=True, timeout=15)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(log.read_text().splitlines(), [str(colocated)])
            colocated.unlink()
            bat = f'""{mod / "Start-HOTD2.bat"}""'
            result = subprocess.run(f'"{os.environ["COMSPEC"]}" /d /s /c {bat}', cwd=root,
                                    env=env, input="", capture_output=True, text=True, timeout=15)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual(log.read_text().splitlines(), [str(nearby)])
            self.assertEqual(viewer_log.read_text().splitlines(), [])
            result = subprocess.run(automatic + ["-GameExe", game.name], cwd=fixture,
                                    env=env, capture_output=True, text=True, timeout=15)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(log.read_text().splitlines(), [str(game)])
            nearby.write_bytes(game.read_bytes() + b"changed executable hash")
            log.unlink(); viewer_log.unlink()
            result = subprocess.run(automatic, env=env, capture_output=True, text=True, timeout=15)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("Unrecognized HOTD2 executable SHA256", result.stdout + result.stderr)
            self.assertIn('SHA256: ' + hashlib.sha256(nearby.read_bytes()).hexdigest().upper(), result.stdout + result.stderr)
            self.assertEqual(log.read_text().splitlines(), [str(nearby)])
            log.unlink(); viewer_log.unlink()
            result = subprocess.run(automatic + ["-GameExe", str(fixture / "missing.exe")],
                                    env=env, capture_output=True, text=True, timeout=15)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("Game executable not found", result.stderr)
            self.assertFalse(log.exists())
            self.assertFalse(viewer_log.exists())
            # A BAT entry must preserve quoted paths/options and the backend's failure status.
            bat = f'""{mod / "Start-HOTD2.bat"}" -GameExe "{game}" -NoWindow -NoVrWindow -InfiniteAmmo"'
            result = subprocess.run(f'"{os.environ["COMSPEC"]}" /d /s /c {bat}', cwd=root,
                                    env=env, input="", capture_output=True, text=True, timeout=15)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual(log.read_text().splitlines(), [str(game), "--no-window", "--no-vrwindow"])
            self.assertEqual(viewer_log.read_text().splitlines(), ["--no-window", "--no-vrwindow", "--infinite-ammo"])
            nearby.unlink(); detected.unlink(); log.unlink(); viewer_log.unlink()
            bat = f'""{mod / "Start-HOTD2.bat"}""'
            result = subprocess.run(f'"{os.environ["COMSPEC"]}" /d /s /c {bat}', cwd=root,
                                    env=env, input="", capture_output=True, text=True, timeout=15)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("Hod2.exe not found", result.stdout + result.stderr)
            self.assertFalse(log.exists())
            self.assertFalse(viewer_log.exists())
            # A first launch must dispatch PowerShell extraction without Python on PATH.
            (build / "menu-font.rgba").unlink()
            (fixture / "tools").mkdir()
            extractor = fixture / "tools/extract_menu_font.ps1"
            extractor.write_text('param($GameExe,$Output)\n[IO.File]::WriteAllBytes($Output,[byte[]](1,2,3))\n')
            no_python = dict(env, PATH=str(Path(os.environ['SystemRoot']) / 'System32'))
            result = subprocess.run(command, env=no_python, capture_output=True, text=True, timeout=15)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual((build / "menu-font.rgba").read_bytes(), b'\x01\x02\x03')
            (build / "menu-font.rgba").unlink()
            log.unlink()
            viewer_log.unlink()
            extractor.write_text("throw 'Font extraction stopped'\n")
            result = subprocess.run(command, env=no_python, capture_output=True, text=True, timeout=15)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('Font extraction stopped', result.stderr)
            self.assertFalse(log.exists())
            self.assertFalse(viewer_log.exists())
            (build / "menu-font.rgba").touch()
            result = subprocess.run(command + ["-Window"], env=env, capture_output=True,
                                    text=True, timeout=15)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("Choose either -Window or -NoWindow.", result.stderr)
            self.assertFalse(log.exists())
            result = subprocess.run(command + ["-NoGraphicsHooks"], env=env, capture_output=True,
                                    text=True, timeout=15)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("--no-graphics-hooks", log.read_text().splitlines())
            self.assertEqual(viewer_log.read_text().splitlines()[:2], ["--no-window", "--no-vrwindow"])
            log.unlink()
            viewer_log.unlink()
            env["HOTD2_TEST_LAUNCH_EXIT"] = "3"
            result = subprocess.run(command, env=env, capture_output=True, text=True, timeout=15)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("Game injection failed.", result.stderr)
            self.assertFalse(viewer_log.exists())


if __name__ == "__main__":
    unittest.main()
