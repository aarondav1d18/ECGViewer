#!/bin/python3
# main.py
from __future__ import annotations
import sys

from PyQt5.QtWidgets import QApplication
from ecg_analysis import ECGQtLauncher

def main() -> int:
    app = QApplication(sys.argv)
    win = ECGQtLauncher()
    win.show()
    return app.exec_()

if __name__ == "__main__":
    raise SystemExit(main())
