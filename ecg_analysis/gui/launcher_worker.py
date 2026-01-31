# ecg_analysis/gui/launcher_worker.py
from __future__ import annotations

'''
This will handle the background worker that processes the ECG file and launches the viewer.
It includes file browsing, validation, and progress dialog management. Implemented due to 
an issue I had with loading really large ECG files where it would make the GUI freeze and then
my os would think the application was not responding and that pop-up would appear.
'''

import os
from typing import Optional, Tuple

from PyQt5.QtCore import Qt, QThread, QFileInfo
from PyQt5.QtCore import QTimer
from PyQt5.QtWidgets import (
    QApplication,
    QFileDialog,
    QMessageBox,
    QProgressDialog,
)

from .worker import ECGWorker, ECGJobConfig
from ecg_analysis.plotter import ViewerConfig, ECGViewer


class LauncherWorkerMixin:
    """
    Mixin providing file selection, validation, and worker-driven viewer launch.

    Responsibilities:
        - Wire basic UI signals (browse, run, close).
        - Validate user input (file path, extension, y-limits).
        - Run `ECGWorker` in a `QThread` with a `QProgressDialog`.
        - On success, create and show an `ECGViewer` configured from UI values.
    """

    _thread: QThread | None = None
    _worker: ECGWorker | None = None
    _progress: QProgressDialog | None = None
    hide_gui: bool = True

    # simple UI event handlers 
    def _connect_basic_signals(self) -> None:
        """Connect the mixin's UI controls to handlers."""
        self.browse_btn.clicked.connect(self.on_browse_file)
        self.file_edit.textChanged.connect(self.on_file_text_changed)
        self.run_button.clicked.connect(self.on_run_clicked)
        self.close_button.clicked.connect(self.close)
        self.file_edit.returnPressed.connect(self.on_run_clicked)

    def on_browse_file(self) -> None:
        """Open a file dialog and populate the file path input."""
        path, _ = QFileDialog.getOpenFileName(
            self,
            "Select ECG text file",
            "",
            "ECG files (*.txt);;All files (*)",
        )
        if path:
            self.file_edit.setText(path)

    def on_file_text_changed(self, text: str) -> None:
        """Update UI state based on whether a valid-looking path is present."""
        text = text.strip()
        if text:
            base = os.path.basename(text)
            self.file_name_label.setText(f"Selected: {base}")
            self.run_button.setEnabled(True)
            self.status_label.setText("Ready to run.")
        else:
            self.file_name_label.setText("")
            self.run_button.setEnabled(False)
            self.status_label.setText("Select an ECG file.")

    # run button + worker setup 
    def on_run_clicked(self) -> None:
        """Validate inputs, create an `ECGJobConfig`, and start the worker thread."""
        file_path = self.file_edit.text().strip()

        if not file_path:
            QMessageBox.critical(self, "Missing file", "Please select an ECG file.")
            return

        ext = os.path.splitext(file_path)[1].lower()
        if ext not in (".txt"):
            QMessageBox.critical(
                self,
                "Invalid file",
                "Please select a .txt file.",
            )
            return

        if not os.path.isfile(file_path):
            QMessageBox.critical(self, "Missing file", "File does not exist on disk.")
            return

        window_val = float(self.window_spin.value())

        ymin_str = self.ymin_edit.text().strip()
        ymax_str = self.ymax_edit.text().strip()

        ylim: Optional[Tuple[float, float]] = None
        if ymin_str or ymax_str:
            if not (ymin_str and ymax_str):
                QMessageBox.critical(
                    self,
                    "Invalid Y-limits",
                    "Provide both Min and Max values.",
                )
                return
            try:
                ymin = float(ymin_str)
                ymax = float(ymax_str)
            except ValueError:
                QMessageBox.critical(
                    self,
                    "Invalid Y-limits",
                    "Y-limits must be numbers.",
                )
                return
            ylim = (ymin, ymax)

        show_artifacts = self.show_artifacts_check.isChecked()
        hide_artifacts = not show_artifacts

        if self._thread is not None:
            QMessageBox.warning(
                self,
                "Already processing",
                "An ECG file is already being processed. Please wait.",
            )
            return

        job = ECGJobConfig(
            file_path=file_path,
            window=window_val,
            ylim=ylim,
            hide_artifacts=hide_artifacts,
            bandpass=False,
        )

        self.status_label.setText("Processing ECG…")
        self.run_button.setEnabled(False)
        self.file_edit.setEnabled(False)
        QApplication.setOverrideCursor(Qt.WaitCursor)

        prog = QProgressDialog("Preparing ECG viewer…", "Cancel", 0, 100, self)
        prog.setWindowTitle("Processing ECG")
        prog.setWindowModality(Qt.WindowModal)
        prog.setMinimumDuration(0)
        prog.setAutoClose(False)
        prog.setAutoReset(False)
        prog.setValue(0)

        self._progress = prog

        thread = QThread(self)
        worker = ECGWorker(job)
        worker.moveToThread(thread)

        worker.progress.connect(self._on_worker_progress)
        worker.error.connect(self._on_worker_error)
        worker.finished.connect(self._on_worker_finished)

        worker.error.connect(thread.quit)
        worker.finished.connect(thread.quit)
        thread.finished.connect(worker.deleteLater)
        thread.finished.connect(self._on_thread_finished)

        thread.started.connect(worker.run)
        prog.canceled.connect(self._on_progress_canceled)

        self._thread = thread
        self._worker = worker
        thread.start()

    # worker callbacks 
    def _on_thread_finished(self) -> None:
        """Cleanup references and restore cursor when the worker thread ends."""
        self._thread = None
        self._worker = None
        QApplication.restoreOverrideCursor()

    def _on_worker_progress(self, message: str, percent: int) -> None:
        """Update the progress dialog with a message and percentage."""
        if self._progress is None:
            return
        self._progress.setLabelText(message)
        self._progress.setValue(percent)
        QApplication.processEvents()

    def _on_progress_canceled(self) -> None:
        """Handle progress dialog cancellation by requesting worker cancel."""
        if self._worker is not None:
            self._worker.request_cancel()
        if self._progress is not None:
            self._progress.setLabelText("Cancelling…")

    def _on_worker_error(self, msg: str) -> None:
        """Handle worker failure: restore UI state and show an error dialog."""
        self.status_label.setText("Error while processing ECG.")
        self.run_button.setEnabled(True)
        self.file_edit.setEnabled(True)

        if self._progress is not None:
            self._progress.reset()
            self._progress.close()
            self._progress = None

        QApplication.restoreOverrideCursor()
        QMessageBox.critical(self, "Error running viewer", msg)

    def _on_worker_finished(self, result: dict) -> None:
        """Handle worker success: build and show the ECG viewer, then restore UI state."""
        if self._progress is not None:
            self._progress.setLabelText("Building ECG viewer…")
            self._progress.setValue(70)
            QApplication.processEvents()

        self.status_label.setText("Opening viewer…")
        self.run_button.setEnabled(False)
        self.file_edit.setEnabled(False)

        t = result["t"]
        v = result["v"]
        fs = result["fs"]
        window = result["window"]
        ylim = result["ylim"]
        hide_artifacts = result["hide_artifacts"]
        file_path = result["file_path"]

        file_prefix = QFileInfo(file_path).baseName()

        cfg = ViewerConfig(
            window_s=window,
            ylim=ylim,
            hide_artifacts=hide_artifacts,
        )

        viewer = ECGViewer(t, v, fs, cfg, file_prefix=file_prefix)
        if self._progress is not None:
            self._progress.setLabelText("Done.")
            self._progress.setValue(100)
            QApplication.processEvents()
            self._progress.close()
            self._progress = None

        QApplication.restoreOverrideCursor()

        self.run_button.setEnabled(True)
        self.file_edit.setEnabled(True)
        self.status_label.setText("Viewer opened.")
        if self.hide_gui:
            self.hide()
            QApplication.processEvents()
            QTimer.singleShot(0, self.close)
        viewer.show()