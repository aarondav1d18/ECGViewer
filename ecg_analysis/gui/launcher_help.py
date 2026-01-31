# ecg_analysis/gui/launcher_help.py
from __future__ import annotations

"""
Help text for the ECG Viewer UI.

This is user-facing documentation for:
- navigation / zoom
- fiducials (P,Q,R,S,T)
- notes (point + region)
- editing / dragging / resizing
- saving/loading
"""

class LauncherHelpMixin:
    """Mixin that fills self.help_text with documentation."""

    def _populate_help_text(self) -> None:
        ht = self.help_text

        def add_heading(title: str) -> None:
            ht.append(f"<b>{title}</b>")

        def add_bullets(lines: list[str]) -> None:
            for line in lines:
                ht.append(f"– {line}")
            ht.append("")

        ht.clear()

        add_heading("Inputs")
        add_bullets(
            [
                "The viewer accepts ECG text exports from LabChart-style tools.",
                "Use one channel per file for best results.",
            ]
        )

        add_heading("Navigation")
        add_bullets(
            [
                "Use the bottom slider to move through the ECG.",
                "Keyboard: Left arrow / A = move left, Right arrow / D = move right.",
                "Click and drag the ECG left/right to scroll (range drag).",
            ]
        )

        add_heading("Zooming")
        add_bullets(
            [
                "Zoom In / Zoom Out buttons change the visible window length.",
                "Rect Zoom: enable the toggle, then drag a rectangle to zoom into that region.",
                "While Rect Zoom is enabled, item dragging/editing is disabled.",
            ]
        )

        add_heading("Viewing")
        add_bullets(
            [
                "Reset View restores the original window length and y-axis range.",
                "The cleaned ECG signal is shown by default.",
                "If enabled, the original ECG (with artefacts) can also be shown.",
            ]
        )

        add_heading("Key Points (P, Q, R, S, T)")
        add_bullets(
            [
                "Coloured markers show the P, Q, R, S and T points on the trace.",
            ]
        )

        ht.append("Tip: if the view becomes confusing, press Reset View in the viewer.\n")

