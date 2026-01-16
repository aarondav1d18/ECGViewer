/**
 * @file ECGModule.cpp
 * @brief Pybind11 bridge for launching the Qt ECG viewer from Python.
 *
 * Exposes a single function `show_ecg_viewer(...)` that:
 * - Validates array shapes (expects 1D arrays).
 * - Copies NumPy arrays into QVector for Qt/QCustomPlot usage.
 * - Reuses an existing QApplication if one already exists (common in Python Qt apps),
 *   otherwise creates a local QApplication and runs the event loop.
 *
 * The viewer supports:
 * - Original vs cleaned traces, optional artifact hiding
 * - Artifact mask overlay behavior (viewer-side)
 * - Optional fixed y-limits
 * - Fiducial point overlays (P/Q/R/S/T time/value pairs)
 * - File prefix used for saving exported data/notes from the UI
 */
#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>

#include <QApplication>
#include <QtGlobal>
#include "ECGViewer.hpp"

namespace py = pybind11;

/**
 * @brief Convert a 1D NumPy array into a QVector<T>.
 *
 * This performs a copy because Qt containers/widgets expect ownership/lifetime
 * independent of the Python buffer.
 *
 * @throws std::runtime_error if the input array is not 1D.
 */
template<typename T>
static QVector<T> toQVector1D(py::array_t<T, py::array::c_style | py::array::forcecast> arr,
                              const char* name)
{
    py::buffer_info info = arr.request();
    if (info.ndim != 1) {
        throw std::runtime_error(std::string(name) + " must be 1D");
    }
    auto* ptr = static_cast<T*>(info.ptr);
    QVector<T> v(info.size);
    for (ssize_t i = 0; i < info.size; ++i)
        v[static_cast<int>(i)] = ptr[i];
    return v;
}

struct FidPair
{
    QVector<double> times;
    QVector<double> vals;
};

/**
 * @brief Launch the ECG viewer window.
 *
 * Inputs:
 * - t: time array (seconds, 1D)
 * - v_orig: original ECG samples aligned with t
 * - v_clean: cleaned ECG samples aligned with t
 * - art_mask: 0/1 style byte mask aligned with t
 * - fs: sampling rate (Hz)
 * - window_s: initial visible window length (seconds)
 * - ylim: optional (ymin, ymax) tuple/list or None
 * - hide_artifacts: whether to hide the original trace in the UI
 * - p/q/r/s/t_times + p/q/r/s/t_vals: fiducial marker series (may be empty)
 * - file_prefix: base name for output files saved from the UI
 *
 * Notes:
 * - If no QApplication exists, one is created and exec() is called.
 *   If a QApplication already exists, this function simply shows the viewer.
 * - Will need to add logic that adds ability to keep the file selection gui open
 *   and allow multiple viewers to be opened from Python if desired.
 *   Can use a tick box for this option in the gui that launchers the viewer.
 */
static void show_ecg_viewer(
    py::array_t<double> t,
    py::array_t<double> v_orig,
    py::array_t<double> v_clean,
    py::array_t<unsigned char> art_mask,
    double fs,
    double window_s,
    py::object ylim,
    bool hide_artifacts,
    py::array_t<double> p_times,
    py::array_t<double> p_vals,
    py::array_t<double> q_times,
    py::array_t<double> q_vals,
    py::array_t<double> r_times,
    py::array_t<double> r_vals,
    py::array_t<double> s_times,
    py::array_t<double> s_vals,
    py::array_t<double> t_times,
    py::array_t<double> t_vals,
    const py::object& file_prefix)
{
    auto tq = toQVector1D<double>(t, "t");
    auto vOrigQ = toQVector1D<double>(v_orig, "v_orig");
    auto vCleanQ = toQVector1D<double>(v_clean, "v_clean");
    auto artQ = toQVector1D<unsigned char>(art_mask, "art_mask");

    if (tq.size() != vOrigQ.size() ||
        tq.size() != vCleanQ.size() ||
        tq.size() != artQ.size()) {
        throw std::runtime_error("t, v_orig, v_clean, art_mask must have same length");
    }

    bool has_ylim = false;
    double ymin = 0.0, ymax = 0.0;
    if (!ylim.is_none()) {
        auto seq = py::cast<py::sequence>(ylim);
        if (py::len(seq) != 2)
            throw std::runtime_error("ylim must be length-2 sequence");
        ymin = py::cast<double>(seq[0]);
        ymax = py::cast<double>(seq[1]);
        has_ylim = true;
    }

    auto loadPair = [&](py::array_t<double> timesArr,
                        py::array_t<double> valsArr,
                        const char* name) -> FidPair
    {
        FidPair out;
        out.times = toQVector1D<double>(timesArr, (std::string(name) + "_times").c_str());
        out.vals = toQVector1D<double>(valsArr, (std::string(name) + "_vals").c_str());
        if (out.times.size() != out.vals.size()) {
            throw std::runtime_error(std::string("times/vals size mismatch for ") + name);
        }
        return out;
    };

    auto P = loadPair(p_times, p_vals, "P");
    auto Q = loadPair(q_times, q_vals, "Q");
    auto R = loadPair(r_times, r_vals, "R");
    auto S = loadPair(s_times, s_vals, "S");
    auto T = loadPair(t_times, t_vals, "T");

    QString filePrefix;
    if (!file_prefix.is_none()) {
        filePrefix = QString::fromStdString(py::cast<std::string>(file_prefix));
    } else {
        filePrefix = QStringLiteral("ecg_data");
    }

    // Reuse existing QApplication if present (Python-level Qt launcher),
    // otherwise create our own and run its event loop.

    QApplication* app = qobject_cast<QApplication*>(QApplication::instance());
    bool created_app = false;

    if (!app) {
        int argc = 0;
        char* argv[] = { nullptr };
        app = new QApplication(argc, argv);
        created_app = true;
    }

    // Allocate viewer on the heap so it outlives this function if needed
    auto* viewer = new ECGViewer::ECGViewer(
        tq,
        vOrigQ,
        vCleanQ,
        artQ,
        fs,
        window_s,
        has_ylim,
        ymin,
        ymax,
        hide_artifacts,
        P.times, P.vals,
        Q.times, Q.vals,
        R.times, R.vals,
        S.times, S.vals,
        T.times, T.vals,
        filePrefix
    );

    viewer->setAttribute(Qt::WA_DeleteOnClose);
    viewer->show();

    // If we created the QApplication here (standalone use), run its loop
    if (created_app) {
        app->exec();
        // you *can* delete app here, but Qt will usually clean up on exit anyway
        // delete app;
    }
}

PYBIND11_MODULE(ECGViewer, m)
{
    m.doc() = "Qt + QCustomPlot ECG viewer with artifacts, noise replacement, and fiducials";
    m.def("show_ecg_viewer", &show_ecg_viewer,
        py::arg("t"),
        py::arg("v_orig"),
        py::arg("v_clean"),
        py::arg("art_mask"),
        py::arg("fs"),
        py::arg("window_s"),
        py::arg("ylim") = py::none(),
        py::arg("hide_artifacts") = false,
        py::arg("p_times"),
        py::arg("p_vals"),
        py::arg("q_times"),
        py::arg("q_vals"),
        py::arg("r_times"),
        py::arg("r_vals"),
        py::arg("s_times"),
        py::arg("s_vals"),
        py::arg("t_times"),
        py::arg("t_vals"),
        py::arg("file_prefix"));
}
