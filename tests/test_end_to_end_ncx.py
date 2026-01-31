from __future__ import annotations

from pathlib import Path
import sys
import numpy as np
import pytest

# Make sure project root is on the import path
ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from ecg_analysis import (
    parse_ecg_file,
    detrend_and_filter,
    detect_artifacts,
    clean_with_noise,
    detect_fiducials,
)

# Your ECG file sits in project root
ECG_PATH = ROOT / "NCX 310325 normal ECG.txt"


def test_end_to_end_ncx_pipeline():

    if not ECG_PATH.exists():
        pytest.skip("Sample ECG file not found in project root")

    # Load
    t, v, fs, meta = parse_ecg_file(str(ECG_PATH))
    assert fs is not None and fs > 0

    # Filter
    v_filt = detrend_and_filter(v, fs, bandpass=True)

    # Artifacts + clean
    art_times = detect_artifacts(t, v_filt, fs)
    v_clean = clean_with_noise(t, v_filt, art_times, fs)

    # Fiducials
    beats = detect_fiducials(t, v_clean, fs, art_times=art_times)
    assert len(beats) >= 5

    # R-peaks must increase
    r_times = np.array([b.r_time for b in beats])
    assert np.all(np.diff(r_times) > 0)

    # HR sanity
    rr = np.diff(r_times)
    rr = rr[rr > 0]
    hr = 60.0 / rr
    hr_mean = float(np.mean(hr))
    assert 30 <= hr_mean <= 200
