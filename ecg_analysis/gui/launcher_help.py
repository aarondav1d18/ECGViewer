# ecg_analysis/gui/launcher_help.py
from __future__ import annotations

''' 
This is only used to provide help for using. Will probably be removed in a later push
but is kept for now.
'''


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
                "The viewer accepts ECG text/CSV exports from LabChart-style tools.",
                "Use one channel per file for best results.",
            ]
        )

        add_heading("Navigation")
        add_bullets(
            [
                "Use the slider at the bottom of the viewer to move through the ECG.",
                "Keyboard: Left/A = move left, Right/D = move right.",
                "Click and drag the ECG left/right to scroll.",
            ]
        )

        add_heading("Zooming")
        add_bullets(
            [
                "Use the mouse wheel to zoom in and out on the time axis.",
                "The Zoom In / Zoom Out buttons change how much ECG is visible.",
                "Rect Zoom lets you drag a box around an area to zoom into it.",
            ]
        )

        add_heading("Viewing")
        add_bullets(
            [
                "Reset View restores a standard time window and y-axis range.",
                "The cleaned ECG signal is shown by default.",
                "You can choose to show or hide the original ECG with artefacts.",
            ]
        )

        add_heading("Key Points (P, Q, R, S, T)")
        add_bullets(
            [
                "Coloured markers show the P, Q, R, S and T points on the trace.",
                "Markers can be dragged left/right; Delete/Backspace removes them.",
                "Use the Manual keypoints tab in the viewer to add new points.",
            ]
        )

        add_heading("Notes")
        add_bullets(
            [
                "Click the Notes… button in the viewer to open the Notes Manager.",
                "Notes are linked to specific times and appear as labelled markers.",
                "Notes can be saved to JSON and loaded again for the same ECG file.",
            ]
        )

        ht.append(
            "Tip: if the view becomes confusing, press Reset View in the viewer.\n"
        )
