"""Launcher option validation without starting a game."""
from pathlib import Path
import subprocess
import unittest


class LaunchOptionsTest(unittest.TestCase):
    def test_splash_flags_reach_game_validation(self):
        launcher = Path(__file__).resolve().parents[1] / "build/HOTD2Launch.exe"
        for flags in (("--skip-splash",), ("--show-splash",),
                      ("--wait", "--skip-splash"),
                      ("--native-window", "--show-splash")):
            with self.subTest(flags=flags):
                result = subprocess.run(
                    [str(launcher), "__missing_hotd2__.exe", *flags],
                    capture_output=True, text=True, timeout=5)
                self.assertEqual(result.returncode, 2)
                self.assertIn("Game executable missing.", result.stderr)


if __name__ == "__main__":
    unittest.main()
