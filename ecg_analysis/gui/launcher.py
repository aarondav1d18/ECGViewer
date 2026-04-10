# ecg_analysis/gui/launcher.py
from __future__ import annotations

from PyQt5.QtWidgets import QMainWindow, QShortcut, QVBoxLayout, QDialog, QLabel, QTextEdit
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

        # Tech glossary 
        self.tech_glossary_button.clicked.connect(self.open_tech_glossary)

        # Keyboard shortcut: Ctrl+O for Open
        shortcut_open = QShortcut(QKeySequence("Ctrl+O"), self)
        shortcut_open.activated.connect(self.on_browse_file)

    """Tech glossary page"""

    def open_tech_glossary(self):
        dlg = QDialog(self)
        dlg.setWindowTitle("Tech Glossary")
        dlg.resize(600, 400)

        layout = QVBoxLayout(dlg)

        text = QTextEdit()
        text.setReadOnly(True)

        text.setHtml("""
        <h2>Tech Glossary</h2><br>

        <b>The following contains a description of external libraries used:</b><br><br>

        <b>MatPlotLib</b><br>
        Used for plotting the data and visualisings the data / algorithms. It is an open-source plotting framework.<br><br>

        <b>NumPy</b><br>
        Used for the algorithms and processing of large data. It is a Python library used for scientific calculation.<br><br>
        
        <b>Pandas</b><br>
        Used to speed up the reading of the data as these files can be quite large. It is an open-source Python library designed for data manipulation and analysis.<br><br>
                     
        <b>SciPy</b><br>
        Used mostly for wave detection and algorithms to help with the algorihtms and detection of key points. It is open-source Python library designed for scientific and technical computing.<br><br>
                     
        <b>PyBinds11</b><br>
        Used to compile the C++ code into a usable python libary.
        """)

        layout.addWidget(text)

        dlg.resize(600, 400)
        dlg.exec_()
