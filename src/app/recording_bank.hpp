#pragma once

#include "core/recording.hpp"
#include <QStringList>

namespace esl {

const RecordingBank* builtinRecording(RecordingId id);
const QStringList& recordingLabels();
const QStringList& recordingKeys();

} // namespace esl
