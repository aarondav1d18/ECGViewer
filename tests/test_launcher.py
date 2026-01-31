"""GUI tests for the ECG Qt launcher / launcher_worker mixin.

These tests exercise the launcher widget's UI handlers without touching the filesystem
or starting real Qt threads:

- Verifies `on_file_text_changed()` updates UI state (labels + run button enablement).
- Verifies browse-button wiring via a patched `QFileDialog.getOpenFileName`.
- Verifies `on_run_clicked()` input validation paths (missing file, bad extension, missing on disk,
  invalid y-limits).
- Verifies the success path launches an `ECGViewer` with the expected `ViewerConfig`.
- Verifies worker error propagation shows the correct QMessageBox and restores UI state.

Threading is simulated with `_FakeThread` / `_FakeWorker` so tests stay deterministic and fast.
"""

import pytest
from PyQt5.QtCore import Qt, QObject, pyqtSignal

from ecg_analysis.gui.launcher import ECGQtLauncher


@pytest.fixture
def launcher(qtbot):
    w = ECGQtLauncher()
    qtbot.addWidget(w)
    w.hide_gui = False
    w.show()
    return w


def test_file_text_changed_enables_run_and_updates_status(launcher):
    launcher.on_file_text_changed("  /tmp/test.csv  ")

    assert launcher.run_button.isEnabled() is True
    assert launcher.status_label.text() == "Ready to run."
    assert "Selected: test.csv" in launcher.file_name_label.text()

    launcher.on_file_text_changed("   ")
    assert launcher.run_button.isEnabled() is False
    assert launcher.status_label.text() == "Select an ECG file."
    assert launcher.file_name_label.text() == ""


def test_browse_file_sets_line_edit(monkeypatch, launcher, qtbot):
    def fake_get_open_file_name(*args, **kwargs):
        return ("/tmp/example.txt", "ECG files (*.txt);;All files (*)")

    monkeypatch.setattr(
        "ecg_analysis.gui.launcher_worker.QFileDialog.getOpenFileName",
        fake_get_open_file_name,
    )

    qtbot.mouseClick(launcher.browse_btn, Qt.LeftButton)
    assert launcher.file_edit.text() == "/tmp/example.txt"


def test_run_clicked_missing_file_shows_error(monkeypatch, launcher):
    calls = []

    def fake_critical(parent, title, text):
        calls.append((title, text))

    monkeypatch.setattr(
        "ecg_analysis.gui.launcher_worker.QMessageBox.critical",
        fake_critical,
    )

    launcher.file_edit.setText("")
    launcher.on_run_clicked()

    assert calls == [("Missing file", "Please select an ECG file.")]


def test_run_clicked_invalid_extension_shows_error(monkeypatch, launcher):
    calls = []

    def fake_critical(parent, title, text):
        calls.append((title, text))

    monkeypatch.setattr(
        "ecg_analysis.gui.launcher_worker.QMessageBox.critical",
        fake_critical,
    )
    monkeypatch.setattr("ecg_analysis.gui.launcher_worker.os.path.isfile", lambda p: True)

    launcher.file_edit.setText("/tmp/not_ecg.json")
    launcher.on_run_clicked()

    assert calls == [("Invalid file", "Please select a .txt file.")]


def test_run_clicked_missing_on_disk_shows_error(monkeypatch, launcher):
    calls = []

    def fake_critical(parent, title, text):
        calls.append((title, text))

    monkeypatch.setattr(
        "ecg_analysis.gui.launcher_worker.QMessageBox.critical",
        fake_critical,
    )
    monkeypatch.setattr("ecg_analysis.gui.launcher_worker.os.path.isfile", lambda p: False)

    launcher.file_edit.setText("/tmp/ecg.txt")
    launcher.on_run_clicked()

    assert calls == [("Missing file", "File does not exist on disk.")]


def test_run_clicked_invalid_ylims_requires_both(monkeypatch, launcher):
    calls = []

    def fake_critical(parent, title, text):
        calls.append((title, text))

    monkeypatch.setattr(
        "ecg_analysis.gui.launcher_worker.QMessageBox.critical",
        fake_critical,
    )
    monkeypatch.setattr("ecg_analysis.gui.launcher_worker.os.path.isfile", lambda p: True)

    launcher.file_edit.setText("/tmp/ecg.txt")
    launcher.ymin_edit.setText("1.0")
    launcher.ymax_edit.setText("")
    launcher.on_run_clicked()

    assert calls == [("Invalid Y-limits", "Provide both Min and Max values.")]


def test_run_clicked_invalid_ylims_not_numbers(monkeypatch, launcher):
    calls = []

    def fake_critical(parent, title, text):
        calls.append((title, text))

    monkeypatch.setattr(
        "ecg_analysis.gui.launcher_worker.QMessageBox.critical",
        fake_critical,
    )
    monkeypatch.setattr("ecg_analysis.gui.launcher_worker.os.path.isfile", lambda p: True)

    launcher.file_edit.setText("/tmp/ecg.txt")
    launcher.ymin_edit.setText("nope")
    launcher.ymax_edit.setText("2.0")
    launcher.on_run_clicked()

    assert calls == [("Invalid Y-limits", "Y-limits must be numbers.")]


class _FakeThread(QObject):
    """Minimal QThread stand-in that synchronously emits started/finished signals."""
    started = pyqtSignal()
    finished = pyqtSignal()

    def __init__(self, parent=None):
        super().__init__(parent)
        self._quit_requested = False

    def start(self):
        self.started.emit()

    def quit(self):
        if self._quit_requested:
            return
        self._quit_requested = True
        self.finished.emit()


class _FakeWorker(QObject):
    """
    Minimal ECGWorker stand-in that emits either `finished` with a small result or `error` on cancel.
    """
    finished = pyqtSignal(object)
    error = pyqtSignal(str)
    progress = pyqtSignal(str, int)

    def __init__(self, job):
        super().__init__()
        self.job = job
        self._cancel = False

    def moveToThread(self, thread):
        return

    def deleteLater(self):
        return

    def request_cancel(self):
        self._cancel = True

    def run(self):
        if self._cancel:
            self.error.emit("Processing cancelled by user.")
            return

        result = {
            "t": [0.0, 0.01, 0.02],
            "v": [10.0, 20.0, 30.0],
            "fs": 100.0,
            "window": self.job.window,
            "ylim": self.job.ylim,
            "hide_artifacts": self.job.hide_artifacts,
            "file_path": self.job.file_path,
        }
        self.finished.emit(result)


def test_run_clicked_success_launches_viewer(monkeypatch, launcher):
    monkeypatch.setattr("ecg_analysis.gui.launcher_worker.os.path.isfile", lambda p: True)

    monkeypatch.setattr("ecg_analysis.gui.launcher_worker.QThread", _FakeThread)
    monkeypatch.setattr("ecg_analysis.gui.launcher_worker.ECGWorker", _FakeWorker)

    cfg_seen = {}

    class FakeViewerConfig:
        def __init__(self, window_s, ylim, hide_artifacts):
            cfg_seen["window_s"] = window_s
            cfg_seen["ylim"] = ylim
            cfg_seen["hide_artifacts"] = hide_artifacts

    viewer_seen = {"shown": 0}

    class FakeViewer:
        def __init__(self, t, v, fs, cfg, file_prefix=None):
            viewer_seen["t"] = t
            viewer_seen["v"] = v
            viewer_seen["fs"] = fs
            viewer_seen["cfg"] = cfg
            viewer_seen["file_prefix"] = file_prefix

        def show(self):
            viewer_seen["shown"] += 1

    monkeypatch.setattr("ecg_analysis.gui.launcher_worker.ViewerConfig", FakeViewerConfig)
    monkeypatch.setattr("ecg_analysis.gui.launcher_worker.ECGViewer", FakeViewer)

    launcher.file_edit.setText("/tmp/ecg.txt")
    launcher.window_spin.setValue(0.4)
    launcher.ymin_edit.setText("")
    launcher.ymax_edit.setText("")
    launcher.show_artifacts_check.setChecked(True)

    launcher.on_run_clicked()

    assert viewer_seen["shown"] == 1
    assert viewer_seen["v"] == [10.0, 20.0, 30.0]
    assert viewer_seen["file_prefix"] == "ecg"

    assert cfg_seen["window_s"] == pytest.approx(0.4)
    assert cfg_seen["ylim"] is None
    assert cfg_seen["hide_artifacts"] is False


def test_run_clicked_worker_error_shows_message(monkeypatch, launcher):
    calls = []

    def fake_critical(parent, title, text):
        calls.append((title, text))

    monkeypatch.setattr(
        "ecg_analysis.gui.launcher_worker.QMessageBox.critical",
        fake_critical,
    )
    monkeypatch.setattr("ecg_analysis.gui.launcher_worker.os.path.isfile", lambda p: True)
    monkeypatch.setattr("ecg_analysis.gui.launcher_worker.QThread", _FakeThread)

    class ErrorWorker(_FakeWorker):
        def run(self):
            self.error.emit("boom")

    monkeypatch.setattr("ecg_analysis.gui.launcher_worker.ECGWorker", ErrorWorker)

    launcher.file_edit.setText("/tmp/ecg.txt")
    launcher.on_run_clicked()

    assert calls == [("Error running viewer", "boom")]
    assert launcher.run_button.isEnabled() is True
    assert launcher.file_edit.isEnabled() is True
