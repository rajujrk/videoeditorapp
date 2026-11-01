#include "cloud/CloudFeatures.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <cmath>

namespace cloudfeatures {

namespace {

const QString kCopySystem = QStringLiteral(
    "You write copy for video creators, working from the transcript of their video. Write in the language the "
    "transcript is in unless asked otherwise. Stay faithful to what is said: do not add facts, names, numbers or "
    "claims that are not in the transcript. Reply with the copy only, as plain text: no preamble, no notes, no "
    "Markdown.");

QString copyTask(const QString& kind)
{
    if (kind == QLatin1String("titles"))
        return QStringLiteral("Write five titles for this video, one per line, each under 70 characters, with no numbering "
                              "or quotation marks. Make them varied: some plain and direct, some that make people curious.");
    if (kind == QLatin1String("script"))
        return QStringLiteral("Rewrite what is said as a clean script for re-recording or a voice-over. Keep the speaker's "
                              "voice and the order of their points; cut fillers, false starts and repetition; break it "
                              "into short paragraphs, one idea each.");
    return QStringLiteral("Write a description to post with this video: an opening sentence saying what it is about, then "
                          "two or three short paragraphs covering the main points in the order they come. Under 250 words.");
}

QJsonObject translationSchema()
{
    const QJsonObject caption{{QStringLiteral("type"), QStringLiteral("object")},
                              {QStringLiteral("properties"),
                               QJsonObject{{QStringLiteral("index"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}},
                                           {QStringLiteral("text"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}}}},
                              {QStringLiteral("required"), QJsonArray{QStringLiteral("index"), QStringLiteral("text")}},
                              {QStringLiteral("additionalProperties"), false}};
    return {{QStringLiteral("type"), QStringLiteral("object")},
            {QStringLiteral("properties"),
             QJsonObject{{QStringLiteral("captions"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")},
                                                                  {QStringLiteral("items"), caption}}}}},
            {QStringLiteral("required"), QJsonArray{QStringLiteral("captions")}},
            {QStringLiteral("additionalProperties"), false}};
}

} // namespace

TextRequest copyRequest(const QString& kind, const QString& transcript, const QString& tone, const QString& notes,
                        const QString& model)
{
    QString task = copyTask(kind);
    if (!tone.trimmed().isEmpty())
        task += QStringLiteral(" Tone: %1.").arg(tone.trimmed());
    if (!notes.trimmed().isEmpty())
        task += QStringLiteral("\n\nAlso keep in mind: %1").arg(notes.trimmed());
    TextRequest request;
    request.system = kCopySystem;
    request.prompt = QStringLiteral("<transcript>\n%1\n</transcript>\n\n%2").arg(transcript, task);
    request.model = model;
    request.effort = QStringLiteral("medium");
    return request;
}

TextRequest translateRequest(const QList<QPair<int, QString>>& captions, const QString& language, const QString& model)
{
    QJsonArray list;
    for (const auto& [index, text] : captions)
        list.append(QJsonObject{{QStringLiteral("index"), index}, {QStringLiteral("text"), text}});
    TextRequest request;
    request.system = QStringLiteral("You translate video subtitles accurately and naturally.");
    request.prompt = QStringLiteral(
        "Translate these subtitles into %1.\nTarget language: %1\n\n"
        "Each caption is shown on screen for a fixed time, so translate each one on its own and keep its meaning inside "
        "it: never move words between captions, merge them or split them. Keep each translation about as long as the "
        "original so it can be read in time; where a caption has two lines (\\n), the translation may have two lines too. "
        "Keep names, numbers and brand names as they are. Neighbouring captions are context for wording. Return every "
        "caption with its index.\n\n<captions>%2</captions>")
                         .arg(language, QString::fromUtf8(QJsonDocument(list).toJson(QJsonDocument::Compact)));
    request.model = model;
    request.effort = QStringLiteral("medium");
    request.schema = translationSchema();
    return request;
}

std::optional<QMap<int, QString>> readTranslation(const QString& answer, const QList<int>& indices)
{
    QMap<int, QString> texts;
    for (const QJsonValue& value : QJsonDocument::fromJson(answer.toUtf8()).object().value(QStringLiteral("captions")).toArray()) {
        const QJsonObject caption = value.toObject();
        const QString text = caption.value(QStringLiteral("text")).toString().trimmed();
        if (!text.isEmpty())
            texts.insert(caption.value(QStringLiteral("index")).toInt(-1), text);
    }
    for (int index : indices)
        if (!texts.contains(index))
            return std::nullopt;
    return texts;
}

TextRequest rankRequest(const QList<RankItem>& items, const QString& purpose, const QString& model)
{
    QJsonArray list;
    for (const RankItem& item : items)
        list.append(QJsonObject{{QStringLiteral("index"), item.index},
                                {QStringLiteral("start"), std::round(item.start * 10) / 10},
                                {QStringLiteral("end"), std::round(item.end * 10) / 10},
                                {QStringLiteral("said"), item.text},
                                {QStringLiteral("feel"), item.why}});
    const QJsonObject entry{{QStringLiteral("type"), QStringLiteral("object")},
                            {QStringLiteral("properties"),
                             QJsonObject{{QStringLiteral("index"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}},
                                         {QStringLiteral("title"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}}}},
                            {QStringLiteral("required"), QJsonArray{QStringLiteral("index"), QStringLiteral("title")}},
                            {QStringLiteral("additionalProperties"), false}};
    TextRequest request;
    request.system = QStringLiteral("You are a video editor choosing the strongest moments of a recording.");
    const QString goal = purpose == QLatin1String("shorts")
                             ? QStringLiteral("stand-alone vertical shorts: each must make sense on its own and open with a hook")
                             : QStringLiteral("a short highlight montage: the most engaging, lively or meaningful moments");
    request.prompt = QStringLiteral(
        "These are moments of a video, with their times in seconds, what is said in them (if anything) and how they "
        "feel from the sound and picture (loud, lively, talk or calm). Rank all of them, best first, for %1. Give each a "
        "title of at most six words in the language that is spoken (English if nothing is said); do not invent "
        "details that are not in what is said.\n\n<moments>%2</moments>")
                         .arg(goal, QString::fromUtf8(QJsonDocument(list).toJson(QJsonDocument::Compact)));
    request.model = model;
    request.effort = QStringLiteral("medium");
    request.schema = {{QStringLiteral("type"), QStringLiteral("object")},
                      {QStringLiteral("properties"),
                       QJsonObject{{QStringLiteral("moments"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")},
                                                                           {QStringLiteral("items"), entry}}}}},
                      {QStringLiteral("required"), QJsonArray{QStringLiteral("moments")}},
                      {QStringLiteral("additionalProperties"), false}};
    return request;
}

std::optional<QList<QPair<int, QString>>> readRanking(const QString& answer, const QList<int>& indices)
{
    QList<QPair<int, QString>> order;
    QList<int> seen;
    for (const QJsonValue& value : QJsonDocument::fromJson(answer.toUtf8()).object().value(QStringLiteral("moments")).toArray()) {
        const int index = value.toObject().value(QStringLiteral("index")).toInt(-1);
        if (!indices.contains(index) || seen.contains(index))
            continue;
        seen << index;
        order.push_back({index, value.toObject().value(QStringLiteral("title")).toString().trimmed()});
    }
    if (seen.size() != indices.size())
        return std::nullopt;
    return order;
}

QStringList languages()
{
    return {QStringLiteral("Arabic"),     QStringLiteral("Chinese (Simplified)"), QStringLiteral("Chinese (Traditional)"),
            QStringLiteral("Dutch"),      QStringLiteral("English"),              QStringLiteral("French"),
            QStringLiteral("German"),     QStringLiteral("Hindi"),                QStringLiteral("Indonesian"),
            QStringLiteral("Italian"),    QStringLiteral("Japanese"),             QStringLiteral("Korean"),
            QStringLiteral("Malay"),      QStringLiteral("Polish"),               QStringLiteral("Portuguese (Brazil)"),
            QStringLiteral("Portuguese (Portugal)"), QStringLiteral("Russian"),   QStringLiteral("Spanish"),
            QStringLiteral("Swedish"),    QStringLiteral("Tamil"),                QStringLiteral("Thai"),
            QStringLiteral("Turkish"),    QStringLiteral("Ukrainian"),            QStringLiteral("Vietnamese")};
}

std::vector<tve::CaptionBlock> speechCaptions(const QList<SpokenWord>& words, tve::Ticks start)
{
    std::vector<tve::TimedWord> timed;
    for (const SpokenWord& word : words)
        timed.push_back({start + tve::Ticks(std::llround(word.start * double(tve::kTicksPerSecond))),
                         start + tve::Ticks(std::llround(std::max(word.end, word.start + 0.05) * double(tve::kTicksPerSecond))),
                         word.text.toStdString()});
    return tve::layoutCaptions(timed);
}

} // namespace cloudfeatures
