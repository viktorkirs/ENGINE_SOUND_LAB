#pragma once

#include "app/preset.hpp"

#include <atomic>
#include <functional>

namespace esl {

enum class ExportMode { Single, Pack, Grid, Drive };

struct ExportOptions {
    ExportMode mode = ExportMode::Single;
    int sampleRate = 48000;
    int bits = 24;
    int channels = 2;
    bool normalize = true;
    bool singleLoop = true;
    double seconds = 4.0;
    double rpm = 1000.0;
    double load = 0.20;
    double throttle = 0.20;
    double rpmMin = 650.0;
    double rpmMax = 2200.0;
    int rpmCount = 4;
    double loadMin = 0.20;
    double loadMax = 0.80;
    int loadCount = 2;
};

struct ExportResult {
    QString directory;
    QString error;
    int wavCount = 0;
    bool cancelled = false;
};

using ExportProgress = std::function<void(int completed, int steps)>;

ExportResult exportAudio(const Preset& preset, const ExportOptions& options, const QString& destination,
                         const std::atomic_bool* cancel = nullptr, const ExportProgress& progress = {});

} // namespace esl
