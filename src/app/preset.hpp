#pragma once

#include "core/engine_model.hpp"

#include <QString>

class QJsonObject;

namespace esl {

struct Preset {
    QString name;
    QString description;
    Settings settings;
};

// Invalid files return an error and leave the supplied preset intact.
QString loadPreset(const QString& path, Preset& preset);
QString savePreset(const QString& path, const Preset& preset);
QJsonObject listenerJson(const Listener& listener);
QString listeningPositionName(ListeningPosition position);
QString firingOrderText(const Model& model);
bool parseFiringOrder(const QString& text, const Model& model, FiringOrder& order);

} // namespace esl
