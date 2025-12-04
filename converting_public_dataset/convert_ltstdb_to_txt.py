import wfdb
import numpy as np
import argparse

def convert(record_path, output_file):
    # Load file (WFDB will find .dat and .hea automatically)
    sig, fields = wfdb.rdsamp(record_path)

    v = sig[:, 0]              # first ECG channel
    fs = fields["fs"]          # sampling frequency
    dt = 1.0 / fs              # sampling interval
    t = np.arange(len(v)) * dt # time vector

    with open(output_file, "w") as f:
        f.write(f"Interval=\t{dt} s\n")
        f.write(f"ChannelTitle=\t{fields['sig_name'][0]}\n")
        f.write("Range=\t10.000 V\n")

        for ti, vi in zip(t, v):
            f.write(f"{ti:.6f}\t{vi:.8f}\n")

    print("✔ Conversion complete")
    print(f"   Input record: {record_path}")
    print(f"   Output file:  {output_file}")
    print(f"   Samples:      {len(v)}")
    print(f"   Duration:     {len(v)/fs/3600:.2f} hours")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("record", help="Path to record (without extension)")
    parser.add_argument("output", help="Output .txt file")
    args = parser.parse_args()

    convert(args.record, args.output)
