#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace esl {

enum class RecordingId { None, T72, T90M, Leopard1, Leopard2, Marder, Puma, Bradley, HummerH2,
                         Bmp12, Bmp3, Dci23, Uaz, Stryker, Ariete, M113 };

struct RecordedLoop {
    double rpm;
    std::vector<std::int16_t> pcm;
    std::vector<std::int16_t> coast; // Optional take at the same speed with the pedal released.
};

struct RecordingBank {
    RecordingId id;
    double pitchTracking; // Unlabelled moving takes retain more of their recorded pitch.
    static constexpr int sampleRate = 24000;
    static constexpr std::size_t maxLoops = 8;
    std::vector<RecordedLoop> loops; // In ascending playback RPM.
};

class RecordingPlayer {
public:
    void prepare(const RecordingBank* bank, double outputRate);
    double tick(double rpm, double coast = 0.0);

private:
    double read(std::size_t index, double rpm, double coast);
    const RecordingBank* bank_ = nullptr;
    std::array<double, RecordingBank::maxLoops> cursors_{};
    std::array<double, RecordingBank::maxLoops> coastCursors_{};
    double rateRatio_ = 1.0;
    std::size_t low_ = 0;
};

} // namespace esl
