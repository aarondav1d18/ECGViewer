# ecg_analysis/gui/launcher.py
from __future__ import annotations

from PyQt5.QtWidgets import QMainWindow, QShortcut
from PyQt5.QtGui import QKeySequence

from .launcher_layout import LauncherLayoutMixin
from .launcher_help import LauncherHelpMixin
from .launcher_worker import LauncherWorkerMixin
from .launcher_styles import LauncherStyleMixin


class ECGQtLauncher(
    QMainWindow,
    LauncherLayoutMixin,
    LauncherHelpMixin,
    LauncherStyleMixin,
    LauncherWorkerMixin,
):
    """Final launcher window class, composed of several mixins."""

    def __init__(self, parent=None) -> None:
        super().__init__(parent)

        self.setWindowTitle("ECG Viewer Launcher")
        self.resize(940, 560)

        # Build UI skeleton
        self._build_layout()

        # Help & styles
        self._populate_help_text()
        self._apply_styles()
        self._center_on_screen()

        # Wire basic signals (browse/run/close)
        self._connect_basic_signals()

        # Keyboard shortcut: Ctrl+O for Open
        shortcut_open = QShortcut(QKeySequence("Ctrl+O"), self)
        shortcut_open.activated.connect(self.on_browse_file)
