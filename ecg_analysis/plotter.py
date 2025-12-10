# plotter.py
from __future__ import annotations

from dataclasses import dataclass
from typing import Optional, Tuple, List

import numpy as np
from PyQt5.QtWidgets import QApplication

from ecg_analysis import ( 
    detect_artifacts,
    clean_with_noise,
    detect_fiducials,
)
from ECGViewer import show_ecg_viewer


@dataclass
class ViewerConfig:
    window_s: float = 10.0
    ylim: Optional[Tuple[float, float]] = (-0.1, 0.15)
    downsample_full: int = 50_000   # TODO: Remove or implement
    downsample_window: int = 20_000 # TODO: Remove or implement
    hide_artifacts: bool = False  # TODO: Remove or implement


class ECGViewer:
    """
    Thin wrapper that matches the original ECGViewer API but uses the Qt viewer
    (ecg_qt_viewer.show_ecg_viewer) under the hood.

    Args:
        t_abs (ndarray): Absolute time values (s).
        v (ndarray): Voltage trace (already detrended/filtered).
        fs (float): Sampling frequency (Hz).
        cfg (ViewerConfig): Viewer configuration.
    """

    def __init__(self,
                 t_abs: np.ndarray,
                 v: np.ndarray,
                 fs: float,
                 cfg: ViewerConfig | None = None,
                 file_prefix: str = None) -> None:
        self.t_abs = np.asarray(t_abs, dtype=float)
        self.v_in = np.asarray(v, dtype=float)
        self.fs = float(fs)
        self.cfg = cfg or ViewerConfig()
        self.file_prefix = file_prefix

        # Time: make relative as in original viewer logic
        self.t0 = float(self.t_abs[0])
        self.t = self.t_abs - self.t0

        self.total_s = float(self.t[-1] - self.t[0])
        self.window_s = min(self.cfg.window_s, max(1.0, self.total_s))

        self.hide_artifacts = self.cfg.hide_artifacts
        self.v_plot = self.v_in

        # This will keep Qt alive during processing. I had issues where I could not click
        # "Cancel" in the progress dialog because the event loop as it was not being pumped.
        app = QApplication.instance()

        def pump():
            if app is not None:
                app.processEvents()

        # Artifacts and cleaned signal
        self.art_times = detect_artifacts(self.t, self.v_in, self.fs)
        pump()

        self.v_clean = clean_with_noise(self.t, self.v_plot, self.art_times, self.fs)
        pump()

        # Fiducials (beat features)
        self.beats = detect_fiducials(self.t, self.v_in, self.fs, art_times=self.art_times)
        pump()

        # Precompute arrays for Qt viewer
        self.art_mask = self._build_artifact_mask()
        pump()
        (
            self.P_times, self.P_vals,
            self.Q_times, self.Q_vals,
            self.R_times, self.R_vals,
            self.S_times, self.S_vals,
            self.T_times, self.T_vals,
        ) = self._beats_to_numpy_fiducials()
        pump()

    # ------------------------------------------------------------------ #
    # Public API
    # ------------------------------------------------------------------ #
    def show(self) -> None:
        """Display the ECG viewer window (Qt)."""
        show_ecg_viewer(
            self.t,            # time (relative)
            self.v_plot,       # original plotted signal (V or mV)
            self.v_clean,      # cleaned (noise replaced) signal
            self.art_mask,     # uint8 mask: 1 at artifact samples
            float(self.fs),
            self.window_s,
            self.cfg.ylim,
            self.hide_artifacts,
            self.P_times, self.P_vals,
            self.Q_times, self.Q_vals,
            self.R_times, self.R_vals,
            self.S_times, self.S_vals,
            self.T_times, self.T_vals,
            self.file_prefix,
        )

    # ------------------------------------------------------------------ #
    # Internal helpers
    # ------------------------------------------------------------------ #
    def _build_artifact_mask(self) -> np.ndarray:
        """Build uint8 mask from artifact times (1 at artifact samples)."""
        t_rel = self.t
        art_times = np.asarray(self.art_times, dtype=float)
        art_mask = np.zeros_like(t_rel, dtype=np.uint8)

        if art_times.size == 0:
            return art_mask

        idx = np.searchsorted(t_rel, art_times)
        idx = np.clip(idx, 0, len(t_rel) - 1)

        left = np.maximum(idx - 1, 0)
        use_left = np.abs(t_rel[left] - art_times) < np.abs(t_rel[idx] - art_times)
        idx = np.where(use_left, left, idx)

        art_mask[idx] = 1
        return art_mask

    def _beats_to_numpy_fiducials(
        self,
    ) -> tuple[np.ndarray, np.ndarray,
            np.ndarray, np.ndarray,
            np.ndarray, np.ndarray,
            np.ndarray, np.ndarray,
            np.ndarray, np.ndarray]:
        """Convert BeatFeatures list to numpy arrays for P/Q/R/S/T."""
        t_rel = self.t
        v_plot = self.v_plot
        beats = self.beats

        def extract_times(attr: str) -> np.ndarray:
            # Preserve beat order, skip None
            return np.asarray(
                [getattr(b, attr) for b in beats if getattr(b, attr) is not None],
                dtype=float,
            )

        # Build time arrays
        P_times = extract_times("p_time")
        Q_times = extract_times("q_time")
        R_times = extract_times("r_time")
        S_times = extract_times("s_time")
        T_times = extract_times("t_time")

        # Vectorised interpolation (one interp per wave type)
        def interp_vals(times: np.ndarray) -> np.ndarray:
            if times.size == 0:
                return np.empty(0, dtype=float)
            return np.interp(times, t_rel, v_plot)

        P_vals = interp_vals(P_times)
        Q_vals = interp_vals(Q_times)
        R_vals = interp_vals(R_times)
        S_vals = interp_vals(S_times)
        T_vals = interp_vals(T_times)

        return (
            P_times, P_vals,
            Q_times, Q_vals,
            R_times, R_vals,
            S_times, S_vals,
            T_times, T_vals,
        )

