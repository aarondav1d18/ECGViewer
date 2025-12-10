from __future__ import annotations
from dataclasses import dataclass
from typing import Optional, Tuple

from PyQt5.QtCore import QObject, pyqtSignal
from ECGViewer import parse_ecg_file_cpp as parse_ecg_file_cpp
from ecg_analysis import detrend_and_filter, detect_artifacts, clean_with_noise, detect_fiducials

'''
This class was implemented to handle ECG file processing in a background thread to prevent GUI 
freezing. It defines an ECGWorker that processes the ECG file, detrends and filters the signal,
and emits progress updates, completion signals, or error messages as needed.
Again this is just due to an issue I had with loading really large ECG files where it would make the
GUI freeze and then my os would think the application was not responding and that pop-up would 
appear.
'''

@dataclass
class ECGJobConfig:
    file_path: str
    window: float
    ylim: Optional[Tuple[float, float]]
    hide_artifacts: bool
    bandpass: bool = False


class ECGWorker(QObject):
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
        """Runs in background thread. No Qt widgets here."""
        try:
            j = self.job

            self.progress.emit("Parsing ECG file…", 5)
            t, v_raw, fs, meta = parse_ecg_file_cpp(j.file_path)
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
