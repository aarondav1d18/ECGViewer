"""Tests for ECGWorker and launcher worker integration logic.

These tests focus on the background processing and launcher-side callbacks used
when building and displaying the ECG viewer:

- Verifies `ECGWorker.run()` behavior on the happy path, including progress emission
  and the final result payload.
- Verifies error handling when parsing fails, filtering fails, or the sampling
  frequency cannot be determined.
- Verifies cooperative cancellation behavior during worker execution.
- Verifies launcher callbacks that react to worker progress, cancellation,
  completion, and failure.
- Verifies viewer construction and GUI state restoration after successful runs.

Modal dialogs and real Qt threads are patched out so tests remain synchronous,
deterministic, and safe to run under pytest/pytest-qt.
"""

import pytest

from PyQt5.QtWidgets import QProgressDialog, QMessageBox
from PyQt5.QtCore import Qt
from ecg_analysis.gui.worker import ECGJobConfig, ECGWorker
from ecg_analysis.gui.launcher import ECGQtLauncher

@pytest.fixture
def launcher(qtbot):
    w = ECGQtLauncher()
    qtbot.addWidget(w)
    w.hide_gui = False
    w.show()
    return w


@pytest.fixture(autouse=True)
def _no_modal_message_boxes(monkeypatch):
    def _fake(*args, **kwargs):
        # Pretend the user clicked OK.
        return QMessageBox.Ok

    monkeypatch.setattr("PyQt5.QtWidgets.QMessageBox.critical", _fake, raising=True)
    monkeypatch.setattr("PyQt5.QtWidgets.QMessageBox.warning", _fake, raising=True)
    monkeypatch.setattr("PyQt5.QtWidgets.QMessageBox.information", _fake, raising=True)
    monkeypatch.setattr("PyQt5.QtWidgets.QMessageBox.question", _fake, raising=True)


def _make_job(tmp_path, **overrides):
    """Create a minimal ECGJobConfig backed by a temporary dummy ECG file.

    Args:
        tmp_path: pytest-provided temporary directory.
        overrides: Field overrides applied to the default job configuration.

    Returns:
        An `ECGJobConfig` instance suitable for worker tests.
    """
    p = tmp_path / "ecg.txt"
    p.write_text("dummy")
    base = dict(
        file_path=str(p),
        window=0.4,
        ylim=None,
        hide_artifacts=False,
        bandpass=False,
    )
    base.update(overrides)
    return ECGJobConfig(**base)


def test_worker_happy_path_emits_progress_and_finished(monkeypatch, qtbot, tmp_path):
    job = _make_job(tmp_path)
    w = ECGWorker(job)

    def fake_parse(path):
        assert path == job.file_path
        return [0.0, 0.01], [1.0, 2.0], 100.0, {"m": 1}

    def fake_filter(v_raw, fs, bandpass=False):
        assert v_raw == [1.0, 2.0]
        assert fs == 100.0
        assert bandpass is False
        return [10.0, 20.0]

    monkeypatch.setattr("ecg_analysis.gui.worker.parse_ecg_file", fake_parse)
    monkeypatch.setattr("ecg_analysis.gui.worker.detrend_and_filter", fake_filter)

    progress = []
    finished = []
    errors = []

    w.progress.connect(lambda msg, pct: progress.append((msg, pct)))
    w.finished.connect(lambda result: finished.append(result))
    w.error.connect(lambda msg: errors.append(msg))

    w.run()

    assert errors == []
    assert len(finished) == 1

    result = finished[0]
    assert result["t"] == [0.0, 0.01]
    assert result["v"] == [10.0, 20.0]
    assert result["fs"] == 100.0
    assert result["window"] == pytest.approx(0.4)
    assert result["ylim"] is None
    assert result["hide_artifacts"] is False
    assert result["file_path"] == job.file_path

    # Make sure progress is reasonably emitted (don’t overfit exact strings)
    assert progress[0] == ("Worker started", 0)
    assert any(pct == 5 for _, pct in progress)
    assert any(pct == 25 for _, pct in progress)
    assert progress[-1][1] == 100


def test_worker_fs_none_emits_error(monkeypatch, tmp_path):
    job = _make_job(tmp_path)
    w = ECGWorker(job)

    def fake_parse(path):
        return [0.0], [1.0], None, {}

    monkeypatch.setattr("ecg_analysis.gui.worker.parse_ecg_file", fake_parse)

    errors = []
    finished = []

    w.error.connect(lambda msg: errors.append(msg))
    w.finished.connect(lambda result: finished.append(result))

    w.run()

    assert finished == []
    assert len(errors) == 1
    assert "Could not determine sampling rate" in errors[0]


def test_worker_parse_raises_emits_error(monkeypatch, tmp_path):
    job = _make_job(tmp_path)
    w = ECGWorker(job)

    def fake_parse(path):
        raise RuntimeError("boom")

    monkeypatch.setattr("ecg_analysis.gui.worker.parse_ecg_file", fake_parse)

    errors = []
    w.error.connect(lambda msg: errors.append(msg))

    w.run()

    assert errors == ["boom"]


def test_worker_filter_raises_emits_error(monkeypatch, tmp_path):
    job = _make_job(tmp_path)
    w = ECGWorker(job)

    monkeypatch.setattr(
        "ecg_analysis.gui.worker.parse_ecg_file",
        lambda path: ([0.0], [1.0], 100.0, {}),
    )

    def fake_filter(v_raw, fs, bandpass=False):
        raise ValueError("bad filter")

    monkeypatch.setattr("ecg_analysis.gui.worker.detrend_and_filter", fake_filter)

    errors = []
    finished = []
    w.error.connect(lambda msg: errors.append(msg))
    w.finished.connect(lambda result: finished.append(result))

    w.run()

    assert finished == []
    assert errors == ["bad filter"]


def test_worker_cancel_after_parse_emits_cancel_error(monkeypatch, tmp_path):
    job = _make_job(tmp_path)
    w = ECGWorker(job)

    monkeypatch.setattr(
        "ecg_analysis.gui.worker.parse_ecg_file",
        lambda path: ([0.0], [1.0], 100.0, {}),
    )

    def fake_filter(v_raw, fs, bandpass=False):
        # request cancel right before returning to simulate user canceling mid-run
        w.request_cancel()
        return [2.0]

    monkeypatch.setattr("ecg_analysis.gui.worker.detrend_and_filter", fake_filter)

    errors = []
    finished = []
    w.error.connect(lambda msg: errors.append(msg))
    w.finished.connect(lambda result: finished.append(result))

    w.run()

    assert finished == []
    assert len(errors) == 1
    assert errors[0] == "Processing cancelled by user."



def test_on_worker_progress_updates_dialog(monkeypatch, launcher, qtbot):
    prog = QProgressDialog("x", "Cancel", 0, 100, launcher)
    prog.setMinimumDuration(0)
    launcher._progress = prog

    launcher._on_worker_progress("Parsing…", 12)

    assert launcher._progress.labelText() == "Parsing…"
    assert launcher._progress.value() == 12


def test_on_progress_canceled_requests_cancel(monkeypatch, launcher):
    class FakeWorker:
        def __init__(self):
            self.cancel_called = 0

        def request_cancel(self):
            self.cancel_called += 1

    launcher._worker = FakeWorker()

    prog = QProgressDialog("x", "Cancel", 0, 100, launcher)
    launcher._progress = prog

    launcher._on_progress_canceled()

    assert launcher._worker.cancel_called == 1
    assert launcher._progress.labelText() == "Cancelling…"


def test_on_thread_finished_resets_state(monkeypatch, launcher):
    launcher._thread = object()
    launcher._worker = object()

    launcher._on_thread_finished()

    assert launcher._thread is None
    assert launcher._worker is None


def test_on_worker_error_resets_ui_and_closes_progress(monkeypatch, launcher):
    calls = []

    def fake_critical(parent, title, text):
        calls.append((title, text))

    monkeypatch.setattr(
        "ecg_analysis.gui.launcher_worker.QMessageBox.critical",
        fake_critical,
    )
    monkeypatch.setattr(
        "ecg_analysis.gui.launcher_worker.QApplication.restoreOverrideCursor",
        lambda: None,
    )

    # simulate disabled state as if processing started
    launcher.status_label.setText("Processing ECG…")
    launcher.run_button.setEnabled(False)
    launcher.file_edit.setEnabled(False)

    prog = QProgressDialog("x", "Cancel", 0, 100, launcher)
    launcher._progress = prog

    launcher._on_worker_error("boom")

    assert launcher.status_label.text() == "Error while processing ECG."
    assert launcher.run_button.isEnabled() is True
    assert launcher.file_edit.isEnabled() is True
    assert launcher._progress is None
    assert calls == [("Error running viewer", "boom")]

def test_on_worker_finished_builds_and_shows_viewer(monkeypatch, launcher):
    monkeypatch.setattr(
        "ecg_analysis.gui.launcher_worker.QApplication.restoreOverrideCursor",
        lambda: None,
    )

    cfg_seen = {}
    viewer_seen = {"shown": 0}

    class FakeViewerConfig:
        def __init__(self, window_s, ylim, hide_artifacts):
            cfg_seen["window_s"] = window_s
            cfg_seen["ylim"] = ylim
            cfg_seen["hide_artifacts"] = hide_artifacts

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

    prog = QProgressDialog("x", "Cancel", 0, 100, launcher)
    launcher._progress = prog

    result = {
        "t": [0.0, 0.01],
        "v": [1.0, 2.0],
        "fs": 100.0,
        "window": 0.4,
        "ylim": (0.0, 3.0),
        "hide_artifacts": True,
        "file_path": "/tmp/ecg.txt",
    }

    launcher._on_worker_finished(result)

    assert viewer_seen["shown"] == 1
    assert viewer_seen["file_prefix"] == "ecg"
    assert cfg_seen["window_s"] == 0.4
    assert cfg_seen["ylim"] == (0.0, 3.0)
    assert cfg_seen["hide_artifacts"] is True

    assert launcher.status_label.text() == "Viewer opened."
    assert launcher.run_button.isEnabled() is True
    assert launcher.file_edit.isEnabled() is True
    assert launcher._progress is None

def test_on_worker_finished_hide_gui_closes_launcher(monkeypatch, launcher):
    monkeypatch.setattr(
        "ecg_analysis.gui.launcher_worker.QApplication.restoreOverrideCursor",
        lambda: None,
    )

    class FakeViewer:
        def __init__(self, *args, **kwargs):
            pass

        def show(self):
            return

    class FakeViewerConfig:
        def __init__(self, *args, **kwargs):
            pass

    monkeypatch.setattr("ecg_analysis.gui.launcher_worker.ECGViewer", FakeViewer)
    monkeypatch.setattr("ecg_analysis.gui.launcher_worker.ViewerConfig", FakeViewerConfig)

    hide_calls = {"n": 0}
    close_calls = {"n": 0}

    launcher.hide_gui = True
    monkeypatch.setattr(launcher, "hide", lambda: hide_calls.__setitem__("n", hide_calls["n"] + 1))
    monkeypatch.setattr(launcher, "close", lambda: close_calls.__setitem__("n", close_calls["n"] + 1))

    monkeypatch.setattr(
        "ecg_analysis.gui.launcher_worker.QTimer.singleShot",
        lambda _ms, fn: fn(),
    )

    result = {
        "t": [0.0],
        "v": [1.0],
        "fs": 100.0,
        "window": 0.4,
        "ylim": None,
        "hide_artifacts": False,
        "file_path": "/tmp/ecg.txt",
    }

    launcher._on_worker_finished(result)

    assert hide_calls["n"] == 1
    assert close_calls["n"] == 1


def test_run_clicked_while_processing_shows_warning(monkeypatch, launcher):
    calls = []

    def fake_warning(parent, title, text):
        calls.append((title, text))
        return 0

    monkeypatch.setattr(
        "PyQt5.QtWidgets.QMessageBox.warning",
        fake_warning,
        raising=True,
    )

    monkeypatch.setattr(
        "ecg_analysis.gui.launcher_worker.os.path.isfile",
        lambda p: True,
        raising=True,
    )

    launcher._thread = object()
    launcher.file_edit.setText("/tmp/ecg.txt")

    launcher.on_run_clicked()

    assert calls == [
        ("Already processing", "An ECG file is already being processed. Please wait.")
    ]
