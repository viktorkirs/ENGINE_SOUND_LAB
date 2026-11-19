#pragma once

#include "app/preset.hpp"
#include <vector>

class QWidget;

namespace esl {
int chooseLibraryPreset(QWidget* parent, const std::vector<Preset>& presets, int current);
}
