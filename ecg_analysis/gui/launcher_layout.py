# ecg_analysis/gui/launcher_layout.py
from __future__ import annotations

import os
from PyQt5.QtCore import Qt
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
    QPushButton,
    QSizePolicy,
    QStatusBar,
    QTextEdit,
    QVBoxLayout,
    QWidget,
)


class LauncherLayoutMixin:
    """
    Mixin that builds the UI for the ECGQtLauncher.

    Expects self to be a QMainWindow.
    Creates attributes:
      - file_edit, file_name_label, file_hint
      - window_spin, ymin_edit, ymax_edit, show_artifacts_check
      - run_button, status_label, help_text
    """

    def _build_layout(self) -> None:
        central = QWidget(self)
        self.setCentralWidget(central)

        root_layout = QGridLayout(central)
        root_layout.setContentsMargins(16, 16, 12, 8)
        root_layout.setHorizontalSpacing(18)
        root_layout.setVerticalSpacing(10)

        # LEFT column
        left_layout = QVBoxLayout()
        left_layout.setSpacing(12)

        # RIGHT column
        right_layout = QVBoxLayout()
        right_layout.setSpacing(8)

        # Header (left)
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

        # Card (left) 
        card = QFrame(central)
        card.setObjectName("card")
        card.setFrameShape(QFrame.NoFrame)
        card_layout = QVBoxLayout(card)
        card_layout.setContentsMargins(14, 14, 14, 14)
        card_layout.setSpacing(12)

        # File group
        file_group = QGroupBox("Input ECG file", card)
        file_group_layout = QVBoxLayout(file_group)
        file_group_layout.setContentsMargins(10, 8, 10, 8)
        file_group_layout.setSpacing(6)

        file_row = QHBoxLayout()
        file_row.setSpacing(6)

        file_label = QLabel("File:")
        self.file_edit = QLineEdit(file_group)
        self.file_edit.setPlaceholderText("Select a .txt ECG file...")
        self.browse_btn = QPushButton("Browse…", file_group)

        file_row.addWidget(file_label)
        file_row.addWidget(self.file_edit, 1)
        file_row.addWidget(self.browse_btn)

        file_group_layout.addLayout(file_row)

        self.file_hint = QLabel("Only text ECG exports are supported.")
        self.file_hint.setStyleSheet("color: #888888; font-size: 10px;")
        file_group_layout.addWidget(self.file_hint)

        self.file_name_label = QLabel("")
        self.file_name_label.setStyleSheet(
            "color: #555555; font-style: italic; font-size: 10px;"
        )
        file_group_layout.addWidget(self.file_name_label)

        card_layout.addWidget(file_group)

        # Viewer settings
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

        y_hint = QLabel(
            "Leave blank for automatic scaling; if used, provide both Min and Max."
        )
        y_hint.setStyleSheet("color: #888888; font-size: 10px; margin-left: 2px;")
        settings_layout.addRow("", y_hint)

        # Show artifacts checkbox
        self.show_artifacts_check = QCheckBox(
            "Show original ECG with artefacts", settings_group
        )
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

        self.close_button = QPushButton("Close")

        button_row.addWidget(self.run_button)
        button_row.addWidget(self.close_button)
        left_layout.addLayout(button_row)

        # Help panel (right) 
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

        # Put into root grid
        root_layout.addLayout(left_layout, 0, 0)
        root_layout.addLayout(right_layout, 0, 1)
        root_layout.setColumnStretch(0, 3)
        root_layout.setColumnStretch(1, 4)

        # Status bar
        status = QStatusBar(self)
        self.setStatusBar(status)
        self.status_label = QLabel("Select an ECG file and click Run.")
        status.addWidget(self.status_label)

    def _center_on_screen(self) -> None:
        screen = self.screen()
        if screen is None:
            return
        geo = self.frameGeometry()
        center = screen.availableGeometry().center()
        geo.moveCenter(center)
        self.move(geo.topLeft())
