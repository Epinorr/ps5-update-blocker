#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run production filesystem code against isolated Linux directories.

No PS5, network connection, real /update, or updater is used. Fault hooks
are compiled only into the host CLI, never into the three PS5 payloads.
"""
import fcntl
import json
import os
import pathlib
import stat
import subprocess
import sys
import tempfile
import unittest

BINARY = pathlib.Path(sys.argv.pop(1)).resolve()
NAMES = ("PS5UPDATE.PUP", "PS5UPDATE.PUP.net.temp")
MARKER = ".ps5-update-blocker-v1"
SIGNATURE = b"ps5-update-blocker\nformat=1\npolicy=nonempty-directory\n"
LOCK = ".ps5-update-blocker.lock"


class FilesystemTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="ps5-update-blocker-test-")
        self.base = pathlib.Path(self.temp.name)
        self.root = self.base / "update"
        self.root.mkdir()

    def tearDown(self):
        self.temp.cleanup()

    def run_action(self, action, expected=0, fault=None, root=None):
        env = os.environ.copy()
        env.pop("UB_TEST_FAULT", None)
        if fault:
            env["UB_TEST_FAULT"] = fault
        process = subprocess.run(
            [str(BINARY), "--test-root", str(root or self.root), action],
            text=True, capture_output=True, timeout=10, env=env,
        )
        self.assertEqual(process.returncode, expected,
                         f"{action}: {process.stdout}\n{process.stderr}")
        self.assertNotIn("AddressSanitizer", process.stderr)
        self.assertNotIn("runtime error:", process.stderr)
        return process.stdout

    def owned(self, index):
        path = self.root / NAMES[index]
        path.mkdir()
        (path / MARKER).write_bytes(SIGNATURE)
        return path

    def tree(self):
        result = {}
        for path in self.root.rglob("*"):
            mode = path.lstat().st_mode
            key = str(path.relative_to(self.root))
            if stat.S_ISLNK(mode):
                result[key] = ("symlink", os.readlink(path))
            elif stat.S_ISREG(mode):
                result[key] = ("file", path.read_bytes())
            elif stat.S_ISDIR(mode):
                result[key] = ("directory",)
            else:
                result[key] = ("special", stat.S_IFMT(mode))
        return result

    def test_status_is_read_only(self):
        (self.root / "unrelated.dat").write_bytes(b"keep")
        before = self.tree()
        output = self.run_action("status")
        self.assertIn("NO_OWNED_PLACEHOLDERS", output)
        self.assertEqual(before, self.tree())

    def test_round_trip_and_no_network_side_effects(self):
        (self.root / "other-file").write_bytes(b"keep")
        self.run_action("disable")
        for name in NAMES:
            self.assertTrue((self.root / name).is_dir())
            self.assertEqual((self.root / name / MARKER).read_bytes(), SIGNATURE)
            with self.assertRaises(IsADirectoryError):
                os.open(self.root / name, os.O_WRONLY)
        self.assertIn("PLACEHOLDERS_PRESENT", self.run_action("status"))
        self.run_action("enable")
        for name in NAMES:
            self.assertFalse((self.root / name).exists())
        self.assertEqual((self.root / "other-file").read_bytes(), b"keep")
        self.assertEqual(set(self.tree()), {"other-file", LOCK})

    def test_repeated_disable_never_toggles(self):
        self.run_action("disable")
        before = self.tree()
        self.run_action("disable")
        self.assertEqual(self.tree(), before)

    def test_repeated_enable_is_idempotent(self):
        self.run_action("enable")
        self.run_action("enable")
        self.assertEqual(set(self.tree()), {LOCK})

    def test_new_process_observes_stored_placeholders(self):
        self.run_action("disable")
        output = self.run_action("status")
        self.assertIn("PLACEHOLDERS_PRESENT", output)
        self.assertIn("HARDWARE_PERSISTENCE=UNVERIFIED", output)
        # Separate host processes are not a console reboot test.

    def test_real_pup_preserved_by_both_actions(self):
        pup = self.root / NAMES[0]
        pup.write_bytes(b"real PUP fixture\x00\xff")
        for action in ("disable", "enable"):
            self.run_action(action, 2)
            self.assertEqual(pup.read_bytes(), b"real PUP fixture\x00\xff")
            self.assertFalse((self.root / NAMES[1]).exists())

    def test_second_path_preflight_prevents_first_mutation(self):
        (self.root / NAMES[1]).write_bytes(b"download in progress")
        self.run_action("disable", 2)
        self.assertFalse((self.root / NAMES[0]).exists())

    def test_empty_legacy_directory_is_not_adopted(self):
        path = self.root / NAMES[0]
        path.mkdir()
        self.run_action("disable", 2)
        self.run_action("enable", 2)
        self.assertEqual(list(path.iterdir()), [])

    def test_extra_contents_preserved_even_with_valid_marker(self):
        first = self.owned(0)
        second = self.owned(1)
        (second / "foreign-file").write_bytes(b"not ours")
        self.run_action("enable", 2)
        self.assertEqual((first / MARKER).read_bytes(), SIGNATURE)
        self.assertEqual((second / "foreign-file").read_bytes(), b"not ours")

    def test_marker_mismatch_is_not_deleted(self):
        path = self.owned(0)
        (path / MARKER).write_bytes(b"other software")
        self.run_action("enable", 2)
        self.assertEqual((path / MARKER).read_bytes(), b"other software")

    def test_symlink_placeholder_does_not_touch_target(self):
        target = self.base / "outside"
        target.mkdir()
        (target / MARKER).write_bytes(SIGNATURE)
        (self.root / NAMES[0]).symlink_to(target, target_is_directory=True)
        self.run_action("disable", 2)
        self.run_action("enable", 2)
        self.assertTrue((self.root / NAMES[0]).is_symlink())
        self.assertEqual((target / MARKER).read_bytes(), SIGNATURE)

    def test_symlink_marker_is_not_followed(self):
        target = self.base / "outside-marker"
        target.write_bytes(SIGNATURE)
        path = self.root / NAMES[0]
        path.mkdir()
        (path / MARKER).symlink_to(target)
        self.run_action("enable", 2)
        self.assertEqual(target.read_bytes(), SIGNATURE)

    def test_hard_link_marker_is_not_owned(self):
        path = self.owned(0)
        outside = self.base / "hard-link"
        os.link(path / MARKER, outside)
        self.run_action("enable", 2)
        self.assertEqual(outside.read_bytes(), SIGNATURE)

    def test_fifo_placeholder_does_not_block(self):
        os.mkfifo(self.root / NAMES[0])
        self.run_action("disable", 2)
        self.assertTrue(stat.S_ISFIFO((self.root / NAMES[0]).lstat().st_mode))

    def test_fifo_marker_does_not_block(self):
        path = self.root / NAMES[0]
        path.mkdir()
        os.mkfifo(path / MARKER)
        self.run_action("enable", 2)
        self.assertTrue(stat.S_ISFIFO((path / MARKER).lstat().st_mode))

    def test_partial_owned_state_can_finish_disable(self):
        self.owned(0)
        self.run_action("status", 3)
        self.run_action("disable")
        self.assertIn("PLACEHOLDERS_PRESENT", self.run_action("status"))

    def test_partial_owned_state_can_finish_enable(self):
        self.owned(1)
        self.run_action("enable")
        self.assertFalse((self.root / NAMES[1]).exists())

    def test_second_mkdir_failure_has_no_false_success(self):
        self.run_action("disable", 1, "mkdir-temp")
        self.assertEqual((self.root / NAMES[0] / MARKER).read_bytes(), SIGNATURE)
        self.assertFalse((self.root / NAMES[1]).exists())
        self.run_action("disable")

    def test_marker_write_failure_preserves_ambiguous_directory(self):
        output = self.run_action("disable", 1, "marker-write")
        self.assertNotIn("Placeholders verified now.", output)
        before = self.tree()
        self.run_action("enable", 2)
        self.assertEqual(self.tree(), before)

    def test_marker_fsync_failure_can_be_retried(self):
        output = self.run_action("disable", 1, "marker-sync")
        self.assertNotIn("Placeholders verified now.", output)
        self.run_action("disable")

    def test_existing_markers_are_synced_on_repeat_disable(self):
        self.run_action("disable")
        self.run_action("disable", 1, "marker-sync")
        self.run_action("disable")

    def test_directory_sync_io_failure_is_not_success(self):
        output = self.run_action("disable", 1, "directory-sync")
        self.assertNotIn("Placeholders verified now.", output)
        self.run_action("disable")

    def test_failed_enable_preserves_remaining_marker(self):
        self.run_action("disable")
        self.run_action("enable", 1, "remove-temp")
        self.assertFalse((self.root / NAMES[0]).exists())
        self.assertEqual((self.root / NAMES[1] / MARKER).read_bytes(), SIGNATURE)
        self.run_action("enable")

    def test_enable_first_failure_preserves_both(self):
        self.run_action("disable")
        before = self.tree()
        self.run_action("enable", 1, "remove-final")
        self.assertEqual(self.tree(), before)

    def test_lock_content_is_preserved(self):
        (self.root / LOCK).write_bytes(b"foreign")
        self.run_action("disable", 2)
        self.assertEqual((self.root / LOCK).read_bytes(), b"foreign")
        self.assertFalse((self.root / NAMES[0]).exists())

    def test_symlink_lock_cannot_truncate_target(self):
        outside = self.base / "lock-target"
        outside.write_bytes(b"keep")
        (self.root / LOCK).symlink_to(outside)
        self.run_action("disable", 1)
        self.assertEqual(outside.read_bytes(), b"keep")

    def test_concurrent_mutation_reports_busy(self):
        with (self.root / LOCK).open("wb") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
            self.run_action("disable", 5)
        self.assertFalse((self.root / NAMES[0]).exists())

    def test_root_symlink_is_rejected(self):
        link = self.base / "root-link"
        link.symlink_to(self.root, target_is_directory=True)
        self.run_action("disable", 4, root=link)
        self.assertEqual(self.tree(), {})


if __name__ == "__main__":
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(FilesystemTests)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    report = {"tests_run": result.testsRun, "failures": len(result.failures),
              "errors": len(result.errors), "host_only": True,
              "ps5_hardware_tested": False, "reboot_persistence_verified": False}
    target = pathlib.Path(__file__).resolve().parents[1] / "build/test-results.json"
    target.parent.mkdir(exist_ok=True)
    target.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    sys.exit(0 if result.wasSuccessful() else 1)
