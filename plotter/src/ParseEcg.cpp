// parse_ecg.cpp

#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>  // std::memcmp
#include <cstdlib>  // std::strtod
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace py = pybind11;

// Core C++ parser types 

struct EcgMeta {
    std::optional<double> interval_s;
    std::optional<std::string> channel_title;
    std::optional<std::string> range;
};

struct EcgData {
    std::vector<double> t;
    std::vector<double> v;
    std::optional<double> fs;
    EcgMeta meta;
};

// Small helpers 

static inline void trim_in_place(std::string &s) {
    std::size_t start = 0;
    while (start < s.size() &&
           std::isspace(static_cast<unsigned char>(s[start]))) {
        ++start;
    }
    std::size_t end = s.size();
    while (end > start &&
           std::isspace(static_cast<unsigned char>(s[end - 1]))) {
        --end;
    }
    if (start == 0 && end == s.size()) return;
    s.assign(s.begin() + static_cast<long>(start),
             s.begin() + static_cast<long>(end));
}

static inline bool starts_with(const std::string &s, const char *prefix) {
    const std::size_t n = std::char_traits<char>::length(prefix);
    return s.size() >= n && std::equal(prefix, prefix + n, s.begin());
}

static inline bool is_numeric_token(const std::string &s) {
    if (s.empty()) return false;
    char c = s[0];
    return std::isdigit(static_cast<unsigned char>(c)) ||
           c == '+' || c == '-' || c == '.';
}

// Median in O(n) expected time using nth_element
static double median_in_place(std::vector<double> &v) {
    const std::size_t n = v.size();
    if (n == 0) return 0.0;

    std::size_t mid = n / 2;
    std::nth_element(v.begin(), v.begin() + static_cast<long>(mid), v.end());
    double m = v[mid];

    if (n % 2 == 1) {
        // odd
        return m;
    } else {
        // even – need max of lower half too
        auto max_it = std::max_element(v.begin(), v.begin() + static_cast<long>(mid));
        return 0.5 * (m + *max_it);
    }
}

/**
 * @brief Read in ECG data from a text file in the specified format.
 * @details The function reads the ECG text file line by line, parsing header
 * information and numeric data points. It extracts time and voltage values,
 * as well as metadata such as sampling interval, channel title, and range.
 * The function returns an EcgData structure containing the parsed data.
 * Exceptions are thrown if the file cannot be opened or if no numeric data
 * rows are found. The function pre-reserves space in the vectors to optimize
 * performance for typical file sizes.
 * 
 * @param path The file path to the ECG text file.
 * @return EcgData The parsed ECG data including time, voltage, sampling frequency, and metadata.
 */
EcgData parse_ecg_file_cpp(const std::string &path) {
    std::ifstream f(path);
    if (!f.is_open()) {
        throw std::runtime_error("Could not open ECG file: " + path);
    }

    // give the file stream a larger buffer (1 MB)
    static constexpr std::size_t BUF_SIZE = 1 << 20;
    std::vector<char> filebuf(BUF_SIZE);
    f.rdbuf()->pubsetbuf(filebuf.data(), static_cast<std::streamsize>(filebuf.size()));

    EcgData result;
    // Pre-reserve some space to avoid many reallocations.
    // Adjust this heuristic if your files are much larger/smaller.
    result.t.reserve(256'000);
    result.v.reserve(256'000);

    std::string line;

    while (std::getline(f, line)) {
        const char* p = line.c_str();
        const char* end = p + line.size();

        // skip leading whitespace
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\r')) {
            ++p;
        }
        if (p >= end) {
            continue;  // empty/whitespace-only line
        }

        // headers 
        // Interval=
        if (end - p >= 9 && std::memcmp(p, "Interval=", 9) == 0) {
            const char* rhs = p + 9;

            // replace tabs with spaces as in Python
            std::string tmp(rhs, end);
            for (char &ch : tmp) {
                if (ch == '\t')
                    ch = ' ';
            }

            const char* q = tmp.c_str();
            const char* qend = q + tmp.size();

            // skip leading spaces
            while (q < qend && (*q == ' ' || *q == '\t')) {
                ++q;
            }

            char* num_end = nullptr;
            double interval = std::strtod(q, &num_end);
            if (num_end != q) {  // conversion succeeded
                result.meta.interval_s = interval;
            }
            continue;
        }

        // ChannelTitle=
        if (end - p >= 13 && std::memcmp(p, "ChannelTitle=", 13) == 0) {
            const char* rhs = p + 13;
            // trim both ends
            while (rhs < end && (*rhs == ' ' || *rhs == '\t')) {
                ++rhs;
            }
            const char* rhs_end = end;
            while (rhs_end > rhs &&
                   (rhs_end[-1] == ' ' || rhs_end[-1] == '\t' || rhs_end[-1] == '\r')) {
                --rhs_end;
            }
            if (rhs < rhs_end) {
                result.meta.channel_title = std::string(rhs, rhs_end);
            }
            continue;
        }

        // Range=
        if (end - p >= 6 && std::memcmp(p, "Range=", 6) == 0) {
            const char* rhs = p + 6;
            while (rhs < end && (*rhs == ' ' || *rhs == '\t')) {
                ++rhs;
            }
            const char* rhs_end = end;
            while (rhs_end > rhs &&
                   (rhs_end[-1] == ' ' || rhs_end[-1] == '\t' || rhs_end[-1] == '\r')) {
                --rhs_end;
            }
            if (rhs < rhs_end) {
                result.meta.range = std::string(rhs, rhs_end);
            }
            continue;
        }

        // other headers ignored...

        // numeric data line 
        // replace tabs with spaces (in-place in std::string)
        for (char &ch : line) {
            if (ch == '\t')
                ch = ' ';
        }
        // re-compute pointers since line data may have moved
        p = line.c_str();
        end = p + line.size();

        // skip leading whitespace again after modification
        while (p < end && std::isspace(static_cast<unsigned char>(*p))) {
            ++p;
        }
        if (p >= end) {
            continue;
        }

        // parse first double (t)
        char* p0_end = nullptr;
        double t_val = std::strtod(p, &p0_end);
        if (p0_end == p) {
            // no conversion – not a numeric line
            continue;
        }

        // skip whitespace between first and second token
        const char* p1 = p0_end;
        while (p1 < end && std::isspace(static_cast<unsigned char>(*p1))) {
            ++p1;
        }
        if (p1 >= end) {
            continue;  // only one numeric token
        }

        // parse second double (v)
        char* p1_end = nullptr;
        double v_val = std::strtod(p1, &p1_end);
        if (p1_end == p1) {
            // second token not numeric
            continue;
        }

        result.t.push_back(t_val);
        result.v.push_back(v_val);
    }

    if (result.t.empty()) {
        throw std::runtime_error("No numeric data rows were found.");
    }

    // sampling frequency fs 
    if (result.meta.interval_s && *result.meta.interval_s > 0.0) {
        result.fs = 1.0 / *result.meta.interval_s;
    } else if (result.t.size() > 1) {
        std::vector<double> dt;
        dt.reserve(result.t.size() - 1);
        for (std::size_t i = 1; i < result.t.size(); ++i) {
            dt.push_back(result.t[i] - result.t[i - 1]);
        }

        double med_dt = median_in_place(dt);
        if (med_dt > 0.0 && std::isfinite(med_dt)) {
            result.fs = 1.0 / med_dt;
        }
    }

    return result;
}


// Pybind11 wrapper 
static py::tuple parse_ecg_file_py(const std::string &path) {
    EcgData data = parse_ecg_file_cpp(path);

    // Create numpy arrays (copies once from std::vector -> numpy buffer)
    py::array_t<double> t_arr(data.t.size());
    py::array_t<double> v_arr(data.v.size());

    auto t_buf = t_arr.mutable_unchecked<1>();
    auto v_buf = v_arr.mutable_unchecked<1>();

    for (ssize_t i = 0; i < static_cast<ssize_t>(data.t.size()); ++i) {
        t_buf(i) = data.t[static_cast<std::size_t>(i)];
        v_buf(i) = data.v[static_cast<std::size_t>(i)];
    }

    // fs: float or None
    py::object fs_obj = py::none();
    if (data.fs) {
        fs_obj = py::float_(*data.fs);
    }

    // meta dict
    py::dict meta;
    if (data.meta.interval_s) {
        meta["interval_s"] = py::float_(*data.meta.interval_s);
    } else {
        meta["interval_s"] = py::none();
    }

    if (data.meta.channel_title) {
        meta["channel_title"] = py::str(*data.meta.channel_title);
    } else {
        meta["channel_title"] = py::none();
    }

    if (data.meta.range) {
        meta["range"] = py::str(*data.meta.range);
    } else {
        meta["range"] = py::none();
    }

    // Return (t, v, fs, meta)
    return py::make_tuple(t_arr, v_arr, fs_obj, meta);
}

PYBIND11_MODULE(parse_ecg, m) {
    m.doc() = "ECG text file parser (C++ implementation)";
    m.def("parse_ecg_file", &parse_ecg_file_py,
          py::arg("path"),
          R"pbdoc(
Parse an ECG text file into (t, v, fs, meta).

Parameters:

path : str
    Path to the ECG text file.

Returns:

t : numpy.ndarray (float64, 1D)
    Absolute time values.
v : numpy.ndarray (float64, 1D)
    Voltage readings.
fs : float or None
    Sampling rate in Hz. If "Interval=" was present, fs = 1 / Interval.
    Otherwise it is estimated as 1 / median(delta_t), or None if it
    cannot be computed.
meta : dict
    Dictionary with keys:
        - "interval_s"
        - "channel_title"
        - "range"
    Each value is either a scalar/string or None.
)pbdoc");
}
