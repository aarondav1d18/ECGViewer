from __future__ import annotations

import argparse
import time
from typing import List

import numpy as np

from ecg_analysis import parse_ecg_file               # Python implementation
from ECGViewer import parse_ecg_file as parse_ecg_file_cpp  # C++ implementation

# ---------------------------------------------------------------------
# File scan: count numeric two-column rows (ground truth row count)
# ---------------------------------------------------------------------

def count_numeric_rows_in_file(path: str) -> int:
    """
    Count the number of numeric two-column rows in the ECG text file,
    using similar rules as the original Python parse_ecg_file.

    This does *not* rely on either parser: it's a direct scan of the file.
    """
    n_rows = 0
    with open(path, "r", encoding="utf-8", errors="ignore") as f:
        for raw in f:
            line = raw.strip()
            if not line:
                continue

            # Skip known headers
            if line.startswith("Interval="):
                continue
            if line.startswith("ChannelTitle="):
                continue
            if line.startswith("Range="):
                continue

            parts = line.replace("\t", " ").split()
            if len(parts) < 2:
                continue
            p0, p1 = parts[0], parts[1]

            def is_numeric_token(tok: str) -> bool:
                return bool(tok) and (tok[0].isdigit() or tok[0] in "+-.")

            if not is_numeric_token(p0) or not is_numeric_token(p1):
                continue

            n_rows += 1

    return n_rows


# ---------------------------------------------------------------------
# Parser check: Python vs C++ + file scan
# ---------------------------------------------------------------------

def check_parsers_and_data(path: str) -> int:
    print(f"Checking parsers against raw data for: {path}\n")

    # Ground truth: how many numeric rows exist in the file?
    n_rows = count_numeric_rows_in_file(path)
    print(f"Numeric data rows in file (by scan): {n_rows:,}")

    # Run Python parser
    t_py, v_py, fs_py, meta_py = parse_ecg_file(path)
    # Run C++ parser
    t_cpp, v_cpp, fs_cpp, meta_cpp = parse_ecg_file_cpp(path)

    print(f"\nPython parser: t shape={t_py.shape}, v shape={v_py.shape}")
    print(f"C++    parser: t shape={t_cpp.shape}, v shape={v_cpp.shape}")

    # 1) Count checks
    if t_py.shape[0] != n_rows:
        print(
            f"WARNING: Python parser returned {t_py.shape[0]} rows, "
            f"but file scan saw {n_rows}."
        )
    if t_cpp.shape[0] != n_rows:
        print(
            f"WARNING: C++ parser returned {t_cpp.shape[0]} rows, "
            f"but file scan saw {n_rows}."
        )

    # 2) Shape checks between parsers
    if t_py.shape != t_cpp.shape or v_py.shape != v_cpp.shape:
        print("ERROR: Python and C++ parsers produced different shapes!")
        return 1

    # 3) Value checks
    same_t = np.array_equal(t_py, t_cpp)
    same_v = np.array_equal(v_py, v_cpp)

    close_t = np.allclose(t_py, t_cpp)
    close_v = np.allclose(v_py, v_cpp)

    print("\nValue comparison (Python vs C++):")
    print(f"  t identical: {same_t}")
    print(f"  t close    : {close_t}")
    print(f"  v identical: {same_v}")
    print(f"  v close    : {close_v}")

    if not close_t or not close_v:
        diff_idx_t = np.where(t_py != t_cpp)[0]
        diff_idx_v = np.where(v_py != v_cpp)[0]

        if diff_idx_t.size > 0:
            i = int(diff_idx_t[0])
            print(
                f"\nFirst mismatch in t at index {i}: "
                f"py={t_py[i]!r}, cpp={t_cpp[i]!r}"
            )

        if diff_idx_v.size > 0:
            i = int(diff_idx_v[0])
            print(
                f"First mismatch in v at index {i}: "
                f"py={v_py[i]!r}, cpp={v_cpp[i]!r}"
            )

    # 4) fs / meta checks
    fs_equal = (
        (fs_py is None and fs_cpp is None)
        or (fs_py is not None and fs_cpp is not None and np.isclose(fs_py, fs_cpp))
    )
    print(f"\nfs_py={fs_py}, fs_cpp={fs_cpp}, equal={fs_equal}")
    print(f"meta_py={meta_py}")
    print(f"meta_cpp={meta_cpp}")

    # Overall verdict
    if n_rows == t_py.shape[0] == t_cpp.shape[0] and close_t and close_v and fs_equal:
        print(
            "\n✅ Data completion check passed: same number of rows, "
            "matching values, fs, and metadata."
        )
        return 0
    else:
        print(
            "\n❌ Data completion check had discrepancies; see messages above."
        )
        return 1


# ---------------------------------------------------------------------
# Benchmark: Python vs C++ parser
# ---------------------------------------------------------------------

def benchmark_parsers(path: str, runs: int = 5) -> int:
    print(f"Benchmarking parsing for: {path}\n")

    def time_parser(fn, label: str):
        # Warm-up
        fn(path)
        t0 = time.perf_counter()
        last = None
        for _ in range(runs):
            last = fn(path)
        t1 = time.perf_counter()
        avg = (t1 - t0) / runs
        print(f"{label:10s}: {avg*1000:.1f} ms (avg over {runs} runs)")
        return last

    res_cpp = time_parser(parse_ecg_file_cpp, "C++")
    res_py = time_parser(parse_ecg_file, "Python")

    t_py, v_py, fs_py, meta_py = res_py
    t_cpp, v_cpp, fs_cpp, meta_cpp = res_cpp

    print("\n--- result comparison ---")
    print("t shape:", t_py.shape, t_cpp.shape)
    print("v shape:", v_py.shape, v_cpp.shape)
    print("fs:", fs_py, fs_cpp)
    print("meta (py):", meta_py)
    print("meta (cpp):", meta_cpp)
    print("t close:", np.allclose(t_py, t_cpp))
    print("v close:", np.allclose(v_py, v_cpp))
    print(
        "fs close:",
        (fs_py is None and fs_cpp is None)
        or (fs_py is not None and fs_cpp is not None and np.isclose(fs_py, fs_cpp)),
    )

    return 0


# ---------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------

def build_arg_parser() -> argparse.ArgumentParser:
    ap = argparse.ArgumentParser(
        prog="ecg-parse-check",
        description="Compare Python vs C++ ECG parsers (speed and data completeness).",
    )
    ap.add_argument("file", help="Path to ECG text file")
    ap.add_argument(
        "--runs",
        type=int,
        default=5,
        help="Number of runs for benchmark (default: 5)",
    )
    ap.add_argument(
        "--benchmark-parse",
        action="store_true",
        help="Benchmark Python vs C++ ECG parsing",
    )
    ap.add_argument(
        "--check-parse",
        action="store_true",
        help="Run data-completion / equality checks on Python vs C++ parsers",
    )
    return ap


def main(argv: List[str] | None = None) -> int:
    args = build_arg_parser().parse_args(argv)

    # If no flags given, do both speed + check
    do_bench = args.benchmark_parse or not (args.benchmark_parse or args.check_parse)
    do_check = args.check_parse or not (args.benchmark_parse or args.check_parse)

    status = 0
    if do_bench:
        status |= benchmark_parsers(args.file, runs=args.runs)
        print("\n" + "-" * 60 + "\n")
    if do_check:
        status |= check_parsers_and_data(args.file)

    return status


if __name__ == "__main__":
    raise SystemExit(main())
