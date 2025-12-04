#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>

#include <QApplication>
#include "EcgQtViewer.hpp"

namespace py = pybind11;

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
    py::array_t<double> t_vals)
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

    auto pTimesQ = toQVector1D<double>(p_times, "p_times");
    auto pValsQ = toQVector1D<double>(p_vals, "p_vals");
    auto qTimesQ = toQVector1D<double>(q_times, "q_times");
    auto qValsQ = toQVector1D<double>(q_vals, "q_vals");
    auto rTimesQ = toQVector1D<double>(r_times, "r_times");
    auto rValsQ = toQVector1D<double>(r_vals, "r_vals");
    auto sTimesQ = toQVector1D<double>(s_times, "s_times");
    auto sValsQ = toQVector1D<double>(s_vals, "s_vals");
    auto tTimesQ = toQVector1D<double>(t_times, "t_times");
    auto tValsQ = toQVector1D<double>(t_vals, "t_vals");

    // simple length checks: allow zero-length for "no points"
    auto checkPair = [](const QVector<double>& a, const QVector<double>& b, const char* name) {
        if (a.size() != b.size())
            throw std::runtime_error(std::string("times/vals size mismatch for ") + name);
    };
    checkPair(pTimesQ, pValsQ, "P");
    checkPair(qTimesQ, qValsQ, "Q");
    checkPair(rTimesQ, rValsQ, "R");
    checkPair(sTimesQ, sValsQ, "S");
    checkPair(tTimesQ, tValsQ, "T");

    int argc = 0;
    char* argv[] = {nullptr};
    QApplication app(argc, argv);

    ECGViewerQt viewer(
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
        pTimesQ, pValsQ,
        qTimesQ, qValsQ,
        rTimesQ, rValsQ,
        sTimesQ, sValsQ,
        tTimesQ, tValsQ
    );

    viewer.show();
    app.exec();
}

PYBIND11_MODULE(ecg_qt_viewer, m)
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
        py::arg("t_vals"));
}
