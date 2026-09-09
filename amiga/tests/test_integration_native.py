#!/usr/bin/env python3
"""Deterministic end-to-end milestones for the native 68000 runner."""

from __future__ import annotations

import re
import subprocess
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
NATIVE = ROOT / "build" / "battle_squadron_native"
REPORT = re.compile(
    r"native: frames=(?P<frames>\d+) pc=\$(?P<pc>[0-9a-f]+) "
    r"files=(?P<files>\d+) blits=(?P<blits>\d+)"
)
AUDIO_REPORT = re.compile(
    r"audio=(?P<audio>\d+) energy=(?P<energy>\d+) ticks=(?P<ticks>\d+)"
)


class NativeIntegrationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        if not NATIVE.is_file():
            raise RuntimeError(f"native runner has not been built: {NATIVE}")

    def run_milestone(self, frames: int) -> dict[str, int | str]:
        completed = subprocess.run(
            [str(NATIVE), "--frames", str(frames)],
            cwd=ROOT,
            check=True,
            capture_output=True,
            text=True,
        )
        match = REPORT.search(completed.stderr)
        self.assertIsNotNone(match, completed.stderr)
        assert match is not None
        result: dict[str, int | str] = {
            "frames": int(match["frames"]),
            "pc": match["pc"],
            "files": int(match["files"]),
            "blits": int(match["blits"]),
        }
        return result

    def test_loader_reaches_original_bond_depacker(self) -> None:
        self.assertEqual(
            self.run_milestone(10),
            {"frames": 10, "pc": "00ab80", "files": 1, "blits": 0},
        )

    def test_bootstrap_loads_first_seven_files(self) -> None:
        self.assertEqual(
            self.run_milestone(500),
            {"frames": 500, "pc": "001cb8", "files": 7, "blits": 0},
        )

    def test_deep_game_loop_is_deterministic(self) -> None:
        self.assertEqual(
            self.run_milestone(50_000),
            {
                "frames": 50_000,
                "pc": "000c90",
                "files": 20,
                "blits": 2_054_047,
            },
        )

    def test_autofire_crosses_game_load_with_music_alive(self) -> None:
        completed = subprocess.run(
            [
                str(NATIVE),
                "--frames",
                "12000",
                "--autofire",
                "--mix-audio",
            ],
            cwd=ROOT,
            check=True,
            capture_output=True,
            text=True,
        )
        milestone = REPORT.search(completed.stderr)
        sound = AUDIO_REPORT.search(completed.stderr)
        self.assertIsNotNone(milestone, completed.stderr)
        self.assertIsNotNone(sound, completed.stderr)
        assert milestone is not None and sound is not None
        self.assertGreaterEqual(int(milestone["files"]), 13)
        self.assertGreater(int(milestone["blits"]), 500_000)
        self.assertGreater(int(sound["audio"]), 100_000)
        self.assertGreater(int(sound["energy"]), 1_000_000_000)
        self.assertGreater(int(sound["ticks"]), 10_000)

    def test_failed_milestone_expectation_returns_nonzero(self) -> None:
        completed = subprocess.run(
            [str(NATIVE), "--frames", "10", "--expect-files", "2"],
            cwd=ROOT,
            check=False,
            capture_output=True,
            text=True,
        )
        self.assertEqual(completed.returncode, 1)
        self.assertIn("expected at least 2 file loads, got 1", completed.stderr)


if __name__ == "__main__":
    unittest.main()
