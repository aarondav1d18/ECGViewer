from __future__ import annotations
import numpy as np
import matplotlib as mpl
import matplotlib.pyplot as plt
from matplotlib.widgets import Slider, Button
from dataclasses import dataclass
from typing import Optional, Tuple
from .utils import (
    minmax_downsample,
    detect_artifacts,
    clean_with_noise,
    detect_fiducials,
)

mpl.rcParams["path.simplify"] = True
mpl.rcParams["path.simplify_threshold"] = 1.0
mpl.rcParams["agg.path.chunksize"] = 10_000


@dataclass
class ViewerConfig:
    """
    Configuration for the Matplotlib ECG viewer.

    Attributes:
        window_s: Window width (seconds) shown in the viewer.
        ylim: Optional fixed y-axis limits as (ymin, ymax). If None, viewer chooses defaults.
        downsample_full: Max number of samples (approx) used for initial full-trace draw.
        downsample_window: Max number of samples (approx) used for the active window draw.
    """
    window_s: float = 10.0
    ylim: Optional[Tuple[float, float]] = (-0.1, 0.15)
    downsample_full: int = 50_000
    downsample_window: int = 20_000


class ECGViewer:
    '''
    ECG signal viewer with horizontal scrolling.

    Args:
        t_abs (ndarray): Absolute time values (s).
        v (ndarray): Voltage trace.
        fs (float): Sampling frequency (Hz).
        cfg (ViewerConfig): Viewer configuration.

    Methods:
        show()           - Open interactive plot.
        jump_to(t)       - Scroll to a given time (s).
        set_ylim(a, b)   - Change y-axis limits.
        toggle_overlay() - Show/hide raw overlay.
    '''
    def __init__(self, t_abs: np.ndarray, v: np.ndarray, fs: float, cfg: ViewerConfig | None = None):
        self.t_abs = t_abs.astype(float, copy=False)
        self.v_in = v.astype(float, copy=False)
        self.fs = float(fs)
        self.cfg = cfg or ViewerConfig()

        self.t0 = float(self.t_abs[0])
        self.t = self.t_abs - self.t0
        self.total_s = float(self.t[-1] - self.t[0])
        self.window_s = min(self.cfg.window_s, max(1.0, self.total_s))
        self.v_plot = self.v_in
        self.y_label = "Voltage (V)"

        self.art_times = detect_artifacts(self.t, self.v_in, self.fs)
        self.v_clean = clean_with_noise(self.t, self.v_plot, self.art_times, self.fs)
        self.beats = detect_fiducials(self.t, self.v_in, self.fs, art_times=self.art_times)

        self._init_fig()

    def show(self) -> None:
        '''Display the ECG viewer window.'''
        # plt.tight_layout()
        plt.show()

    def _plot_fiducials(self):
        # Prepare lists of each fiducial type
        P_times, P_vals = [], []
        Q_times, Q_vals = [], []
        R_times, R_vals = [], []
        S_times, S_vals = [], []
        T_times, T_vals = [], []

        for b in self.beats:
            # R (always present)
            R_times.append(b.r_time)
            R_vals.append(np.interp(b.r_time, self.t, self.v_plot))

            # Q
            if b.q_time is not None:
                Q_times.append(b.q_time)
                Q_vals.append(np.interp(b.q_time, self.t, self.v_plot))

            # S
            if b.s_time is not None:
                S_times.append(b.s_time)
                S_vals.append(np.interp(b.s_time, self.t, self.v_plot))

            # P
            if b.p_time is not None:
                P_times.append(b.p_time)
                P_vals.append(np.interp(b.p_time, self.t, self.v_plot))

            # T
            if b.t_time is not None:
                T_times.append(b.t_time)
                T_vals.append(np.interp(b.t_time, self.t, self.v_plot))

        # Scatter plot, each with different color/marker
        self.P_scatter = self.ax.scatter(P_times, P_vals, s=20, c='blue', label='P')
        self.Q_scatter = self.ax.scatter(Q_times, Q_vals, s=20, c='green', label='Q')
        self.R_scatter = self.ax.scatter(R_times, R_vals, s=40, c='red', marker='^', label='R')
        self.S_scatter = self.ax.scatter(S_times, S_vals, s=20, c='purple', label='S')
        self.T_scatter = self.ax.scatter(T_times, T_vals, s=20, c='orange', label='T')

        self.ax.legend(loc="upper right")

    def _plot_fiducial_lines(self, x0, x1):
        """
        Draw vertical lines for P, Q, R, S, T within the current window [x0, x1].
        """
        # Remove existing fiducial lines if any
        if hasattr(self, 'fiducial_artists'):
            for line, txt in self.fiducial_artists:
                line.remove()
                txt.remove()
            self.fiducial_artists.clear()

        self.fiducial_artists = []

        for b in self.beats:
            for label, t_val in [
                ("P", b.p_time),
                ("Q", b.q_time),
                ("R", b.r_time),
                ("S", b.s_time),
                ("T", b.t_time),
            ]:
                if t_val is None or t_val < x0 or t_val > x1:
                    continue

                # Get the y-position for placing label (top of graph)
                y_top = self.ax.get_ylim()[1]

                # Draw the vertical line
                line = self.ax.axvline(
                    t_val, 
                    color="black",
                    linestyle="--",
                    linewidth=0.8,
                    alpha=0.8
                )

                # Add text label slightly above the waveform
                ymin, ymax = self.ax.get_ylim()
                margin = 0.02 * (ymax - ymin)  # 2% of axis height

                txt = self.ax.text(
                    t_val,
                    ymax - margin,           # slightly below top
                    f"{label} @ {t_val:.3f}s",
                    rotation=90,
                    va="top",                # y is now the *top* of the text
                    ha="right",
                    fontsize=12,
                    color="black",
                    clip_on=True,            # clip to axes just in case
                )

                self.fiducial_artists.append((line, txt))

    def jump_to(self, t_start: float) -> None:
        '''Scroll to a given start time (s).'''
        self.s_start.set_val(float(np.clip(t_start, self.s_start.valmin, self.s_start.valmax)))

    def set_ylim(self, ymin: float, ymax: float) -> None:
        '''Change y-axis limits.'''
        self.ax.set_ylim(ymin, ymax)
        self.fig.canvas.draw_idle()

    def toggle_overlay(self) -> None:
        '''Toggle visibility of the raw overlay trace.'''
        self.ln_orig.set_visible(not self.ln_orig.get_visible())
        self.fig.canvas.draw_idle()

    def _init_fig(self) -> None:
        '''Initialize figure, sliders, and controls.'''
        self.fig, self.ax = plt.subplots(figsize=(12, 5))
        plt.subplots_adjust(bottom=0.22)

        left = 0.0
        right = min(self.window_s, self.total_s)
        t_d0, v_d0 = minmax_downsample(self.t, self.v_plot, self.cfg.downsample_full)
        t_dc, v_dc = minmax_downsample(self.t, self.v_clean, self.cfg.downsample_full)

        (self.ln_clean,) = self.ax.plot(t_dc, v_dc, linewidth=0.9, label="cleaned")
        (self.ln_orig,) = self.ax.plot(t_d0, v_d0, linewidth=0.6, color="red", alpha=0.6, label="original")
        self.ax.axhline(0, color="black", linewidth=1)
        self.ax.set_xlim(left, right)
        self.ax.set_ylim(*self.cfg.ylim if self.cfg.ylim else (-0.1, 0.15))
        self.ax.set_xlabel("Time (s)")
        self.ax.set_ylabel(self.y_label)
        self.ax.set_title("ECG Viewer")
        self.ax.grid(True, linestyle="-", alpha=0.6)

        ax_sl = plt.axes([0.12, 0.08, 0.7, 0.04], facecolor="lightgoldenrodyellow")
        self.s_start = Slider(
            ax=ax_sl, label="Start (s)",
            valmin=0.0, valmax=max(0.0, self.total_s - self.window_s),
            valinit=left, valstep=1.0 / (self.fs or 1000.0),
        )
        self.s_start.on_changed(self._on_slider)

        ax_left = plt.axes([0.12, 0.015, 0.08, 0.05])
        ax_right = plt.axes([0.22, 0.015, 0.08, 0.05])
        self.b_left = Button(ax_left, "Left")
        self.b_right = Button(ax_right, "Right")
        self.b_left.on_clicked(lambda _e: self._nudge(-self.window_s * 0.2))
        self.b_right.on_clicked(lambda _e: self._nudge(+self.window_s * 0.2))

        self.fig.canvas.mpl_connect("key_press_event", self._on_key)
        self._plot_fiducial_lines(left, right)

    def _slice_down(self, x0: float, x1: float):
        '''Return downsampled segment for current window.'''
        m = (self.t >= x0) & (self.t <= x1)
        tx, vxo = minmax_downsample(self.t[m], self.v_plot[m], self.cfg.downsample_window)
        txc, vxc = minmax_downsample(self.t[m], self.v_clean[m], self.cfg.downsample_window)
        return tx, vxo, txc, vxc

    def _on_slider(self, _val) -> None:
        '''Update plot on slider movement.'''
        x0 = self.s_start.val
        x1 = x0 + self.window_s
        self._plot_fiducial_lines(x0, x1)
        tx, vxo, txc, vxc = self._slice_down(x0, x1)
        self.ln_orig.set_data(tx, vxo)
        self.ln_clean.set_data(txc, vxc)
        self.ax.set_xlim(x0, x1)
        self.fig.canvas.draw_idle()

    def _nudge(self, delta: float) -> None:
        '''Shift view window left or right.'''
        new_val = float(np.clip(self.s_start.val + delta, self.s_start.valmin, self.s_start.valmax))
        self.s_start.set_val(new_val)

    def _on_key(self, event) -> None:
        '''Keyboard controls for navigation.'''
        if event.key in ("left", "a"):
            self._nudge(-self.window_s * 0.2)
        elif event.key in ("right", "d"):
            self._nudge(+self.window_s * 0.2)
