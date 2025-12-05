#include "ECGViewer.hpp"

#include <QString>

// Pretty empty file so far but going to be used for notes etc etc
namespace ECGViewer {
void ECGViewer::onInsertManualFiducial()
{
    QString choice = manualTypeCombo_ ? manualTypeCombo_->currentText() : QString("R");

    FiducialType type = FiducialType::R;
    if (choice == "P")      type = FiducialType::P;
    else if (choice == "Q") type = FiducialType::Q;
    else if (choice == "R") type = FiducialType::R;
    else if (choice == "S") type = FiducialType::S;
    else if (choice == "T") type = FiducialType::T;

    // Insert at the centre of the current window
    double newTime = 0.5 * (currentX0 + currentX1);

    // Clamp to full duration just in case
    if (newTime < 0.0) newTime = 0.0;
    if (newTime > total_time_) newTime = total_time_;

    // Get Y value from clean signal at nearest sample
    double absTime = t_.first() + newTime;
    int sampleIndex = static_cast<int>(std::round((absTime - t_.first()) * fs_));
    if (sampleIndex < 0) sampleIndex = 0;
    if (sampleIndex >= vClean_.size()) sampleIndex = vClean_.size() - 1;
    double newVal = vClean_[sampleIndex];

    // Insert into correct vectors, keeping them sorted by time
    QVector<double>& times = timesFor(type);
    QVector<double>& vals = valsFor(type);

    int insertIndex = 0;
    while (insertIndex < times.size() && times[insertIndex] < newTime)
        ++insertIndex;

    times.insert(insertIndex, newTime);
    vals.insert(insertIndex, newVal);

    // Update the correct scatter graph
    switch (type) {
    case FiducialType::P:
        graphP_->setData(pTimes_, pVals_);
        break;
    case FiducialType::Q:
        graphQ_->setData(qTimes_, qVals_);
        break;
    case FiducialType::R:
        graphR_->setData(rTimes_, rVals_);
        break;
    case FiducialType::S:
        graphS_->setData(sTimes_, sVals_);
        break;
    case FiducialType::T:
        graphT_->setData(tTimes_, tVals_);
        break;
    }

    // Recreate fiducial lines/labels for the current window
    updateFiducialLines(currentX0, currentX1);

    // Replot so the new fiducial appears (and is draggable using your existing logic)
    plot_->replot();
}
} // namespace ECGViewer