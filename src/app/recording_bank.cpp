#include "app/recording_bank.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtEndian>

namespace esl {
namespace {

std::vector<std::int16_t> loadPcm(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    const auto bytes = file.readAll();
    if (bytes.size() < 8 || bytes.size() % 2) return {};
    std::vector<std::int16_t> pcm(bytes.size() / 2);
    qFromLittleEndian<qint16>(bytes.constData(), pcm.size(), pcm.data());
    return pcm;
}

RecordingBank loadBank(RecordingId id, const QJsonObject& entry) {
    RecordingBank bank{id, entry.value("pitchTracking").toDouble(1.0), {}};
    const auto anchors = entry.value("rpm").toArray();
    if (anchors.size() > qsizetype(RecordingBank::maxLoops)) return bank;
    for (const auto& value : anchors) {
        const int rpm = value.toInt();
        if (rpm <= 0 || (!bank.loops.empty() && rpm <= bank.loops.back().rpm)) return {id, bank.pitchTracking, {}};
        const auto prefix = QStringLiteral(":/recordings/%1/%2").arg(entry.value("directory").toString()).arg(rpm);
        RecordedLoop loop{double(rpm), loadPcm(prefix + QStringLiteral(".pcm")), {}};
        if (loop.pcm.empty()) return {id, bank.pitchTracking, {}};
        if (entry.value("coast").toBool() && !bank.loops.empty()) {
            loop.coast = loadPcm(prefix + QStringLiteral("-coast.pcm"));
            if (loop.coast.empty()) return {id, bank.pitchTracking, {}};
        }
        bank.loops.push_back(std::move(loop));
    }
    return bank;
}

struct Catalog {
    QStringList keys, labels;
    std::vector<RecordingBank> banks;
};

const Catalog& catalog() {
    static const Catalog result = [] {
        Catalog data;
        QFile file(QStringLiteral(":/recordings/banks.json"));
        if (!file.open(QIODevice::ReadOnly)) return data;
        const auto entries = QJsonDocument::fromJson(file.readAll()).array();
        for (const auto& item : entries) {
            const auto row = item.toObject();
            const auto id = static_cast<RecordingId>(data.banks.size());
            data.keys.append(row.value("key").toString());
            data.labels.append(row.value("label").toString());
            data.banks.push_back(loadBank(id, row));
        }
        return data;
    }();
    return result;
}

} // namespace

const RecordingBank* builtinRecording(RecordingId id) {
    const auto& banks = catalog().banks;
    const auto index = static_cast<std::size_t>(id);
    return index < banks.size() && !banks[index].loops.empty() ? &banks[index] : nullptr;
}

const QStringList& recordingLabels() { return catalog().labels; }
const QStringList& recordingKeys() { return catalog().keys; }

} // namespace esl
