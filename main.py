# ecg_analysis/main.py
from __future__ import annotations

from typing import Optional, Tuple

import tkinter as tk
from ECGViewer import parse_ecg_file_cpp as parse_ecg_file_cpp
from ecg_analysis import detrend_and_filter, ECGViewer, ViewerConfig, ECGGuiApp


def run_ecg_viewer(
    file_path: str,
    window: float = 0.4,
    ylim: Optional[Tuple[float, float]] = None,
    hide_artifacts: bool = False,
    bandpass: bool = False,
) -> int:
    """
    Core pipeline: load ECG (C++), process in Python, show Qt viewer.
    Called by the GUI instead of parsing CLI arguments.
    """

    # Load (C++ parser)
    t, v_raw, fs, meta = parse_ecg_file_cpp(file_path)
    if fs is None:
        raise RuntimeError(
            "Could not determine sampling rate "
            "(Interval missing and time column irregular)."
        )

    # Process (filter/detrend)
    v = detrend_and_filter(v_raw, fs, bandpass=bandpass)

    # View (Qt viewer via wrapper)
    cfg = ViewerConfig(
        window_s=window,
        ylim=ylim,
        hide_artifacts=hide_artifacts,
    )
    viewer = ECGViewer(t, v, fs, cfg)
    viewer.show()

    return 0


def main() -> int:
    """
    GUI entry point: launch the Tk GUI, which will call run_ecg_viewer(...)
    with the options selected by the user.
    """
    root = tk.Tk()
    # Pass the core function into the GUI so it can call it
    app = ECGGuiApp(root, run_callback=run_ecg_viewer)
    root.mainloop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
