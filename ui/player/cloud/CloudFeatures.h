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

// Texts (index → text: captions and titles) checked for spelling, grammar and punctuation,
// each in its own language, the meaning, wording and line breaks kept.
TextRequest correctionRequest(const QList<QPair<int, QString>>& texts, const QString& model);
struct Correction {
    QString text; // the corrected text
    QString note; // what was fixed, in a few words
};
// The texts Claude changed, by index (unknown indices and texts that came back the same
// left out); nullopt when the answer cannot be read.
std::optional<QMap<int, Correction>> readCorrections(const QString& answer, const QMap<int, QString>& asked);
// How many texts go in one request.
constexpr int kTextsPerRequest = 120;

// A prompt for making a picture ("picture"), a sticker ("sticker") or a video ("video")
// like the one given (encoded, e.g. "image/jpeg").
TextRequest imagePromptRequest(const QByteArray& image, const QString& mediaType, const QString& purpose,
                               const QString& model);

// The languages offered for translation (English names).
QStringList languages();

// Captions for speech placed at `start` on the timeline.
std::vector<tve::CaptionBlock> speechCaptions(const QList<SpokenWord>& words, tve::Ticks start);

} // namespace cloudfeatures
