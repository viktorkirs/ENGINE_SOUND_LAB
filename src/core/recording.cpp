#include "core/recording.hpp"

#include <algorithm>
#include <cmath>

namespace esl {

void RecordingPlayer::prepare(const RecordingBank* bank, double outputRate) {
    bank_ = bank;
    rateRatio_ = RecordingBank::sampleRate / outputRate;
    cursors_ = {};
    coastCursors_ = {};
    low_ = 0;
}

static double readPcm(const std::vector<std::int16_t>& pcm, double& cursor, double step) {
    const auto a = static_cast<std::size_t>(cursor);
    const auto b = a + 1 == pcm.size() ? 0 : a + 1;
    const auto before = a == 0 ? pcm.size() - 1 : a - 1;
    const auto after = b + 1 == pcm.size() ? 0 : b + 1;
    const double y1 = pcm[a], y2 = pcm[b];
    const double m1 = 0.5 * (y2 - pcm[before]), m2 = 0.5 * (pcm[after] - y1);
    const double t = cursor - a;
    const double value = std::lerp(y1, y2, t) + t * (1.0 - t) *
        std::lerp(m1 - (y2 - y1), (y2 - y1) - m2, t);
    cursor += step;
    if (cursor >= pcm.size()) cursor -= pcm.size();
    return value / 32768.0;
}

double RecordingPlayer::read(std::size_t index, double rpm, double coast) {
    const auto& loop = bank_->loops[index];
    const double pitch = std::lerp(1.0, rpm / loop.rpm, bank_->pitchTracking);
    const double step = rateRatio_ * std::clamp(pitch, 0.25, 2.0);
    const double power = readPcm(loop.pcm, cursors_[index], step);
    if (loop.coast.empty()) return power;
    const double lift = readPcm(loop.coast, coastCursors_[index], step);
    // Equal power with finite slopes at either end. Both tape heads keep turning.
    const double mix = coast * coast * (3.0 - 2.0 * coast);
    return std::sqrt(1.0 - mix) * power + std::sqrt(mix) * lift;
}

double RecordingPlayer::tick(double rpm, double coast) {
    const auto& loops = bank_->loops;
    while (low_ > 0 && rpm < loops[low_].rpm) --low_;
    while (low_ + 1 < loops.size() && rpm >= loops[low_ + 1].rpm) ++low_;
    const double first = read(low_, rpm, coast);
    if (low_ + 1 == loops.size() || rpm <= loops[low_].rpm) return first;
    const double t = (rpm - loops[low_].rpm) / (loops[low_ + 1].rpm - loops[low_].rpm);
    // Equal-power blend. The two microphone takes have independent phases.
    return std::sqrt(1.0 - t) * first + std::sqrt(t) * read(low_ + 1, rpm, coast);
}

} // namespace esl
