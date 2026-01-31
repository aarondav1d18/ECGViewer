import io
import math
from pathlib import Path

import numpy as np
import pytest
import sys
import importlib

ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

import ecg_analysis as pkg
from ecg_analysis import (
    parse_ecg_file,
    detrend_and_filter,
    bandpass_qrs,
    detect_r_peaks,
    detect_fiducials,
    detect_artifacts,
    minmax_downsample,
    BeatFeatures,
    clean_with_noise
)

rr_interval_global = None


def _impl_module():
    return importlib.import_module(pkg.parse_ecg_file.__module__)


def _generate_synthetic_ecg(
    fs: float = 250.0,
    duration_s: float = 10.0,
    heart_rate_bpm: float = 60.0,
) -> tuple[np.ndarray, np.ndarray, float, np.ndarray]:
    """
    Generate a simple synthetic ECG-like signal with:
      - R peaks at known times
      - Small P and T waves
      - Q and S as small negative deflections around R

    Returns
    -------
    t : np.ndarray
        Time vector (s)
    v : np.ndarray
        Synthetic ECG voltage
    fs : float
        Sampling frequency (Hz)
    r_times : np.ndarray
        Ground-truth R-peak times (s)
    """
    global rr_interval_global

    rng = np.random.default_rng(0)

    t = np.arange(0, duration_s, 1.0 / fs)
    n = t.size

    v = np.zeros_like(t, dtype=float)

    # R-peaks
    rr_interval = 60.0 / heart_rate_bpm  # seconds per beat
    rr_interval_global = rr_interval
    # Start a bit into the trace to avoid edge issues
    r_times = np.arange(1.0, duration_s - 1.0, rr_interval)

    r_indices = np.round(r_times * fs).astype(int)
    r_indices = r_indices[(r_indices > 0) & (r_indices < n - 1)]

    # For each beat, add simple P-Q-R-S-T morphology
    for ri in r_indices:
        # R: tall positive spike (Gaussian)
        for offset in range(-2, 3):
            idx = ri + offset
            if 0 <= idx < n:
                v[idx] += math.exp(-0.5 * (offset / 1.0) ** 2) * 0.15

        # Q: small negative dip before R
        q_idx = ri - int(0.03 * fs)
        if 0 <= q_idx < n:
            v[q_idx] -= 0.05

        # S: small negative dip after R
        s_idx = ri + int(0.04 * fs)
        if 0 <= s_idx < n:
            v[s_idx] -= 0.05

        # P: small bump well before R
        p_idx = ri - int(0.18 * fs)
        if 0 <= p_idx < n:
            v[p_idx] += 0.2

        # T: broader bump after S
        t_idx = ri + int(0.3 * fs)
        if 0 <= t_idx < n:
            for offset in range(-4, 5):
                idx = t_idx + offset
                if 0 <= idx < n:
                    v[idx] += 0.18 * math.exp(-0.5 * (offset / 3.0) ** 2)

    # Add mild baseline wander and noise
    baseline = 0.0
    noise = rng.normal(0.0, 0.005, size=n)
    v = v + baseline + noise

    return t, v, fs, r_times


@pytest.fixture
def synthetic_ecg():
    return _generate_synthetic_ecg()


def test_parse_ecg_file_roundtrip(tmp_path: Path, synthetic_ecg):
    """
    Write a text file with headers and numeric data, then ensure
    parse_ecg_file recovers time, voltage, and fs correctly.
    """
    t, v, fs, _ = synthetic_ecg

    interval = 1.0 / fs
    file_path = tmp_path / "ecg_test.txt"

    with file_path.open("w", encoding="utf-8") as f:
        f.write(f"Interval={interval}\n")
        f.write("ChannelTitle=ECG\n")
        f.write("Range=10.000 V\n")
        f.write("SomeOtherHeader=shouldBeIgnored\n")

        # Numeric data
        for ti, vi in zip(t, v):
            f.write(f"{ti:.6f}\t{vi:.6f}\n")

    t_read, v_read, fs_read, meta = parse_ecg_file(str(file_path))

    assert t_read.shape == t.shape
    assert v_read.shape == v.shape

    # fs should be derived from Interval
    assert fs_read == pytest.approx(fs, rel=1e-6)

    # Numeric values should match (within small numeric tolerance)
    np.testing.assert_allclose(t_read, t, rtol=0, atol=1e-8)
    np.testing.assert_allclose(v_read, v, rtol=0, atol=1e-5)

    # meta dictionary should reflect Interval and headers
    assert meta["interval_s"] == pytest.approx(interval)
    assert meta["channel_title"] == "ECG"
    assert meta["range"] == "10.000 V"



def test_detrend_and_filter_removes_baseline(synthetic_ecg):
    """
    Add a slow-varying baseline drift and verify that detrend_and_filter
    (with bandpass=False) largely removes it.
    """
    t, v_clean, fs, _ = synthetic_ecg

    slow_baseline = 0.5 * np.sin(2 * np.pi * 0.05 * t)  # strong low freq drift
    v_noisy = v_clean + slow_baseline

    v_detrended = detrend_and_filter(v_noisy, fs, bandpass=False)

    # The difference between detrended and clean should be much smaller
    # than the original baseline amplitude.
    err_before = np.std(v_noisy - v_clean)
    err_after = np.std(v_detrended - v_clean)

    assert err_after < 0.5 * err_before


def test_bandpass_qrs_shape_and_energy(synthetic_ecg):
    """
    bandpass_qrs should preserve shape length and not produce NaNs/inf.
    """
    t, v, fs, _ = synthetic_ecg

    v_qrs = bandpass_qrs(v, fs)

    assert v_qrs.shape == v.shape
    assert np.all(np.isfinite(v_qrs))

    # Should not be identically zero and should alter signal somewhat
    assert np.std(v_qrs) > 0
    assert np.std(v_qrs - v) > 0



def test_detect_r_peaks_matches_known_times(synthetic_ecg):
    """
    Ensure detect_r_peaks finds peaks near each synthetic beat time.
    We don't require exactly one peak per beat, only that every true
    R time has a nearby detected peak.
    """
    t, v, fs, r_times_true = synthetic_ecg

    r_idx = detect_r_peaks(t, v, fs)
    detected_times = t[r_idx]

    # Should find at least as many peaks as beats (but might be more)
    assert r_idx.size >= r_times_true.size

    # For each true R time, ensure there's a detected peak within ~20 ms
    tol = 0.02
    for rt in r_times_true:
        assert np.any(np.abs(detected_times - rt) <= tol), (
            f"No detected R peak within {tol}s of true R time {rt}"
        )



def test_detect_fiducials_returns_per_beat_structure(synthetic_ecg):
    """
    detect_fiducials should return BeatFeatures objects whose R times
    line up with the synthetic beats (within tolerance). There may be
    more detected beats than true beats, but not fewer.
    """
    t, v, fs, r_times_true = synthetic_ecg

    beats = detect_fiducials(t, v, fs)
    # The implementation stores per-beat RR intervals; the last is None.
    assert len(beats) >= r_times_true.size
    assert all(isinstance(b, BeatFeatures) for b in beats)

    r_times_detected = np.array([b.r_time for b in beats])

    # For each true R time, there should be a BeatFeatures R nearby
    tol = 0.02
    for rt in r_times_true:
        assert np.any(np.abs(r_times_detected - rt) <= tol), (
            f"No BeatFeatures R within {tol}s of true R time {rt}"
        )

    # Basic sanity: Q < R < S when present
    for b in beats:
        if b.q_idx is not None:
            assert b.q_idx < b.r_idx
        if b.s_idx is not None:
            assert b.s_idx > b.r_idx


def test_minmax_downsample_basic():
    """Ensure min/max downsampling preserves extrema and reduces size."""
    x = np.linspace(0, 1, 1000)
    y = np.sin(2 * np.pi * 5 * x)

    xs, ys = minmax_downsample(x, y, target=100)

    # Length should be <= 2 * target
    assert ys.size <= 200

    # Extrema should be roughly preserved
    assert np.isclose(np.max(ys), 1.0, atol=0.05)
    assert np.isclose(np.min(ys), -1.0, atol=0.05)


def test_minmax_downsample_noop_when_target_large():
    """If target >= input size, the function should return originals."""
    x = np.array([0, 1, 2])
    y = np.array([10, 20, 30])

    xs, ys = minmax_downsample(x, y, target=10)
    np.testing.assert_array_equal(xs, x)
    np.testing.assert_array_equal(ys, y)


def test_detect_artifacts_finds_spikes(synthetic_ecg):
    """Ensure detect_artifacts catches narrow high-amplitude spikes."""
    t, v, fs, _ = synthetic_ecg
    v = v.copy()

    # Inject sharp artifacts
    spike_times = np.array([2.0, 5.0])
    spike_idx = (spike_times * fs).astype(int)

    for idx in spike_idx:
        if 0 <= idx < v.size:
            v[idx] += 5.0  # strong spike

    art_times = detect_artifacts(t, v, fs)
    assert art_times.size >= spike_times.size

    for st in spike_times:
        assert np.any(np.abs(art_times - st) < 0.02)


def test_detect_r_peaks_handles_noisy_signal(synthetic_ecg):
    """Ensure R-peak detection still returns reasonable indices when noise is added."""
    t, v, fs, r_true = synthetic_ecg
    rng = np.random.default_rng(0)

    v_noisy = v + rng.normal(0, 0.01, size=v.size)
    r_idx = detect_r_peaks(t, v_noisy, fs)

    # Should still find at least N peaks
    assert r_idx.size >= r_true.size

    # Should be near true peaks
    for rt in r_true:
        assert np.any(np.abs(t[r_idx] - rt) < 0.03)


def test_parse_ecg_file_estimates_fs_when_no_interval(tmp_path):
    p = tmp_path / "no_interval.txt"
    with p.open("w", encoding="utf-8") as f:
        f.write("ChannelTitle=ECG\n")
        f.write("Range=10V\n")
        f.write("garbage header\n")
        f.write("0.000 1.0\n")
        f.write("0.004 2.0\n")
        f.write("0.008 3.0\n")

    t, v, fs, meta = parse_ecg_file(str(p))
    assert fs == pytest.approx(250.0, rel=1e-6)
    assert meta["interval_s"] is None
    assert meta["channel_title"] == "ECG"
    assert meta["range"] == "10V"


def test_parse_ecg_file_ignores_bad_interval_and_still_parses(tmp_path):
    p = tmp_path / "bad_interval.txt"
    with p.open("w", encoding="utf-8") as f:
        f.write("Interval=not_a_number\n")
        f.write("0.000 1.0\n")
        f.write("0.010 2.0\n")

    t, v, fs, meta = parse_ecg_file(str(p))
    assert meta["interval_s"] is None
    assert fs == pytest.approx(100.0, rel=1e-6)


def test_parse_ecg_file_raises_if_no_numeric_rows(tmp_path):
    p = tmp_path / "empty_numeric.txt"
    with p.open("w", encoding="utf-8") as f:
        f.write("Interval=0.004\n")
        f.write("hello world\n")
        f.write("x y\n")

    with pytest.raises(ValueError, match="No numeric data rows"):
        parse_ecg_file(str(p))


def test_detrend_and_filter_falls_back_to_mean_on_exception(monkeypatch, synthetic_ecg):
    _, v, fs, _ = synthetic_ecg

    class Boom:
        def rolling(self, *a, **k):
            raise RuntimeError("boom")

    m = _impl_module()
    monkeypatch.setattr(m.pd, "Series", lambda *a, **k: Boom())

    out = detrend_and_filter(v, fs, bandpass=False)
    assert out.shape == v.shape
    assert np.all(np.isfinite(out))



def test_detect_artifacts_returns_empty_when_fs_invalid(synthetic_ecg):
    t, v, _, _ = synthetic_ecg
    out = detect_artifacts(t, v, fs=None)
    assert out.size == 0

    out = detect_artifacts(t[:4], v[:4], fs=250.0)
    assert out.size == 0


def test_detect_artifacts_returns_empty_when_no_spikes(synthetic_ecg):
    m = _impl_module()
    fs = 250.0
    t = np.arange(0, 10, 1 / fs)
    v = np.zeros_like(t)

    out = m.detect_artifacts(t, v, fs)
    assert out.size == 0


def test_detect_artifacts_ignores_wide_blocks(synthetic_ecg):
    # Make a long run of huge derivative so block.size > 3, therefore ignored.
    m = _impl_module()
    fs = 250.0
    t = np.arange(0, 10, 1 / fs)
    v = np.zeros_like(t)

    j = int(2.0 * fs)
    v[j:j+20] = np.arange(20) * 100.0

    out = m.detect_artifacts(t, v, fs)
    assert out.size == 0


def test_minmax_downsample_target_le_zero_returns_input():
    x = np.array([0, 1, 2], dtype=float)
    y = np.array([3, 4, 5], dtype=float)

    xs, ys = minmax_downsample(x, y, target=0)
    np.testing.assert_array_equal(xs, x)
    np.testing.assert_array_equal(ys, y)


def test_minmax_downsample_handles_zero_length_segments():
    # Construct a case where linspace produces repeated indices
    x = np.arange(3)
    y = np.array([0.0, 1.0, 0.0])
    xs, ys = minmax_downsample(x, y, target=10)
    np.testing.assert_array_equal(xs, x)
    np.testing.assert_array_equal(ys, y)


def test_detect_r_peaks_returns_empty_when_find_peaks_empty(monkeypatch, synthetic_ecg):
    t, v, fs, _ = synthetic_ecg

    m = _impl_module()
    monkeypatch.setattr(m, "bandpass_qrs", lambda vv, f: np.zeros_like(vv))
    monkeypatch.setattr(m, "find_peaks", lambda *a, **k: (np.array([], dtype=int), {}))

    r = detect_r_peaks(t, v, fs)
    assert r.size == 0


def test_detect_r_peaks_artifact_filtering_removes_nearby(monkeypatch, synthetic_ecg):
    t, v, fs, _ = synthetic_ecg

    # Pretend bandpass produces something meaningful
    m = _impl_module()
    monkeypatch.setattr(m, "bandpass_qrs", lambda vv, f: vv)

    # Pretend peaks at indices 100, 200, 300
    monkeypatch.setattr(
        m,
        "find_peaks",
        lambda *a, **k: (np.array([100, 200, 300], dtype=int), {"prominences": np.array([1, 1, 1])}),
    )

    # Widths all valid (50 ms)
    monkeypatch.setattr(
        m,
        "peak_widths",
        lambda vqrs, idx, rel_height=0.5: (np.array([0.05 * fs] * len(idx)), None, None, None),
    )

    # art near 200 (within 10ms => removed)
    art_times = np.array([t[200]])
    r = detect_r_peaks(t, v, fs, art_times=art_times)

    assert np.array_equal(r, np.array([100, 300], dtype=int))


def test_detect_r_peaks_width_filtering_drops_all(monkeypatch, synthetic_ecg):
    t, v, fs, _ = synthetic_ecg
    m = _impl_module()
    monkeypatch.setattr(m, "bandpass_qrs", lambda vv, f: vv)
    monkeypatch.setattr(m, "find_peaks", lambda *a, **k: (np.array([100, 200], dtype=int), {}))

    # Too wide (0.3s) => rejected (>0.16s)
    monkeypatch.setattr(
        m,
        "peak_widths",
        lambda vqrs, idx, rel_height=0.5: (np.array([0.30 * fs] * len(idx)), None, None, None),
    )

    r = detect_r_peaks(t, v, fs)
    assert r.size == 0


def test_detect_r_peaks_amp_filtering_drops_small(monkeypatch, synthetic_ecg):
    t, v, fs, _ = synthetic_ecg

    m = _impl_module()
    monkeypatch.setattr(m, "bandpass_qrs", lambda vv, f: vv)
    monkeypatch.setattr(m, "find_peaks", lambda *a, **k: (np.array([10, 20, 30], dtype=int), {}))
    monkeypatch.setattr(
        m,
        "peak_widths",
        lambda vqrs, idx, rel_height=0.5: (np.array([0.05 * fs] * len(idx)), None, None, None),
    )

    # Make median amplitude large so the small one gets dropped.
    v2 = v.copy()
    v2[10] = 10.0
    v2[20] = 10.0
    v2[30] = 0.1

    r = detect_r_peaks(t, v2, fs)
    assert np.array_equal(r, np.array([10, 20], dtype=int))


def test__is_near_artifact_binary_search_edges():
    m = _impl_module()
    f = m._is_near_artifact

    art = np.array([10, 20, 30], dtype=int)
    assert f(10, art, noise_gap=1) is True
    assert f(9, art, noise_gap=2) is True
    assert f(8, art, noise_gap=1) is False
    assert f(100, art, noise_gap=5) is False
    assert f(10, None, noise_gap=5) is False
    assert f(10, np.array([], dtype=int), noise_gap=5) is False


def test_detect_fiducials_rejects_p_and_t_near_artifacts(monkeypatch, synthetic_ecg):
    t, v, fs, r_times = synthetic_ecg

    # Force r peaks deterministically (avoid depending on full pipeline)
    r_idx = (r_times * fs).astype(int)
    m = _impl_module()
    monkeypatch.setattr(m, "detect_r_peaks", lambda *a, **k: r_idx)

    # First run to learn which indices the algorithm actually selects.
    beats0 = detect_fiducials(t, v, fs, art_times=None)
    assert len(beats0) >= 1
    b0 = beats0[0]
    assert b0.p_idx is not None
    assert b0.t_idx is not None

    # Place artifacts exactly on the indices chosen for beat 0.
    # Add a tiny epsilon so searchsorted doesn't land just before due to float edges.
    eps = 1e-12
    art_times = np.array([t[b0.p_idx] + eps, t[b0.t_idx] + eps], dtype=float)

    beats = detect_fiducials(t, v, fs, art_times=art_times)

    # First beat P and T should be rejected
    b0b = beats[0]
    assert b0b.p_idx is None
    assert b0b.t_idx is None

    # Later beats likely still have something (sanity)
    assert len(beats) >= 2


def test_clean_with_noise_returns_copy_when_no_artifacts(synthetic_ecg):
    t, y, fs, _ = synthetic_ecg
    out = clean_with_noise(t, y, np.array([], dtype=float), fs)
    np.testing.assert_allclose(out, y)
    assert out is not y  # copy


def test_clean_with_noise_modifies_near_artifact(monkeypatch, synthetic_ecg):
    t, y, fs, _ = synthetic_ecg
    y2 = y.copy()

    # Force spline failure => linear fallback path
    m = _impl_module()
    monkeypatch.setattr(
        m,
        "UnivariateSpline",
        lambda *a, **k: (_ for _ in ()).throw(RuntimeError("nope")),
    )

    art_times = np.array([2.0], dtype=float)
    out = clean_with_noise(t, y2, art_times, fs)

    assert out.shape == y2.shape
    assert np.any(out != y2)  # something changed
