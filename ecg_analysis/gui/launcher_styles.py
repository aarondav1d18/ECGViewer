# ecg_analysis/gui/launcher_styles.py
from __future__ import annotations


class LauncherStyleMixin:
    """Mixin that applies the Qt stylesheet for the launcher."""

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
