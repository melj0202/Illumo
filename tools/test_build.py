"""Regression tests for the standard-library build front end."""

import contextlib
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import build


class BuildTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.profiles = self.root / "profiles.json"

    def invoke(self, *args):
        output, errors = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(output), contextlib.redirect_stderr(errors):
            code = build.main(list(args))
        return code, output.getvalue(), errors.getvalue()

    def write_profiles(self, settings):
        self.profiles.write_text(json.dumps({"version": 1, "profiles": {"custom": settings}}))

    def test_defaults_unchanged(self):
        args = build.create_parser().parse_args(["build"])
        self.assertEqual(args.config, "Release")
        self.assertEqual(args.build_dir, build.DEFAULT_BUILD_DIRECTORY)
        self.assertFalse(args.no_tests or args.no_docs or args.no_tidy or args.tracy)

    def test_profile_cli_overrides_and_app_passthrough(self):
        self.write_profiles({"config": "Debug", "no_docs": True, "tracy": True,
                             "parallel": 0, "cmake_arg": ["-DEXAMPLE=one"]})
        code, output, errors = self.invoke("run", "--profile", "custom", "--profiles-file", str(self.profiles),
                                          "--config", "Release", "--docs", "--no-tracy", "--parallel", "2",
                                          "--cmake-arg=-DEXAMPLE=two", "--dry-run", "--", "--profile", "game")
        self.assertEqual(code, 0, errors)
        self.assertIn("-DCMAKE_BUILD_TYPE=Release", output)
        self.assertIn("-DILLUMO_BUILD_DOCUMENTATION=ON", output)
        self.assertIn("-DILLUMO_ENABLE_TRACY=OFF", output)
        self.assertIn("--parallel 2", output)
        self.assertLess(output.index("-DEXAMPLE=one"), output.index("-DEXAMPLE=two"))
        self.assertIn("--profile game", output)

    def test_unknown_profile_does_not_launch(self):
        with patch.object(build.subprocess, "run") as run:
            code, _, errors = self.invoke("build", "--profile", "absent")
        self.assertEqual(code, 1)
        self.assertIn("Unknown profile", errors)
        run.assert_not_called()

    def test_invalid_profile_schema(self):
        for settings in ({"fresh": True}, {"parallel": True}, {"parallel": -1},
                         {"no_docs": "false"}, {"config": "Fast"}, {"cmake_arg": "-DX=1"},
                         {"build_dir": ""}, []):
            with self.subTest(settings=settings):
                self.write_profiles(settings)
                with self.assertRaises(build.BuildError):
                    build.load_profiles(self.profiles)

    def test_corrupt_file_preserved_on_save(self):
        self.profiles.write_text("{broken")
        code, _, errors = self.invoke("profile-save", "custom", "--profiles-file", str(self.profiles))
        self.assertEqual(code, 1)
        self.assertIn("Could not read", errors)
        self.assertEqual(self.profiles.read_text(), "{broken")

    def test_missing_explicit_profile_file(self):
        with self.assertRaises(build.BuildError):
            build.load_profiles(self.profiles)

    def test_save_round_trip_and_preserve_other_profiles(self):
        for name in ("first", "second"):
            code, _, errors = self.invoke("profile-save", name, "--profiles-file", str(self.profiles),
                                          "--config", "Debug", "--no-tests", "--fresh", "--clean")
            self.assertEqual(code, 0, errors)
        saved = build.load_profiles(self.profiles, include_builtin=False)
        self.assertEqual(set(saved), {"first", "second"})
        self.assertEqual(saved["first"]["config"], "Debug")
        self.assertTrue(saved["first"]["no_tests"])
        self.assertNotIn("fresh", saved["first"])
        self.assertNotIn("clean", saved["first"])

    def test_save_dry_run_does_not_write(self):
        code, _, errors = self.invoke("profile-save", "preview", "--profiles-file", str(self.profiles), "--dry-run")
        self.assertEqual(code, 0, errors)
        self.assertFalse(self.profiles.exists())

    def test_builtin_play_and_sanitizer_profiles(self):
        # "dev" is optimized with debug tools; "debug-noasan" keeps Debug
        # code without AddressSanitizer (fast WASM guest bounds traps).
        dev = build.profile_arguments(build.BUILTIN_PROFILES["dev"])
        self.assertIn("--config=RelWithDebInfo", dev)
        no_asan = build.profile_arguments(build.BUILTIN_PROFILES["debug-noasan"])
        self.assertIn("--config=Debug", no_asan)
        self.assertIn("--cmake-arg=-DILLUMO_ENABLE_ASAN=OFF", no_asan)
        self.assertNotEqual(build.BUILTIN_PROFILES["debug-noasan"]["build_dir"],
                            build.BUILTIN_PROFILES["debug"]["build_dir"])

    def test_save_from_builtin_to_new_shared_file(self):
        code, _, errors = self.invoke("profile-save", "custom", "--profile", "debug",
                                      "--profiles-file", str(self.profiles))
        self.assertEqual(code, 0, errors)
        self.assertEqual(build.load_profiles(self.profiles)["custom"]["config"], "Debug")

    def test_auto_parallel_profile_before_positional_name(self):
        self.write_profiles({"parallel": 0})
        code, _, errors = self.invoke("profile-save", "copy", "--profile", "custom",
                                      "--profiles-file", str(self.profiles))
        self.assertEqual(code, 0, errors)
        self.assertEqual(build.load_profiles(self.profiles)["copy"]["parallel"], 0)

    def test_failed_atomic_save_preserves_original(self):
        self.write_profiles({"config": "Debug"})
        original = self.profiles.read_bytes()
        with patch.object(build.os, "replace", side_effect=OSError("locked")):
            code, _, errors = self.invoke("profile-save", "custom", "--profiles-file", str(self.profiles))
        self.assertEqual(code, 1)
        self.assertIn("locked", errors)
        self.assertEqual(self.profiles.read_bytes(), original)
        self.assertEqual(list(self.root.glob("*.tmp")), [])

    def cache_arguments(self, text, *extra):
        (self.root / "CMakeCache.txt").write_text(text)
        return build.create_parser().parse_args(["configure", "--build-dir", str(self.root), *extra])

    def test_foreign_cache_rejected_even_with_fresh(self):
        args = self.cache_arguments(f"CMAKE_HOME_DIRECTORY:INTERNAL={self.root}\n", "--fresh")
        with self.assertRaisesRegex(build.BuildError, "belongs to source"):
            build.validate_build_settings(args)

    def test_generator_conflict_and_explicit_fresh(self):
        text = f"CMAKE_HOME_DIRECTORY:INTERNAL={build.SOURCE_DIRECTORY}\nCMAKE_GENERATOR:INTERNAL=Ninja\n"
        args = self.cache_arguments(text, "--generator", "Visual Studio 17 2022")
        with self.assertRaisesRegex(build.BuildError, "conflicts"):
            build.validate_build_settings(args)
        args.fresh = True
        build.validate_build_settings(args)

    def test_incomplete_cache(self):
        args = self.cache_arguments("CMAKE_GENERATOR:INTERNAL=Ninja\n")
        with self.assertRaisesRegex(build.BuildError, "incomplete cache"):
            build.validate_build_settings(args)

    def test_doctor_json_and_no_build_side_effects(self):
        with patch.object(build.shutil, "which", return_value=None), patch.object(build.subprocess, "run") as run:
            code, output, errors = self.invoke("doctor", "--json", "--no-wasm", "--build-dir", str(self.root / "new"))
        self.assertEqual(code, 1)
        self.assertFalse(json.loads(output)["ok"])
        self.assertIn("No configuration", errors)
        self.assertFalse((self.root / "new").exists())
        run.assert_not_called()

    def test_doctor_old_cmake_and_timeout(self):
        with patch.object(build.shutil, "which", return_value="tool"), patch.object(
            build.subprocess, "run", side_effect=[subprocess.CompletedProcess([], 0, "cmake version 3.19", ""),
                                                 subprocess.TimeoutExpired("ctest", 10)]):
            code, output, _ = self.invoke("doctor", "--json", "--no-tidy", "--no-docs", "--no-wasm", "--build-dir", str(self.root))
        self.assertEqual(code, 1)
        errors = [item for item in json.loads(output)["checks"] if item["status"] == "error"]
        self.assertEqual([item["name"] for item in errors], ["cmake", "ctest"])

    def test_doctor_dry_run_does_not_launch_probes(self):
        with patch.object(build.shutil, "which", return_value="tool"), patch.object(build.subprocess, "run") as run:
            code, output, errors = self.invoke("doctor", "--json", "--dry-run", "--no-wasm", "--build-dir", str(self.root))
        self.assertEqual(code, 0, errors)
        self.assertIn("Version probe skipped", output)
        run.assert_not_called()

    def test_doctor_honors_cmake_feature_overrides(self):
        def which(name):
            return "cmake" if name == "cmake" else None
        with patch.object(build.shutil, "which", side_effect=which), patch.object(
            build.subprocess, "run", return_value=subprocess.CompletedProcess([], 0, "cmake version 4.3", "")):
            code, output, errors = self.invoke("doctor", "--json", "--no-tests", "--no-docs", "--no-wasm",
                                               "--build-dir", str(self.root),
                                               "--cmake-arg=-DILLUMO_ENABLE_CLANG_TIDY:BOOL=OFF")
        self.assertEqual(code, 0, errors)
        self.assertTrue(json.loads(output)["ok"])

    def test_architecture_conflict(self):
        args = self.cache_arguments(
            f"CMAKE_HOME_DIRECTORY:INTERNAL={build.SOURCE_DIRECTORY}\nCMAKE_GENERATOR_PLATFORM:INTERNAL=x64\n",
            "--architecture", "ARM64")
        with self.assertRaisesRegex(build.BuildError, "architecture.*conflicts"):
            build.validate_build_settings(args)

    def test_doctor_explicit_tidy_executable_and_enable_override(self):
        with patch.object(build.shutil, "which", side_effect=lambda name: name) as which, patch.object(
            build.subprocess, "run", return_value=subprocess.CompletedProcess([], 0, "cmake version 4.3", "")):
            code, _, errors = self.invoke("doctor", "--json", "--no-tests", "--no-docs", "--no-tidy", "--no-wasm",
                                          "--build-dir", str(self.root),
                                          "--cmake-arg=-DILLUMO_ENABLE_CLANG_TIDY=ON",
                                          "--cmake-arg=-DILLUMO_CLANG_TIDY_EXECUTABLE=custom-tidy")
        self.assertEqual(code, 0, errors)
        which.assert_any_call("custom-tidy")

    def test_command_failure_context_and_exit_code(self):
        with patch.object(build.subprocess, "run", return_value=subprocess.CompletedProcess([], 7)):
            with self.assertRaises(build.BuildError) as failure:
                build.CommandRunner(False).run(["fake", "build"], self.root)
        self.assertEqual(failure.exception.exit_code, 7)
        self.assertIn(str(self.root), str(failure.exception))
        self.assertIn("subsequent steps were skipped", str(failure.exception))

    def test_command_launch_error_is_user_facing(self):
        with patch.object(build.subprocess, "run", side_effect=FileNotFoundError("missing")):
            with self.assertRaisesRegex(build.BuildError, "Could not start"):
                build.CommandRunner(False).run(["missing"], self.root)


class DashboardProgressTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.log_path = Path(self.temporary.name) / "action.log"
        self.progress = build.DashboardProgress("Build application", "Debug | TestApp", self.log_path, started=10.0)

    def test_phases_and_tool_progress_reset_between_commands(self):
        self.progress.consume('> "cmake" -S source -B build', 11)
        self.assertEqual(self.progress.phase, "Configuring")
        self.progress.consume('> "cmake" --build build', 12)
        self.progress.consume("[3/9] Building CXX object file", 13)
        self.assertEqual((self.progress.completed, self.progress.total), (3, 9))
        self.progress.consume("2/12 Test #2: example ... Passed", 14)
        self.assertEqual(self.progress.phase, "Testing")
        self.progress.consume("1/12 Test #1: slower ... Passed", 15)
        self.assertEqual(self.progress.completed, 2)
        self.progress.consume('> "cmake" --build build --target Next', 16)
        self.assertEqual(self.progress.phase, "Building")
        self.assertEqual(self.progress.total, 0)
        self.assertEqual(self.progress.phase_started, 16)

    def test_percentages_do_not_imply_action_success(self):
        self.progress.consume("[100%] Built target First", 12)
        rendered = build.render_dashboard_progress(self.progress, 96, 30, now=14, ansi=False)
        self.assertIn("RUNNING", rendered)
        self.assertIn("100% (tool-reported)", rendered)
        self.assertNotIn("SUCCEEDED", rendered)

    def test_indeterminate_progress_and_quiet_elapsed(self):
        first = build.render_dashboard_progress(self.progress, 96, 30, now=11, ansi=False)
        later = build.render_dashboard_progress(self.progress, 96, 30, now=17, ansi=False)
        self.assertIn("Elapsed 1.0s", first)
        self.assertIn("Elapsed 7.0s", later)
        self.assertIn("has not reported a total", later)

    def test_bounded_tail_and_control_sequence_sanitizing(self):
        for index in range(300):
            self.progress.consume(f"line {index}", 11)
        self.progress.consume("\x1b[2J\x1b]0;hostile-title\x07warning C4100: parameter", 12)
        self.progress.consume("error: failed", 12)
        self.assertEqual(len(self.progress.lines), 200)
        self.assertEqual((self.progress.warnings, self.progress.errors), (1, 1))
        for columns, rows in ((96, 30), (40, 15), (10, 5), (2, 2)):
            rendered = build.render_dashboard_progress(self.progress, columns, rows, now=14, ansi=False)
            self.assertNotIn("\x1b", rendered)
            self.assertLessEqual(len(rendered.splitlines()), max(1, rows - 2))
            self.assertTrue(all(len(line) <= max(1, columns - 1) for line in rendered.splitlines()))

    def test_real_stream_success_and_complete_log(self):
        code = "import sys; print('> cmake --build tree'); print('[1/2] first'); print('[2/2] second'); sys.stdout.write('final without newline')"
        with patch.object(build, "paint_dashboard_progress"):
            build.run_dashboard_progress([sys.executable, "-u", "-c", code], self.progress)
        self.assertEqual(self.progress.returncode, 0)
        self.assertEqual(self.progress.lines[-1], "final without newline")
        self.assertTrue(self.log_path.read_text().endswith("final without newline"))
        self.assertIsNotNone(self.progress.finished)

    def test_cmake_diagnostics_and_test_names(self):
        for line in ("CMake Warning at file.cmake:12 (message):", "CMake Warning (dev) in CMakeLists.txt:"):
            self.progress.consume(line, 11)
        self.progress.consume("CMake Error at file.cmake:20 (message):", 11)
        self.progress.consume("105/335 Test #105: Illumo.MeshLoader.ErrorHandling ... Passed", 11)
        self.assertEqual((self.progress.warnings, self.progress.errors), (2, 1))

    def test_real_failure_preserves_exit_status_and_stderr(self):
        code = "import sys; print('error: compiler failed', file=sys.stderr); sys.exit(7)"
        with patch.object(build, "paint_dashboard_progress"):
            build.run_dashboard_progress([sys.executable, "-u", "-c", code], self.progress)
        self.assertEqual(self.progress.returncode, 7)
        self.assertEqual(self.progress.errors, 1)
        self.assertIn("error: compiler failed", self.log_path.read_text())
        self.assertIn("FAILED", build.render_dashboard_progress(self.progress, 96, 30, ansi=False))

    def test_launch_failure_is_reported(self):
        with patch.object(build.subprocess, "Popen", side_effect=FileNotFoundError("missing tool")), \
             patch.object(build, "paint_dashboard_progress"):
            build.run_dashboard_progress(["missing"], self.progress)
        self.assertEqual(self.progress.returncode, 1)
        self.assertIn("missing tool", self.progress.lines[-1])

    @unittest.skipUnless(sys.platform == "win32", "Windows process ownership")
    def test_job_cleans_descendants_after_launcher_exits(self):
        active_counts = []
        original = build.WindowsProgressJob.terminate
        def record_active(job):
            accounting = job.accounting_type()
            self.assertTrue(job.api.QueryInformationJobObject(
                job.handle, 1, job.ctypes.byref(accounting), job.ctypes.sizeof(accounting), None,
            ))
            active_counts.append(accounting.active)
            original(job)
            self.assertTrue(job.api.QueryInformationJobObject(
                job.handle, 1, job.ctypes.byref(accounting), job.ctypes.sizeof(accounting), None,
            ))
            self.assertEqual(accounting.active, 0)
        code = "import subprocess,sys; subprocess.Popen([sys.executable,'-c','import time; time.sleep(30)']); print('launcher finished')"
        with patch.object(build.WindowsProgressJob, "terminate", autospec=True, side_effect=record_active), \
             patch.object(build, "paint_dashboard_progress"):
            build.run_dashboard_progress([sys.executable, "-u", "-c", code], self.progress)
        self.assertEqual(self.progress.returncode, 0)
        self.assertGreater(max(active_counts), 0)

    @unittest.skipUnless(sys.platform == "win32", "Windows startup gate")
    def test_failed_job_assignment_never_launches_requested_command(self):
        marker = Path(self.temporary.name) / "must-not-exist"
        code = f"from pathlib import Path; Path({str(marker)!r}).write_text('started')"
        with patch.object(build.WindowsProgressJob, "assign", side_effect=OSError("assignment denied")), \
             patch.object(build, "paint_dashboard_progress"):
            build.run_dashboard_progress([sys.executable, "-c", code], self.progress)
        self.assertEqual(self.progress.returncode, 1)
        self.assertFalse(marker.exists())
        self.assertIn("assignment denied", self.progress.lines[-1])

    def test_progress_action_restores_terminal_and_records_duration(self):
        from unittest.mock import Mock
        state = build.DashboardState(applications=("TestApp",))
        state.selected = next(i for i, item in enumerate(build.DASHBOARD_ITEMS) if item[2] == "build")
        terminal = Mock()
        def finish(command, progress):
            progress.returncode = 7
            progress.finished = progress.started + 2
        with patch.object(build, "REPOSITORY_ROOT", Path(self.temporary.name)), \
             patch.object(build, "run_dashboard_progress", side_effect=finish), \
             patch.object(build, "read_action_choice", return_value="") as choice, \
             patch.object(build, "dashboard_follow_up") as follow_up, \
             contextlib.redirect_stdout(io.StringIO()) as output:
            build.execute_dashboard_action(state, "build", terminal)
        terminal.leave.assert_called_once()
        terminal.enter.assert_called_once()
        choice.assert_called_once()
        # The follow-up (d/l) opens only after the console is back in raw mode.
        self.assertEqual(follow_up.call_args.args[0], "")
        self.assertEqual(state.status_kind, "failure")
        self.assertIn("Build everything failed (2.0s)", state.status)
        self.assertIn(build.ANSI_LEAVE_SCREEN, output.getvalue())

    @unittest.skipUnless(sys.platform == "win32" and sys.stdin.isatty(), "requires isolated Windows console")
    def test_native_console_progress_cancellation(self):
        calls = 0
        def cancel_once(progress):
            nonlocal calls
            calls += 1
            if calls == 1:
                raise KeyboardInterrupt
        with patch.object(build, "paint_dashboard_progress", side_effect=cancel_once):
            build.run_dashboard_progress(
                [sys.executable, "-u", "-c", "import time; time.sleep(20)"], self.progress,
            )
        self.assertTrue(self.progress.cancelled)
        self.assertEqual(self.progress.returncode, 130)


class DashboardMouseTests(unittest.TestCase):
    def setUp(self):
        self.state = build.DashboardState(applications=("TestApp",))
        self.regions = []
        self.rendered = build.render_dashboard(
            self.state, 96, ansi=False, hit_regions=self.regions, mouse_enabled=True,
        )

    def test_hit_regions_match_rendered_rows(self):
        lines = self.rendered.splitlines()
        self.assertEqual(len(self.regions), len(build.DASHBOARD_ITEMS))
        for region in self.regions:
            self.assertIn(build.DASHBOARD_ITEMS[region.index][1], lines[region.row - 1])
            self.assertEqual(
                build.dashboard_visible_width(lines[region.row - 1]), region.right
            )
            if region.previous_x is not None:
                self.assertIn(lines[region.row - 1][region.previous_x - 1], "<‹")

    def test_rows_fit_narrow_and_wide_terminals(self):
        for columns in (40, 56, 72, 80, 96, 120):
            rendered = build.render_dashboard(self.state, columns, ansi=False)
            for line in rendered.splitlines():
                self.assertLessEqual(
                    build.dashboard_visible_width(line),
                    max(20, columns - 1),
                    msg=repr(line),
                )
            if columns < 60:
                self.assertNotIn("applications, tests, and optional PDFs", rendered)

    def test_tui_newlines_reset_column(self):
        self.assertEqual(build.tui_newlines("a\nb"), "a\r\nb")
        self.assertEqual(build.tui_newlines("a\r\nb"), "a\r\nb")

    def test_posix_escape_sequences_do_not_quit_on_arrows(self):
        for sequence, expected in (
            ("[A", "up"),
            ("[B", "down"),
            ("[C", "right"),
            ("[D", "left"),
            ("OA", "up"),
            ("OB", "down"),
            ("OC", "right"),
            ("OD", "left"),
            ("[1;5A", "up"),
            ("", "quit"),
            ("[I", "unknown"),
            ("[15~", "unknown"),
        ):
            self.assertEqual(
                build.posix_escape_to_key(sequence, False), expected, sequence
            )
        self.assertEqual(build.posix_escape_to_key("", True), "escape")
        self.assertEqual(build.posix_escape_to_key("[I", True), "escape")

    def test_visible_width_ignores_ansi_and_counts_wide_glyphs(self):
        self.assertEqual(build.dashboard_visible_width("abc"), 3)
        self.assertEqual(
            build.dashboard_visible_width(f"{build.ANSI_REVERSE}abc{build.ANSI_RESET}"),
            3,
        )
        self.assertEqual(build.dashboard_visible_width("字"), 2)

    def test_setting_clicks_and_backward_controls(self):
        region = self.regions[0]
        for x, kind, expected in ((5, "left", "enter"), (region.previous_x, "left", "left"),
                                   (5, "right", "left")):
            self.state.selected = 8
            result = build.dashboard_mouse_key(
                self.state, build.DashboardMouseEvent(x, region.row, kind), self.regions,
            )
            self.assertEqual(result, expected)
            self.assertEqual(self.state.selected, 0)

    def test_action_left_click_only(self):
        region = next(r for r in self.regions if build.DASHBOARD_ITEMS[r.index][2] == "stats")
        result = build.dashboard_mouse_key(
            self.state, build.DashboardMouseEvent(10, region.row, "left"), self.regions,
        )
        self.assertEqual(result, "enter")
        self.assertEqual(build.DASHBOARD_ITEMS[self.state.selected][2], "stats")
        self.assertEqual(build.dashboard_mouse_key(
            self.state, build.DashboardMouseEvent(10, region.row, "right"), self.regions,
        ), "unknown")

    def test_borders_headers_and_empty_regions_ignore_clicks(self):
        right = self.regions[0].right  # The right border's column.
        self.assertEqual(self.regions[0].row, 6)
        for x, y in ((1, 6), (right, 6), (4, 1), (4, 5), (right + 1, 6)):
            self.assertEqual(build.dashboard_mouse_key(
                self.state, build.DashboardMouseEvent(x, y, "left"), self.regions,
            ), "unknown")
        self.assertEqual(build.dashboard_mouse_key(
            self.state, build.DashboardMouseEvent(5, 6, "left"), [],
        ), "unknown")

    def test_button_transitions_ignore_release_drag_double_click(self):
        self.assertEqual(build.dashboard_mouse_transition(0, 1, 0), ("hover", 0))
        self.assertEqual(build.dashboard_mouse_transition(1, 0, 0), ("left", 1))
        for buttons, flags, previous in ((0, 0, 1), (1, 1, 1), (1, 0, 1), (1, 2, 0)):
            self.assertIsNone(build.dashboard_mouse_transition(buttons, flags, previous)[0])
        self.assertEqual(build.dashboard_mouse_transition(2, 0, 0), ("right", 2))

    def test_hover_selects_without_activation_or_setting_changes(self):
        for region in self.regions:
            for x in (5, region.previous_x or 5):
                result = build.dashboard_mouse_key(
                    self.state, build.DashboardMouseEvent(x, region.row, "hover"), self.regions,
                )
                self.assertEqual(result, "unknown")
                self.assertEqual(self.state.selected, region.index)
                self.assertEqual(self.state.configuration_index, 0)
                self.assertTrue(self.state.testing_enabled)

    def test_hover_outside_menu_preserves_selection(self):
        self.state.selected = 6
        right = self.regions[0].right
        for x, y in ((1, 6), (right, 6), (5, 5), (5, 1), (100, 100)):
            result = build.dashboard_mouse_key(
                self.state, build.DashboardMouseEvent(x, y, "hover"), self.regions,
            )
            self.assertEqual(result, "unknown")
            self.assertEqual(self.state.selected, 6)

    def test_hover_redraws_only_when_highlight_changes(self):
        from unittest.mock import Mock
        terminal = Mock()
        terminal.windows_input = object()
        row = self.regions[1].row
        terminal.read_event.side_effect = [
            build.DashboardMouseEvent(5, row, "hover"),
            build.DashboardMouseEvent(6, row, "hover"), "quit",
        ]
        with patch.object(build, "DashboardTerminal", return_value=terminal), \
             patch.object(build, "dashboard_terminal_size", return_value=build.os.terminal_size((96, 30))), \
             patch.object(build.sys.stdin, "isatty", return_value=True), \
             patch.object(build, "execute_dashboard_action") as execute, \
             contextlib.redirect_stdout(io.StringIO()) as output:
            with patch.object(build.sys.stdout, "isatty", return_value=True):
                self.assertEqual(build.run_dashboard(), 0)
        self.assertEqual(output.getvalue().count(build.ANSI_CLEAR), 2)
        execute.assert_not_called()

    def test_wheel_decoding_and_navigation_does_not_activate(self):
        self.assertEqual(build.dashboard_mouse_transition(120 << 16, 4, 0)[0], "wheel_up")
        self.assertEqual(build.dashboard_mouse_transition(0xFF88 << 16, 4, 0)[0], "wheel_down")
        for kind, expected in (("wheel_up", "up"), ("wheel_down", "down")):
            self.assertEqual(build.dashboard_mouse_key(
                self.state, build.DashboardMouseEvent(10, self.regions[6].row, kind), self.regions,
            ), expected)
            self.assertEqual(self.state.selected, 0)

    @unittest.skipUnless(sys.platform == "win32", "Windows console ABI")
    def test_native_record_layout_modes_and_translation(self):
        import ctypes
        from unittest.mock import Mock

        api = Mock()
        original_mode = 0x0277
        def get_mode(handle, output):
            output._obj.value = original_mode
            return 1
        api.GetConsoleMode.side_effect = get_mode
        api.GetStdHandle.side_effect = [10, 11]
        with patch.object(ctypes, "WinDLL", return_value=api):
            reader = build.WindowsDashboardInput()
        self.assertEqual(ctypes.sizeof(reader.record_type), 20)
        mode = api.SetConsoleMode.call_args.args[1]
        self.assertEqual(mode & 0x0098, 0x0098)
        self.assertEqual(mode & 0x0247, 0)

        def screen_info(handle, output):
            output._obj.window.left = 2
            output._obj.window.top = 10
            return 1
        api.GetConsoleScreenBufferInfo.side_effect = screen_info
        def read_mouse(handle, output, size, count):
            count._obj.value = 1
            output._obj.kind = 2
            mouse = output._obj.event.mouse
            mouse.buttons = 1
            mouse.flags = 0
            mouse.position.x = 7
            mouse.position.y = 15
            return 1
        api.ReadConsoleInputW.side_effect = read_mouse
        self.assertEqual(reader.read_event(), build.DashboardMouseEvent(6, 6, "left"))
        reader.close()
        api.SetConsoleMode.assert_called_with(10, original_mode)

    def test_terminal_restored_on_input_error(self):
        from unittest.mock import Mock
        terminal = Mock()
        terminal.windows_input = object()
        terminal.read_event.side_effect = build.BuildError("read failed")
        with patch.object(build, "DashboardTerminal", return_value=terminal), \
             patch.object(build.sys.stdin, "isatty", return_value=True), \
             patch.object(build.sys.stdout, "isatty", return_value=True), \
             contextlib.redirect_stdout(io.StringIO()):
            # redirect_stdout replaces isatty; patch the replacement too.
            with patch.object(build.sys.stdout, "isatty", return_value=True):
                with self.assertRaises(build.BuildError):
                    build.run_dashboard()
        terminal.leave.assert_called_once()

    def test_visual_cleanup_even_when_mode_restore_fails(self):
        from unittest.mock import Mock
        terminal = build.DashboardTerminal()
        terminal.windows_input = Mock()
        terminal.windows_input.close.side_effect = build.BuildError("restore failed")
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            with self.assertRaisesRegex(build.BuildError, "restore failed"):
                terminal.leave()
        self.assertIn(build.ANSI_SHOW_CURSOR + build.ANSI_LEAVE_SCREEN, output.getvalue())
        self.assertIsNone(terminal.windows_input)

    def test_menu_input_error_returns_failure_without_traceback(self):
        with patch.object(build, "run_dashboard", side_effect=build.BuildError("read failed")), \
             contextlib.redirect_stderr(io.StringIO()) as errors:
            self.assertEqual(build.main(["menu"]), 1)
        self.assertEqual(errors.getvalue(), "error: read failed\n")

    @unittest.skipUnless(sys.platform == "win32" and sys.stdin.isatty(), "requires isolated Windows console")
    def test_native_console_mouse_and_keyboard_smoke(self):
        from ctypes import wintypes
        reader = build.WindowsDashboardInput()
        ctypes = reader.ctypes
        try:
            info = reader.info_type()
            self.assertTrue(reader.api.GetConsoleScreenBufferInfo(reader.output_handle, ctypes.byref(info)))
            write = reader.api.WriteConsoleInputW
            write.argtypes = [wintypes.HANDLE, ctypes.POINTER(reader.record_type),
                              wintypes.DWORD, ctypes.POINTER(wintypes.DWORD)]
            write.restype = wintypes.BOOL
            record = reader.record_type()
            record.kind = 2
            record.event.mouse.position.x = info.window.left + 5
            record.event.mouse.position.y = info.window.top + 5
            record.event.mouse.buttons = 1
            written = wintypes.DWORD()
            self.assertTrue(write(reader.handle, ctypes.byref(record), 1, ctypes.byref(written)))
            self.assertEqual(written.value, 1)
            event = reader.read_event()
            while event == "resize":
                event = reader.read_event()
            self.assertEqual(event, build.DashboardMouseEvent(6, 6, "left"))
            record.event.mouse.buttons = 0
            record.event.mouse.flags = 1
            record.event.mouse.position.y += 1
            self.assertTrue(write(reader.handle, ctypes.byref(record), 1, ctypes.byref(written)))
            self.assertEqual(reader.read_event(), build.DashboardMouseEvent(6, 7, "hover"))
            record = reader.record_type()
            record.kind = 1
            record.event.key.down = True
            record.event.key.key = ord("Q")
            record.event.key.character = "q"
            self.assertTrue(write(reader.handle, ctypes.byref(record), 1, ctypes.byref(written)))
            self.assertEqual(reader.read_event(), "quit")
            record.event.key.repeat = 3
            self.assertTrue(write(reader.handle, ctypes.byref(record), 1, ctypes.byref(written)))
            self.assertEqual(reader.read_event(text_mode=True), build.DashboardTextEvent("qqq"))
            record.event.key.repeat = 1
            record.event.key.key = 0x08
            record.event.key.character = "\x08"
            self.assertTrue(write(reader.handle, ctypes.byref(record), 1, ctypes.byref(written)))
            self.assertEqual(reader.read_event(text_mode=True), "backspace")
            record.event.key.key = 0x1B
            record.event.key.character = "\x1b"
            self.assertTrue(write(reader.handle, ctypes.byref(record), 1, ctypes.byref(written)))
            self.assertEqual(reader.read_event(text_mode=True), "escape")
        finally:
            reader.close()
        restored = wintypes.DWORD()
        self.assertTrue(reader.api.GetConsoleMode(reader.handle, ctypes.byref(restored)))
        self.assertEqual(restored.value, reader.original_mode)


class ToolboxTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.inventory = [
            {"name": "One.A+B", "properties": {"LABELS": ["One", "IllumoWorkspace"]}},
            {"name": "Two.Failure", "properties": {"LABELS": ["Two", "IllumoWorkspace"]}},
        ]

    def test_filter_name_and_project(self):
        self.assertEqual(build.matching_tests(self.inventory, "a+b", "One"), self.inventory[:1])
        self.assertEqual(build.matching_tests(self.inventory, "a+b", "Two"), [])

    def test_refresh_can_remove_active_project_label(self):
        from unittest.mock import Mock
        with patch.object(build, "read_test_inventory", side_effect=[self.inventory, self.inventory[1:]]), \
             patch.object(build, "execute_toolbox_run"), patch.object(build, "run_history", return_value=[]), \
             patch.object(build, "choose_toolbox", side_effect=["filter", "refresh", "filter", "back"]):
            build.run_test_explorer(build.DashboardState(), Mock())

    def test_empty_and_long_selections_never_expand(self):
        with self.assertRaisesRegex(build.BuildError, "No tests"):
            build.test_name_batches([])
        with self.assertRaisesRegex(build.BuildError, "safe command length"):
            build.test_name_batches(["x" * 50], 30)

    def test_exact_regex_and_batched_names(self):
        import re
        names = ["One.A+B", "foo[1]", "a(b)", "x|y", "a^$", "space name", "back\\slash"]
        batches = build.test_name_batches(names, 30)
        self.assertGreater(len(batches), 1)
        self.assertEqual([name for batch, _ in batches for name in batch], names)
        for batch, expression in batches:
            self.assertLessEqual(len(expression), 30)
            for name in batch:
                self.assertIsNotNone(re.fullmatch(expression, name))
                self.assertIsNone(re.fullmatch(expression, name + "suffix"))

    def test_discovery_rejects_other_config_in_generated_project(self):
        project = build.ProjectInfo("Custom", self.root / "Custom", test_runners=("CustomTests",), discovery_targets=("CustomTestsDiscover",))
        workspace = build.WorkspaceProjects(self.root, (project,))
        directory = self.root / "build" / "Custom"
        directory.mkdir(parents=True)
        (directory / "CustomTests-Release-discovered.cmake").write_text("# release")
        build.require_discovery(self.root / "build", "Release", workspace)
        with self.assertRaisesRegex(build.BuildError, "fallback is rejected"):
            build.require_discovery(self.root / "build", "Debug", workspace)

    def test_single_config_stale_discovery_rejected(self):
        (self.root / "CMakeCache.txt").write_text("CMAKE_BUILD_TYPE:STRING=Debug\nCMAKE_GENERATOR:INTERNAL=Ninja\n")
        with self.assertRaisesRegex(build.BuildError, "Single-configuration cache is Debug"):
            build.require_discovery(self.root, "Release", build.WorkspaceProjects(self.root, ()))

    def test_only_declared_discovery_targets(self):
        (self.root / "App").mkdir()
        (self.root / "CMakeLists.txt").write_text("add_subdirectory(App)\n")
        (self.root / "App/CMakeLists.txt").write_text("add_executable(AppTests main.cpp)\nillumo_discover_test_runner(AppTests App)\nadd_executable(OptionalGpuTests EXCLUDE_FROM_ALL gpu.cpp)\n")
        workspace = build.discover_workspace_projects(self.root)
        self.assertEqual(workspace.discovery_targets, ("AppTestsDiscover",))

    def test_all_progress_actions_record_effective_identity(self):
        state = build.DashboardState()
        for action in ("build", "build_app", "test", "coverage", "tidy", "docs"):
            command = [sys.executable, str(build.__file__), *build.dashboard_action_arguments(state, action)]
            record = build.new_run_record(state, action, command)
            expected = "Debug" if action in ("coverage", "tidy") else "Release"
            self.assertEqual(record["identity"]["configuration"], expected)
        self.assertEqual(build.new_run_record(state, "coverage", [sys.executable, str(build.__file__), "coverage"])["settings"]["generator"], "Ninja")

    def test_inventory_preserves_ctest_properties(self):
        properties = [{"name": "WORKING_DIRECTORY", "value": "isolated"},
                      {"name": "TIMEOUT", "value": 17}, {"name": "LABELS", "value": ["One"]}]
        response = subprocess.CompletedProcess([], 0, json.dumps({"tests": [{"name": "One.Test", "command": ["test.exe"], "properties": properties}]}), "")
        with patch.object(build, "require_discovery"), patch.object(build, "validate_build_settings"), \
             patch.object(build.subprocess, "run", return_value=response):
            test = build.read_test_inventory({"config": "Release", "build_dir": str(self.root)})[0]
        self.assertEqual(test["properties"]["TIMEOUT"], 17)
        self.assertEqual(test["properties"]["WORKING_DIRECTORY"], "isolated")
        self.assertEqual(test["command"], ["test.exe"])

    def test_profile_overrides_and_switch_reset(self):
        state = build.DashboardState()
        settings = {"config": "Debug", "build_dir": "custom-build", "generator": "Ninja", "architecture": "x64",
                    "no_tidy": True, "tracy": True, "parallel": 3, "cmake_arg": ["-DFOO=ON"]}
        build.apply_dashboard_profile(state, "custom", settings)
        args = build.create_parser().parse_args(build.dashboard_action_arguments(state, "build"))
        self.assertEqual((args.config, str(args.build_dir), args.generator, args.parallel), ("Debug", "custom-build", "Ninja", 3))
        self.assertTrue(args.no_tidy and args.tracy)
        self.assertEqual(args.cmake_arg, ["-DFOO=ON"])
        state.selected = next(i for i, item in enumerate(build.DASHBOARD_ITEMS) if item[2] == "configuration")
        build.adjust_dashboard_setting(state, 1)
        self.assertEqual(build.dashboard_settings(state)["config"], "RelWithDebInfo")
        self.assertEqual(state.profile_settings["config"], "Debug")
        build.apply_dashboard_profile(state, "release", build.BUILTIN_PROFILES["release"])
        self.assertEqual(state.overrides, {})
        self.assertFalse(build.dashboard_settings(state)["no_tidy"])
        self.assertEqual(state.configuration, "Release")

    def test_parallel_override_does_not_get_shadowed_by_profile(self):
        state = build.DashboardState()
        build.apply_dashboard_profile(state, "custom", {"parallel": 4})
        state.selected = next(i for i, item in enumerate(build.DASHBOARD_ITEMS) if item[2] == "parallel")
        build.adjust_dashboard_setting(state, 1)
        self.assertEqual(build.dashboard_settings(state)["parallel"], 8)

    def test_search_entry_is_separate_from_navigation(self):
        rows = [build.action_row("run", "Run"), build.ToolboxRow("test", "test")]
        view = build.ToolboxView(selected=1)
        self.assertIsNone(build.toolbox_event(view, rows, "search", [], 4))
        for character in "qhjkl/":
            build.toolbox_event(view, rows, build.DashboardTextEvent(character), [], 4)
        self.assertEqual(view.selected, 1)
        build.toolbox_event(view, rows, "backspace", [], 4)
        self.assertEqual(build.toolbox_event(view, rows, "enter", [], 4), "search")
        self.assertEqual(view.search, "qhjkl")
        build.toolbox_event(view, rows, "search", [], 4)
        build.toolbox_event(view, rows, build.DashboardTextEvent("x"), [], 4)
        build.toolbox_event(view, rows, "escape", [], 4)
        self.assertEqual(view.search, "qhjkl")

    def test_scroll_hover_alignment_and_explicit_actions(self):
        rows = [build.action_row("run", "Run selected")] + [build.ToolboxRow(str(i), f"Test {i}") for i in range(50)]
        view = build.ToolboxView(selected=35)
        rendered, regions, page = build.render_toolbox("Tests", rows, view, 90, 22)
        self.assertGreater(view.top, 0)
        region = regions[-1]
        self.assertIn(rows[region.index].label, rendered.splitlines()[region.row - 1])
        self.assertIsNone(build.toolbox_event(view, rows, build.DashboardMouseEvent(4, region.row, "left"), regions, page))
        self.assertEqual(view.focused, rows[region.index].key)
        build.toolbox_event(view, rows, "home", [], page)
        _, regions, page = build.render_toolbox("Tests", rows, view, 90, 22)
        region = regions[0]
        self.assertIsNone(build.toolbox_event(view, rows, build.DashboardMouseEvent(3, region.row, "hover"), regions, page))
        self.assertEqual(build.toolbox_event(view, rows, "enter", regions, page), "run")

    def test_scrolled_out_rows_have_no_hit_region(self):
        rows = [build.ToolboxRow(str(i), f"row {i}") for i in range(80)]
        view = build.ToolboxView(selected=70)
        _, regions, _ = build.render_toolbox("Tests", rows, view, 90, 20)
        self.assertNotIn(0, [hit.index for hit in regions])
        _, regions, _ = build.render_toolbox("Tests", rows, view, 10, 8)
        self.assertEqual(regions, [])

    def test_diagnostic_locations_and_raw_messages(self):
        lines = [r"src\a.cpp(12,3): error C123: failure", "src/b.cpp:4:7: warning: tidy [check]",
                 "CMake Error at CMakeLists.txt:8 (message):", "fatal error without location"]
        diagnostics = build.parse_diagnostics(lines, self.root)
        self.assertEqual([item["severity"] for item in diagnostics], ["error", "warning", "error", "error"])
        self.assertEqual([item["line"] for item in diagnostics], [12, 4, 8, None])
        self.assertEqual(Path(diagnostics[1]["source"]), self.root / "src/b.cpp")
        self.assertIsNone(build.parse_diagnostics(lines, None)[1]["source"])

    def test_diagnostics_use_command_context_and_msvc_prefix(self):
        command = ["cmake", "--build", str(self.root / "build")]
        lines = ["> " + build.format_command(command), "../src/test.cpp:7: warning: test",
                 "1>" + str(self.root / "native.cpp") + "(9): error C1000: test"]
        diagnostics = build.parse_diagnostics(lines, self.root, [{"command": command, "working_directory": str(self.root / "build")}])
        self.assertEqual(Path(diagnostics[0]["source"]).resolve(), self.root / "src/test.cpp")
        self.assertEqual(Path(diagnostics[1]["source"]), self.root / "native.cpp")

    def test_current_source_missing_and_out_of_range(self):
        path = self.root / "a.cpp"
        item = {"log_line": 0, "source": str(path), "line": 2}
        self.assertIn("Source unavailable", "\n".join(build.diagnostic_preview(item, ["error"])))
        path.write_text("one\ntwo\nthree\n")
        preview = "\n".join(build.diagnostic_preview(item, ["error"]))
        self.assertIn("CURRENT SOURCE", preview)
        self.assertIn("> 2: two", preview)
        item["line"] = 9
        self.assertIn("Invalid location", "\n".join(build.diagnostic_preview(item, [])))

    def test_rerun_latest_identity_and_missing_results(self):
        identity = {"checkout": "ours", "build_directory": "build", "configuration": "Release"}
        run = {"identity": identity, "action": "test", "tests_complete": True,
               "tests": [{"name": "Two.Failure", "status": "failed"}]}
        foreign = {**run, "identity": {**identity, "configuration": "Debug"}}
        self.assertEqual(build.failed_test_names([foreign, run], identity, self.inventory), ["Two.Failure"])
        for history, message in [([foreign], "No recorded"), ([{**run, "tests_complete": False}, run], "incomplete"),
                                 ([{**run, "tests": []}], "no failed")]:
            with self.assertRaisesRegex(build.BuildError, message):
                build.failed_test_names(history, identity, self.inventory)
        with self.assertRaisesRegex(build.BuildError, "missing"):
            build.failed_test_names([run], identity, [])

    def test_legacy_logs_and_versioned_history(self):
        (self.root / "legacy.log").write_text("warning")
        log = self.root / "current.log"
        log.write_text("error")
        build.write_json_atomic(log.with_suffix(".json"), {"version": 1, "status": "failed"})
        history = {Path(run["log"]).name: run for run in build.run_history(self.root)}
        self.assertNotIn("identity", history["legacy.log"])
        self.assertEqual(history["current.log"]["status"], "failed")

    def test_xml_failure_duration_disabled_and_missing(self):
        xml = b'''<Site><Testing><Test Status="failed"><Name>fail</Name><Results>
          <NamedMeasurement name="Execution Time"><Value>1.25</Value></NamedMeasurement>
          <NamedMeasurement name="Completion Status"><Value>Timeout</Value></NamedMeasurement>
          </Results></Test><Test Status="notrun"><Name>disabled</Name><Results>
          <NamedMeasurement name="Completion Status"><Value>Disabled</Value></NamedMeasurement>
          </Results></Test></Testing></Site>'''
        results = build.parse_ctest_results(xml, ["fail", "disabled", "missing"])
        self.assertEqual([test["status"] for test in results], ["failed", "disabled", "incomplete"])
        self.assertEqual(results[0]["duration"], 1.25)
        self.assertEqual(results[0]["completion"], "Timeout")

    def test_cancelled_results_preserve_completed_tests(self):
        path = self.root / "run.log"
        progress = build.DashboardProgress("Tests", "Release", path)
        progress.cancelled, progress.returncode = True, 130
        build.write_json_atomic(path.with_suffix(".tests.json"), {"complete": False, "tests": [
            {"name": "passed", "status": "passed", "duration": 1},
            {"name": "pending", "status": "pending", "duration": None}]})
        record = {"tests": [], "tests_complete": False}
        build.finish_run_record(progress, record)
        self.assertEqual(record["status"], "cancelled")
        self.assertEqual([test["status"] for test in record["tests"]], ["passed", "cancelled"])
        self.assertFalse(record["tests_complete"])

    def test_request_empty_selection_does_not_configure(self):
        request = self.root / "request.json"
        build.write_json_atomic(request, {"version": 1, "settings": {}, "names": [], "mode": "run"})
        with patch.object(build, "configure") as configure:
            with self.assertRaisesRegex(build.BuildError, "No tests"):
                build.run_toolbox_request(build.argparse.Namespace(request=request))
            configure.assert_not_called()

    def test_invalid_request_reports_error(self):
        request = self.root / "request.json"
        for data in ([], {"version": 2}, {"version": 1, "mode": "run", "names": [5]}):
            request.write_text(json.dumps(data))
            with self.assertRaises(build.BuildError):
                build.run_toolbox_request(build.argparse.Namespace(request=request))

    @unittest.skipUnless(build.shutil.which("cmake") and build.shutil.which("ninja"), "requires CMake and Ninja")
    def test_live_ctest_failure_rerun_and_exact_selection(self):
        directory, working = self.root / "binary", self.root / "isolated"
        working.mkdir()
        script = self.root / "case.py"
        script.write_text("import sys\nfrom pathlib import Path\nsys.exit(0 if Path('allow').is_file() else 1)\n")
        name = "Sample.Fail+[case]"
        (self.root / "CMakeLists.txt").write_text(
            'cmake_minimum_required(VERSION 3.20)\nproject(Toolbox NONE)\ninclude(CTest)\n'
            f'add_test(NAME "{name}" COMMAND "{Path(sys.executable).as_posix()}" "{script.as_posix()}")\n'
            f'add_test(NAME "{name}suffix" COMMAND "{Path(sys.executable).as_posix()}" "{script.as_posix()}")\n'
            f'set_tests_properties("{name}" "{name}suffix" PROPERTIES LABELS "Sample;IllumoWorkspace" TIMEOUT 9 WORKING_DIRECTORY "{working.as_posix()}")\n')
        configure = subprocess.run(["cmake", "-S", str(self.root), "-B", str(directory), "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release"], capture_output=True, text=True)
        self.assertEqual(configure.returncode, 0, configure.stdout + configure.stderr)
        request, result = self.root / "request.json", self.root / "results.json"
        settings = {"config": "Release", "build_dir": str(directory)}
        build.write_json_atomic(request, {"version": 1, "settings": settings, "names": [name],
                                         "mode": "run", "build_first": False, "result": str(result)})
        with patch.object(build, "REPOSITORY_ROOT", self.root), patch.object(build, "SOURCE_DIRECTORY", self.root), contextlib.redirect_stdout(io.StringIO()):
            inventory = build.read_test_inventory(settings)
            self.assertEqual(inventory[0]["properties"]["TIMEOUT"], 9)
            with self.assertRaisesRegex(build.BuildError, "failed or"):
                build.run_toolbox_request(build.argparse.Namespace(request=request))
            failed = json.loads(result.read_text())
            self.assertTrue(failed["complete"])
            self.assertEqual([(test["name"], test["status"]) for test in failed["tests"]], [(name, "failed")])
            identity = build.build_identity(settings)
            record = {"identity": identity, "action": "test", "tests_complete": True, "tests": failed["tests"]}
            self.assertEqual(build.failed_test_names([record], identity, inventory), [name])
            (working / "allow").write_text("ready")
            build.run_toolbox_request(build.argparse.Namespace(request=request))
            passed = json.loads(result.read_text())
            self.assertTrue(passed["complete"])
            self.assertEqual(passed["tests"][0]["status"], "passed")

    def test_copied_orchestrator_runs_without_tool_modules(self):
        import shutil
        shutil.copy2(build.__file__, self.root / "build.py")
        (self.root / "Product").mkdir()
        (self.root / "CMakeLists.txt").write_text("add_subdirectory(Product)\n")
        (self.root / "Product/CMakeLists.txt").write_text("add_executable(Product main.cpp)\nadd_executable(ProductTests tests.cpp)\nillumo_discover_test_runner(ProductTests Product)\n")
        # The child picks Unicode or ASCII glyphs from its own stdout encoding.
        result = subprocess.run([sys.executable, str(self.root / "build.py"), "menu", "--snapshot"],
                                capture_output=True, text=True, encoding="utf-8", errors="replace")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("Development Tools", result.stdout)
        self.assertIn("Product", result.stdout)

    def test_artifacts_only_exist_and_open_without_build(self):
        settings = {"build_dir": str(self.root), "config": "Release"}
        with patch.object(build, "REPOSITORY_ROOT", self.root):
            self.assertEqual(build.available_artifacts(settings, []), [("Selected build directory", self.root)])
        with patch.object(build.os, "startfile", create=True) as start, patch.object(build, "run_build") as run:
            build.open_artifact(self.root)
            start.assert_called_once_with(str(self.root.resolve()))
            run.assert_not_called()
            with self.assertRaisesRegex(build.BuildError, "no longer exists"):
                build.open_artifact(self.root / "missing")


class WasmRuntimeTests(unittest.TestCase):
    """The game only exists as a WASM package, so the orchestrator must build it."""

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    def parse(self, *extra):
        return build.create_parser().parse_args(["build", "--build-dir", str(self.root), *extra])

    def test_configure_always_states_the_wasm_option(self):
        # A stale cached OFF used to survive every configure and drop the game.
        expected = "ON" if build.WASM_HOST_SUPPORTED else "OFF"
        self.assertEqual(build.configure_definitions(self.parse())["ILLUMO_BUILD_WASM_RUNTIME"], expected)
        self.assertEqual(build.configure_definitions(self.parse("--no-wasm"))["ILLUMO_BUILD_WASM_RUNTIME"], "OFF")
        self.assertEqual(build.configure_definitions(self.parse("--wasm"))["ILLUMO_BUILD_WASM_RUNTIME"], "ON")
        overridden = self.parse("--wasm", "--cmake-arg=-DILLUMO_BUILD_WASM_RUNTIME:BOOL=OFF")
        self.assertFalse(build.wasm_requested(overridden))

    def test_missing_toolchain_fails_before_configure(self):
        arguments = self.parse("--wasm", f"--cmake-arg=-DILLUMO_WASM_TOOLS={self.root / 'none'}")
        with patch.object(build, "WASM_HOST_SUPPORTED", True):
            with self.assertRaisesRegex(build.BuildError, "wasm-tools.*--no-wasm"):
                build.require_wasm_toolchain(arguments, dry_run=False)
            with contextlib.redirect_stderr(io.StringIO()) as errors:
                build.require_wasm_toolchain(arguments, dry_run=True)
            self.assertIn("warning:", errors.getvalue())
            build.require_wasm_toolchain(self.parse("--no-wasm"), dry_run=False)
        with patch.object(build, "WASM_HOST_SUPPORTED", False):
            with self.assertRaisesRegex(build.BuildError, "Windows x64"):
                build.require_wasm_toolchain(self.parse("--wasm"), dry_run=True)

    def test_toolchain_names_follow_cmake_pins(self):
        wasmtime, wasi_sdk = build.wasm_toolchain_packages()
        text = build.WASM_CMAKE_MODULE.read_text(encoding="utf-8")
        self.assertIn(wasmtime, text)
        self.assertIn(wasi_sdk, text)

    def test_wasi_guest_trees_are_not_applications(self):
        (self.root / "CMakeLists.txt").write_text("add_subdirectory(App)\n")
        for name, text in (("App", "add_executable(App main.cpp)\n"),
                           ("Guest", 'if(NOT CMAKE_SYSTEM_NAME STREQUAL "WASI")\nendif()\nadd_executable(GuestModule a.cpp)\n')):
            (self.root / name).mkdir()
            (self.root / name / "CMakeLists.txt").write_text(text)
        self.assertEqual(build.discover_workspace_projects(self.root).applications, ("App",))

    def test_installed_apps_follow_cmake_staging(self):
        apps = build.installed_apps()
        self.assertEqual([app.name for app in apps], ["game", "illed", "meshviewer"])
        self.assertEqual(apps[0].module, "IllumoGame.wasm")
        self.assertEqual(build.app_names(self.root), (build.DEFAULT_APP,))

    def test_runtime_outputs_and_retired_outputs(self):
        suffix = ".exe" if build.os.name == "nt" else ""
        release = self.root / "Release"
        apps = (build.AppPackage("game", "IllumoGame.wasm"),
                build.AppPackage("illed", "IllEd.wasm"))
        (release / "apps" / "game").mkdir(parents=True)
        (release / f"IllumoRuntime{suffix}").write_bytes(b"")
        self.assertFalse(build.runtime_outputs(self.root, "Release", apps).playable("game"))
        (release / "apps" / "game" / "app.json").write_text("{}")
        (release / "apps" / "game" / "IllumoGame.wasm").write_bytes(b"\0" * 2048)
        (release / f"IllEd{suffix}").write_bytes(b"")
        (release / "game").mkdir()
        outputs = build.runtime_outputs(self.root, "Release", apps)
        self.assertTrue(outputs.playable("game") and not outputs.playable("illed"))
        self.assertFalse(outputs.complete)
        self.assertIn("game (2.0 KB)", outputs.describe())
        self.assertIn("missing illed", outputs.describe())
        self.assertEqual(outputs.retired, (release / f"IllEd{suffix}", release / "game"))
        self.assertFalse(build.runtime_outputs(self.root, "Debug", apps).playable("game"))

    def test_play_runs_the_selected_app_through_the_runtime(self):
        commands = []
        arguments = build.create_parser().parse_args(
            ["play", "--app", "meshviewer", "--no-build", "--build-dir", str(self.root),
             "--", "--open", "model.obj"])
        with patch.object(build.CommandRunner, "run",
                          lambda _self, command, *_: commands.append(list(command))), \
             patch.object(build, "executable_path",
                          return_value=self.root / "IllumoRuntime.exe"), \
             patch.object(build, "runtime_outputs", return_value=build.RuntimeOutputs()):
            build.run_application(arguments)
        self.assertEqual(commands[-1][1:], ["--app", "meshviewer", "--open", "model.obj"])
        commands.clear()
        explicit = build.create_parser().parse_args(
            ["play", "--no-build", "--build-dir", str(self.root), "--", "--package", "pkg"])
        with patch.object(build.CommandRunner, "run",
                          lambda _self, command, *_: commands.append(list(command))), \
             patch.object(build, "executable_path",
                          return_value=self.root / "IllumoRuntime.exe"), \
             patch.object(build, "runtime_outputs", return_value=build.RuntimeOutputs()):
            build.run_application(explicit)
        self.assertEqual(commands[-1][1:], ["--package", "pkg"])

    def workspace(self):
        project = build.ProjectInfo("App", self.root / "App", test_runners=("AppTests",),
                                    discovery_targets=("AppTestsDiscover",), test_prefixes=("App.",))
        return build.WorkspaceProjects(self.root, (project,))

    def listing(self):
        executable = self.root / "Release"
        return [
            {"name": "App.One", "command": [str(executable / "AppTests.exe"), "--run", "App.One"]},
            {"name": "App.Wasm.Package", "command": [str(executable / "AppWasmTests.exe"), "--run", "App.Wasm.Package"]},
            {"name": "App.Script", "command": [str(self.root.parent / "cmake.exe"), "-P", "x.cmake"]},
        ]

    def test_test_builds_every_executable_ctest_runs(self):
        commands = []
        runner = build.CommandRunner(False)
        with patch.object(runner, "run", side_effect=commands.append), \
             patch.object(build, "ctest_listing", return_value=self.listing()), \
             patch.object(build, "existing_tool", return_value="ctest"):
            build.build_test_executables(self.parse(), "cmake", runner, self.workspace())
        targets = [command[command.index("--target") + 1:] for command in commands]
        self.assertEqual(targets, [["AppTestsDiscover"], ["AppWasmTests"]])

    def test_exact_test_resolves_through_ctest_before_prefixes(self):
        commands = []
        arguments = build.create_parser().parse_args(
            ["test", "--test", "App.Wasm.Package", "--build-dir", str(self.root)])
        with patch.object(build, "configure", return_value="cmake"), \
             patch.object(build, "discover_workspace_projects", return_value=self.workspace()), \
             patch.object(build, "ctest_listing", return_value=self.listing()), \
             patch.object(build, "existing_tool", return_value="ctest"), \
             patch.object(build.CommandRunner, "run", lambda _self, command, *_: commands.append(list(command))):
            build.run_tests(arguments)
        self.assertEqual(commands[0][commands[0].index("--target") + 1:], ["AppWasmTests"])
        self.assertIn(r"^App\.Wasm\.Package$", commands[1])
        # A discovered runner's case keeps the direct --run invocation.
        self.assertEqual(self.workspace().resolve_test_target("App.One"), "AppTests")

    def test_dashboard_wasm_setting_round_trips(self):
        state = build.DashboardState(applications=("illed", "game"))
        self.assertEqual((state.application, state.application_label), ("game", "IllumoGame"))
        state.selected = next(i for i, item in enumerate(build.DASHBOARD_ITEMS) if item[2] == "wasm")
        with patch.object(build, "WASM_HOST_SUPPORTED", True):
            state.wasm_enabled = True
            build.adjust_dashboard_setting(state, 1)
            self.assertTrue(build.dashboard_settings(state)["no_wasm"])
            args = build.create_parser().parse_args(build.dashboard_action_arguments(state, "play"))
            self.assertTrue(args.no_wasm)
            self.assertEqual((args.command, args.app, args.package),
                             ("play", "IllumoRuntime", "game"))
            launch = build.create_parser().parse_args(
                build.dashboard_action_arguments(state, "launch"))
            self.assertTrue(launch.no_build and launch.package == "game")
            build.apply_dashboard_profile(state, "release", build.BUILTIN_PROFILES["release"])
            self.assertTrue(state.wasm_enabled)

    def test_console_fits_a_default_terminal(self):
        state = build.DashboardState(applications=("game",))
        for mouse in (False, True):
            rendered = build.render_dashboard(state, 120, ansi=False, mouse_enabled=mouse)
            self.assertLessEqual(len(rendered.splitlines()), 30)


class DashboardPolishTests(unittest.TestCase):
    """Hotkeys, profile cycling, run badges, styled layout and progress."""

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.state = build.DashboardState(applications=("game",), profiles_file=self.root / "none.json")

    def index(self, key):
        return [item[2] for item in build.DASHBOARD_ITEMS].index(key)

    def test_hotkeys_select_and_run_their_action(self):
        self.assertEqual(build.printable_key("p"), "key:p")
        self.assertEqual(build.printable_key("\x00"), "unknown")
        self.assertEqual(build.dashboard_key_action(self.state, "key:b"), "build")
        self.assertEqual(self.state.selected, self.index("build"))
        self.assertIsNone(build.dashboard_key_action(self.state, "key:z"))
        letters = list(build.DASHBOARD_HOTKEYS.values())
        self.assertEqual(len(letters), len(set(letters)))
        self.assertFalse(set(letters) & set("hjkl"))

    def test_repeat_and_enter_only_run_actions(self):
        self.assertIsNone(build.dashboard_key_action(self.state, "key:."))
        self.assertIn("Nothing to repeat", self.state.status)
        self.state.last_action = "docs"
        self.assertEqual(build.dashboard_key_action(self.state, "key:."), "docs")
        self.assertEqual(self.state.selected, self.index("docs"))
        self.state.selected = self.index("configuration")
        self.assertIsNone(build.dashboard_key_action(self.state, "enter"))

    def test_navigation_keys(self):
        first_action = next(i for i, item in enumerate(build.DASHBOARD_ITEMS) if item[0] == "action")
        self.assertTrue(build.dashboard_navigate(self.state, "tab"))
        self.assertEqual(self.state.selected, first_action)
        self.assertTrue(build.dashboard_navigate(self.state, "tab"))
        self.assertEqual(self.state.selected, 0)
        build.dashboard_navigate(self.state, "end")
        self.assertEqual(self.state.selected, len(build.DASHBOARD_ITEMS) - 1)
        build.dashboard_navigate(self.state, "page_down")
        self.assertEqual(self.state.selected, len(build.DASHBOARD_ITEMS) - 1)
        self.assertFalse(build.dashboard_navigate(self.state, "enter"))
        for sequence, expected in (("[H", "home"), ("OF", "end"), ("[5~", "page_up"), ("[6~", "page_down")):
            self.assertEqual(build.posix_escape_to_key(sequence, False), expected)

    def test_profile_setting_cycles_and_marks_overrides(self):
        self.state.selected = self.index("profile")
        names = list(build.load_profiles(self.state.profiles_file, allow_missing=True))
        build.adjust_dashboard_setting(self.state, 1)
        self.assertEqual(self.state.profile_name, names[0])
        self.assertEqual(self.state.configuration, build.BUILTIN_PROFILES[names[0]]["config"])
        self.state.selected = self.index("tracy")
        build.adjust_dashboard_setting(self.state, 1)
        self.assertEqual(self.state.profile_label, f"{names[0]} (edited)")
        self.state.selected = self.index("profile")
        build.adjust_dashboard_setting(self.state, -1)
        self.assertIsNone(self.state.profile_name)
        self.assertEqual((self.state.overrides, self.state.profile_label), ({}, "Default"))
        build.adjust_dashboard_setting(self.state, -1)
        self.assertEqual(self.state.profile_name, names[-1])

    def test_selection_hint_shows_the_equivalent_command(self):
        self.state.selected = self.index("build")
        hint = build.dashboard_selection_hint(self.state)
        self.assertTrue(hint.startswith("$ python build.py build "), hint)
        self.assertIn("--config=Release", hint)
        self.state.selected = self.index("configuration")
        self.assertIn("AddressSanitizer", build.dashboard_selection_hint(self.state))
        rendered = build.render_dashboard(self.state, 120, ansi=False)
        self.assertIn("AddressSanitizer", rendered.splitlines()[-1])

    def test_last_run_badges_follow_the_selected_tree(self):
        identity = build.build_identity(build.dashboard_settings(self.state))
        self.state.history = [
            {"action": "build", "identity": {**identity, "configuration": "Debug"},
             "status": "failed", "started": 50.0},
            {"action": "build", "identity": identity, "status": "succeeded",
             "elapsed": 192.0, "started": 100.0},
            {"action": "docs", "identity": {}, "status": "failed", "started": 100.0},
        ]
        glyphs = build.ASCII_GLYPHS
        self.assertEqual(build.dashboard_run_badge(self.state, "build", glyphs, now=100.0 + 7200),
                         ("+ 3m 12s | 2h ago", build.ANSI_GREEN))
        self.assertEqual(build.dashboard_run_badge(self.state, "docs", glyphs, now=130.0)[0], "x failed | just now")
        self.assertIsNone(build.dashboard_run_badge(self.state, "play", glyphs))
        self.state.configuration_index = build.DASHBOARD_CONFIGURATIONS.index("MinSizeRel")
        self.assertIsNone(build.dashboard_run_badge(self.state, "build", glyphs))

    def test_styled_rows_keep_geometry_and_never_split_escapes(self):
        self.state.history = [{"action": "build", "status": "succeeded", "elapsed": 5.0, "started": 0.0,
                               "identity": build.build_identity(build.dashboard_settings(self.state))}]
        self.state.git_summary = "main @ 0123abcd | clean"
        for columns in (40, 72, 96, 140):
            plain = build.render_dashboard(self.state, columns, ansi=False)
            styled = build.render_dashboard(self.state, columns, ansi=True)
            self.assertEqual(len(plain.splitlines()), len(styled.splitlines()))
            for plain_line, styled_line in zip(plain.splitlines(), styled.splitlines()):
                self.assertEqual(build._ANSI_SEQUENCE.sub("", styled_line.replace("\x1b[0m", "")), plain_line)
        segments = [("abc", build.ANSI_RED), ("defgh", build.ANSI_GREEN)]
        self.assertEqual(build.render_segments(segments, 6, False, ellipsis="..."), "abc...")
        self.assertEqual(build.render_segments(segments, 10, False), "abcdefgh  ")
        self.assertEqual(build._ANSI_SEQUENCE.sub("", build.render_segments(segments, 5, True, ellipsis="~")), "abcd~")
        self.assertEqual(build.render_segments(segments, 2, False, ellipsis="..."), "..")
        self.assertIs(build.terminal_glyphs("cp1252"), build.ASCII_GLYPHS)
        self.assertIs(build.terminal_glyphs("utf-8"), build.UNICODE_GLYPHS)

    def test_progress_timeline_eta_and_first_errors(self):
        progress = build.DashboardProgress("Build everything", "Release", self.root / "run.log", started=0.0)
        progress.consume('> "cmake" -S source -B build', 1)
        progress.consume('> "cmake" --build build', 5)
        progress.consume("[25/100] Building CXX object a.cpp", 15)
        self.assertEqual([phase[0] for phase in progress.phases], ["Configuring", "Building"])
        self.assertEqual(progress.phases[0][2], 5)
        self.assertEqual(build.progress_eta(progress, 15), "ETA ~30.0s")
        rendered = build.render_dashboard_progress(progress, 110, 40, now=15, ansi=False)
        self.assertIn("Configuring 4.0s", rendered)
        self.assertIn("ETA ~30.0s", rendered)
        progress.consume("src/a.cpp(3): error C2065: 'x': undeclared identifier", 16)
        progress.returncode = 2
        progress.finish(17)
        self.assertEqual(progress.phases[-1][2], 17)
        rendered = build.render_dashboard_progress(progress, 110, 40, now=30, ansi=False)
        self.assertIn("First errors", rendered)
        self.assertIn("error C2065", rendered)
        self.assertIn("d diagnostics", rendered)

    def test_quiet_tool_uses_the_last_run_as_an_estimate(self):
        progress = build.DashboardProgress("Docs", "Release", self.root / "run.log", started=0.0)
        progress.previous_elapsed = 40.0
        rendered = build.render_dashboard_progress(progress, 110, 30, now=10, ansi=False)
        self.assertIn("~25% of the last run (40.0s)", rendered)
        self.assertIn("has not reported a total", rendered)
        self.assertIn("Last run 40.0s", rendered)

    def test_terminal_signals_only_drive_the_taskbar_where_supported(self):
        progress = build.DashboardProgress("Build", "Release", self.root / "run.log", started=0.0)
        progress.completed, progress.total = 3, 4
        with patch.dict(build.os.environ, {"WT_SESSION": "1"}):
            self.assertIn("\x1b]9;4;1;75\x07", build.terminal_progress_signals(progress))
            self.assertIn("\x1b]9;4;0;0\x07", build.terminal_progress_signals(None))
        with patch.dict(build.os.environ, {}, clear=True):
            signals = build.terminal_progress_signals(progress)
            self.assertNotIn("9;4", signals)
            self.assertIn("Illumo | Build | Starting 75%", signals)

    def test_styled_toolbox_keeps_hit_rows_and_adds_a_scrollbar(self):
        rows = [build.action_row("run", "Run selected")] + [
            build.ToolboxRow(str(i), f"Test {i}", tone="bad" if i % 2 else "ok") for i in range(50)]
        view = build.ToolboxView(selected=30)
        plain, plain_regions, _ = build.render_toolbox("Tests", rows, view, 90, 22)
        styled, styled_regions, _ = build.render_toolbox("Tests", rows, view, 90, 22, ansi=True)
        self.assertEqual(plain_regions, styled_regions)
        self.assertNotIn("\x1b", plain)
        styled_lines = [build._ANSI_SEQUENCE.sub("", line) for line in styled.splitlines()]
        for region in styled_regions:
            self.assertIn(rows[region.index].label, styled_lines[region.row - 1])
        glyphs = build.terminal_glyphs()
        # The scrollbar sits in the last content column, inside the frame.
        self.assertTrue(any(line[-3] == glyphs["thumb"] for line in plain.splitlines()))
        self.assertTrue(all(len(line) == 89 for line in plain.splitlines()))
        self.assertTrue(plain.splitlines()[0].startswith(glyphs["top_left"]))
        self.assertTrue(plain.splitlines()[-1].startswith(glyphs["bottom_left"]))

    def test_throughput_sparkline_and_last_run_comparison(self):
        glyphs = build.ASCII_GLYPHS
        self.assertEqual(build.sparkline([0, 7, 14], glyphs, low=0), "_=#")
        self.assertEqual(build.sparkline([3, 3], glyphs), "==")
        self.assertEqual(build.sparkline([0, 0], glyphs, low=0), "__")
        progress = build.DashboardProgress("Build", "Release", self.root / "run.log", started=0.0)
        progress.consume('> "cmake" --build build', 0)
        for second in range(1, 7):
            progress.consume(f"[{second * 10}/200] Building CXX object f{second}.cpp", second)
        self.assertEqual(build.progress_throughput(progress, 6.0), [10, 10, 10, 10, 10])
        rendered = build.render_dashboard_progress(progress, 110, 40, now=6, ansi=False)
        self.assertIn("Rate:", rendered)
        self.assertIn("10.0 steps/s", rendered)
        progress.consume('> "ctest" --test-dir build', 7)
        self.assertEqual(len(progress.samples), 0)
        self.assertEqual(build.compare_with_last_run(90, 100), "10% faster than the last run (1m 40s)")
        self.assertEqual(build.compare_with_last_run(150, 100), "50% slower than the last run (1m 40s)")
        self.assertIn("about the same", build.compare_with_last_run(100.5, 100))
        progress.returncode, progress.previous_elapsed = 0, 20.0
        progress.finish(10)
        self.assertIn("Action finished in 10.0s  50% faster than the last run (20.0s)",
                      build.render_dashboard_progress(progress, 110, 40, ansi=False))

    def test_build_trends_group_actions_and_color_outcomes(self):
        history = [  # Newest first, as run_history returns.
            {"action": "build", "status": "succeeded", "elapsed": 90.0, "started": 30.0,
             "identity": {"configuration": "Release"}},
            {"action": "test", "status": "failed", "elapsed": 10.0, "started": 20.0},
            {"action": "build", "status": "failed", "elapsed": 30.0, "started": 10.0},
            {"action": "build", "status": "succeeded", "elapsed": 120.0, "started": 0.0},
            {"action": "build", "status": "running", "started": 40.0},
            {"log": "legacy.log"},
        ]
        trends = build.action_trends(history)
        self.assertEqual([action for action, _runs in trends], ["build", "test"])
        self.assertEqual([run["elapsed"] for run in trends[0][1]], [120.0, 30.0, 90.0])
        row = build.trend_row("build", trends[0][1], build.ASCII_GLYPHS, now=90.0)
        self.assertEqual("".join(text for text, _style in row.segments), row.label)
        self.assertIn("median   1m 45s", row.label)
        self.assertIn(" 66% ok", row.label)
        self.assertEqual([style for text, style in row.segments[1:4]],
                         [build.ANSI_GREEN, build.ANSI_RED, build.ANSI_GREEN])
        self.assertIn("2 succeeded, 1 failed", row.details[0])
        self.assertIn("14% faster than the median", row.details[3])
        rendered, regions, _ = build.render_toolbox("Trends", [row], build.ToolboxView(), 100, 20)
        self.assertIn(row.label, rendered.splitlines()[regions[0].row - 1])

    def test_fuzzy_palette_ranks_and_applies_choices(self):
        self.assertIsNone(build.fuzzy_match("xyz", "Build everything"))
        self.assertEqual(build.fuzzy_match("bev", "Build everything")[1], (0, 6, 7))
        self.assertGreater(build.fuzzy_match("tidy", "Run clang-tidy")[0],
                           build.fuzzy_match("tidy", "Toolchain doctor (tidy)x")[0] - 100)
        rows, total = build.palette_rows(self.state, "cfg deb")
        self.assertGreater(total, len(rows))
        self.assertEqual(rows[0].key, "set:configuration=Debug")
        self.assertEqual("".join(text for text, _style in rows[0].segments), rows[0].label)
        rows, _ = build.palette_rows(self.state, "play")
        self.assertEqual(rows[0].key, "action:play")
        terminal = unittest.mock.Mock()
        self.assertIsNone(build.apply_palette_choice(self.state, terminal, "set:configuration=Debug"))
        self.assertEqual((self.state.configuration, self.state.overrides["config"]), ("Debug", "Debug"))
        self.assertIsNone(build.apply_palette_choice(self.state, terminal, "set:parallel=8 jobs"))
        self.assertEqual(build.dashboard_settings(self.state)["parallel"], 8)
        self.assertIsNone(build.apply_palette_choice(self.state, terminal, "set:tracy=On"))
        self.assertTrue(self.state.tracy_enabled)
        self.assertIsNone(build.apply_palette_choice(self.state, terminal, "profile:release"))
        self.assertEqual((self.state.profile_name, self.state.overrides), ("release", {}))
        self.assertEqual(build.apply_palette_choice(self.state, terminal, "action:docs"), "docs")
        with patch.object(build, "run_trends_view") as trends:
            build.apply_palette_choice(self.state, terminal, "tool:trends")
        trends.assert_called_once_with(terminal)

    def test_palette_types_filters_and_runs(self):
        terminal = unittest.mock.Mock()
        terminal.read_event.side_effect = [build.DashboardTextEvent(c) for c in "tidyx"] + [
            "backspace", "enter"]
        with patch.object(build.shutil, "get_terminal_size", return_value=build.os.terminal_size((100, 32))), \
             contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(build.run_command_palette(self.state, terminal), "action:tidy")
        terminal.read_event.side_effect = [build.DashboardTextEvent("zzzz"), "enter", "escape"]
        with patch.object(build.shutil, "get_terminal_size", return_value=build.os.terminal_size((100, 32))), \
             contextlib.redirect_stdout(io.StringIO()):
            self.assertIsNone(build.run_command_palette(self.state, terminal))

    def test_watch_snapshot_skips_generated_trees(self):
        for relative in ("App/Source/a.cpp", "App/CMakeLists.txt", "App/app.json", "App/notes.txt",
                         "build-x/gen.cpp", ".git/x.h", "Illumo/thirdparty/lib.h", "Illumo/Source/b.h",
                         "docs/snippet.cpp", "archive/old.cpp", "App/__pycache__/m.json"):
            path = self.root / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("x")
        before = build.watch_snapshot(self.root)
        self.assertEqual(sorted(before), ["App/CMakeLists.txt", "App/Source/a.cpp", "App/app.json",
                                          "Illumo/Source/b.h"])
        (self.root / "App/Source/a.cpp").unlink()
        (self.root / "App/Source/new.hpp").write_text("x")
        self.assertEqual(build.watch_changes(before, build.watch_snapshot(self.root)),
                         ["App/Source/a.cpp", "App/Source/new.hpp"])

    def test_watch_rebuilds_on_change_and_stops_on_ctrl_c(self):
        commands = []
        snapshots = iter([{"a.cpp": 1}, {"a.cpp": 1}, {"a.cpp": 2}, {"a.cpp": 2}])
        sleeps = iter([None, None, None, KeyboardInterrupt()])

        def sleep(_seconds):
            value = next(sleeps)
            if value is not None:
                raise value
        arguments = build.create_parser().parse_args(
            ["watch", "--build-dir", str(self.root), "--test", "App.Case+1", "--interval", "0.5"])
        with patch.object(build, "configure", return_value="cmake"), \
             patch.object(build, "existing_tool", return_value="ctest"), \
             patch.object(build, "watch_snapshot", side_effect=lambda _root: next(snapshots)), \
             patch.object(build.time, "sleep", side_effect=sleep), \
             patch.object(build.CommandRunner, "run", lambda _self, command, *_: commands.append(list(command))), \
             contextlib.redirect_stdout(io.StringIO()) as output:
            build.run_watch(arguments)
        self.assertEqual(len(commands), 4)  # Build and test, twice.
        self.assertIn(r"^(App\.Case\+1)$", commands[1])
        self.assertIn("1 changed (a.cpp)", output.getvalue())
        self.assertIn("Watch stopped after 2 cycles", output.getvalue())
        with self.assertRaises(SystemExit), contextlib.redirect_stderr(io.StringIO()):
            build.create_parser().parse_args(["watch", "--interval", "0"])

    def test_tall_terminals_get_the_block_banner(self):
        compact = build.render_dashboard(self.state, 120, ansi=False, terminal_rows=30)
        with patch.object(build, "terminal_glyphs", return_value=build.UNICODE_GLYPHS):
            tall = build.render_dashboard(self.state, 120, ansi=False, terminal_rows=40)
            narrow = build.render_dashboard(self.state, 40, ansi=False, terminal_rows=40)
        with patch.object(build, "terminal_glyphs", return_value=build.ASCII_GLYPHS):
            ascii_tall = build.render_dashboard(self.state, 120, ansi=False, terminal_rows=40)
        self.assertEqual(len(tall.splitlines()), len(compact.splitlines()) + 3)
        self.assertLessEqual(len(tall.splitlines()), build.BANNER_MIN_ROWS)
        for row in build.BANNER_ROWS:
            self.assertIn(row, tall)
            self.assertNotIn(row, narrow)
            self.assertNotIn(row, ascii_tall)
        styled = build.render_dashboard(self.state, 120, ansi=True, terminal_rows=40)
        self.assertEqual(len(styled.splitlines()), len(build.render_dashboard(
            self.state, 120, ansi=False, terminal_rows=40).splitlines()))

    def test_switches_configuration_colors_and_status_time(self):
        glyphs = build.UNICODE_GLYPHS
        self.assertEqual(build.dashboard_value_text("On", glyphs), "● On")
        self.assertEqual(build.dashboard_value_text("Off", glyphs), "○ Off")
        self.assertEqual(build.dashboard_value_text("Release", glyphs), "Release")
        self.assertIn(build.ANSI_YELLOW, build.dashboard_value_style("configuration", "Debug", self.state))
        self.state.status, self.state.status_time = "Build everything succeeded (3.0s)", "14:02"
        status_row = next(line for line in build.render_dashboard(self.state, 100, ansi=False).splitlines()
                          if "succeeded (3.0s)" in line)
        self.assertTrue(status_row.rstrip(build.terminal_glyphs()["vertical"]).rstrip().endswith("14:02"))

    def test_frames_are_exact_width_and_trim_labels(self):
        glyphs = build.UNICODE_GLYPHS
        for width in (8, 24, 60):
            self.assertEqual(len(build.frame_rule("╭", "╮", [("a long label " * 5, "")], width, glyphs, False)), width)
            self.assertEqual(len(build.frame_line([("x" * 100, "")], width, glyphs, False)), width)
        self.assertEqual(build.frame_rule("╭", "╮", [("hi", "")], 12, glyphs, False), "╭─ hi ─────╮")
        progress = build.DashboardProgress("Build", "Release", self.root / "run.log", started=0.0)
        rendered = build.render_dashboard_progress(progress, 60, 20, now=1, ansi=False).splitlines()
        self.assertTrue(rendered[0].startswith("╭") or rendered[0].startswith("+"))
        self.assertIn("RUNNING", rendered[2])
        self.assertTrue(any("Output" in line and line.startswith(("├", "+")) for line in rendered))
        self.assertTrue(rendered[-1].startswith(("╰", "+")))
        self.assertIn("Ctrl+C cancels", rendered[-1])

    def test_doctor_text_is_an_aligned_table(self):
        with patch.object(build.shutil, "which", return_value=None), \
             contextlib.redirect_stdout(io.StringIO()) as output, \
             contextlib.redirect_stderr(io.StringIO()):
            code = build.main(["doctor", "--build-dir", str(self.root), "--no-wasm", "--no-docs"])
        self.assertEqual(code, 1)
        lines = [line for line in output.getvalue().splitlines() if line.strip()]
        self.assertTrue(lines[-1].strip().endswith("errors"))
        detail_columns = {line.index("Not found") for line in lines if "Not found" in line}
        self.assertEqual(len(detail_columns), 1)

    def test_shine_sweeps_once_per_period(self):
        self.assertEqual(build.shine_band(0, 10), (-3, 0))
        self.assertEqual(build.shine_band(4, 10), (3, 6))
        self.assertIsNone(build.shine_band(20, 10))
        self.assertEqual(build.shine_band(build.SHINE_PERIOD_FRAMES + 4, 10), (3, 6))
        lit = build.apply_shine([(c, "s") for c in "ab cd"], (1, 4))
        self.assertEqual([style for _text, style in lit], ["s", build.ANSI_SHINE, "s", build.ANSI_SHINE, "s"])
        self.assertEqual(build.apply_shine([("a", "s")], None), [("a", "s")])

    def test_animation_only_recolors_never_moves(self):
        self.state.status_frame = 0
        still = build.render_dashboard(self.state, 110, ansi=False, terminal_rows=40)
        frames = {build.render_dashboard(self.state, 110, ansi=True, terminal_rows=40, frame=frame)
                  for frame in range(0, 70, 2)}
        self.assertGreater(len(frames), 3)
        for styled in frames:
            plain = "\n".join(build._ANSI_SEQUENCE.sub("", line.replace("\x1b[0m", ""))
                              for line in styled.splitlines())
            self.assertEqual(plain, still)
        whole = build.render_dashboard(self.state, 110, ansi=False).splitlines()
        revealed = build.render_dashboard(self.state, 110, ansi=False, reveal=4).split("\n")
        self.assertEqual(len(revealed), len(whole))
        self.assertEqual(revealed[:4], whole[:4])
        self.assertFalse(any(revealed[4:]))

    def run_loop(self, events, environment=None):
        terminal = unittest.mock.Mock()
        terminal.windows_input = object()
        terminal.read_event.side_effect = events
        with patch.object(build, "DashboardTerminal", return_value=terminal), \
             patch.object(build, "refresh_dashboard_context"), \
             patch.object(build, "dashboard_terminal_size", return_value=build.os.terminal_size((100, 30))), \
             patch.object(build.sys.stdin, "isatty", return_value=True), \
             patch.dict(build.os.environ, environment or {}), \
             contextlib.redirect_stdout(io.StringIO()) as output:
            with patch.object(build.sys.stdout, "isatty", return_value=True):
                self.assertEqual(build.run_dashboard(), 0)
        return terminal, output.getvalue()

    def test_ticks_unroll_the_menu_and_repaint_in_place(self):
        terminal, output = self.run_loop(["tick"] * 12 + ["quit"], {"ILLUMO_NO_ANIMATION": ""})
        self.assertEqual(output.count(build.ANSI_CLEAR), 1)
        self.assertGreaterEqual(output.count(build.ANSI_HOME + ""), 10)
        self.assertEqual(terminal.read_event.call_args.kwargs["timeout"], build.DASHBOARD_TICK_SECONDS)
        # Any input ends the unroll immediately.
        _terminal, output = self.run_loop(["down", "quit"], {"ILLUMO_NO_ANIMATION": ""})
        self.assertIn("Build everything", output.split(build.ANSI_CLEAR)[-1])

    def test_no_animation_environment_blocks_on_input(self):
        terminal, output = self.run_loop(["quit"], {"ILLUMO_NO_ANIMATION": "1"})
        self.assertIsNone(terminal.read_event.call_args.kwargs["timeout"])
        self.assertIn("Build everything", output)

    @unittest.skipUnless(sys.platform == "win32", "Windows console ABI")
    def test_native_reader_times_out_into_ticks(self):
        import ctypes
        api = unittest.mock.Mock()
        api.GetStdHandle.side_effect = [10, 11]
        api.GetConsoleMode.return_value = 1
        api.WaitForSingleObject.return_value = 0x102
        with patch.object(ctypes, "WinDLL", return_value=api):
            reader = build.WindowsDashboardInput()
        self.assertEqual(reader.read_event(timeout=0.05), "tick")
        api.ReadConsoleInputW.assert_not_called()

    def test_progress_streak_sheen_and_finale(self):
        glyphs = build.UNICODE_GLYPHS
        forward = build.activity_bar(0.25, 20, glyphs)
        backward = build.activity_bar(1.5, 20, glyphs)  # Step 18 of a 15-cell travel: returning.
        for bar in (forward, backward):
            self.assertEqual(sum(len(text) for text, _style in bar), 20)
        head = f"\x1b[38;5;{build.COMET_COLORS[-1]}m"
        streak = [style for text, style in forward if text == glyphs["bar_fill"]]
        self.assertEqual(streak[-1], head)
        self.assertEqual([s for t, s in backward if t == glyphs["bar_fill"]][0], head)
        lit = build.progress_bar(0.5, 20, glyphs, shine=4)
        self.assertEqual([style for _text, style in lit[3:6]], [build.ANSI_SHINE] * 3)
        progress = build.DashboardProgress("Build", "Release", self.root / "run.log", started=0.0)
        progress.consume("[5/10] Building", 1)
        with patch.object(build, "motion_enabled", return_value=True):
            moving = {build.render_dashboard_progress(progress, 100, 24, now=t, ansi=True) for t in (2, 2.2, 2.4)}
            self.assertEqual(len(moving), 3)
            progress.returncode = 0
            progress.finish(10.0)
            flash = build.render_dashboard_progress(progress, 100, 24, now=10.05, ansi=True)
            steady = build.render_dashboard_progress(progress, 100, 24, now=10.2, ansi=True)
            after = build.render_dashboard_progress(progress, 100, 24, now=12, ansi=True)
        self.assertIn(build.PROGRESS_BADGE_FLASHES["SUCCEEDED"], flash)
        self.assertNotIn(build.PROGRESS_BADGE_FLASHES["SUCCEEDED"], steady)
        self.assertNotIn(build.PROGRESS_BADGE_FLASHES["SUCCEEDED"], after)
        with patch.object(build, "motion_enabled", return_value=False), \
             patch.object(build, "paint_dashboard_progress") as paint:
            build.play_progress_finale(progress)
        paint.assert_not_called()

    def test_finished_runs_with_tool_totals_still_summarize(self):
        progress = build.DashboardProgress("Build", "Release", self.root / "run.log", started=0.0)
        progress.previous_elapsed = 10.0
        progress.consume('> "cmake" --build build', 0)
        progress.consume("[980/980] Linking", 7)
        progress.returncode = 0
        progress.finish(8.0)
        rendered = build.render_dashboard_progress(progress, 110, 30, now=20, ansi=False)
        self.assertIn("Action finished in 8.0s", rendered)
        self.assertIn("980/980 steps", rendered)
        self.assertIn("20% faster than the last run", rendered)
        self.assertNotIn("tool-reported", rendered)
        progress.returncode, progress.completed = 2, 490
        rendered = build.render_dashboard_progress(progress, 110, 30, now=20, ansi=False)
        self.assertIn("490/980 steps", rendered)
        self.assertNotIn("faster", rendered)

    def test_palette_cursor_blinks_on_ticks(self):
        terminal = unittest.mock.Mock()
        terminal.read_event.side_effect = ["tick", "tick", build.DashboardTextEvent("x"), "escape"]
        with patch.object(build, "motion_enabled", return_value=True), \
             patch.object(build.shutil, "get_terminal_size", return_value=build.os.terminal_size((100, 32))), \
             patch.object(build, "palette_rows", wraps=build.palette_rows) as rows, \
             contextlib.redirect_stdout(io.StringIO()) as output:
            self.assertIsNone(build.run_command_palette(self.state, terminal))
        self.assertEqual(terminal.read_event.call_args.kwargs["timeout"], 0.5)
        self.assertEqual(rows.call_count, 2)  # Blinks repaint without re-ranking.
        self.assertEqual(output.getvalue().count(build.ANSI_CLEAR), 2)

    def test_run_history_label(self):
        run = {"log": str(self.root / "build-1.log"), "status": "failed", "action": "build",
               "identity": {"configuration": "Debug"}, "elapsed": 75.0, "started": 0.0}
        label = build.run_history_label(run, build.ASCII_GLYPHS, now=7200.0)
        self.assertTrue(label.startswith("x failed    build      Debug"), label)
        self.assertIn("1m 15s", label)
        self.assertIn("2h ago", label)
        self.assertIn("legacy log", build.run_history_label({"log": "old.log"}, build.ASCII_GLYPHS))


if __name__ == "__main__":
    unittest.main()
