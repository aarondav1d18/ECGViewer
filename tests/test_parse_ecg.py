import gc
import math
from pathlib import Path

import numpy as np
import pytest

from ECGViewer import parse_ecg_file_cpp


def write_tmp(tmp_path: Path, text: str) -> Path:
    p = tmp_path / "ecg.txt"
    p.write_text(text, encoding="utf-8", newline="\n")
    return p


def assert_float_or_none(x):
    assert x is None or isinstance(x, float)


def test_parses_with_interval_and_meta(tmp_path):
    p = write_tmp(
        tmp_path,
        "\n".join(
            [
                "Interval=0.004",
                "ChannelTitle= Lead I  ",
                "Range= +/- 2 mV",
                "SomeOtherHeader=whatever",
                "0.000  0.1",
                "0.004  0.2  123 456",  # extra columns ignored
                "0.008  0.3",
                "",
            ]
        ),
    )

    t, v, fs, meta = parse_ecg_file_cpp(str(p))

    assert isinstance(t, np.ndarray)
    assert isinstance(v, np.ndarray)
    assert t.dtype == np.float64
    assert v.dtype == np.float64
    assert t.shape == v.shape == (3,)

    np.testing.assert_allclose(t, np.array([0.0, 0.004, 0.008], dtype=np.float64))
    np.testing.assert_allclose(v, np.array([0.1, 0.2, 0.3], dtype=np.float64))

    assert fs == pytest.approx(250.0)  # 1 / 0.004

    assert meta["interval_s"] == pytest.approx(0.004)
    assert meta["channel_title"] == "Lead I"
    assert meta["range"] == "+/- 2 mV"


def test_infers_fs_from_median_dt(tmp_path):
    # dt values: 0.01, 0.02, 0.01 -> median = 0.01 -> fs=100
    p = write_tmp(
        tmp_path,
        "\n".join(
            [
                "ChannelTitle=X",
                "0.00 1.0",
                "0.01 1.1",
                "0.03 1.2",
                "0.04 1.3",
            ]
        ),
    )

    t, v, fs, meta = parse_ecg_file_cpp(str(p))

    assert fs == pytest.approx(100.0)
    assert meta["interval_s"] is None
    assert meta["channel_title"] == "X"


def test_interval_invalid_falls_back_to_time_inference(tmp_path):
    p = write_tmp(
        tmp_path,
        "\n".join(
            [
                "Interval=abc",
                "0.0 1.0",
                "0.5 2.0",
                "1.0 3.0",
            ]
        ),
    )

    t, v, fs, meta = parse_ecg_file_cpp(str(p))

    assert meta["interval_s"] is None
    assert fs == pytest.approx(2.0)  # median dt = 0.5 => fs=2


def test_interval_zero_does_not_set_fs_from_interval(tmp_path):
    p = write_tmp(
        tmp_path,
        "\n".join(
            [
                "Interval=0",
                "0.0 1.0",
                "0.1 2.0",
                "0.2 3.0",
            ]
        ),
    )

    _, _, fs, meta = parse_ecg_file_cpp(str(p))

    # interval_s gets parsed as 0.0, but fs should be inferred from t
    assert meta["interval_s"] == pytest.approx(0.0)
    assert fs == pytest.approx(10.0)


def test_skips_headerish_lines_with_equals(tmp_path):
    p = write_tmp(
        tmp_path,
        "\n".join(
            [
                "RandomKey=RandomValue",
                "Another=Thing",
                "0 1",
                "1 2",
            ]
        ),
    )

    t, v, fs, meta = parse_ecg_file_cpp(str(p))
    np.testing.assert_allclose(t, [0.0, 1.0])
    np.testing.assert_allclose(v, [1.0, 2.0])
    assert_float_or_none(fs)


def test_numeric_formats_exponent_and_sign(tmp_path):
    p = write_tmp(
        tmp_path,
        "\n".join(
            [
                "Interval=1e-2",
                "-1e0   +2.5E+2",
                ".5     -3.0",
            ]
        ),
    )

    t, v, fs, meta = parse_ecg_file_cpp(str(p))

    np.testing.assert_allclose(t, [-1.0, 0.5])
    np.testing.assert_allclose(v, [250.0, -3.0])
    assert fs == pytest.approx(100.0)  # 1 / 0.01
    assert meta["interval_s"] == pytest.approx(0.01)


def test_raises_if_no_numeric_rows(tmp_path):
    p = write_tmp(
        tmp_path,
        "\n".join(
            [
                "Interval=0.01",
                "ChannelTitle=Lead",
                "Range=2mV",
                "NotNumeric notnumeric",
                "Foo=Bar",
            ]
        ),
    )

    with pytest.raises(RuntimeError, match="No numeric data rows were found"):
        parse_ecg_file_cpp(str(p))


def test_arrays_survive_gc(tmp_path):
    p = write_tmp(tmp_path, "0 1\n1 2\n2 3\n")
    t, v, fs, meta = parse_ecg_file_cpp(str(p))

    for _ in range(5):
        gc.collect()

    assert float(t[0]) == 0.0
    assert float(v[-1]) == 3.0
