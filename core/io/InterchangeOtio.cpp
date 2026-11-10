// OpenTimelineIO's JSON (.otio): a Timeline of a Stack of Tracks of Clips, Gaps and
// Transitions, written in the schema versions OpenTimelineIO 0.15 and later read.
#include "io/Interchange.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <stdexcept>

namespace tve::interchange {

namespace {

using nlohmann::json;

double rateOf(FrameRate rate)
{
    return double(rate.num) / double(rate.den);
}

// The common rates exactly (29.97 is 30000/1001); others to the nearest whole frame rate.
FrameRate frameRateOf(double rate)
{
    for (const FrameRate r : {FrameRate{24000, 1001}, FrameRate{30000, 1001}, FrameRate{60000, 1001}})
        if (std::abs(rate - rateOf(r)) < 0.005)
            return r;
    return FrameRate{std::max(1, int(std::lround(rate))), 1};
}

json rationalTime(Ticks ticks, FrameRate rate)
{
    return {{"OTIO_SCHEMA", "RationalTime.1"}, {"rate", rateOf(rate)}, {"value", double(framesOf(ticks, rate))}};
}

json timeRange(Ticks start, Ticks duration, FrameRate rate)
{
    return {{"OTIO_SCHEMA", "TimeRange.1"}, {"start_time", rationalTime(start, rate)}, {"duration", rationalTime(duration, rate)}};
}

json item(const char* schema, const std::string& name, Ticks start, Ticks duration, FrameRate rate)
{
    return {{"OTIO_SCHEMA", schema},  {"name", name},    {"metadata", json::object()},
            {"effects", json::array()}, {"markers", json::array()}, {"enabled", true},
            {"source_range", timeRange(start, duration, rate)}};
}

const char* markerColour(MarkerColor color)
{
    switch (color) {
    case MarkerColor::Red: return "RED";
    case MarkerColor::Orange: return "ORANGE";
    case MarkerColor::Yellow: return "YELLOW";
    case MarkerColor::Green: return "GREEN";
    case MarkerColor::Blue: return "BLUE";
    case MarkerColor::Purple: return "PURPLE";
    }
    return "BLUE";
}

MarkerColor markerColourFrom(const std::string& name, Notes& notes)
{
    if (name == "RED" || name == "PINK" || name == "MAGENTA")
        return MarkerColor::Red;
    if (name == "ORANGE")
        return MarkerColor::Orange;
    if (name == "YELLOW")
        return MarkerColor::Yellow;
    if (name == "GREEN")
        return MarkerColor::Green;
    if (name == "PURPLE")
        return MarkerColor::Purple;
    if (name != "BLUE" && name != "CYAN" && !name.empty())
        notes.addLine("Marker colours this app doesn't have (as the nearest)");
    return MarkerColor::Blue;
}

// A track's children: its items with Gaps between them and Transitions before the clips
// they lead into.
json children(const Track& track, FrameRate rate, bool linkedAudio)
{
    json out = json::array();
    Ticks at = 0;
    for (const Item& it : track.items) {
        if (it.start > at)
            out.push_back(item("Gap.1", "", 0, it.start - at, rate));
        if (it.transitionIn && !out.empty() && it.start == at) {
            out.push_back({{"OTIO_SCHEMA", "Transition.1"},
                           {"name", "Cross Dissolve"},
                           {"metadata", json::object()},
                           {"transition_type", "SMPTE_Dissolve"},
                           {"in_offset", rationalTime(it.transitionIn->before, rate)},
                           {"out_offset", rationalTime(it.transitionIn->after, rate)}});
        }
        json clip = item("Clip.2", it.name, it.sourceIn, it.duration, rate);
        clip["active_media_reference_key"] = "DEFAULT_MEDIA";
        if (it.kind == Item::Kind::Title) {
            clip["media_references"] = {{"DEFAULT_MEDIA",
                                         {{"OTIO_SCHEMA", "GeneratorReference.1"},
                                          {"name", "Title"},
                                          {"generator_kind", "title"},
                                          {"parameters", {{"text", it.text}}},
                                          {"available_range", nullptr},
                                          {"metadata", json::object()}}}};
            clip["source_range"] = timeRange(0, it.duration, rate);
        } else {
            clip["media_references"] = {{"DEFAULT_MEDIA",
                                         {{"OTIO_SCHEMA", "ExternalReference.1"},
                                          {"name", it.name},
                                          {"target_url", fileUrl(it.path)},
                                          {"available_range", nullptr},
                                          {"metadata", json::object()}}}};
            if (it.freeze)
                clip["effects"].push_back({{"OTIO_SCHEMA", "FreezeFrame.1"}, {"name", ""}, {"effect_name", "FreezeFrame"},
                                           {"metadata", json::object()}, {"time_scalar", 0.0}});
            else if (it.speed != 1.0 || it.reversed)
                clip["effects"].push_back({{"OTIO_SCHEMA", "LinearTimeWarp.1"}, {"name", ""}, {"effect_name", "LinearTimeWarp"},
                                           {"metadata", json::object()},
                                           {"time_scalar", it.reversed ? -it.speed : it.speed}});
        }
        if (linkedAudio)
            clip["metadata"]["tve"] = {{"linkedAudio", true}}; // a video clip's own sound
        out.push_back(std::move(clip));
        at = it.start + it.duration;
    }
    return out;
}

json trackJson(const Track& track, FrameRate rate, bool linkedAudio = false)
{
    return {{"OTIO_SCHEMA", "Track.1"},
            {"name", linkedAudio ? track.name + " (sound)" : track.name},
            {"kind", track.audio || linkedAudio ? "Audio" : "Video"},
            {"enabled", track.enabled},
            {"metadata", json::object()},
            {"effects", json::array()},
            {"markers", json::array()},
            {"source_range", nullptr},
            {"children", children(track, rate, linkedAudio)}};
}

// --- Reading ---

Ticks ticksOf(const json& time)
{
    if (!time.is_object())
        return 0;
    const double rate = time.value("rate", 0.0);
    if (rate <= 0.0)
        return 0;
    return fromSeconds(time.value("value", 0.0) / rate);
}

double rateIn(const json& time)
{
    return time.is_object() ? time.value("rate", 0.0) : 0.0;
}

std::string schemaOf(const json& j)
{
    return j.is_object() ? j.value("OTIO_SCHEMA", std::string()) : std::string();
}

bool isSchema(const json& j, const char* name)
{
    const std::string schema = schemaOf(j);
    return schema.rfind(std::string(name) + ".", 0) == 0;
}

struct Range {
    Ticks start = 0;
    Ticks duration = 0;
};

Range rangeOf(const json& item)
{
    const json& range = item.contains("source_range") ? item["source_range"] : json();
    if (!range.is_object())
        return {};
    return {ticksOf(range.value("start_time", json())), ticksOf(range.value("duration", json()))};
}

// An item's length on its track: its source range, or a nested composition's own.
Ticks lengthOf(const json& item)
{
    if (const Range r = rangeOf(item); r.duration > 0)
        return r.duration;
    if (isSchema(item, "Track")) {
        Ticks sum = 0;
        for (const json& child : item.value("children", json::array()))
            if (!isSchema(child, "Transition"))
                sum += lengthOf(child);
        return sum;
    }
    if (isSchema(item, "Stack")) {
        Ticks longest = 0;
        for (const json& child : item.value("children", json::array()))
            longest = std::max(longest, lengthOf(child));
        return longest;
    }
    return 0;
}

const json* activeReference(const json& clip)
{
    if (clip.contains("media_references") && clip["media_references"].is_object()) {
        const std::string key = clip.value("active_media_reference_key", std::string("DEFAULT_MEDIA"));
        const json& references = clip["media_references"];
        if (references.contains(key))
            return &references[key];
        if (!references.empty())
            return &references.begin().value();
    }
    if (clip.contains("media_reference"))
        return &clip["media_reference"];
    return nullptr;
}

void readMarkers(const json& owner, Ticks offset, std::vector<Marker>& markers, Notes& notes)
{
    for (const json& m : owner.value("markers", json::array())) {
        Marker marker;
        const json& range = m.value("marked_range", json());
        marker.time = offset + ticksOf(range.value("start_time", json()));
        marker.duration = ticksOf(range.value("duration", json()));
        marker.color = markerColourFrom(m.value("color", std::string()), notes);
        marker.note = m.value("comment", std::string());
        if (marker.note.empty())
            marker.note = m.value("name", std::string());
        if (m.contains("metadata") && m["metadata"].contains("tve"))
            marker.chapter = m["metadata"]["tve"].value("chapter", false);
        markers.push_back(marker);
    }
}

Track readTrack(const json& source, const std::string& base, std::vector<Marker>& markers, Notes& notes)
{
    Track track;
    track.audio = source.value("kind", std::string("Video")) == "Audio";
    track.name = source.value("name", std::string());
    track.enabled = source.value("enabled", true);
    if (!source.value("effects", json::array()).empty())
        notes.addLine("Track effects (left out)");
    Ticks at = 0;
    std::optional<Transition> pending;
    for (const json& child : source.value("children", json::array())) {
        if (isSchema(child, "Transition")) {
            pending = Transition{ticksOf(child.value("in_offset", json())), ticksOf(child.value("out_offset", json())),
                                 child.value("transition_type", std::string()) == "SMPTE_Dissolve" ? "dissolve" : "other"};
            continue;
        }
        const Ticks length = lengthOf(child);
        if (isSchema(child, "Gap")) {
            at += length;
            pending.reset();
            continue;
        }
        if (!isSchema(child, "Clip")) {
            notes.add(isSchema(child, "Stack") || isSchema(child, "Track") ? "Nested sequences (left as space)"
                                                                          : "Items this app doesn't know (left as space)");
            at += length;
            pending.reset();
            continue;
        }
        const json& meta = child.value("metadata", json::object());
        if (meta.is_object() && meta.contains("tve") && meta["tve"].value("linkedAudio", false)) {
            at += length; // the sound of a video clip, which plays its own
            pending.reset();
            continue;
        }
        Item it;
        const Range range = rangeOf(child);
        it.name = child.value("name", std::string());
        it.start = at;
        it.duration = length;
        it.sourceIn = range.start;
        it.hasVideo = !track.audio;
        it.hasAudio = true;
        it.transitionIn = pending;
        pending.reset();
        const json* reference = activeReference(child);
        if (reference && isSchema(*reference, "ExternalReference")) {
            it.path = pathFromUrl(reference->value("target_url", std::string()), base);
        } else if (reference && isSchema(*reference, "GeneratorReference")) {
            it.kind = Item::Kind::Title;
            const json& parameters = reference->value("parameters", json::object());
            it.text = parameters.is_object() ? parameters.value("text", std::string()) : std::string();
            if (reference->value("generator_kind", std::string()) != "title")
                notes.add("Generated clips (as titles)");
            if (it.text.empty())
                it.text = it.name.empty() ? "Title" : it.name;
        } else {
            notes.add("Clips without a media file (left as space)");
            at += length;
            continue;
        }
        for (const json& effect : child.value("effects", json::array())) {
            if (isSchema(effect, "FreezeFrame")) {
                it.freeze = true;
            } else if (isSchema(effect, "LinearTimeWarp")) {
                const double scalar = effect.value("time_scalar", 1.0);
                if (scalar == 0.0) {
                    it.freeze = true;
                } else {
                    it.speed *= std::abs(scalar);
                    it.reversed = scalar < 0.0;
                }
            } else {
                notes.add("Effects (left out)");
            }
        }
        readMarkers(child, at - range.start, markers, notes);
        track.items.push_back(std::move(it));
        at += length;
    }
    return track;
}

} // namespace

std::string writeOtio(const Timeline& timeline, Notes&)
{
    json tracks = json::array();
    for (const Track& track : timeline.tracks)
        if (!track.audio)
            tracks.push_back(trackJson(track, timeline.rate));
    for (const Track& track : timeline.tracks)
        if (track.audio)
            tracks.push_back(trackJson(track, timeline.rate));
    // Video clips' own sound, as audio tracks (other editors keep sound on audio tracks).
    for (const Track& track : timeline.tracks) {
        if (track.audio)
            continue;
        Track sound = track;
        std::erase_if(sound.items, [](const Item& it) { return it.kind != Item::Kind::Media || !it.hasAudio; });
        if (!sound.items.empty())
            tracks.push_back(trackJson(sound, timeline.rate, true));
    }
    json markers = json::array();
    for (const Marker& m : timeline.markers) {
        markers.push_back({{"OTIO_SCHEMA", "Marker.2"},
                           {"name", m.note},
                           {"comment", m.note},
                           {"color", markerColour(m.color)},
                           {"marked_range", timeRange(m.time, m.duration, timeline.rate)},
                           {"metadata", {{"tve", {{"chapter", m.chapter}}}}}});
    }
    const json root{
        {"OTIO_SCHEMA", "Timeline.1"},
        {"name", timeline.name},
        {"metadata", {{"tve", {{"width", timeline.width}, {"height", timeline.height}}}}},
        {"global_start_time", rationalTime(0, timeline.rate)},
        {"tracks",
         {{"OTIO_SCHEMA", "Stack.1"},
          {"name", "tracks"},
          {"enabled", true},
          {"metadata", json::object()},
          {"effects", json::array()},
          {"markers", markers},
          {"source_range", nullptr},
          {"children", tracks}}},
    };
    return root.dump(4) + "\n";
}

Timeline readOtio(const std::string& text, const std::string& base, Notes& notes)
{
    json root;
    try {
        root = json::parse(text);
    } catch (const json::exception&) {
        throw std::runtime_error("it isn't an OpenTimelineIO file (not JSON)");
    }
    // A collection (a bin of timelines): its first timeline.
    if (isSchema(root, "SerializableCollection")) {
        const auto children = root.value("children", json::array());
        const auto first = std::ranges::find_if(children, [](const json& c) { return isSchema(c, "Timeline"); });
        if (first == children.end())
            throw std::runtime_error("the collection has no timeline");
        if (std::ranges::count_if(children, [](const json& c) { return isSchema(c, "Timeline"); }) > 1)
            notes.addLine("The collection's other timelines (only the first is opened)");
        root = *first;
    }
    if (!isSchema(root, "Timeline"))
        throw std::runtime_error("it isn't an OpenTimelineIO timeline");
    Timeline timeline;
    timeline.name = root.value("name", std::string());
    timeline.width = 0;
    timeline.height = 0;
    if (root.contains("metadata") && root["metadata"].contains("tve")) {
        timeline.width = root["metadata"]["tve"].value("width", 0);
        timeline.height = root["metadata"]["tve"].value("height", 0);
    }
    const json& stack = root.value("tracks", json::object());
    // The rate: the timeline's start time's, else the first clip's.
    double rate = rateIn(root.value("global_start_time", json()));
    for (const json& track : stack.value("children", json::array()))
        for (const json& child : track.value("children", json::array()))
            if (rate <= 0.0 && child.contains("source_range") && child["source_range"].is_object())
                rate = rateIn(child["source_range"].value("duration", json()));
    timeline.rate = frameRateOf(rate > 0.0 ? rate : 30.0);
    readMarkers(stack, 0, timeline.markers, notes);
    std::vector<Track> video, audio;
    for (const json& track : stack.value("children", json::array())) {
        if (!isSchema(track, "Track")) {
            notes.addLine("Nested compositions in the top stack (left out)");
            continue;
        }
        Track read = readTrack(track, base, timeline.markers, notes);
        (read.audio ? audio : video).push_back(std::move(read));
    }
    for (Track& t : video)
        timeline.tracks.push_back(std::move(t));
    for (Track& t : audio)
        timeline.tracks.push_back(std::move(t));
    return timeline;
}

} // namespace tve::interchange
