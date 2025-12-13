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
                "The viewer accepts ECG text/CSV exports from LabChart-style tools.",
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
                "Drag a marker label/line to move it left/right.",
                "Hover a marker and press Delete/Backspace to remove it.",
                "Use the Manual keypoints tab to insert a new point at the centre of the current view.",
            ]
        )

        add_heading("Notes Overview")
        add_bullets(
            [
                "Notes come in two types:",
                "Point notes: a single time marker (duration = 0).",
                "Region notes: a time span (duration > 0).",
                "Notes appear on the plot as labelled markers (point) or shaded regions (region).",
            ]
        )

        add_heading("Creating Notes")
        add_bullets(
            [
                "Use the Notes… button to open the Notes Manager and create/edit/delete notes.",
                "You can also create region notes directly on the plot:",
                "Hold Shift and click-drag on empty space to create a region note.",
                "Release the mouse to finish; the editor may open after creation depending on your build.",
            ]
        )

        add_heading("Editing Notes (Plot)")
        add_bullets(
            [
                "Double-click a note (marker/label/region) to open the note editor.",
                "Drag a note to move it in time.",
                "Hover a note and press Delete/Backspace to delete it.",
            ]
        )

        add_heading("Editing Region Notes (Shift + Drag)")
        add_bullets(
            [
                "Hold Shift and click a region note to modify it:",
                "Near the left edge: drag to resize the start time.",
                "Near the right edge: drag to resize the end time.",
                "Away from edges: drag to move the entire region.",
                "Edge detection uses a small pixel tolerance, so aim near the boundary.",
            ]
        )

        add_heading("Notes Manager (Notes…)")
        add_bullets(
            [
                "Search filters notes by tag and detail text.",
                "Double-click a note in the list to jump the view to that time (and optionally edit).",
                "Use New/Edit/Delete buttons to manage notes.",
            ]
        )

        add_heading("Saving and Loading")
        add_bullets(
            [
                "Notes can be saved to JSON and loaded again later.",
                "Region notes store both start time and duration.",
                "The Save button also exports ECG data and saves notes to the default data folder (if notes exist).",
                "Loading notes may warn you if the file prefix does not match the current ECG file.",
            ]
        )

        add_heading("Tips")
        add_bullets(
            [
                "If interactions feel “stuck”, toggle Rect Zoom off and try Reset View.",
                "Region notes are clamped to the signal duration (start >= 0, end <= total length).",
                "Very small regions may be treated as point notes depending on your minimum duration threshold.",
            ]
        )

        ht.append("Tip: if the view becomes confusing, press Reset View in the viewer.\n")

