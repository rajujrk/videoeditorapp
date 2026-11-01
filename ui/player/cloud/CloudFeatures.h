#pragma once

#include "cloud/CloudText.h"
#include "cloud/CloudVoices.h"
#include "model/Captions.h"

#include <QMap>
#include <QStringList>
#include <optional>

// The AI features' requests and how their answers are read.
namespace cloudfeatures {

// "titles", "description" or "script" from a transcript; `tone` and `notes` may be empty.
TextRequest copyRequest(const QString& kind, const QString& transcript, const QString& tone, const QString& notes,
                        const QString& model);

// Captions (index → text) into a language, each kept to its own caption (its timing).
TextRequest translateRequest(const QList<QPair<int, QString>>& captions, const QString& language, const QString& model);
// The translations by index; nullopt unless every index asked for has one.
std::optional<QMap<int, QString>> readTranslation(const QString& answer, const QList<int>& indices);
// How many captions go in one request.
constexpr int kCaptionsPerRequest = 120;

// Moments of a video for Claude to rank, best first, with a short title each: for a
// highlight montage ("montage") or stand-alone vertical shorts ("shorts").
struct RankItem {
    int index = 0;
    double start = 0; // seconds
    double end = 0;
    QString text;     // what is said
    QString why;      // "loud", "lively", "talk", "calm"
};
TextRequest rankRequest(const QList<RankItem>& items, const QString& purpose, const QString& model);
// The indices best first with their titles; nullopt unless every index asked for is there once.
std::optional<QList<QPair<int, QString>>> readRanking(const QString& answer, const QList<int>& indices);

// The languages offered for translation (English names).
QStringList languages();

// Captions for speech placed at `start` on the timeline.
std::vector<tve::CaptionBlock> speechCaptions(const QList<SpokenWord>& words, tve::Ticks start);

} // namespace cloudfeatures
