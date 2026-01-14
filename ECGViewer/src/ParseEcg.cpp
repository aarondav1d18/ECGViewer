// ParseECG.cpp (condensed, same behavior)
#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#endif


namespace py = pybind11;

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

static inline bool is_space(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
}

static inline const char *skip_spaces(const char *p, const char *end) {
    while (p < end && is_space(static_cast<unsigned char>(*p))) ++p;
    return p;
}

static inline const char *skip_to_eol(const char *p, const char *end) {
    while (p < end && *p != '\n') ++p;
    if (p < end && *p == '\n') ++p;
    return p;
}

static inline bool starts_with(const char *p, const char *end, const char *lit) {
    const std::size_t n = std::strlen(lit);
    return static_cast<std::size_t>(end - p) >= n && std::memcmp(p, lit, n) == 0;
}

static inline double pow10_i(int e) {
    static const double pos[] = {
        1e0,  1e1,  1e2,  1e3,  1e4,  1e5,  1e6,  1e7,  1e8,  1e9,
        1e10, 1e11, 1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19,
        1e20, 1e21, 1e22
    };
    static const double neg[] = {
        1e0,  1e-1,  1e-2,  1e-3,  1e-4,  1e-5,  1e-6,  1e-7,  1e-8,  1e-9,
        1e-10, 1e-11, 1e-12, 1e-13, 1e-14, 1e-15, 1e-16, 1e-17, 1e-18, 1e-19,
        1e-20, 1e-21, 1e-22
    };

    if (e >= 0) {
        if (e <= 22) return pos[e];
        return std::pow(10.0, static_cast<double>(e));
    }

    int a = -e;
    if (a <= 22) return neg[a];
    return std::pow(10.0, static_cast<double>(e));
}

static inline bool parse_double(const char *p, const char *end, double &out, const char *&next) {
    p = skip_spaces(p, end);
    if (p >= end) return false;

    bool neg = false;
    if (*p == '+' || *p == '-') {
        neg = (*p == '-');
        ++p;
        if (p >= end) return false;
    }

    uint64_t int_part = 0;
    bool saw_digit = false;
    while (p < end) {
        unsigned char c = static_cast<unsigned char>(*p);
        if (c < '0' || c > '9') break;
        saw_digit = true;

        uint64_t d = static_cast<uint64_t>(c - '0');
        if (int_part <= (UINT64_MAX - d) / 10) int_part = int_part * 10 + d;
        ++p;
    }

    uint64_t frac_part = 0;
    int frac_digits = 0;
    if (p < end && *p == '.') {
        ++p;
        while (p < end) {
            unsigned char c = static_cast<unsigned char>(*p);
            if (c < '0' || c > '9') break;
            saw_digit = true;

            if (frac_digits < 18) {
                frac_part = frac_part * 10 + static_cast<uint64_t>(c - '0');
                ++frac_digits;
            }
            ++p;
        }
    }

    if (!saw_digit) return false;

    int exp10 = 0;
    if (p < end && (*p == 'e' || *p == 'E')) {
        const char *pe = p + 1;
        if (pe < end) {
            bool exp_neg = false;
            if (*pe == '+' || *pe == '-') {
                exp_neg = (*pe == '-');
                ++pe;
            }

            int e = 0;
            bool saw_e = false;
            while (pe < end) {
                unsigned char c = static_cast<unsigned char>(*pe);
                if (c < '0' || c > '9') break;
                saw_e = true;
                if (e < 10000) e = e * 10 + static_cast<int>(c - '0');
                ++pe;
            }

            if (saw_e) {
                exp10 = exp_neg ? -e : e;
                p = pe;
            }
        }
    }

    double val = static_cast<double>(int_part);
    if (frac_digits > 0) val += static_cast<double>(frac_part) * pow10_i(-frac_digits);
    if (exp10 != 0) val *= pow10_i(exp10);

    out = neg ? -val : val;
    next = p;
    return true;
}

static double median_in_place(std::vector<double> &v) {
    std::size_t n = v.size();
    if (n == 0) return 0.0;

    std::size_t mid = n / 2;
    std::nth_element(v.begin(), v.begin() + static_cast<long>(mid), v.end());
    double m = v[mid];
    if (n % 2 == 1) return m;

    auto max_it = std::max_element(v.begin(), v.begin() + static_cast<long>(mid));
    return 0.5 * (m + *max_it);
}

static inline std::optional<std::string> read_trimmed_eol_text(const char *&p, const char *end) {
    p = skip_spaces(p, end);
    const char *s = p;
    while (p < end && *p != '\n' && *p != '\r') ++p;

    const char *e = p;
    while (e > s && is_space(static_cast<unsigned char>(e[-1]))) --e;

    if (e <= s) return std::nullopt;
    return std::string(s, e);
}

static EcgData parse_ecg_bytes(const char *buf, std::size_t len) {
    EcgData result;

    const char *p = buf;
    const char *end = buf + len;

    std::size_t est_rows = len / 32;
    if (est_rows < 256'000) est_rows = 256'000;
    if (est_rows > 50'000'000) est_rows = 50'000'000;
    result.t.reserve(est_rows);
    result.v.reserve(est_rows);

    while (p < end) {
        p = skip_spaces(p, end);
        if (p >= end) break;

        if (starts_with(p, end, "Interval=")) {
            p += 9;
            double interval = 0.0;
            const char *next = nullptr;
            if (parse_double(p, end, interval, next)) result.meta.interval_s = interval;
            p = skip_to_eol(p, end);
            continue;
        }

        if (starts_with(p, end, "ChannelTitle=")) {
            p += 13;
            if (auto s = read_trimmed_eol_text(p, end)) result.meta.channel_title = std::move(*s);
            p = skip_to_eol(p, end);
            continue;
        }

        if (starts_with(p, end, "Range=")) {
            p += 6;
            if (auto s = read_trimmed_eol_text(p, end)) result.meta.range = std::move(*s);
            p = skip_to_eol(p, end);
            continue;
        }

        // Fast-skip any header-ish line that contains '=' before whitespace.
        {
            const char *q = p;
            while (q < end && !is_space(static_cast<unsigned char>(*q)) && *q != '\n' && *q != '\r') {
                if (*q == '=') {
                    p = skip_to_eol(p, end);
                    goto continue_outer;
                }
                ++q;
            }
        }

        // Numeric row: two floats
        {
            double t_val = 0.0;
            const char *p0_end = nullptr;
            if (!parse_double(p, end, t_val, p0_end)) {
                p = skip_to_eol(p, end);
                goto continue_outer;
            }

            double v_val = 0.0;
            const char *p1_end = nullptr;
            if (!parse_double(p0_end, end, v_val, p1_end)) {
                p = skip_to_eol(p, end);
                goto continue_outer;
            }

            result.t.push_back(t_val);
            result.v.push_back(v_val);
            p = skip_to_eol(p1_end, end);
        }

    continue_outer:
        continue;
    }

    if (result.t.empty()) throw std::runtime_error("No numeric data rows were found.");

    if (result.meta.interval_s && *result.meta.interval_s > 0.0) {
        result.fs = 1.0 / *result.meta.interval_s;
    } else if (result.t.size() > 1) {
        std::vector<double> dt;
        dt.reserve(result.t.size() - 1);
        for (std::size_t i = 1; i < result.t.size(); ++i) dt.push_back(result.t[i] - result.t[i - 1]);

        double med_dt = median_in_place(dt);
        if (med_dt > 0.0 && std::isfinite(med_dt)) result.fs = 1.0 / med_dt;
    }

    return result;
}

static EcgData parse_ecg_file_cpp(const std::string &path) {
#if defined(__unix__) || defined(__APPLE__)
    int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) throw std::runtime_error("Could not open ECG file: " + path);

    struct stat st;
    if (::fstat(fd, &st) != 0) {
        ::close(fd);
        throw std::runtime_error("Could not stat ECG file: " + path);
    }

    if (st.st_size <= 0) {
        ::close(fd);
        throw std::runtime_error("ECG file is empty: " + path);
    }

    std::size_t len = static_cast<std::size_t>(st.st_size);
    void *map = ::mmap(nullptr, len, PROT_READ, MAP_PRIVATE, fd, 0);
    ::close(fd);
    if (map == MAP_FAILED) throw std::runtime_error("mmap failed for ECG file: " + path);

    try {
        EcgData data = parse_ecg_bytes(static_cast<const char *>(map), len);
        ::munmap(map, len);
        return data;
    } catch (...) {
        ::munmap(map, len);
        throw;
    }
#elif defined(_WIN32)
    // Convert UTF-8 std::string -> UTF-16 for Win32 wide APIs
    int wlen = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    if (wlen <= 0) {
        throw std::runtime_error("Invalid UTF-8 path.");
    }

    std::wstring wpath;
    wpath.resize(static_cast<std::size_t>(wlen - 1));
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, &wpath[0], wlen);

    HANDLE hFile = CreateFileW(
        wpath.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );
    if (hFile == INVALID_HANDLE_VALUE) {
        throw std::runtime_error("Could not open ECG file: " + path);
    }

    LARGE_INTEGER size;
    if (!GetFileSizeEx(hFile, &size)) {
        CloseHandle(hFile);
        throw std::runtime_error("Could not stat ECG file: " + path);
    }

    if (size.QuadPart <= 0) {
        CloseHandle(hFile);
        throw std::runtime_error("ECG file is empty: " + path);
    }

    if (size.QuadPart > static_cast<LONGLONG>(std::numeric_limits<std::size_t>::max())) {
        CloseHandle(hFile);
        throw std::runtime_error("ECG file too large to map in this build: " + path);
    }

    std::size_t len = static_cast<std::size_t>(size.QuadPart);

    HANDLE hMap = CreateFileMappingW(hFile, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (!hMap) {
        CloseHandle(hFile);
        throw std::runtime_error("CreateFileMapping failed for ECG file: " + path);
    }

    void *view = MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, 0);
    if (!view) {
        CloseHandle(hMap);
        CloseHandle(hFile);
        throw std::runtime_error("MapViewOfFile failed for ECG file: " + path);
    }

    try {
        EcgData data = parse_ecg_bytes(static_cast<const char *>(view), len);
        UnmapViewOfFile(view);
        CloseHandle(hMap);
        CloseHandle(hFile);
        return data;
    } catch (...) {
        UnmapViewOfFile(view);
        CloseHandle(hMap);
        CloseHandle(hFile);
        throw;
    }

#else
    // Fallback (non-unix): read file into memory, then parse.
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) {
        throw std::runtime_error("Could not open ECG file: " + path);
    }

    f.seekg(0, std::ios::end);
    std::streampos endp = f.tellg();
    if (endp <= 0) {
        throw std::runtime_error("ECG file is empty: " + path);
    }
    f.seekg(0, std::ios::beg);

    std::string buf;
    buf.resize(static_cast<std::size_t>(endp));
    f.read(&buf[0], static_cast<std::streamsize>(buf.size()));

    return parse_ecg_bytes(buf.data(), buf.size());
#endif
}
static py::object opt_to_py(const std::optional<double> &v) {
    if (v) {
        return py::float_(*v);
    }
    return py::none();
}

static py::object opt_to_py(const std::optional<std::string> &v) {
    if (v) {
        return py::str(*v);
    }
    return py::none();
}


static py::tuple parse_ecg_file_py(const std::string &path) {
    EcgData data = [&]() {
        py::gil_scoped_release release;
        return parse_ecg_file_cpp(path);
    }();

    auto *t_vec = new std::vector<double>(std::move(data.t));
    auto *v_vec = new std::vector<double>(std::move(data.v));

    py::ssize_t n = static_cast<py::ssize_t>(t_vec->size());
    if (n != static_cast<py::ssize_t>(v_vec->size())) {
        delete t_vec;
        delete v_vec;
        throw std::runtime_error("Internal error: t and v sizes differ.");
    }

    py::capsule t_caps(t_vec, [](void *p) { delete static_cast<std::vector<double> *>(p); });
    py::capsule v_caps(v_vec, [](void *p) { delete static_cast<std::vector<double> *>(p); });

    py::array t_arr(
        py::buffer_info(
            t_vec->data(), sizeof(double), py::format_descriptor<double>::format(),
            1, { n }, { static_cast<py::ssize_t>(sizeof(double)) }
        ),
        t_caps
    );
    py::array v_arr(
        py::buffer_info(
            v_vec->data(), sizeof(double), py::format_descriptor<double>::format(),
            1, { n }, { static_cast<py::ssize_t>(sizeof(double)) }
        ),
        v_caps
    );

    py::dict meta;
    meta["interval_s"] = opt_to_py(data.meta.interval_s);
    meta["channel_title"] = opt_to_py(data.meta.channel_title);
    meta["range"] = opt_to_py(data.meta.range);

    return py::make_tuple(t_arr, v_arr, opt_to_py(data.fs), meta);
}

PYBIND11_MODULE(parseECG, m) {
    m.doc() = "ECG text file parser (C++ implementation, optimized)";
    m.def("parse_ecg_file", &parse_ecg_file_py, py::arg("path"), R"pbdoc(
Parse an ECG text file into (t, v, fs, meta).

Optimized parser for LabChart-style exports:
- Interval= header sets sampling rate (fast path)
- Numeric rows: <time> <value> (whitespace separated)
- Skips other headers quickly

Returns:
    t: numpy.ndarray float64
    v: numpy.ndarray float64
    fs: float or None
    meta: dict
)pbdoc");
}
