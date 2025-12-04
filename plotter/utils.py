from __future__ import annotations
import tempfile
import numpy as np
import pandas as pd
from scipy.ndimage import uniform_filter1d
from scipy.interpolate import UnivariateSpline
from scipy.signal import butter, sosfiltfilt, find_peaks, peak_widths, medfilt
from dataclasses import dataclass
from line_profiler import profile

@dataclass
class BeatFeatures:
    r_time: float
    r_idx: int
    q_time: float | None = None
    q_idx: int | None = None
    s_time: float | None = None
    s_idx: int | None = None
    p_time: float | None = None
    p_idx: int | None = None
    t_time: float | None = None
    t_idx: int | None = None

def parse_ecg_file(path: str) -> tuple[np.ndarray, np.ndarray, float | None]:
    '''
    Parse an ECG text file into time, voltage, and sampling frequency.
    Reads only numeric two-column lines, ignoring headers such as
    "Interval=" or "ExcelDateTime=". If "Interval=" is found, fs = 1 / Interval.
    Otherwise fs is estimated as 1 / median(delta_t).

    Args:
        path (str): Path to the ECG text file.

    Returns:
        (t, v, fs):
            t (ndarray): Absolute time values.
            v (ndarray): Voltage readings.
            fs (float | None): Sampling rate in Hz.

    Notes:
        - Need to add some logic for reading in the headers properly. Currently
        just ignores them except for Interval which is fine since they are all the 
        same for the data we have but not good if it changes.
    '''
    interval: float | None = None
    meta: dict = {
        "interval_s": None,
        "channel_title": None,
        "range": None,
    }

    with tempfile.NamedTemporaryFile(mode="w+", encoding="utf-8", newline="\n", delete=True) as tmp:
        with open(path, "r", encoding="utf-8", errors="ignore") as f:
            for raw in f:
                line = raw.strip()
                if not line:
                    continue

                if line.startswith("Interval="):
                    try:
                        rhs = line.split("=", 1)[1].strip()
                        tok = rhs.replace("\t", " ").split()[0]
                        interval = float(tok)
                        meta["interval_s"] = interval
                    except Exception:
                        pass
                    continue

                if line.startswith("ChannelTitle="):
                    meta["channel_title"] = line.split("=", 1)[1].strip() or None
                    continue

                if line.startswith("Range="):
                    meta["range"] = line.split("=", 1)[1].strip() or None
                    continue

                # ignore other headers entirely...

                parts = line.replace("\t", " ").split()
                if len(parts) < 2:
                    continue
                p0, p1 = parts[0], parts[1]
                if not (p0[0].isdigit() or p0[0] in "+-."):
                    continue
                if not (p1[0].isdigit() or p1[0] in "+-."):
                    continue
                tmp.write(f"{p0} {p1}\n")
        tmp.flush()

        t_parts, v_parts = [], []
        for chunk in pd.read_csv(
            tmp.name,
            delim_whitespace=True,
            header=None,
            names=["t", "v"],
            dtype={"t": "float64", "v": "float64"},
            engine="c",
            chunksize=250_000,
            memory_map=True,
            low_memory=False,
        ):
            if not chunk.empty:
                t_parts.append(chunk["t"].to_numpy(copy=False))
                v_parts.append(chunk["v"].to_numpy(copy=False))

    if not t_parts:
        raise ValueError("No numeric data rows were found.")

    t = np.concatenate(t_parts)
    v = np.concatenate(v_parts)

    if interval and interval > 0:
        fs = 1.0 / interval
    else:
        dt = float(np.median(np.diff(t))) if t.size > 1 else 0.0
        fs = 1.0 / dt if dt > 0 else None

    return t, v, fs, meta

@profile
def detrend_and_filter(v: np.ndarray, fs: float | None, bandpass: bool = True) -> np.ndarray:
    '''
    Remove baseline drift or apply a 0.5–40 Hz bandpass filter.

    Args:
        v (ndarray): Input voltage trace.
        fs (float | None): Sampling rate in Hz.
        bandpass (bool): If True, apply 0.5–40 Hz Butterworth bandpass.

    Returns:
        ndarray: Filtered or detrended signal.

    Theory:
        ECG signals typically contain frequencies between 0.5 and 40 Hz.
        - Baseline drift (caused by breathing or electrode motion) occurs below ~0.5 Hz.
        - High-frequency noise (e.g. muscle artifacts) appears above ~40 Hz.

        When `bandpass` is True:
            A 4th-order Butterworth bandpass is applied with corner frequencies
            f_low = 0.5 Hz and f_high = 40 Hz. The normalized cutoffs are:
                w_low  = f_low  / (fs / 2)
                w_high = f_high / (fs / 2)
            The Butterworth filter has a maximally flat passband response and is
            implemented in second-order sections for numerical stability.
            The signal is filtered forward and backward using `sosfiltfilt`,
            producing zero-phase distortion.

        When `bandpass` is False:
            Baseline drift is removed by estimating the slow-varying component
            using a rolling median window:
                baseline[i] = median(v[i - w/2 : i + w/2])
            where the window size w ~= 0.2 * fs (about 200 ms). This captures the
            low-frequency baseline while preserving the shape of QRS complexes.
            The detrended signal is:
                v_detrended = v - baseline
    '''
    v = np.asarray(v, dtype=np.float32)

    win = int(max(3, round(0.2 * (fs or 250.0))))
    if win % 2 == 0:
        win += 1
    try:
        baseline = (
            pd.Series(v)
            .rolling(window=win, center=True, min_periods=1)
            .median()
            .to_numpy()
            .astype(np.float32, copy=False)
        )
        return v - baseline
    except Exception:
        return v - np.float32(np.mean(v, dtype=np.float64))


def minmax_downsample(x: np.ndarray, y: np.ndarray, target: int) -> tuple[np.ndarray, np.ndarray]:
    '''Downsample by keeping local min and max per segment (preserves peaks).'''
    n = x.size
    if target <= 0 or n <= target:
        return x, y

    idx = np.linspace(0, n, num=target + 1, dtype=int)

    xs = np.empty(2 * target, dtype=x.dtype)
    ys = np.empty(2 * target, dtype=y.dtype)
    k = 0

    for i in range(target):
        s, e = idx[i], idx[i + 1]
        if e - s <= 0:
            continue
        xb, yb = x[s:e], y[s:e]
        jmin = int(np.argmin(yb))
        jmax = int(np.argmax(yb))

        xs[k] = xb[jmin]
        ys[k] = yb[jmin]
        k += 1
        xs[k] = xb[jmax]
        ys[k] = yb[jmax]
        k += 1

    xs = xs[:k]
    ys = ys[:k]
    o = np.argsort(xs)
    return xs[o], ys[o]


def detect_artifacts(t: np.ndarray, v: np.ndarray, fs: float | None) -> np.ndarray:
    '''
    Detect narrow, non-physiological spikes in an ECG signal.

    Args:
        t (ndarray): Time values in seconds.
        v (ndarray): Voltage trace.
        fs (float | None): Sampling rate in Hz.

    Returns:
        ndarray: Time values (s) where transient artifacts are detected.

    Theory:
        Biological ECG waveforms (P, QRS, T) have finite slope limits based on
        heart rate and tissue conduction speed. Extremely sharp changes in
        voltage that exceed physiological slope limits are therefore likely to
        originate from sensor noise, motion artifacts, or ADC spikes.

        This detector uses the first derivative of the signal and a robust
        statistical threshold based on the Median Absolute Deviation (MAD).
        It assumes normal ECG slope distributions and identifies outliers as
        follows:

        1. Derivative:
            dv[i] = v[i+1] - v[i]
            The derivative highlights rapid voltage transitions.

        2. Robust deviation estimate:
            mad = median(|dv - median(dv)|)
            This measures spread without being affected by spikes.

        3. Convert MAD to an equivalent standard deviation estimate:
            sigma = 1.4826 * mad
            (This scaling constant makes sigma ~= std(dv) for normal data.)

        4. Outlier detection:
            A point is marked as an artifact if:
                |dv - median(dv)| > 12 * sigma
            The threshold of 12 sigma rejects roughly 99.999999% of normal
            Gaussian variation, isolating only extreme impulses.

        5. Temporal grouping:
            Adjacent flagged points are merged into a single event.
            Only isolated short events (<3 samples) are accepted to avoid
            classifying real QRS complexes as noise.

        6. Spacing constraint:
            Detected artifact times are filtered to enforce a minimum gap of
            2 / fs seconds, preventing multiple detections of a single spike.
    Notes:
        - The method assumes a reasonably high sampling rate (fs > 0).
        - If no artifacts are detected, an empty array is returned.
        - NOTE: Dont merge this theory into main branch - put in docs
    '''
    if not fs or fs <= 0 or v.size < 5:
        return np.empty(0, dtype=float)

    dv = np.diff(v)
    med = float(np.median(dv))
    mad = float(np.median(np.abs(dv - med))) + 1e-12
    sigma = 1.4826 * mad

    idx = np.flatnonzero(np.abs(dv - med) > 12.0 * sigma)
    if idx.size == 0:
        return np.empty(0, dtype=float)

    cuts = np.flatnonzero(np.diff(idx) > 1)
    starts = np.r_[0, cuts + 1]
    ends = np.r_[cuts, idx.size - 1]
    picks = []
    for s, e in zip(starts, ends):
        block = idx[s:e + 1]
        if block.size <= 3:
            j = block[int(np.argmax(np.abs(dv[block])))]
            picks.append(min(j + 1, t.size - 1))
    if not picks:
        return np.empty(0, dtype=float)

    times = t[np.asarray(sorted(set(picks)))]
    if times.size <= 1:
        return times

    dt = 2.0 / fs

    # First always kept
    mask = np.empty(times.size, dtype=bool)
    mask[0] = True
    mask[1:] = np.diff(times) > dt

    return times[mask]


@profile
def clean_with_noise(t: np.ndarray, y: np.ndarray, art_times: np.ndarray, fs: float | None) -> np.ndarray:
    if art_times.size == 0 or not fs or fs <= 0:
        return y.copy()

    y_out = y.copy()
    n = y_out.size

    base_window_ms = 12
    extra_tail_ms = 10
    noise_strength = 0.1
    taper_ms = 8
    fit_neighbors = 45

    win_half = int(base_window_ms * fs / 1000)
    tail_extra = int(extra_tail_ms * fs / 1000)
    feather = int(taper_ms * fs / 1000)
    k_neighbor = int(fit_neighbors * fs / 1000)

    rng = np.random.default_rng(12345)

    # Precompute derivative once
    dv = np.diff(y_out, prepend=y_out[0])

    # ---- 1) Precompute local stats with uniform filter (O(N)) ----
    win_loc = max(3, 2 * k_neighbor)  # ensure odd-ish, non-zero

    # local mean of y and dv
    mean_y = uniform_filter1d(y_out.astype(np.float64), size=win_loc, mode="nearest")
    mean_dv = uniform_filter1d(dv.astype(np.float64), size=win_loc, mode="nearest")

    # local mean of squares for std
    mean_y2 = uniform_filter1d((y_out**2).astype(np.float64), size=win_loc, mode="nearest")
    mean_dv2 = uniform_filter1d((dv**2).astype(np.float64), size=win_loc, mode="nearest")

    std_y = np.sqrt(np.maximum(mean_y2 - mean_y**2, 0.0)) + 1e-6
    std_dv = np.sqrt(np.maximum(mean_dv2 - mean_dv**2, 0.0)) + 1e-6

    # ---- 2) Cache tapers by length to avoid repeated linspace/cos ----
    taper_cache: dict[int, np.ndarray] = {}

    def get_taper(L: int) -> np.ndarray:
        """Cosine taper of length L (0..1..0)."""
        # Use full length L; inside we may use only the first/last feather samples.
        if L not in taper_cache:
            if L <= 1:
                taper_cache[L] = np.ones(L, dtype=float)
            else:
                # 0..pi for left, pi..0 for right is symmetric; here we precompute 0..pi
                x = np.linspace(0.0, np.pi, L)
                taper_cache[L] = 0.5 * (1.0 - np.cos(x))
        return taper_cache[L]

    # art_times is assumed sorted; t is sorted
    for art in art_times:
        j = int(np.clip(np.searchsorted(t, art), 0, n - 1))

        s = j - win_half
        e = j + win_half

        # Use precomputed local stats
        local_mean = float(mean_y[j])
        local_std = float(std_y[j])
        dv_std = float(std_dv[j])

        amp_thr = 2.5 * local_std
        dv_thr  = 2.5 * dv_std

        # extend left
        while s > 0:
            if (abs(y_out[s] - local_mean) > amp_thr) or (abs(dv[s]) > dv_thr):
                s -= 1
            else:
                break

        # extend right
        while e < n - 1:
            if (abs(y_out[e] - local_mean) > amp_thr) or (abs(dv[e]) > dv_thr):
                e += 1
            else:
                break

        s = max(0, s - tail_extra)
        e = min(n, e + tail_extra)

        if e - s < 3:
            continue

        # Local trend window
        left_start = max(0, s - k_neighbor)
        right_end  = min(n, e + k_neighbor)

        x_fit = t[left_start:right_end]
        y_fit = y_out[left_start:right_end]

        # Spline trend (still accurate, but costly)
        try:
            spline = UnivariateSpline(x_fit, y_fit, s=len(x_fit) * 0.002)
            trend = spline(t[s:e])
        except Exception:
            # fallback: linear
            trend = np.linspace(y_out[s], y_out[e-1], e - s)

        sigma = noise_strength * local_std
        noise = rng.normal(0, sigma, e - s)

        replacement = trend + noise

        # Cosine taper blending
        L = e - s
        w = np.ones(L, dtype=float)

        if feather > 1:
            lf = min(feather, L)
            rf = lf  # same length for right side
            taper_full = get_taper(max(lf, rf))

            w[:lf] = taper_full[:lf]       # rising
            w[-rf:] = taper_full[:rf][::-1]  # falling

        seg = y_out[s:e]
        y_out[s:e] = (1.0 - w) * seg + w * replacement

    return y_out


def bandpass_qrs(v: np.ndarray, fs: float) -> np.ndarray:
    '''
    Emphasize QRS complexes with a narrow 5–15 Hz bandpass filter.

    Args:
        v (ndarray): Input ECG trace (voltage).
        fs (float): Sampling frequency in Hz.

    Returns:
        ndarray: QRS-emphasized version of the ECG trace.

    Theory:
        The QRS complex is dominated by mid-frequency components. A narrow
        bandpass centered around ~10 Hz can suppress slow P/T waves and
        high-frequency noise, making R peaks easier to detect.

        Choice of band:
            - Lower cutoff f_low  = 5 Hz
            - Upper cutoff f_high = 15 Hz

        These values are chosen as a compromise:
            - 5 Hz is high enough to attenuate baseline wander and most P/T
              wave energy.
            - 15 Hz is low enough to reduce EMG and high-frequency noise.

        Implementation:
            1. Normalized cutoffs are computed relative to the Nyquist
               frequency (fs / 2).
            2. A 2nd-order Butterworth bandpass is designed:
                   sos = butter(2, [f_low, f_high], btype="band", output="sos")
            3. The filter is applied with sosfiltfilt to achieve:
                   - Zero-phase distortion (forward/backward filtering).
                   - Doubling of the effective filter order.
    Notes:
        - Once this is going to be merged remove this as it is mostly just for myself
            to remember the theory behind it. And it does not need a docstring this size
            for the function
    '''
    nyq = 0.5 * fs
    lo = 5.0 / nyq
    hi = 15.0 / nyq
    sos = butter(2, [lo, hi], btype="band", output="sos")
    return sosfiltfilt(sos, v.astype(float, copy=False))

def detect_r_peaks(
        t: np.ndarray, 
        v: np.ndarray, 
        fs: float,
        art_times: np.ndarray | None = None
) -> np.ndarray:
    """
    Detect indices of R peaks in an ECG trace.

    Args:
        t (ndarray): Time vector in seconds, same length as `v`.
        v (ndarray): ECG voltage trace (ideally pre-filtered and detrended).
        fs (float): Sampling frequency in Hz.
        art_times (ndarray | None): Optional array of artifact times (s),
            e.g. pacing spikes detected by `detect_artifacts`. R peaks that
            occur too close to these times can be removed.

    Returns:
        ndarray: Integer indices into `t`/`v` where R peaks are detected.

    Method (high level):
        1. QRS-emphasizing bandpass
           The ECG is first passed through `bandpass_qrs` (~=5–15 Hz) to
           suppress baseline wander, P/T waves and very high-frequency noise.
           This produces a signal `v_qrs` in which QRS complexes stand out.

        2. Initial peak detection
           Peaks are detected on `abs(v_qrs)` using
           :func:`scipy.signal.find_peaks`. The minimum distance between peaks
           is chosen to be compatible with physiologic heart rates, and the
           prominence threshold is set relative to the standard deviation of
           `v_qrs` so that only clearly elevated peaks are kept.

        3. Artifact suppression (optional)
           If `art_times` is provided, candidate peaks that fall within a
           small time window around any artifact are discarded, reducing
           false detections from pacing spikes or narrow ADC glitches.

        4. Morphology filtering
           For each remaining candidate, the peak width (and optionally local
           slope) is measured on `v_qrs`. Peaks whose width is outside a
           plausible QRS range (too narrow = noise, too wide = T/P/slow drift)
           are rejected. Very small peaks relative to the median R amplitude
           are also dropped.

        The resulting indices should correspond to one R peak per QRS complex
        in most normal and moderately abnormal rhythms. Heavily distorted
        morphologies or extreme noise may still require manual review or
        additional tuning of the thresholds.
    """
    v_qrs = bandpass_qrs(v, fs)

    # Basic statistics for thresholds
    prom = 0.5 * np.std(v_qrs)
    distance = int(0.25 * fs)

    r_idx, props = find_peaks(v_qrs, prominence=prom, distance=distance)

    if art_times is not None and art_times.size > 0:
        art_idx = np.clip(np.searchsorted(t, art_times), 0, t.size - 1)
        max_gap = int(0.01 * fs)  # 10 ms around artifact

        # art_idx is sorted. For each r_idx, find where it would be inserted
        # and compute distance to the nearest artifact index (left/right).
        pos = np.searchsorted(art_idx, r_idx)

        # indices of nearest artifact on the left and right
        left_idx = np.clip(pos - 1, 0, art_idx.size - 1)
        right_idx = np.clip(pos, 0, art_idx.size - 1)

        left_dist = np.abs(r_idx - art_idx[left_idx])
        right_dist = np.abs(r_idx - art_idx[right_idx])
        min_dist = np.minimum(left_dist, right_dist)

        # keep peak if it's farther than max_gap from *any* artifact
        mask = min_dist > max_gap
        r_idx = r_idx[mask]


    if r_idx.size == 0:
        return r_idx

    # estimate full width at half prominence (in samples)
    widths, _, _, _ = peak_widths(v_qrs, r_idx, rel_height=0.5)
    widths_sec = widths / fs

    # very rough physiological bounds: 30–160 ms
    min_qrs = 0.03  # 30 ms
    max_qrs = 0.16  # 160 ms

    width_ok = (widths_sec >= min_qrs) & (widths_sec <= max_qrs)

    r_idx = r_idx[width_ok]
    if r_idx.size == 0:
        return r_idx

    amp = np.abs(v[r_idx])
    med_amp = np.median(amp)
    keep_amp = amp > 0.4 * med_amp 

    r_idx = r_idx[keep_amp]
    if r_idx.size == 0:
        return r_idx

    return r_idx


def _is_near_artifact(idx: int,
                      art_idx_all: np.ndarray | None,
                      noise_gap: int) -> bool:
    """
    Return True if any artifact index is within `noise_gap` samples of `idx`.

    Uses binary search on the sorted artifact index array so the check is
    O(log M) instead of O(M).
    """
    if art_idx_all is None or art_idx_all.size == 0:
        return False

    # art_idx_all is sorted
    pos = np.searchsorted(art_idx_all, idx)

    # Candidate neighbors: element at pos and pos-1 (if they exist)
    if pos < art_idx_all.size:
        if abs(int(art_idx_all[pos]) - idx) < noise_gap:
            return True
    if pos > 0:
        if abs(int(art_idx_all[pos - 1]) - idx) < noise_gap:
            return True

    return False

@profile
def detect_fiducials(
    t: np.ndarray,
    v: np.ndarray,
    fs: float,
    art_times: np.ndarray | None = None,
) -> list[BeatFeatures]:
    """
    Detect fiducial points P, Q, R, S, and T for each heartbeat.

    Args:
        t (ndarray): Time vector in seconds.
        v (ndarray): ECG voltage trace (preferably detrended/filtered).
        fs (float): Sampling frequency in Hz.
        art_times (ndarray | None): Optional array of artifact times (s),
            e.g. from `detect_artifacts`. P and T points that land too
            close to these times are rejected.

    Returns:
        list[BeatFeatures]: One BeatFeatures instance per detected beat,
        containing indices/times of P, Q, R, S, and T when available.
    """

    qrs_half = int(0.06 * fs)          # +-60 ms for Q and S around R
    p_min = int(0.08 * fs)             # 80 ms
    p_max = int(0.25 * fs)             # 250 ms
    r_ref_span = int(0.02 * fs)        # refine R within +-20 ms
    t_margin_after_S = int(0.06 * fs)  # start T >=60 ms after S
    t_margin_before_Q = int(0.04 * fs) # end T <=40 ms before next Q
    t_last_start = int(0.12 * fs)      # fallback T window for last beat
    t_last_end = int(0.60 * fs)        # fallback T window for last beat
    noise_gap = int(0.01 * fs)         # 10 ms around artifact indices

    # artifact indices (for P/T rejection)
    art_idx_all: np.ndarray | None = None
    if art_times is not None and art_times.size > 0:
        # sorted integer indices into t
        art_idx_all = np.searchsorted(t, art_times).astype(int, copy=False)

    # rough R-peak indices
    r_idx_raw = detect_r_peaks(t, v, fs, art_times=art_times)
    beats: list[BeatFeatures] = []

    # refine R, then find Q, S, P
    for ri0 in r_idx_raw:
        # refine R: snap to actual max of v in +-20 ms window
        left = max(0, ri0 - r_ref_span)
        right = min(t.size, ri0 + r_ref_span)
        seg = v[left:right]
        if seg.size > 0:
            ri = int(left + np.argmax(seg))
        else:
            ri = int(ri0)

        bf = BeatFeatures(r_time=float(t[ri]), r_idx=ri)

        # Q: local min before R
        q_start = max(0, ri - qrs_half)
        q_end = ri
        if q_end > q_start:
            q_local = int(np.argmin(v[q_start:q_end]) + q_start)
            bf.q_idx = q_local
            bf.q_time = float(t[q_local])

        # S: local min after R
        s_start = ri
        s_end = min(t.size, ri + qrs_half * 2)
        if s_end > s_start:
            s_local = int(np.argmin(v[s_start:s_end]) + s_start)
            bf.s_idx = s_local
            bf.s_time = float(t[s_local])

        # P: small peak before QRS
        p_end = max(0, ri - p_min)
        p_start = max(0, ri - p_max)
        if p_end > p_start:
            p_seg = v[p_start:p_end]
            if p_seg.size > 0:
                p_local = int(np.argmax(p_seg) + p_start)

                # reject if P lands on or very near an artifact
                if _is_near_artifact(p_local, art_idx_all, noise_gap):
                    p_local = None

                if p_local is not None:
                    bf.p_idx = p_local
                    bf.p_time = float(t[p_local])

        beats.append(bf)

    # second pass: find T BETWEEN S_i and Q_{i+1}
    for i, bf in enumerate(beats):
        if i < len(beats) - 1:
            next_b = beats[i + 1]

            # choose S_i or fallback R_i
            s_idx = bf.s_idx if bf.s_idx is not None else bf.r_idx
            # choose Q_{i+1} or fallback R_{i+1}
            q_next_idx = next_b.q_idx if next_b.q_idx is not None else next_b.r_idx

            t_start = s_idx + t_margin_after_S
            t_end = q_next_idx - t_margin_before_Q
        else:
            # last beat: simple window after R
            t_start = bf.r_idx + t_last_start
            t_end = min(t.size - 1, bf.r_idx + t_last_end)

        if t_end <= t_start:
            continue

        t_start = max(0, t_start)
        t_end = min(t.size, t_end)

        seg = v[t_start:t_end]
        if seg.size == 0:
            continue

        # dominant peak in between-beats segment (handles inverted T with abs)
        t_rel = int(np.argmax(np.abs(seg)))
        t_local = t_start + t_rel

        # reject T if on/near artifact
        if _is_near_artifact(t_local, art_idx_all, noise_gap):
            continue

        bf.t_idx = t_local
        bf.t_time = float(t[t_local])

    return beats
