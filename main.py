from __future__ import annotations

from PyQt5.QtWidgets import QApplication
from ecg_analysis import ECGQtLauncher
import sys

def main() -> int:
    app = QApplication(sys.argv)
    # Exit the application when the last window (launcher or viewer) is closed
    app.setQuitOnLastWindowClosed(True)
    win = ECGQtLauncher()
    win.show()
    return app.exec_()

if __name__ == "__main__":
	raise SystemExit(main())