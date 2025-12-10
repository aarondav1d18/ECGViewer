from __future__ import annotations

import os
from typing import Callable, Optional, Tuple
from .worker import ECGWorker, ECGJobConfig  # or wherever you put it
from ecg_analysis import ViewerConfig, ECGViewer
from PyQt5.QtCore import Qt, QThread, QFileInfo
from PyQt5.QtWidgets import (
    QCheckBox,
    QDoubleSpinBox,
    QFormLayout,
    QFrame,
    QGridLayout,
    QGroupBox,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QMainWindow,
    QMessageBox,
    QPushButton,
    QSizePolicy,
    QStatusBar,
    QTextEdit,
    QVBoxLayout,
    QWidget,
    QFileDialog,
    QSpacerItem,
    QApplication,
    QProgressDialog
)


class ECGQtLauncher(QMainWindow):
    """
    Qt-based launcher that replaces the old Tk ECGGuiApp.

    Collects:
      - file_path (.txt / .csv)
      - window length
      - optional y-limits
      - show/hide original ECG with artefacts

    Does NOT run the pipeline itself. main.py reads .result and calls run_ecg_viewer.
    """

    def __init__(self, run_callback: Callable,  parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self._current_thread = None  # to keep a reference
        self.run_callback = run_callback
        self.setWindowTitle("ECG Viewer Launcher")
        self.resize(940, 560)

        self._accepted: bool = False
        self._result: dict | None = None

        central = QWidget(self)
        self.setCentralWidget(central)

        root_layout = QGridLayout(central)
        root_layout.setContentsMargins(16, 16, 12, 8)
        root_layout.setHorizontalSpacing(18)
        root_layout.setVerticalSpacing(10)

        # ------------------------------------------------------------------ #
        # LEFT: header + settings card + buttons
        # ------------------------------------------------------------------ #
        left_layout = QVBoxLayout()
        left_layout.setSpacing(12)

        # Header
        header_box = QVBoxLayout()
        title = QLabel("ECG Viewer")
        t_font = title.font()
        t_font.setPointSize(t_font.pointSize() + 6)
        t_font.setBold(True)
        title.setFont(t_font)

        subtitle = QLabel("Load a local ECG recording and configure how it is displayed.")
        subtitle.setStyleSheet("color: #666666;")

        header_box.addWidget(title)
        header_box.addWidget(subtitle)
        header_box.addSpacing(2)

        left_layout.addLayout(header_box)

        # Settings "card"
        card = QFrame(central)
        card.setObjectName("card")
        card.setFrameShape(QFrame.NoFrame)
        card_layout = QVBoxLayout(card)
        card_layout.setContentsMargins(14, 14, 14, 14)
        card_layout.setSpacing(12)

        # File selection group
        file_group = QGroupBox("Input ECG file", card)
        file_group_layout = QVBoxLayout(file_group)
        file_group_layout.setContentsMargins(10, 8, 10, 8)
        file_group_layout.setSpacing(6)

        file_row = QHBoxLayout()
        file_row.setSpacing(6)

        file_label = QLabel("File:")
        self.file_edit = QLineEdit(file_group)
        self.file_edit.setPlaceholderText("Select a .txt or .csv ECG file...")
        browse_btn = QPushButton("Browse…", file_group)

        file_row.addWidget(file_label)
        file_row.addWidget(self.file_edit, 1)
        file_row.addWidget(browse_btn)

        file_group_layout.addLayout(file_row)

        # Tiny helper under file input
        self.file_hint = QLabel("Only text or CSV ECG exports are supported.")
        self.file_hint.setStyleSheet("color: #888888; font-size: 10px;")
        file_group_layout.addWidget(self.file_hint)

        # Label that shows just the base name nicely
        self.file_name_label = QLabel("")
        self.file_name_label.setStyleSheet("color: #555555; font-style: italic; font-size: 10px;")
        file_group_layout.addWidget(self.file_name_label)

        card_layout.addWidget(file_group)

        # Viewer settings group
        settings_group = QGroupBox("Viewer settings", card)
        settings_layout = QFormLayout(settings_group)
        settings_layout.setContentsMargins(10, 8, 10, 10)
        settings_layout.setSpacing(6)
        settings_layout.setLabelAlignment(Qt.AlignLeft | Qt.AlignVCenter)

        # Window length
        window_row = QHBoxLayout()
        self.window_spin = QDoubleSpinBox(settings_group)
        self.window_spin.setRange(0.05, 10_000.0)
        self.window_spin.setDecimals(3)
        self.window_spin.setValue(0.4)
        self.window_spin.setSingleStep(0.1)
        self.window_spin.setSuffix("  s")
        window_row.addWidget(self.window_spin)
        window_row.addStretch(1)

        settings_layout.addRow("Window length:", window_row)

        window_hint = QLabel("Typical values are 0.3–1.0 s for detailed inspection.")
        window_hint.setStyleSheet("color: #888888; font-size: 10px; margin-left: 2px;")
        settings_layout.addRow("", window_hint)

        # Y-limits
        y_widget = QWidget(settings_group)
        y_layout = QHBoxLayout(y_widget)
        y_layout.setContentsMargins(0, 0, 0, 0)
        y_layout.setSpacing(6)

        self.ymin_edit = QLineEdit(y_widget)
        self.ymin_edit.setPlaceholderText("Min (optional)")
        self.ymax_edit = QLineEdit(y_widget)
        self.ymax_edit.setPlaceholderText("Max (optional)")
        self.ymin_edit.setMaximumWidth(120)
        self.ymax_edit.setMaximumWidth(120)

        y_layout.addWidget(self.ymin_edit)
        y_layout.addWidget(self.ymax_edit)
        y_layout.addStretch(1)

        settings_layout.addRow("Y-limits:", y_widget)

        y_hint = QLabel("Leave blank for automatic scaling; if used, provide both Min and Max.")
        y_hint.setStyleSheet("color: #888888; font-size: 10px; margin-left: 2px;")
        settings_layout.addRow("", y_hint)

        # Show original ECG with artefacts
        self.show_artifacts_check = QCheckBox("Show original ECG with artefacts", settings_group)
        self.show_artifacts_check.setChecked(True)
        settings_layout.addRow("", self.show_artifacts_check)

        card_layout.addWidget(settings_group)
        card_layout.addStretch(1)

        left_layout.addWidget(card)

        # Bottom buttons
        button_row = QHBoxLayout()
        button_row.setSpacing(8)
        button_row.addStretch(1)

        self.run_button = QPushButton("Run viewer")
        self.run_button.setDefault(True)
        self.run_button.setEnabled(False)
        self.run_button.setObjectName("primaryButton")
        self.run_button.setToolTip("Run the ECG viewer with the selected file and settings.")

        close_button = QPushButton("Close")
        close_button.setToolTip("Close the launcher without opening the viewer.")

        button_row.addWidget(self.run_button)
        button_row.addWidget(close_button)

        left_layout.addLayout(button_row)

        # ------------------------------------------------------------------ #
        # RIGHT: help panel
        # ------------------------------------------------------------------ #
        right_layout = QVBoxLayout()
        right_layout.setSpacing(8)

        help_title = QLabel("How to use the ECG viewer")
        h_font = help_title.font()
        h_font.setBold(True)
        help_title.setFont(h_font)

        help_sub = QLabel("Short guide to navigation, zooming, keypoints and notes.")
        help_sub.setStyleSheet("color: #666666;")

        right_layout.addWidget(help_title)
        right_layout.addWidget(help_sub)

        help_frame = QFrame(central)
        help_frame.setObjectName("helpCard")
        help_frame.setFrameShape(QFrame.NoFrame)
        help_layout = QVBoxLayout(help_frame)
        help_layout.setContentsMargins(12, 10, 12, 10)
        help_layout.setSpacing(6)

        self.help_text = QTextEdit(help_frame)
        self.help_text.setReadOnly(True)
        self.help_text.setLineWrapMode(QTextEdit.WidgetWidth)
        self.help_text.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Expanding)

        help_layout.addWidget(self.help_text)
        right_layout.addWidget(help_frame, 1)

        # Put columns into grid
        root_layout.addLayout(left_layout, 0, 0)
        root_layout.addLayout(right_layout, 0, 1)
        root_layout.setColumnStretch(0, 3)
        root_layout.setColumnStretch(1, 4)

        # Status bar
        status = QStatusBar(self)
        self.setStatusBar(status)
        self.status_label = QLabel("Select an ECG file and click Run.")
        status.addWidget(self.status_label)

        # ------------------------------------------------------------------ #
        # Signals & wiring
        # ------------------------------------------------------------------ #
        browse_btn.clicked.connect(self.on_browse_file)
        self.file_edit.textChanged.connect(self.on_file_text_changed)
        self.run_button.clicked.connect(self.on_run_clicked)
        close_button.clicked.connect(self.close)
        self.file_edit.returnPressed.connect(self.on_run_clicked)

        # Keyboard shortcut: Ctrl+O to open file dialog
        self.shortcut_open = self.file_edit.shortcut = self.file_edit
        self.file_edit.setToolTip("Type a path or use Browse… (Ctrl+O) to select a file.")
        self.file_edit.parent().installEventFilter(self)

        # Fill help text and style
        self._populate_help_text()
        self._center_on_screen()
        self._apply_styles()

        self._thread: QThread | None = None
        self._worker: ECGWorker | None = None
        self._progress: QProgressDialog | None = None

    # ------------------------------------------------------------------ #
    # Public API (used by main.py)
    # ------------------------------------------------------------------ #
    @property
    def accepted(self) -> bool:
        return self._accepted

    @property
    def result(self) -> Optional[dict]:
        return self._result

    # ------------------------------------------------------------------ #
    # Appearance helpers
    # ------------------------------------------------------------------ #
    def _apply_styles(self) -> None:
        self.setStyleSheet(
            """
            QMainWindow {
                background-color: #f4f5f7;
            }
            QLabel {
                font-size: 11px;
            }
            QLineEdit, QDoubleSpinBox {
                background: #ffffff;
            }
            #card, #helpCard {
                background-color: #ffffff;
                border-radius: 8px;
                border: 1px solid #d0d0d0;
            }
            QGroupBox {
                font-weight: bold;
                border: none;
                margin-top: 4px;
            }
            QGroupBox::title {
                subcontrol-origin: margin;
                left: 0px;
                padding: 0px;
            }
            QPushButton {
                padding: 6px 14px;
                border-radius: 6px;
                border: 1px solid #b0b0b0;
                background-color: #ffffff;
            }
            QPushButton:hover {
                background-color: #f0f0f0;
            }
            QPushButton:pressed {
                background-color: #e0e0e0;
            }
            QPushButton#primaryButton, QPushButton:default {
                background-color: #2f80ed;
                color: #ffffff;
                border: 1px solid #2f80ed;
            }
            QPushButton#primaryButton:hover, QPushButton:default:hover {
                background-color: #2d74d3;
            }
            QPushButton#primaryButton:pressed {
                background-color: #255fb2;
            }
            QStatusBar {
                background-color: #ffffff;
            }
            """
        )

    def _center_on_screen(self) -> None:
        screen = self.screen()
        if screen is None:
            return
        geo = self.frameGeometry()
        center = screen.availableGeometry().center()
        geo.moveCenter(center)
        self.move(geo.topLeft())

    def _populate_help_text(self) -> None:
        def add_heading(title: str) -> None:
            self.help_text.append(f"<b>{title}</b>")

        def add_bullets(lines: list[str]) -> None:
            for line in lines:
                self.help_text.append(f"– {line}")
            self.help_text.append("")

        self.help_text.clear()

        add_heading("Inputs")
        add_bullets([
            "The viewer accepts ECG text/CSV exports from LabChart-style tools.",
            "Use one channel per file for best results.",
        ])

        add_heading("Navigation")
        add_bullets([
            "Use the slider at the bottom of the viewer to move through the ECG.",
            "Keyboard: Left/A = move left, Right/D = move right.",
            "Click and drag the ECG left/right to scroll.",
        ])

        add_heading("Zooming")
        add_bullets([
            "Use the mouse wheel to zoom in and out on the time axis.",
            "The Zoom In / Zoom Out buttons change how much ECG is visible.",
            "Rect Zoom lets you drag a box around an area to zoom into it.",
        ])

        add_heading("Viewing")
        add_bullets([
            "Reset View restores a standard time window and y-axis range.",
            "The cleaned ECG signal is shown by default.",
            "You can choose to show or hide the original ECG with artefacts.",
        ])

        add_heading("Key Points (P, Q, R, S, T)")
        add_bullets([
            "Coloured markers show the P, Q, R, S and T points on the trace.",
            "Markers can be dragged left/right; Delete/Backspace removes them.",
            "Use the Manual keypoints tab in the viewer to add new points.",
        ])

        add_heading("Notes")
        add_bullets([
            "Click the Notes… button in the viewer to open the Notes Manager.",
            "Notes are linked to specific times and appear as labelled markers.",
            "Notes can be saved to JSON and loaded again for the same ECG file.",
        ])

        self.help_text.append(
            "Tip: if the view becomes confusing, press Reset View in the viewer.\n"
        )

    # ------------------------------------------------------------------ #
    # Event handling / slots
    # ------------------------------------------------------------------ #
    def on_browse_file(self) -> None:
        path, _ = QFileDialog.getOpenFileName(
            self,
            "Select ECG text file",
            "",
            "ECG files (*.txt *.csv);;All files (*)",
        )
        if path:
            self.file_edit.setText(path)

    def on_file_text_changed(self, text: str) -> None:
        text = text.strip()
        # Update base name label for long paths
        if text:
            base = os.path.basename(text)
            self.file_name_label.setText(f"Selected: {base}")
        else:
            self.file_name_label.setText("")

        if text:
            self.run_button.setEnabled(True)
            self.status_label.setText("Ready to run.")
        else:
            self.run_button.setEnabled(False)
            self.status_label.setText("Select an ECG file.")

    def on_run_clicked(self) -> None:
        file_path = self.file_edit.text().strip()

        if not file_path:
            QMessageBox.critical(self, "Missing file", "Please select an ECG file.")
            return

        ext = os.path.splitext(file_path)[1].lower()
        if ext not in (".txt", ".csv"):
            QMessageBox.critical(
                self,
                "Invalid file",
                "Please select a .txt or .csv file.",
            )
            return

        if not os.path.isfile(file_path):
            QMessageBox.critical(self, "Missing file", "File does not exist on disk.")
            return

        # Window length
        window_val = float(self.window_spin.value())

        # Y limits
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

        # If something is already running, don't spawn another
        if self._thread is not None:
            QMessageBox.warning(
                self,
                "Already processing",
                "An ECG file is already being processed. Please wait.",
            )
            return

        # --- build job config for worker ---
        job = ECGJobConfig(
            file_path=file_path,
            window=window_val,
            ylim=ylim,
            hide_artifacts=hide_artifacts,
            bandpass=False,
        )

        # --- UI busy state ---
        self.status_label.setText("Processing ECG…")
        self.run_button.setEnabled(False)
        self.file_edit.setEnabled(False)
        QApplication.setOverrideCursor(Qt.WaitCursor)

        # --- progress dialog ---
        prog = QProgressDialog("Preparing ECG viewer…", "Cancel", 0, 100, self)
        prog.setWindowTitle("Processing ECG")
        prog.setWindowModality(Qt.WindowModal)
        prog.setMinimumDuration(0)
        prog.setAutoClose(False)
        prog.setAutoReset(False)
        prog.setValue(0)

        self._progress = prog

        # --- worker + thread setup ---
        thread = QThread(self)
        worker = ECGWorker(job)
        worker.moveToThread(thread)

        # connections: worker -> GUI
        worker.progress.connect(self._on_worker_progress)
        worker.error.connect(self._on_worker_error)
        worker.finished.connect(self._on_worker_finished)

        # thread lifecycle
        worker.error.connect(thread.quit)
        worker.finished.connect(thread.quit)
        thread.finished.connect(worker.deleteLater)
        thread.finished.connect(self._on_thread_finished)

        # start signal
        thread.started.connect(worker.run)

        # cancel from dialog
        prog.canceled.connect(self._on_progress_canceled)

        # stash refs so we can cancel later
        self._thread = thread
        self._worker = worker

        # go
        thread.start()

    def _on_thread_finished(self) -> None:
        # Thread object is finished; clear pointer
        self._thread = None
        self._worker = None
        QApplication.restoreOverrideCursor()
        # progress dialog cleanup handled in finished/error handlers
    
    def _on_worker_progress(self, message: str, percent: int) -> None:
        if self._progress is None:
            return
        self._progress.setLabelText(message)
        self._progress.setValue(percent)
        QApplication.processEvents()  # keep dialog responsive
    
    def _on_progress_canceled(self) -> None:
        if self._worker is not None:
            self._worker.request_cancel()
        if self._progress is not None:
            self._progress.setLabelText("Cancelling…")
    
    def _on_worker_error(self, msg: str) -> None:
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
        # We are back in the GUI thread.
        # Keep the progress dialog open while we build the viewer.

        if self._progress is not None:
            self._progress.setLabelText("Building ECG viewer…")
            self._progress.setValue(70)
            QApplication.processEvents()

        # Reset some UI bits after we're done building everything
        # but keep the busy cursor until the viewer is ready.
        self.status_label.setText("Opening viewer…")
        self.run_button.setEnabled(False)      # keep disabled until viewer ready
        self.file_edit.setEnabled(False)

        # Unpack the result from the worker
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

        # This is where the heavy stuff still happens (inside ECGViewer.__init__)
        # but now the progress dialog stays visible.
        viewer = ECGViewer(t, v, fs, cfg, file_prefix=file_prefix)

        # Give the user the feeling we actually finished
        if self._progress is not None:
            self._progress.setLabelText("Done.")
            self._progress.setValue(100)
            QApplication.processEvents()
            self._progress.close()
            self._progress = None

        QApplication.restoreOverrideCursor()

        # Re-enable UI (or close if you prefer one-shot)
        self.run_button.setEnabled(True)
        self.file_edit.setEnabled(True)

        self.status_label.setText("Viewer opened.")
        viewer.show()

        # Optional: auto-close launcher after viewer opens
        self.close()
