from __future__ import annotations
from dataclasses import dataclass
from typing import Optional, Tuple

from PyQt5.QtCore import QObject, pyqtSignal
try:
    from ECGViewer import parse_ecg_file_cpp as parse_ecg_file
except ImportError:
    from ecg_analysis import parse_ecg_file
from ecg_analysis import detrend_and_filter, detect_artifacts, clean_with_noise, detect_fiducials


@dataclass
class ECGJobConfig:
    """
    Configuration for a background ECG processing job.

    Attributes:
        file_path: Path to the ECG text file to parse.
        window: Viewer window width (seconds) requested by the user/UI.
        ylim: Optional y-axis limits as (ymin, ymax).
        hide_artifacts: Whether the viewer should hide artifact regions/markers.
        bandpass: If True, apply a bandpass filter in preprocessing (if supported).
    """
    file_path: str
    window: float
    ylim: Optional[Tuple[float, float]]
    hide_artifacts: bool
    bandpass: bool = False


class ECGWorker(QObject):
    """Background worker that parses and preprocesses an ECG file.

    Intended to be run in a `QThread` to prevent GUI freezes when loading large files.
    Emits progress updates, a result dictionary on success, or an error string on failure.

    Signals:
        finished: Emits a result dict on success.
        error: Emits an error message string on failure (including user cancel).
        progress: Emits (message, percent) updates during processing.
    """
    finished = pyqtSignal(object) # emits result dict on success
    error = pyqtSignal(str) # emits error message
    progress = pyqtSignal(str, int) # (message, percentage)

    def __init__(self, job: ECGJobConfig) -> None:
        super().__init__()
        self.job = job
        self._cancel = False

    def request_cancel(self) -> None:
        self._cancel = True

    def _check_cancel(self):
        if self._cancel:
            raise RuntimeError("Processing cancelled by user.")

    def run(self) -> None:
        """Execute the job logic (intended to run in a background thread)."""
        try:
            j = self.job
            self.progress.emit("Worker started", 0)

            self.progress.emit("Parsing ECG file…", 5)
            t, v_raw, fs, meta = parse_ecg_file(j.file_path)
            self._check_cancel()

            if fs is None:
                raise RuntimeError(
                    "Could not determine sampling rate "
                    "(Interval missing and time column irregular)."
                )

            self.progress.emit("Detrending / filtering…", 25)
            v = detrend_and_filter(v_raw, fs, bandpass=j.bandpass)
            self._check_cancel()

            result = {
                "t": t,
                "v": v,
                "fs": fs,
                "window": j.window,
                "ylim": j.ylim,
                "hide_artifacts": j.hide_artifacts,
                "file_path": j.file_path,
            }
            self.progress.emit("Finished preprocessing.", 100)
            self.finished.emit(result)

        except Exception as e:
            # Any error, including cancel, lands here
            self.error.emit(str(e))
