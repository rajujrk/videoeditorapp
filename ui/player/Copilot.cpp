#include "Copilot.h"

#include "Language.h"
#include "LookLibrary.h"
#include "ProjectController.h"

#include "cloud/CloudServices.h"
#include "commands/EditCommands.h"
#include "commands/StructureCommands.h"
#include "gpu/EffectLibrary.h"

#include <QJsonDocument>

#include <cmath>
#include <set>

using namespace tve;

namespace {

constexpr int kMostRounds = 12;   // tool rounds in one request
constexpr std::size_t kMostWords = 3000; // what is said, described

QJsonObject prop(const char* type, const char* description)
{
    return {{QStringLiteral("type"), QLatin1String(type)}, {QStringLiteral("description"), QLatin1String(description)}};
}

QJsonObject schema(const QJsonObject& properties, const QStringList& required)
{
    return {{QStringLiteral("type"), QStringLiteral("object")},
            {QStringLiteral("properties"), properties},
            {QStringLiteral("required"), QJsonArray::fromStringList(required)},
            {QStringLiteral("additionalProperties"), false}};
}

QJsonObject tool(const char* name, const char* description, const QJsonObject& input)
{
    return {{QStringLiteral("name"), QLatin1String(name)},
            {QStringLiteral("description"), QLatin1String(description)},
            {QStringLiteral("input_schema"), input}};
}

QString seconds(Ticks t)
{
    return QString::number(toSeconds(t), 'f', 2);
}

QString quoted(const std::string& text, int most = 60)
{
    QString t = QString::fromStdString(text).simplified();
    if (t.size() > most)
        t = t.left(most - 1) + QStringLiteral("…");
    return QStringLiteral("“%1”").arg(t);
}

// A time from seconds, on the project's frames.
Ticks at(const Project& project, double secondsValue)
{
    const Ticks frame = frameDuration(project.settings.frameRate);
    return Ticks(std::llround(secondsValue * double(kTicksPerSecond) / double(frame))) * frame;
}

QString clipName(const Project& project, const Clip& clip)
{
    if (clip.text)
        return QStringLiteral("title %1").arg(quoted(clip.text->text, 30));
    if (const MediaItem* media = project.findMedia(clip.mediaId))
        return QStringLiteral("“%1”").arg(QString::fromStdString(media->path).section(QLatin1Char('/'), -1));
    return QStringLiteral("clip %1").arg(clip.id);
}

std::set<Id> clipIds(const Project& project)
{
    std::set<Id> ids;
    for (const Track& track : project.tracks)
        for (const Clip& clip : track.clips)
            ids.insert(clip.id);
    return ids;
}

Copilot::Outcome failed(const QString& why)
{
    return {false, {{QStringLiteral("error"), why}}, {}};
}

// Runs a command on the copy: its outcome, with the clips it made.
Copilot::Outcome applyTo(Project& project, std::unique_ptr<Command> command, const QString& change)
{
    const std::set<Id> before = clipIds(project);
    if (auto error = command->redo(project))
        return failed(QString::fromStdString(*error));
    QJsonArray created;
    for (Id id : clipIds(project))
        if (!before.contains(id))
            created.append(double(id));
    QJsonObject result{{QStringLiteral("ok"), true}};
    if (!created.isEmpty())
        result.insert(QStringLiteral("created_clip_ids"), created);
    return {true, result, change};
}

const QStringList& toolNames()
{
    static const QStringList names = [] {
        QStringList out;
        for (const QJsonValue& t : Copilot::tools())
            out << t.toObject().value(QStringLiteral("name")).toString();
        return out;
    }();
    return names;
}

// One value against its schema (types, enums, ranges, arrays' items).
QString checkValue(const QJsonValue& value, const QJsonObject& spec, const QString& path)
{
    const QString type = spec.value(QStringLiteral("type")).toString();
    const bool fits = type == QLatin1String("string")    ? value.isString()
                    : type == QLatin1String("number")    ? value.isDouble()
                    : type == QLatin1String("integer")   ? value.isDouble() && std::trunc(value.toDouble()) == value.toDouble()
                    : type == QLatin1String("boolean")   ? value.isBool()
                    : type == QLatin1String("array")     ? value.isArray()
                    : type == QLatin1String("object")    ? value.isObject()
                                                         : true;
    if (!fits)
        return QStringLiteral("%1 must be %2").arg(path, type == QLatin1String("integer") ? QStringLiteral("a whole number") : type);
    if (spec.contains(QStringLiteral("enum")) && !spec.value(QStringLiteral("enum")).toArray().contains(value))
        return QStringLiteral("%1 isn't one of the allowed values").arg(path);
    if (value.isDouble()) {
        if (spec.contains(QStringLiteral("minimum")) && value.toDouble() < spec.value(QStringLiteral("minimum")).toDouble())
            return QStringLiteral("%1 must be at least %2").arg(path).arg(spec.value(QStringLiteral("minimum")).toDouble());
        if (spec.contains(QStringLiteral("maximum")) && value.toDouble() > spec.value(QStringLiteral("maximum")).toDouble())
            return QStringLiteral("%1 must be at most %2").arg(path).arg(spec.value(QStringLiteral("maximum")).toDouble());
    }
    if (value.isArray()) {
        const QJsonObject items = spec.value(QStringLiteral("items")).toObject();
        const QJsonArray array = value.toArray();
        for (qsizetype i = 0; i < array.size(); ++i)
            if (QString why = checkValue(array[i], items, QStringLiteral("%1[%2]").arg(path).arg(i)); !why.isEmpty())
                return why;
    }
    if (value.isObject() && spec.contains(QStringLiteral("properties"))) {
        const QJsonObject object = value.toObject();
        const QJsonObject properties = spec.value(QStringLiteral("properties")).toObject();
        for (const QJsonValue& name : spec.value(QStringLiteral("required")).toArray())
            if (!object.contains(name.toString()))
                return QStringLiteral("%1 is missing %2").arg(path, name.toString());
        for (auto it = object.begin(); it != object.end(); ++it) {
            if (!properties.contains(it.key())) {
                if (spec.value(QStringLiteral("additionalProperties")).toBool(true))
                    continue;
                return QStringLiteral("%1 has no field %2").arg(path, it.key());
            }
            if (QString why = checkValue(it.value(), properties.value(it.key()).toObject(), path + QLatin1Char('.') + it.key());
                !why.isEmpty())
                return why;
        }
    }
    return {};
}

} // namespace

Copilot::Copilot(ProjectController* project) : QObject(project), m_project(project) {}

const QJsonArray& Copilot::tools()
{
    static const QJsonArray all = [] {
        const QJsonObject clipId = prop("integer", "The clip's id, from the project description or a tool's result");
        QJsonObject captionItem = schema({{QStringLiteral("start"), prop("number", "Seconds")},
                                          {QStringLiteral("end"), prop("number", "Seconds")},
                                          {QStringLiteral("text"), prop("string", "What the caption says")}},
                                         {QStringLiteral("start"), QStringLiteral("end"), QStringLiteral("text")});
        QJsonObject captions = prop("array", "Every caption, in order; they replace the caption track");
        captions.insert(QStringLiteral("items"), captionItem);
        QJsonObject clipIds = prop("array", "Clip ids");
        clipIds.insert(QStringLiteral("items"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}});
        QJsonObject values = prop("object", "Parameter values by parameter id (numbers); the others keep their defaults");
        values.insert(QStringLiteral("additionalProperties"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}});
        QJsonObject grade = schema(
            {{QStringLiteral("clip_ids"), clipIds},
             {QStringLiteral("exposure"), prop("number", "Stops, -4 to 4 (0: unchanged)")},
             {QStringLiteral("contrast"), prop("number", "-1 to 1")},
             {QStringLiteral("saturation"), prop("number", "-1 (grey) to 1")},
             {QStringLiteral("temperature"), prop("number", "-1 (cooler) to 1 (warmer)")},
             {QStringLiteral("tint"), prop("number", "-1 (greener) to 1 (more magenta)")}},
            {QStringLiteral("clip_ids")});
        return QJsonArray{
            tool("split_clip", "Cut a clip in two at a time inside it (seconds on the timeline). Use it to separate a "
                               "part to trim, move or delete.",
                 schema({{QStringLiteral("clip_id"), clipId}, {QStringLiteral("at"), prop("number", "Seconds on the timeline")}},
                        {QStringLiteral("clip_id"), QStringLiteral("at")})),
            tool("trim_clip", "Move a clip's start and/or end (seconds on the timeline). Trimming the start keeps the "
                              "frames under the rest of the clip where they are; it can't reach past the media.",
                 schema({{QStringLiteral("clip_id"), clipId},
                         {QStringLiteral("start"), prop("number", "The clip's new start, seconds")},
                         {QStringLiteral("end"), prop("number", "The clip's new end, seconds")}},
                        {QStringLiteral("clip_id")})),
            tool("move_clip", "Move a clip to a time, and optionally to another track of its kind. Clips on a track can't overlap.",
                 schema({{QStringLiteral("clip_id"), clipId},
                         {QStringLiteral("start"), prop("number", "Its new start, seconds")},
                         {QStringLiteral("track_id"), prop("integer", "The track to move it to (default: its own)")}},
                        {QStringLiteral("clip_id"), QStringLiteral("start")})),
            tool("delete_clips", "Remove clips. With close_gaps, what follows on each clip's track moves back to close "
                                 "the space (a ripple delete).",
                 schema({{QStringLiteral("clip_ids"), clipIds},
                         {QStringLiteral("close_gaps"), prop("boolean", "Close the space each leaves (default false)")}},
                        {QStringLiteral("clip_ids")})),
            tool("add_title", "Add a title (text on the picture) for a while. It goes on the video track given, else on "
                              "the lowest video track above the main one that is free then (a new track if none is).",
                 schema({{QStringLiteral("text"), prop("string", "The title's text (new lines with \\n)")},
                         {QStringLiteral("start"), prop("number", "Seconds")},
                         {QStringLiteral("duration"), prop("number", "Seconds")},
                         {QStringLiteral("track_id"), prop("integer", "A video track (default: chosen as above)")}},
                        {QStringLiteral("text"), QStringLiteral("start"), QStringLiteral("duration")})),
            tool("set_title_text", "Change a title's text, keeping its style.",
                 schema({{QStringLiteral("clip_id"), clipId}, {QStringLiteral("text"), prop("string", "Its new text")}},
                        {QStringLiteral("clip_id"), QStringLiteral("text")})),
            tool("set_transition", "Put a transition into a clip from the clip that ends where it starts on its track, or "
                                   "remove it (transition_id empty). Ids are in <transitions>.",
                 schema({{QStringLiteral("clip_id"), clipId},
                         {QStringLiteral("transition_id"), prop("string", "A transition's id, or \"\" to remove it")},
                         {QStringLiteral("duration"), prop("number", "Seconds, 0.1 to 10 (default 1)")}},
                        {QStringLiteral("clip_id"), QStringLiteral("transition_id")})),
            tool("add_effect", "Add a video effect to the end of a clip's effect stack. Ids and parameters are in <effects>.",
                 schema({{QStringLiteral("clip_id"), clipId},
                         {QStringLiteral("effect_id"), prop("string", "The effect's id")},
                         {QStringLiteral("values"), values}},
                        {QStringLiteral("clip_id"), QStringLiteral("effect_id")})),
            tool("remove_effect", "Remove an effect from a clip's stack by its position (0 first).",
                 schema({{QStringLiteral("clip_id"), clipId}, {QStringLiteral("index"), prop("integer", "Its position, 0 first")}},
                        {QStringLiteral("clip_id"), QStringLiteral("index")})),
            tool("set_color", "Adjust clips' basic colour: exposure, contrast, saturation, temperature, tint. Values given "
                              "replace the clip's own; the rest of its grade stays.",
                 grade),
            tool("set_look", "Give a clip a look (a filter) from <looks> at an intensity, or remove it (look_id empty).",
                 schema({{QStringLiteral("clip_id"), clipId},
                         {QStringLiteral("look_id"), prop("string", "A look's id, or \"\" to remove it")},
                         {QStringLiteral("intensity"), prop("number", "0 to 1 (default 1)")}},
                        {QStringLiteral("clip_id"), QStringLiteral("look_id")})),
            tool("set_captions", "Replace every caption on the caption track (made if there is none) with these. To change "
                                 "a few, send all of them with those changed.",
                 schema({{QStringLiteral("captions"), captions}}, {QStringLiteral("captions")})),
        };
    }();
    return all;
}

const QJsonArray& Copilot::systemPrompt()
{
    static const QJsonArray prompt = [] {
        QString text = QStringLiteral(
            "You are the editing copilot in TechieVideoEditor, a video editor for the Mac. The person asks for changes to "
            "their project in their own words, and you make them with the tools, which edit a copy of the project. When "
            "you finish, the person sees the list of changes and decides whether to apply them, all together as one undo "
            "step. So make every change the request needs, then say in a sentence or two what you did, and anything you "
            "couldn't do and why.\n\n"
            "Each request comes with the project as it is then, inside <project>. Times are seconds on the timeline. Use "
            "the clip and track ids it gives; tools that make clips return the new ids. A tool's result says when a change "
            "failed and why: correct the call, or say what stopped it.\n\n"
            "When a request is unclear, or would remove much of the edit, ask instead of guessing (without tools). Keep "
            "replies short and plain: the person is editing, not reading.\n\n");
        text += QStringLiteral("<effects>\n");
        for (const LoadedEffect& effect : EffectLibrary::builtIn().effects()) {
            const EffectManifest& m = effect.manifest;
            text += QString::fromStdString(m.id) + QStringLiteral(" — ") + QString::fromStdString(m.displayTitle());
            QStringList params;
            for (const EffectParamSpec& p : m.params)
                if (p.type != ParamType::Color)
                    params << QStringLiteral("%1 %2–%3").arg(QString::fromStdString(p.id)).arg(p.min).arg(p.max);
            if (!params.isEmpty())
                text += QStringLiteral(" (") + params.join(QStringLiteral(", ")) + QLatin1Char(')');
            text += QLatin1Char('\n');
        }
        text += QStringLiteral("</effects>\n<transitions>\n");
        for (const LoadedEffect& t : EffectLibrary::builtIn().transitions())
            text += QString::fromStdString(t.manifest.id) + QStringLiteral(" — ") + QString::fromStdString(t.manifest.displayTitle())
                  + QLatin1Char('\n');
        text += QStringLiteral("</transitions>\n<looks>\n");
        for (const LookInfo& look : builtInLooks())
            text += QString::fromStdString(look.id) + QStringLiteral(" — ") + QString::fromStdString(look.title) + QLatin1Char('\n');
        text += QStringLiteral("</looks>");
        return QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("text")}, {QStringLiteral("text"), text}}};
    }();
    return prompt;
}

QString Copilot::check(const QString& name, const QJsonObject& input)
{
    for (const QJsonValue& t : tools()) {
        const QJsonObject definition = t.toObject();
        if (definition.value(QStringLiteral("name")).toString() == name)
            return checkValue(input, definition.value(QStringLiteral("input_schema")).toObject(), QStringLiteral("input"));
    }
    return QStringLiteral("there is no tool %1").arg(name);
}

Copilot::Outcome Copilot::run(const QString& name, const QJsonObject& input, Project& project)
{
    if (QString why = check(name, input); !why.isEmpty())
        return failed(why);
    const auto id = [&](const char* key) { return Id(input.value(QLatin1String(key)).toInteger()); };
    const auto time = [&](const char* key) { return at(project, input.value(QLatin1String(key)).toDouble()); };
    const auto findClip = [&](Id clipId) -> std::optional<ClipLocation> { return project.locateClip(clipId); };

    if (name == QLatin1String("split_clip")) {
        auto location = findClip(id("clip_id"));
        if (!location)
            return failed(QStringLiteral("there is no clip %1").arg(id("clip_id")));
        const QString what = clipName(project, *location->clip);
        return applyTo(project, std::make_unique<SplitClipCommand>(id("clip_id"), time("at")),
                       tr("Split %1 at %2 s").arg(what, seconds(time("at"))));
    }
    if (name == QLatin1String("trim_clip")) {
        auto location = findClip(id("clip_id"));
        if (!location)
            return failed(QStringLiteral("there is no clip %1").arg(id("clip_id")));
        if (!input.contains(QStringLiteral("start")) && !input.contains(QStringLiteral("end")))
            return failed(QStringLiteral("give a new start, end or both"));
        const QString what = clipName(project, *location->clip);
        std::vector<std::unique_ptr<Command>> steps;
        if (input.contains(QStringLiteral("start")))
            steps.push_back(std::make_unique<TrimClipCommand>(id("clip_id"), TrimClipCommand::Edge::Start, time("start")));
        if (input.contains(QStringLiteral("end")))
            steps.push_back(std::make_unique<TrimClipCommand>(id("clip_id"), TrimClipCommand::Edge::End, time("end")));
        Outcome done = applyTo(project, std::make_unique<CompositeCommand>(std::move(steps), "Trim"), {});
        if (done.ok) {
            const Clip& clip = *project.locateClip(id("clip_id"))->clip;
            done.change = tr("Trim %1 to %2–%3 s").arg(what, seconds(clip.start), seconds(clip.end()));
        }
        return done;
    }
    if (name == QLatin1String("move_clip")) {
        auto location = findClip(id("clip_id"));
        if (!location)
            return failed(QStringLiteral("there is no clip %1").arg(id("clip_id")));
        const Id track = input.contains(QStringLiteral("track_id")) ? id("track_id") : location->track->id;
        const QString what = clipName(project, *location->clip);
        return applyTo(project, std::make_unique<MoveClipCommand>(id("clip_id"), track, time("start")),
                       tr("Move %1 to %2 s").arg(what, seconds(time("start"))));
    }
    if (name == QLatin1String("delete_clips")) {
        const bool ripple = input.value(QStringLiteral("close_gaps")).toBool(false);
        std::vector<std::unique_ptr<Command>> steps;
        QStringList names;
        // Later clips first, so a ripple doesn't move the ones still to go.
        std::vector<std::pair<Ticks, Id>> order;
        for (const QJsonValue& v : input.value(QStringLiteral("clip_ids")).toArray()) {
            auto location = findClip(Id(v.toInteger()));
            if (!location)
                return failed(QStringLiteral("there is no clip %1").arg(v.toInteger()));
            order.push_back({location->clip->start, location->clip->id});
            names << clipName(project, *location->clip);
        }
        std::ranges::sort(order, std::greater{});
        for (const auto& [start, clipId] : order)
            steps.push_back(std::make_unique<DeleteClipCommand>(clipId, ripple));
        if (steps.empty())
            return failed(QStringLiteral("no clips given"));
        return applyTo(project, std::make_unique<CompositeCommand>(std::move(steps), "Delete"),
                       tr("Delete %1%2").arg(names.join(QStringLiteral(", ")),
                                                         ripple ? QStringLiteral(", closing the gaps") : QString()));
    }
    if (name == QLatin1String("add_title")) {
        const Ticks start = time("start");
        const Ticks duration = std::max(time("duration"), frameDuration(project.settings.frameRate));
        if (start < 0)
            return failed(QStringLiteral("start must be 0 or later"));
        Id track = input.contains(QStringLiteral("track_id")) ? id("track_id") : kNoId;
        std::vector<std::unique_ptr<Command>> steps;
        if (track == kNoId) {
            // The lowest free video track above the main one.
            bool main = true;
            for (const Track& t : project.tracks) {
                if (t.kind != TrackKind::Video)
                    continue;
                if (main) {
                    main = false;
                    continue;
                }
                if (!t.locked && !t.overlaps(start, duration)) {
                    track = t.id;
                    break;
                }
            }
            if (track == kNoId) {
                auto add = std::make_unique<AddTrackCommand>(TrackKind::Video, "Titles");
                if (auto error = add->redo(project))
                    return failed(QString::fromStdString(*error));
                track = project.tracks.back().id;
                for (const Track& t : project.tracks)
                    if (t.kind == TrackKind::Video && t.name == "Titles" && t.clips.empty())
                        track = t.id;
            }
        }
        ClipText text;
        text.text = input.value(QStringLiteral("text")).toString().toStdString();
        return applyTo(project, std::make_unique<AddTitleCommand>(track, start, duration, text),
                       tr("Add title %1 at %2 s for %3 s")
                           .arg(quoted(text.text), seconds(start), seconds(duration)));
    }
    if (name == QLatin1String("set_title_text")) {
        auto location = findClip(id("clip_id"));
        if (!location || !location->clip->text)
            return failed(QStringLiteral("clip %1 isn't a title").arg(id("clip_id")));
        ClipText text = *location->clip->text;
        const std::string before = text.text;
        text.text = input.value(QStringLiteral("text")).toString().toStdString();
        return applyTo(project, std::make_unique<SetClipTextCommand>(id("clip_id"), text),
                       tr("Change title %1 to %2").arg(quoted(before, 30), quoted(text.text)));
    }
    if (name == QLatin1String("set_transition")) {
        auto location = findClip(id("clip_id"));
        if (!location)
            return failed(QStringLiteral("there is no clip %1").arg(id("clip_id")));
        const QString what = clipName(project, *location->clip);
        const std::string transitionId = input.value(QStringLiteral("transition_id")).toString().toStdString();
        if (transitionId.empty())
            return applyTo(project, std::make_unique<SetClipTransitionCommand>(id("clip_id"), std::nullopt),
                           tr("Remove the transition into %1").arg(what));
        const LoadedEffect* found = EffectLibrary::builtIn().findTransition(transitionId);
        if (!found)
            return failed(QStringLiteral("there is no transition %1").arg(QString::fromStdString(transitionId)));
        const double secondsValue = input.value(QStringLiteral("duration")).toDouble(1.0);
        if (secondsValue < 0.1 || secondsValue > 10.0)
            return failed(QStringLiteral("duration must be 0.1 to 10 seconds"));
        ClipTransition transition{.transitionId = transitionId, .duration = at(project, secondsValue)};
        return applyTo(project, std::make_unique<SetClipTransitionCommand>(id("clip_id"), transition),
                       tr("Add a %1 transition into %2 (%3 s)")
                           .arg(catalogueText(found->manifest.displayTitle()), what, seconds(transition.duration)));
    }
    if (name == QLatin1String("add_effect") || name == QLatin1String("remove_effect")) {
        auto location = findClip(id("clip_id"));
        if (!location || location->track->kind == TrackKind::Audio)
            return failed(QStringLiteral("clip %1 isn't a clip with a picture").arg(id("clip_id")));
        const QString what = clipName(project, *location->clip);
        std::vector<ClipEffect> effects = location->clip->effects;
        if (name == QLatin1String("remove_effect")) {
            const qint64 index = input.value(QStringLiteral("index")).toInteger();
            if (index < 0 || index >= qint64(effects.size()))
                return failed(QStringLiteral("the clip has %1 effects").arg(effects.size()));
            const ClipEffect gone = effects[std::size_t(index)];
            effects.erase(effects.begin() + index);
            const LoadedEffect* loaded = EffectLibrary::builtIn().find(gone.effectId);
            return applyTo(project, std::make_unique<SetClipEffectsCommand>(id("clip_id"), effects, "Remove Effect"),
                           tr("Remove %1 from %2")
                               .arg(loaded ? catalogueText(loaded->manifest.displayTitle()) : QString::fromStdString(gone.effectId), what));
        }
        const std::string effectId = input.value(QStringLiteral("effect_id")).toString().toStdString();
        const LoadedEffect* loaded = EffectLibrary::builtIn().find(effectId);
        if (!loaded)
            return failed(QStringLiteral("there is no effect %1").arg(QString::fromStdString(effectId)));
        ClipEffect effect{.effectId = effectId};
        const QJsonObject values = input.value(QStringLiteral("values")).toObject();
        for (auto it = values.begin(); it != values.end(); ++it) {
            const EffectParamSpec* param = loaded->manifest.findParam(it.key().toStdString());
            if (!param || param->type == ParamType::Color)
                return failed(QStringLiteral("%1 has no number parameter %2").arg(QString::fromStdString(effectId), it.key()));
            effect.values[param->id] = {float(std::clamp(it.value().toDouble(), param->min, param->max))};
        }
        effects.push_back(effect);
        return applyTo(project, std::make_unique<SetClipEffectsCommand>(id("clip_id"), effects, "Add Effect"),
                       tr("Add %1 to %2").arg(catalogueText(loaded->manifest.displayTitle()), what));
    }
    if (name == QLatin1String("set_color")) {
        const QJsonArray ids = input.value(QStringLiteral("clip_ids")).toArray();
        std::vector<std::unique_ptr<Command>> steps;
        QStringList names;
        static const std::vector<std::tuple<const char*, double ColorGrade::*, double, double>> fields{
            {"exposure", &ColorGrade::exposure, -4.0, 4.0},     {"contrast", &ColorGrade::contrast, -1.0, 1.0},
            {"saturation", &ColorGrade::saturation, -1.0, 1.0}, {"temperature", &ColorGrade::temperature, -1.0, 1.0},
            {"tint", &ColorGrade::tint, -1.0, 1.0}};
        for (const QJsonValue& v : ids) {
            auto location = findClip(Id(v.toInteger()));
            if (!location || location->track->kind != TrackKind::Video || location->clip->text)
                return failed(QStringLiteral("clip %1 isn't a video clip").arg(v.toInteger()));
            ColorGrade grade = location->clip->color;
            for (const auto& [key, field, low, high] : fields)
                if (input.contains(QLatin1String(key)))
                    grade.*field = std::clamp(input.value(QLatin1String(key)).toDouble(), low, high);
            steps.push_back(std::make_unique<SetClipColorCommand>(std::vector<Id>{location->clip->id}, grade));
            names << clipName(project, *location->clip);
        }
        if (steps.empty())
            return failed(QStringLiteral("no clips given"));
        QStringList what;
        for (const auto& [key, field, low, high] : fields)
            if (input.contains(QLatin1String(key)))
                what << QStringLiteral("%1 %2").arg(QLatin1String(key)).arg(input.value(QLatin1String(key)).toDouble(), 0, 'g', 2);
        return applyTo(project, std::make_unique<CompositeCommand>(std::move(steps), "Color"),
                       tr("Colour of %1: %2").arg(names.join(QStringLiteral(", ")), what.join(QStringLiteral(", "))));
    }
    if (name == QLatin1String("set_look")) {
        auto location = findClip(id("clip_id"));
        if (!location || location->track->kind != TrackKind::Video || location->clip->text)
            return failed(QStringLiteral("clip %1 isn't a video clip").arg(id("clip_id")));
        const QString what = clipName(project, *location->clip);
        const std::string lookId = input.value(QStringLiteral("look_id")).toString().toStdString();
        if (lookId.empty())
            return applyTo(project, std::make_unique<SetClipLookCommand>(id("clip_id"), std::nullopt, "Remove Look"),
                           tr("Remove the look from %1").arg(what));
        const LookInfo* look = findLook(lookId);
        if (!look)
            return failed(QStringLiteral("there is no look %1").arg(QString::fromStdString(lookId)));
        const double intensity = std::clamp(input.value(QStringLiteral("intensity")).toDouble(1.0), 0.0, 1.0);
        return applyTo(project,
                       std::make_unique<SetClipLookCommand>(id("clip_id"), ClipLook{look->id, look->title, intensity, look->grade},
                                                            "Apply Look"),
                       tr("Give %1 the look %2 (%3%)")
                           .arg(what, QString::fromStdString(look->title))
                           .arg(qRound(intensity * 100)));
    }
    if (name == QLatin1String("set_captions")) {
        std::vector<CaptionBlock> blocks;
        for (const QJsonValue& v : input.value(QStringLiteral("captions")).toArray()) {
            const QJsonObject c = v.toObject();
            const Ticks start = at(project, c.value(QStringLiteral("start")).toDouble());
            const Ticks end = at(project, c.value(QStringLiteral("end")).toDouble());
            if (end <= start)
                return failed(QStringLiteral("each caption must end after it starts"));
            blocks.push_back({start, end, c.value(QStringLiteral("text")).toString().toStdString(), {}});
        }
        const std::size_t count = blocks.size();
        return applyTo(project, std::make_unique<SetCaptionsCommand>(std::move(blocks), "Captions"),
                       tr("Set the captions (%1)").arg(count));
    }
    return failed(QStringLiteral("there is no tool %1").arg(name));
}

QString Copilot::describeProject() const
{
    const Project& p = m_project->m_project;
    QString out;
    const ProjectSettings& s = p.settings;
    out += QStringLiteral("Canvas %1×%2, %3 fps%4%5. Length %6 s.\n")
               .arg(s.width)
               .arg(s.height)
               .arg(double(s.frameRate.num) / s.frameRate.den, 0, 'g', 5)
               .arg(isHdr(s.colour) ? QStringLiteral(", HDR") : QString())
               .arg(s.spherical ? QStringLiteral(", 360°") : QString())
               .arg(seconds(p.duration()));
    out += QStringLiteral("Tracks, top first:\n");
    std::size_t words = 0;
    Id main = kNoId;
    for (const Track& t : p.tracks)
        if (t.kind == TrackKind::Video && main == kNoId)
            main = t.id;
    for (auto it = p.tracks.rbegin(); it != p.tracks.rend(); ++it) {
        const Track& track = *it;
        const QString kind = track.captions ? QStringLiteral("captions")
                           : track.kind == TrackKind::Audio ? QStringLiteral("audio")
                           : track.kind == TrackKind::Title ? QStringLiteral("titles")
                                                            : QStringLiteral("video");
        QStringList flags{kind};
        if (track.id == main)
            flags << QStringLiteral("main");
        if (track.locked)
            flags << QStringLiteral("locked");
        if (track.hidden || track.muted)
            flags << QStringLiteral("off");
        out += QStringLiteral("- track %1 “%2” (%3)%4\n")
                   .arg(track.id)
                   .arg(QString::fromStdString(track.name), flags.join(QStringLiteral(", ")))
                   .arg(track.clips.empty() ? QStringLiteral(": empty") : QString());
        if (track.captions) {
            for (const Clip& clip : track.clips)
                if (clip.text)
                    out += QStringLiteral("  caption %1 %2–%3 %4\n").arg(clip.id).arg(seconds(clip.start), seconds(clip.end()),
                                                                          quoted(clip.text->text, 120));
            continue;
        }
        for (const Clip& clip : track.clips) {
            QStringList details;
            const MediaItem* media = p.findMedia(clip.mediaId);
            if (media && !clip.text) {
                details << QStringLiteral("source %1–%2 of %3 s").arg(seconds(clip.sourceIn), seconds(clip.sourceIn + clip.sourceSpan()),
                                                                        seconds(media->duration));
                if (media->still)
                    details << QStringLiteral("photo");
            }
            if (clip.speed != 1.0 || clip.reversed || clip.freeze)
                details << (clip.freeze ? QStringLiteral("freeze frame")
                                        : QStringLiteral("speed %1%2").arg(clip.speed).arg(clip.reversed ? QStringLiteral(" reversed") : QString()));
            if (clip.transitionIn)
                details << QStringLiteral("transition in: %1 %2 s").arg(QString::fromStdString(clip.transitionIn->transitionId),
                                                                         seconds(clip.transitionIn->duration));
            if (!clip.effects.empty()) {
                QStringList ids;
                for (const ClipEffect& e : clip.effects)
                    ids << QString::fromStdString(e.effectId) + (e.enabled ? QString() : QStringLiteral(" (off)"));
                details << QStringLiteral("effects: ") + ids.join(QStringLiteral(", "));
            }
            if (!clip.color.isNeutral())
                details << QStringLiteral("graded (exposure %1, contrast %2, saturation %3, temperature %4, tint %5)")
                               .arg(clip.color.exposure)
                               .arg(clip.color.contrast)
                               .arg(clip.color.saturation)
                               .arg(clip.color.temperature)
                               .arg(clip.color.tint);
            if (clip.look)
                details << QStringLiteral("look: %1").arg(QString::fromStdString(clip.look->id));
            if (clip.gainDb != 0.0f)
                details << QStringLiteral("volume %1 dB").arg(clip.gainDb);
            if (!clip.keyframes.empty())
                details << QStringLiteral("keyframed");
            if (clip.adjustment)
                details << QStringLiteral("adjustment layer");
            out += QStringLiteral("  clip %1 %2 %3–%4%5\n")
                       .arg(clip.id)
                       .arg(clip.adjustment ? QStringLiteral("adjustment") : clip.drawing ? QStringLiteral("drawing") : clipName(p, clip))
                       .arg(seconds(clip.start), seconds(clip.end()))
                       .arg(details.isEmpty() ? QString() : QStringLiteral(" (") + details.join(QStringLiteral("; ")) + QLatin1Char(')'));
            // What is said in it, in phrases on the timeline (as far as the budget goes).
            if (media && media->hasAudio && clip.audioEnabled && !clip.freeze && words < kMostWords) {
                const std::vector<TimedWord> spoken = m_project->mediaWords(*media);
                QStringList phrase;
                Ticks phraseStart = -1, phraseEnd = 0;
                const auto flush = [&] {
                    if (!phrase.isEmpty())
                        out += QStringLiteral("    said %1–%2 “%3”\n").arg(seconds(phraseStart), seconds(phraseEnd), phrase.join(QLatin1Char(' ')));
                    phrase.clear();
                    phraseStart = -1;
                };
                for (const TimedWord& w : spoken) {
                    if (w.end <= clip.sourceIn || w.start >= clip.sourceIn + clip.sourceSpan() || clip.reversed)
                        continue;
                    const Ticks start = clip.start + Ticks(double(w.start - clip.sourceIn) / clip.speed);
                    const Ticks end = clip.start + Ticks(double(w.end - clip.sourceIn) / clip.speed);
                    if (phraseStart >= 0 && (phrase.size() >= 12 || start - phraseEnd > kTicksPerSecond / 2))
                        flush();
                    if (phraseStart < 0)
                        phraseStart = start;
                    phraseEnd = end;
                    phrase << QString::fromStdString(w.text).trimmed();
                    if (++words >= kMostWords)
                        break;
                }
                flush();
            }
        }
    }
    if (!p.markers.empty()) {
        out += QStringLiteral("Markers:\n");
        for (const tve::Marker& m : p.markers)
            out += QStringLiteral("- %1 s%2 %3\n").arg(seconds(m.time), m.chapter ? QStringLiteral(" (chapter)") : QString(), quoted(m.note));
    }
    if (words >= kMostWords)
        out += QStringLiteral("(What is said is cut short here.)\n");
    return out;
}

void Copilot::add(const QVariantMap& message)
{
    m_messages << message;
    emit changed();
}

void Copilot::setChangesState(const QString& state)
{
    for (qsizetype i = m_messages.size() - 1; i >= 0; --i) {
        QVariantMap m = m_messages[i].toMap();
        if (m.value(QStringLiteral("kind")) == QLatin1String("changes") && m.value(QStringLiteral("state")) == QLatin1String("proposed")) {
            m.insert(QStringLiteral("state"), state);
            m_messages[i] = m;
            break;
        }
    }
}

bool Copilot::ask(const QString& request)
{
    const QString text = request.trimmed();
    if (text.isEmpty() || m_busy)
        return false;
    if (!m_proposal.isEmpty())
        discardChanges(); // asked again instead: those aren't applied
    add({{QStringLiteral("kind"), QStringLiteral("you")}, {QStringLiteral("text"), text}});
    m_base = m_project->m_project;
    m_sandbox = m_project->m_project;
    m_proposal.clear();
    m_rounds = 0;
    QString content;
    if (!m_note.isEmpty())
        content += m_note + QStringLiteral("\n\n");
    m_note.clear();
    content += QStringLiteral("<project>\n") + describeProject() + QStringLiteral("</project>\n\n") + text;
    QJsonObject last = m_history.isEmpty() ? QJsonObject() : m_history.last().toObject();
    if (last.value(QStringLiteral("role")) == QLatin1String("user") && last.value(QStringLiteral("content")).isArray()) {
        // (It stopped after tool results: the request joins that message, so turns alternate.)
        QJsonArray blocks = last.value(QStringLiteral("content")).toArray();
        blocks.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("text")}, {QStringLiteral("text"), content}});
        last.insert(QStringLiteral("content"), blocks);
        m_history[m_history.size() - 1] = last;
    } else {
        m_history.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), content}});
    }
    send();
    return true;
}

void Copilot::send()
{
    AgentRequest request;
    request.system = systemPrompt();
    request.tools = tools();
    request.messages = m_history;
    request.model = m_project->m_cloud->claudeModel();
    if (!m_project->m_cloud->converse(request, [this](const AgentResult& result) { received(result); })) {
        rollBack();
        add({{QStringLiteral("kind"), QStringLiteral("error")}, {QStringLiteral("text"), tr("Another AI job is running; try again when it's done.")}});
        m_busy = false;
        emit changed();
        return;
    }
    m_busy = true;
    emit changed();
}

void Copilot::received(const AgentResult& result)
{
    if (!result.turn.ok) {
        rollBack();
        if (!result.turn.cancelled)
            add({{QStringLiteral("kind"), QStringLiteral("error")}, {QStringLiteral("text"), result.turn.error}});
        m_busy = false;
        emit changed();
        return;
    }
    m_history.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("assistant")}, {QStringLiteral("content"), result.content}});
    if (result.toolCalls.isEmpty() || result.turn.stopReason != QLatin1String("tool_use")) {
        finishTurn(result.turn.text);
        return;
    }
    // Every call's result in one message.
    QJsonArray results;
    for (const ToolCall& call : result.toolCalls) {
        QJsonObject block{{QStringLiteral("type"), QStringLiteral("tool_result")}, {QStringLiteral("tool_use_id"), call.id}};
        if (call.invalid) {
            block.insert(QStringLiteral("is_error"), true);
            block.insert(QStringLiteral("content"),
                         QString::fromUtf8(QJsonDocument(QJsonObject{{QStringLiteral("INVALID_JSON"), call.raw}}).toJson(QJsonDocument::Compact)));
        } else {
            const Outcome outcome = run(call.name, call.input, *m_sandbox);
            if (!outcome.ok)
                block.insert(QStringLiteral("is_error"), true);
            block.insert(QStringLiteral("content"), QString::fromUtf8(QJsonDocument(outcome.result).toJson(QJsonDocument::Compact)));
            if (outcome.ok && !outcome.change.isEmpty())
                m_proposal << outcome.change;
        }
        results.append(block);
    }
    m_history.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), results}});
    if (++m_rounds >= kMostRounds) {
        // (Its last calls have their results; it is told why it stops with the next request.)
        m_note = QStringLiteral("(You stopped after %1 rounds of tools.)").arg(kMostRounds);
        finishTurn(tr("That took more steps than I can take at once; here is what I did so far."));
        return;
    }
    emit changed();
    send();
}

// The request that ended in an error (or was cancelled) goes, with its turns so far: the
// history ends where it did before it (only trailing turns are removed, so what came before
// keeps its thinking valid), and nothing it made is proposed.
void Copilot::rollBack()
{
    while (!m_history.isEmpty()) {
        const QJsonObject last = m_history.last().toObject();
        m_history.removeLast();
        if (last.value(QStringLiteral("role")) == QLatin1String("user") && last.value(QStringLiteral("content")).isString())
            break;
    }
    m_proposal.clear();
    m_base.reset();
    m_sandbox.reset();
}

void Copilot::finishTurn(const QString& text)
{
    m_busy = false;
    if (m_project->m_cloud->job() == QLatin1String("copilot"))
        m_project->m_cloud->endJob();
    if (!text.trimmed().isEmpty())
        add({{QStringLiteral("kind"), QStringLiteral("copilot")}, {QStringLiteral("text"), text.trimmed()}});
    if (!m_proposal.isEmpty())
        add({{QStringLiteral("kind"), QStringLiteral("changes")},
             {QStringLiteral("changes"), m_proposal},
             {QStringLiteral("state"), QStringLiteral("proposed")}});
    emit changed();
}

bool Copilot::applyChanges()
{
    if (m_proposal.isEmpty() || !m_base || !m_sandbox || m_busy)
        return false;
    const QString label = m_proposal.size() == 1 ? tr("Copilot: %1").arg(m_proposal.first())
                                                 : tr("Copilot: %1 changes").arg(m_proposal.size());
    if (!m_project->apply(std::make_unique<ApplyEditsCommand>(*m_base, *m_sandbox, label.toStdString()))) {
        add({{QStringLiteral("kind"), QStringLiteral("error")},
             {QStringLiteral("text"), tr("The project changed since these were made: ask again to have them made for it as it is now.")}});
        return false;
    }
    setChangesState(QStringLiteral("applied"));
    m_note = QStringLiteral("(The person applied your changes.)");
    m_proposal.clear();
    m_base.reset();
    m_sandbox.reset();
    emit changed();
    return true;
}

void Copilot::discardChanges()
{
    if (m_proposal.isEmpty())
        return;
    setChangesState(QStringLiteral("discarded"));
    m_note = QStringLiteral("(The person didn't apply your last changes: the project is as it was before them.)");
    m_proposal.clear();
    m_base.reset();
    m_sandbox.reset();
    emit changed();
}

void Copilot::startOver()
{
    cancel();
    m_history = {};
    m_messages.clear();
    m_proposal.clear();
    m_base.reset();
    m_sandbox.reset();
    m_note.clear();
    m_busy = false;
    emit changed();
}

void Copilot::cancel()
{
    if (m_busy)
        m_project->m_cloud->cancel();
}
