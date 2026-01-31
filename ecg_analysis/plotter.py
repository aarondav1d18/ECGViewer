# ecg_analysis/plotter.py
from __future__ import annotations

from dataclasses import dataclass
from typing import Optional, Tuple
import numpy as np


@dataclass
class ViewerConfig:
    """
    Shared configuration for ECG viewer front-ends.

    Attributes:
        window_s: Window width (seconds) shown in the viewer.
        ylim: Optional fixed y-axis limits as (ymin, ymax). If None, viewer chooses defaults.
        downsample_full: Max number of samples (approx) used for initial draw (mpl).
        downsample_window: Max number of samples (approx) used for window draw (mpl).
        hide_artifacts: Whether to hide artifact regions/markers if supported by the backend.
    """
    window_s: float = 10.0
    ylim: Optional[Tuple[float, float]] = (-0.1, 0.15)
    downsample_full: int = 50_000
    downsample_window: int = 20_000
    hide_artifacts: bool = False


def _cpp_available() -> bool:
    """Return True if the optional C++/Qt ECG viewer extension can be imported."""
    try:
        from ECGViewer import show_ecg_viewer
        return True
    except Exception:
        return False


class ECGViewer:
    """Backend-selecting ECG viewer wrapper.

    Chooses the C++/Qt implementation when available, otherwise falls back to the
    Matplotlib implementation. The public API is a small compatibility layer that
    forwards supported methods to the chosen backend.

    Args:
        t: Time array (seconds).
        v: ECG voltage array.
        fs: Sampling frequency (Hz).
        cfg: Optional viewer configuration; defaults to `ViewerConfig()`.
        file_prefix: Optional prefix used by the C++ backend for exports/saves.
    """

    def __init__(
        self,
        t: np.ndarray,
        v: np.ndarray,
        fs: float,
        cfg: ViewerConfig | None = None,
        file_prefix: str | None = None,
    ) -> None:
        cfg = cfg or ViewerConfig()

        if _cpp_available():
            from ecg_analysis import ECGViewerCPP
            self._impl = ECGViewerCPP(t, v, fs, cfg, file_prefix=file_prefix)
        else:
            from ecg_analysis import ECGViewerMPL
            self._impl = ECGViewerMPL(t, v, fs, cfg)

    def show(self) -> None:
        return self._impl.show()

    def jump_to(self, t: float) -> None:
        """Scroll/jump the view to a given time (seconds), if supported by the backend."""
        if hasattr(self._impl, "jump_to"):
            return self._impl.jump_to(t)

    def set_ylim(self, ymin: float, ymax: float) -> None:
        if hasattr(self._impl, "set_ylim"):
            return self._impl.set_ylim(ymin, ymax)

    def toggle_overlay(self) -> None:
        if hasattr(self._impl, "toggle_overlay"):
            return self._impl.toggle_overlay()
