# main.py
from __future__ import annotations

import argparse
from typing import Optional, Tuple, List

# If this is inside a package, use relative imports:
# from .utils import detrend_and_filter
# from .plotter import ECGViewer, ViewerConfig
# from parse_ecg import parse_ecg_file as parse_ecg_file_cpp

# If running as a simple script in the same directory, use:
from utils import detrend_and_filter
from plotter import ECGViewer, ViewerConfig
from parse_ecg import parse_ecg_file as parse_ecg_file_cpp


def build_arg_parser() -> argparse.ArgumentParser:
    ap = argparse.ArgumentParser(
        prog="ecg-viewer",
        description="Interactive ECG viewer with fixed y-axis and horizontal scrolling.",
    )
    ap.add_argument("file", help="Path to ECG text file")
    ap.add_argument(
        "--window",
        type=float,
        default=0.4,
        help="Window length in seconds (default: 10.0)",
    )
    ap.add_argument(
        "--ylim",
        nargs=2,
        type=float,
        metavar=("YMIN", "YMAX"),
        help="Fixed y-axis limits, e.g. --ylim -2 2 (mV if --mv, else V)",
    )
    ap.add_argument(
        "--mv",
        action="store_true",
        help="Display in millivolts (multiply by 1000)",
    )
    ap.add_argument(
        "--bandpass",
        action="store_true",
        help="Apply 0.5–40 Hz bandpass (requires SciPy)",
    )
    ap.add_argument(
        "--hide-artifacts",
        action="store_true",
        help="Hide artifact markers (not implemented in Qt viewer)",
    )
    return ap


def main(argv: List[str] | None = None) -> int:
    args = build_arg_parser().parse_args(argv)

    # Load (C++ parser)
    t, v_raw, fs, meta = parse_ecg_file_cpp(args.file)
    if fs is None:
        raise RuntimeError(
            "Could not determine sampling rate "
            "(Interval missing and time column irregular)."
        )

    # Process (filter/detrend)
    v = detrend_and_filter(v_raw, fs, bandpass=args.bandpass)

    # View (Qt viewer via wrapper)
    cfg = ViewerConfig(
        window_s=args.window,
        ylim=tuple(args.ylim) if args.ylim else None,
        as_mv=args.mv,
        hide_artifacts=args.hide_artifacts,
    )
    viewer = ECGViewer(t, v, fs, cfg)
    viewer.show()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
