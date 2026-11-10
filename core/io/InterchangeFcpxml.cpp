// Final Cut Pro XML (.fcpxml, version 1.10 written; 1.8 to 1.13 read): resources (the format,
// the media assets, the title and dissolve effects) and a project whose sequence's spine is
// the main track, with the other tracks' clips connected to it in lanes (above it for
// video, below it for sound). Read with libxml2 (the system's), never fetching anything.
#include "io/Interchange.h"

#include <libxml/parser.h>
#include <libxml/tree.h>

#include <cmath>
#include <functional>
#include <map>
#include <memory>
#include <numeric>
#include <sstream>
#include <stdexcept>

namespace tve::interchange {

namespace {

const char* const kTitleUid = ".../Titles.localized/Bumper:Opener.localized/Basic Title.localized/Basic Title.moti";
const char* const kDissolveUid = "FxPlug:4731E73A-8DAC-4113-9A30-AE85B1761265";

std::string escape(const std::string& text)
{
    std::string out;
    for (char c : text) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        case '\'': out += "&apos;"; break;
        default:
            // Control characters other than tab and newlines aren't allowed in XML 1.0.
            if (static_cast<unsigned char>(c) >= 0x20 || c == '\t' || c == '\n' || c == '\r')
                out += c;
        }
    }
    return out;
}

// A time as FCPXML writes it: whole frames of `rate`, as a rational number of seconds.
std::string timeText(Ticks ticks, FrameRate rate)
{
    const std::int64_t frames = framesOf(ticks, rate);
    if (frames == 0)
        return "0s";
    std::int64_t num = frames * rate.den;
    std::int64_t den = rate.num;
    const std::int64_t g = std::gcd(std::llabs(num), den);
    num /= g;
    den /= g;
    return den == 1 ? std::to_string(num) + "s" : std::to_string(num) + "/" + std::to_string(den) + "s";
}

// --- Writing ---

struct Writer {
    const Timeline& timeline;
    Notes& notes;
    std::ostringstream xml;
    std::map<std::string, std::string> assetIds; // by path
    int nextId = 1;
    int nextStyle = 1;

    std::string t(Ticks ticks) const { return timeText(ticks, timeline.rate); }

    std::string newId() { return "r" + std::to_string(nextId++); }
};

// A spine element: where it is on the timeline and in its own time (for what hangs off it).
struct SpineElement {
    const Item* item = nullptr; // null: a gap
    Ticks offset = 0;
    Ticks duration = 0;
    Ticks start = 0; // its own time at its beginning
    std::vector<std::string> attached; // connected clips and markers, written inside it
};

std::string clipXml(Writer& w, const Item& it, Ticks offset, int lane, bool audioTrack, bool enabled)
{
    std::ostringstream x;
    const std::string laneAttr = lane != 0 ? " lane=\"" + std::to_string(lane) + "\"" : std::string();
    const std::string enabledAttr = enabled ? "" : " enabled=\"0\"";
    if (it.kind == Item::Kind::Title) {
        const std::string style = "ts" + std::to_string(w.nextStyle++);
        x << "<title ref=\"" << w.assetIds.at("") << "\"" << laneAttr << " offset=\"" << w.t(offset) << "\" name=\"Title\" "
          << "start=\"0s\" duration=\"" << w.t(it.duration) << "\"" << enabledAttr << ">"
          << "<text><text-style ref=\"" << style << "\">" << escape(it.text) << "</text-style></text>"
          << "<text-style-def id=\"" << style << "\"><text-style font=\"Helvetica\" fontSize=\"63\" fontColor=\"1 1 1 1\" "
          << "alignment=\"center\"/></text-style-def>";
        return x.str();
    }
    const std::string ref = w.assetIds.at(it.path);
    const bool retimed = it.freeze || it.reversed || it.speed != 1.0;
    const char* element = it.still ? "video" : "asset-clip";
    x << "<" << element << " ref=\"" << ref << "\"" << laneAttr << " offset=\"" << w.t(offset) << "\" name=\""
      << escape(it.name) << "\" start=\"" << (retimed || it.still ? "0s" : w.t(it.sourceIn)) << "\" duration=\""
      << w.t(it.duration) << "\"" << enabledAttr;
    if (!it.still) {
        // Which of the file's pictures and sound the clip plays.
        if (audioTrack && it.hasVideo)
            x << " srcEnable=\"audio\"";
        else if (!audioTrack && !it.hasAudio)
            x << " srcEnable=\"video\"";
    }
    x << ">";
    if (retimed && !it.still) {
        // The clip's own time (0 to its duration) to the media's.
        const Ticks first = it.reversed ? it.sourceIn + it.sourceSpan() : it.sourceIn;
        const Ticks last = it.freeze ? it.sourceIn : it.reversed ? it.sourceIn : it.sourceIn + it.sourceSpan();
        x << "<timeMap><timept time=\"0s\" value=\"" << w.t(first) << "\" interp=\"linear\"/><timept time=\""
          << w.t(it.duration) << "\" value=\"" << w.t(last) << "\" interp=\"linear\"/></timeMap>";
    }
    return x.str();
}

std::string closing(const Item* it)
{
    if (!it)
        return "</gap>";
    if (it->kind == Item::Kind::Title)
        return "</title>";
    return it->still ? "</video>" : "</asset-clip>";
}

} // namespace

std::string writeFcpxml(const Timeline& timeline, Notes& notes)
{
    Writer w{timeline, notes, {}, {}, 1, 1};
    const std::string format = w.newId();
    // Assets: each file once.
    std::ostringstream resources;
    resources << "<format id=\"" << format << "\" frameDuration=\"" << timeText(frameDuration(timeline.rate), timeline.rate)
              << "\" width=\"" << timeline.width << "\" height=\"" << timeline.height << "\"/>";
    std::map<std::string, Ticks> lengths;
    std::map<std::string, const Item*> firsts;
    bool titles = false;
    for (const Track& track : timeline.tracks)
        for (const Item& it : track.items) {
            if (it.kind == Item::Kind::Title) {
                titles = true;
                continue;
            }
            lengths[it.path] = std::max({lengths[it.path], it.mediaDuration, it.sourceIn + it.sourceSpan(), it.duration});
            if (!firsts.contains(it.path))
                firsts[it.path] = &it;
        }
    for (const auto& [path, first] : firsts) {
        const std::string id = w.newId();
        w.assetIds[path] = id;
        resources << "<asset id=\"" << id << "\" name=\"" << escape(fileName(path)) << "\" start=\"0s\" duration=\""
                  << (first->still ? "0s" : w.t(lengths[path])) << "\" hasVideo=\"" << (first->hasVideo ? 1 : 0)
                  << "\" format=\"" << format << "\"";
        if (first->hasAudio || !first->still)
            resources << " hasAudio=\"1\" audioSources=\"1\" audioChannels=\"2\" audioRate=\"48000\"";
        resources << "><media-rep kind=\"original-media\" src=\"" << escape(fileUrl(path)) << "\"/></asset>";
    }
    if (titles) {
        w.assetIds[""] = w.newId();
        resources << "<effect id=\"" << w.assetIds[""] << "\" name=\"Basic Title\" uid=\"" << kTitleUid << "\"/>";
    }
    const std::string dissolve = w.newId();
    resources << "<effect id=\"" << dissolve << "\" name=\"Cross Dissolve\" uid=\"" << kDissolveUid << "\"/>";

    // The spine: the main (lowest video) track, its spaces as gaps, to the timeline's end.
    const Track* main = nullptr;
    for (const Track& track : timeline.tracks)
        if (!track.audio && !main)
            main = &track;
    std::vector<SpineElement> spine;
    Ticks at = 0;
    const Ticks end = std::max<Ticks>(timeline.duration(), frameDuration(timeline.rate));
    if (main) {
        for (const Item& it : main->items) {
            if (it.start > at)
                spine.push_back({nullptr, at, it.start - at, 0, {}});
            spine.push_back({&it, it.start, it.duration, it.freeze || it.reversed || it.speed != 1.0 || it.still ? 0 : it.sourceIn, {}});
            at = it.start + it.duration;
        }
    }
    if (at < end)
        spine.push_back({nullptr, at, end - at, 0, {}});
    // Whatever is at `time` on the spine, to hang a connected clip or marker off.
    const auto holder = [&](Ticks time) -> SpineElement& {
        for (SpineElement& e : spine)
            if (time >= e.offset && time < e.offset + e.duration)
                return e;
        return spine.back();
    };
    const auto local = [&](const SpineElement& e, Ticks time) { return e.start + (time - e.offset); };

    // The other tracks, connected in lanes: video above (1, 2…), sound below (-1, -2…).
    int videoLane = 0, audioLane = 0;
    for (const Track& track : timeline.tracks) {
        if (&track == main)
            continue;
        const int lane = track.audio ? -(++audioLane) : ++videoLane;
        for (const Item& it : track.items) {
            if (it.transitionIn)
                notes.add("Transitions off the main track (left out in Final Cut XML)");
            if (it.kind == Item::Kind::Title && track.audio)
                continue;
            SpineElement& e = holder(it.start);
            e.attached.push_back(clipXml(w, it, local(e, it.start), lane, track.audio, track.enabled) + closing(&it));
        }
    }
    // Markers, on what is at their time.
    bool coloured = false;
    for (const Marker& m : timeline.markers) {
        SpineElement& e = holder(m.time);
        std::ostringstream x;
        x << "<" << (m.chapter ? "chapter-marker" : "marker") << " start=\"" << w.t(local(e, m.time)) << "\" duration=\""
          << w.t(std::max(m.duration, frameDuration(timeline.rate))) << "\" value=\"" << escape(m.note) << "\""
          << (m.chapter ? " posterOffset=\"0s\"" : "") << "/>";
        e.attached.push_back(x.str());
        coloured = coloured || m.color != MarkerColor::Blue;
    }
    if (coloured)
        notes.addLine("Marker colours (Final Cut's markers have none)");

    std::ostringstream body;
    for (std::size_t i = 0; i < spine.size(); ++i) {
        const SpineElement& e = spine[i];
        if (e.item && e.item->transitionIn && i > 0 && spine[i - 1].item && spine[i - 1].offset + spine[i - 1].duration == e.offset) {
            const Transition& tr = *e.item->transitionIn;
            body << "<transition name=\"Cross Dissolve\" offset=\"" << w.t(e.offset - tr.before) << "\" duration=\""
                 << w.t(tr.before + tr.after) << "\"><filter-video ref=\"" << dissolve << "\" name=\"Cross Dissolve\"/></transition>";
        }
        if (e.item)
            body << clipXml(w, *e.item, e.offset, 0, false, main->enabled);
        else
            body << "<gap name=\"Gap\" offset=\"" << w.t(e.offset) << "\" start=\"0s\" duration=\"" << w.t(e.duration) << "\">";
        for (const std::string& attached : e.attached)
            body << attached;
        body << closing(e.item);
    }

    w.xml << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<!DOCTYPE fcpxml>\n<fcpxml version=\"1.10\">"
          << "<resources>" << resources.str() << "</resources>"
          << "<library><event name=\"" << escape(timeline.name) << "\"><project name=\"" << escape(timeline.name) << "\">"
          << "<sequence format=\"" << format << "\" duration=\"" << w.t(end) << "\" tcStart=\"0s\" tcFormat=\"NDF\" "
          << "audioLayout=\"stereo\" audioRate=\"48k\"><spine>" << body.str() << "</spine></sequence>"
          << "</project></event></library></fcpxml>\n";
    return w.xml.str();
}

// --- Reading ---

namespace {

struct DocDeleter {
    void operator()(xmlDoc* doc) const { xmlFreeDoc(doc); }
};

std::string attr(xmlNode* node, const char* name)
{
    xmlChar* value = xmlGetProp(node, reinterpret_cast<const xmlChar*>(name));
    if (!value)
        return {};
    std::string out(reinterpret_cast<const char*>(value));
    xmlFree(value);
    return out;
}

bool is(xmlNode* node, const char* name)
{
    return node && node->type == XML_ELEMENT_NODE && xmlStrcmp(node->name, reinterpret_cast<const xmlChar*>(name)) == 0;
}

std::vector<xmlNode*> elements(xmlNode* parent)
{
    std::vector<xmlNode*> out;
    for (xmlNode* child = parent ? parent->children : nullptr; child; child = child->next)
        if (child->type == XML_ELEMENT_NODE)
            out.push_back(child);
    return out;
}

xmlNode* first(xmlNode* parent, const char* name)
{
    for (xmlNode* child : elements(parent))
        if (is(child, name))
            return child;
    return nullptr;
}

std::string textOf(xmlNode* node)
{
    xmlChar* content = xmlNodeGetContent(node);
    std::string out = content ? reinterpret_cast<const char*>(content) : "";
    xmlFree(content);
    return out;
}

// "1001/30000s", "5s", "1.5s" (missing: 0).
Ticks parseTime(const std::string& text)
{
    if (text.empty())
        return 0;
    std::string body = text;
    if (body.back() == 's')
        body.pop_back();
    try {
        if (const auto slash = body.find('/'); slash != std::string::npos) {
            const long long num = std::stoll(body.substr(0, slash));
            const long long den = std::stoll(body.substr(slash + 1));
            if (den <= 0)
                return 0;
            return Ticks(static_cast<__int128>(num) * kTicksPerSecond / den);
        }
        return fromSeconds(std::stod(body));
    } catch (const std::exception&) {
        return 0;
    }
}

FrameRate rateOfFrame(Ticks frame)
{
    for (const FrameRate r : {FrameRate{24000, 1001}, FrameRate{30000, 1001}, FrameRate{60000, 1001}})
        if (std::llabs(frameDuration(r) - frame) < 1000)
            return r;
    return FrameRate{std::max(1, int(std::lround(double(kTicksPerSecond) / double(std::max<Ticks>(frame, 1))))), 1};
}

struct Asset {
    std::string path;
    Ticks start = 0;
    bool hasVideo = true;
    bool hasAudio = true;
    bool still = false;
};

struct Reader {
    std::string base;
    Notes& notes;
    std::map<std::string, Asset> assets;
    std::map<std::string, std::string> effects; // id → name
    std::vector<Track> video;                   // [0]: the spine; then lanes 1, 2…
    std::map<int, Track> audioLanes;            // by lane (below: -1, -2…; sound above: 100 + lane)
    std::vector<Marker> markers;

    Track& videoLane(int lane)
    {
        while (int(video.size()) <= lane)
            video.push_back(Track{.audio = false, .name = "Video " + std::to_string(video.size() + 1)});
        return video[std::size_t(lane)];
    }
    Track& audioLane(int lane)
    {
        Track& track = audioLanes[lane];
        track.audio = true;
        return track;
    }

    void noteChildren(xmlNode* node)
    {
        for (xmlNode* child : elements(node)) {
            const std::string name = reinterpret_cast<const char*>(child->name);
            if (name.rfind("adjust-", 0) == 0 || name.rfind("filter-", 0) == 0)
                notes.add("Effects, transforms and volume (left out)");
        }
    }

    // A clip's media, its source range and speed, from an asset-clip, video, audio or clip.
    bool media(xmlNode* node, Item& it)
    {
        xmlNode* source = node;
        Ticks inner = 0; // a clip's own media element starts this far into the clip
        if (is(node, "clip")) {
            source = first(node, "video");
            if (!source)
                source = first(node, "audio");
            if (!source)
                return false;
            inner = parseTime(attr(node, "start")) - parseTime(attr(source, "offset"));
        }
        const auto found = assets.find(attr(source, "ref"));
        if (found == assets.end())
            return false;
        const Asset& asset = found->second;
        it.path = asset.path;
        it.still = asset.still;
        it.hasVideo = asset.hasVideo && attr(node, "srcEnable") != "audio";
        it.hasAudio = asset.hasAudio && attr(node, "srcEnable") != "video";
        const Ticks localStart = parseTime(attr(node, "start"));
        it.sourceIn = std::max<Ticks>((is(node, "clip") ? parseTime(attr(source, "start")) + inner : localStart) - asset.start, 0);
        if (xmlNode* map = first(node, "timeMap")) {
            const auto points = elements(map);
            if (points.size() >= 2) {
                const Ticks t0 = parseTime(attr(points.front(), "time")), t1 = parseTime(attr(points.back(), "time"));
                const Ticks v0 = parseTime(attr(points.front(), "value")), v1 = parseTime(attr(points.back(), "value"));
                if (points.size() > 2)
                    notes.add("Variable speed (as its average)");
                if (t1 > t0) {
                    const double rate = double(v1 - v0) / double(t1 - t0);
                    const auto valueAt = [&](Ticks t) { return v0 + Ticks(std::llround(double(t - t0) * rate)); };
                    const Ticks a = valueAt(localStart), b = valueAt(localStart + it.duration);
                    it.freeze = v1 == v0;
                    it.reversed = rate < 0.0;
                    it.speed = it.freeze ? 1.0 : std::abs(rate);
                    it.sourceIn = std::max<Ticks>(std::min(a, b) - asset.start, 0);
                }
            }
        }
        if (first(node, "conform-rate"))
            notes.addLine("Frame rate conforming (played at the file's own rate)");
        return true;
    }

    // One story element at `time` on the timeline, on `lane` (0: the spine).
    void element(xmlNode* node, Ticks time, int lane, std::optional<Transition> transition)
    {
        Item it;
        it.start = time;
        it.duration = parseTime(attr(node, "duration"));
        it.name = attr(node, "name");
        it.transitionIn = transition;
        bool audio = false;
        bool known = true;
        if (is(node, "asset-clip") || is(node, "video") || is(node, "audio") || is(node, "clip")) {
            if (!media(node, it)) {
                notes.add("Clips whose media isn't in the file (left as space)");
                known = false;
            } else if (lane < 0) {
                // Below the spine: its sound (pictures there would be under the main track).
                if (!it.hasAudio) {
                    notes.add("Pictures below the main storyline (left out)");
                    known = false;
                }
                audio = true;
                it.hasVideo = false;
            } else {
                audio = is(node, "audio") || (!it.hasVideo && it.hasAudio);
            }
        } else if (is(node, "title")) {
            if (lane < 0) {
                notes.add("Titles below the main storyline (left out)");
                known = false;
            }
            it.kind = Item::Kind::Title;
            std::string text;
            for (xmlNode* t : elements(node))
                if (is(t, "text"))
                    text += textOf(t);
            it.text = text.empty() ? (it.name.empty() ? "Title" : it.name) : text;
        } else if (is(node, "gap")) {
            known = false;
        } else {
            notes.add(is(node, "ref-clip") ? "Compound clips (left as space)"
                    : is(node, "sync-clip") ? "Synchronized clips (left as space)"
                    : is(node, "mc-clip") ? "Multicam clips (left as space)"
                                            : "Items this app doesn't know (left as space)");
            known = false;
        }
        if (known && it.duration > 0) {
            if (audio)
                audioLane(lane < 0 ? lane : 100 + lane).items.push_back(it);
            else
                videoLane(std::max(lane, 0)).items.push_back(it);
        }
        if (known)
            noteChildren(node);
        // What hangs off it: markers and connected clips (in its own time).
        const Ticks start = parseTime(attr(node, "start"));
        for (xmlNode* child : elements(node)) {
            if (is(child, "marker") || is(child, "chapter-marker")) {
                Marker m;
                m.time = time + parseTime(attr(child, "start")) - start;
                m.duration = parseTime(attr(child, "duration"));
                if (m.duration <= frameDuration(FrameRate{24000, 1001}))
                    m.duration = 0; // a point marker (Final Cut's are a frame long)
                m.note = attr(child, "value");
                m.chapter = is(child, "chapter-marker");
                markers.push_back(m);
                continue;
            }
            const std::string childLane = attr(child, "lane");
            if (childLane.empty())
                continue;
            const int l = std::stoi(childLane);
            const Ticks at = time + parseTime(attr(child, "offset")) - start;
            if (is(child, "spine"))
                storyline(child, at, l);
            else
                element(child, at, l, std::nullopt);
        }
    }

    // A spine: its elements one after another (offsets in its own time), transitions between.
    void storyline(xmlNode* spine, Ticks origin, int lane)
    {
        if (lane != 0)
            notes.addLine("Secondary storylines (as connected clips; their transitions left out)");
        std::optional<std::pair<Ticks, Ticks>> pending; // a transition's offset and duration
        std::optional<std::string> pendingKind;
        Ticks zero = -1;
        for (xmlNode* child : elements(spine)) {
            const Ticks offset = parseTime(attr(child, "offset"));
            if (zero < 0)
                zero = offset;
            if (is(child, "transition")) {
                pending = std::make_pair(offset, parseTime(attr(child, "duration")));
                xmlNode* filter = first(child, "filter-video");
                const std::string name = filter ? effects[attr(filter, "ref")] : attr(child, "name");
                pendingKind = name == "Cross Dissolve" || name.empty() ? "dissolve" : "other";
                continue;
            }
            std::optional<Transition> transition;
            if (pending && lane == 0) {
                const Ticks before = std::clamp<Ticks>(offset - pending->first, 0, pending->second);
                transition = Transition{before, pending->second - before, *pendingKind};
            }
            pending.reset();
            element(child, origin + offset - (lane == 0 ? 0 : zero), lane, transition);
        }
    }
};

} // namespace

Timeline readFcpxml(const std::string& text, const std::string& base, Notes& notes)
{
    // No network, no external entities or DTDs: only what is in the file.
    std::unique_ptr<xmlDoc, DocDeleter> doc(xmlReadMemory(text.data(), int(text.size()), "timeline.fcpxml", nullptr,
                                                          XML_PARSE_NONET | XML_PARSE_NOERROR | XML_PARSE_NOWARNING));
    xmlNode* root = doc ? xmlDocGetRootElement(doc.get()) : nullptr;
    if (!is(root, "fcpxml"))
        throw std::runtime_error("it isn't Final Cut Pro XML");
    Reader reader{base, notes, {}, {}, {}, {}, {}};
    Timeline timeline;
    std::map<std::string, std::pair<Ticks, std::pair<int, int>>> formats; // id → frame, size
    for (xmlNode* resource : elements(first(root, "resources"))) {
        const std::string id = attr(resource, "id");
        if (is(resource, "format")) {
            formats[id] = {parseTime(attr(resource, "frameDuration")),
                           {std::atoi(attr(resource, "width").c_str()), std::atoi(attr(resource, "height").c_str())}};
        } else if (is(resource, "asset")) {
            Asset asset;
            std::string src = attr(resource, "src"); // (before 1.9)
            if (xmlNode* rep = first(resource, "media-rep"))
                src = attr(rep, "src");
            asset.path = pathFromUrl(src, base);
            asset.start = parseTime(attr(resource, "start"));
            asset.hasVideo = attr(resource, "hasVideo") == "1";
            asset.hasAudio = attr(resource, "hasAudio") == "1";
            asset.still = asset.hasVideo && !asset.hasAudio && parseTime(attr(resource, "duration")) == 0;
            reader.assets[id] = asset;
        } else if (is(resource, "effect")) {
            reader.effects[id] = attr(resource, "name");
        } else if (is(resource, "media")) {
            reader.effects[id] = attr(resource, "name"); // (a compound clip's sequence)
        }
    }
    // The first project's sequence, wherever its event is.
    xmlNode* project = nullptr;
    int projects = 0;
    std::function<void(xmlNode*)> find = [&](xmlNode* node) {
        for (xmlNode* child : elements(node)) {
            if (is(child, "project")) {
                if (!project)
                    project = child;
                ++projects;
            } else if (is(child, "library") || is(child, "event")) {
                find(child);
            }
        }
    };
    find(root);
    if (projects > 1)
        notes.addLine("The file's other projects (only the first is opened)");
    xmlNode* sequence = project ? first(project, "sequence") : nullptr;
    xmlNode* spine = sequence ? first(sequence, "spine") : nullptr;
    if (!spine)
        throw std::runtime_error("it has no project with a timeline");
    timeline.name = attr(project, "name");
    const auto format = formats.find(attr(sequence, "format"));
    if (format != formats.end()) {
        timeline.rate = rateOfFrame(format->second.first);
        timeline.width = format->second.second.first;
        timeline.height = format->second.second.second;
    } else {
        timeline.width = timeline.height = 0;
    }
    reader.videoLane(0).name = "Video 1";
    // The spine's offsets count from the sequence's start timecode.
    reader.storyline(spine, -parseTime(attr(sequence, "tcStart")), 0);
    for (Track& track : reader.video) {
        std::ranges::stable_sort(track.items, {}, &Item::start);
        timeline.tracks.push_back(std::move(track));
    }
    int n = 0;
    for (auto& [lane, track] : reader.audioLanes) {
        std::ranges::stable_sort(track.items, {}, &Item::start);
        track.name = "Audio " + std::to_string(++n);
        timeline.tracks.push_back(std::move(track));
    }
    std::ranges::sort(reader.markers, {}, &Marker::time);
    timeline.markers = std::move(reader.markers);
    return timeline;
}

} // namespace tve::interchange
