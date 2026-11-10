// A CMX 3600 EDL (.edl): the main video track (with its clips' sound) as events timed from
// 01:00:00:00, each with its file in comments ("FROM CLIP NAME", "SOURCE FILE") as editors
// that relink by them expect; dissolves as D events, speed as M2 lines, markers as LOC lines.
#include "io/Interchange.h"
#include "model/Translate.h"

#include <cmath>
#include <cstdio>
#include <sstream>

namespace tve::interchange {

namespace {

// EDLs count whole frames at the nearest whole rate (29.97 as 30, non-drop).
int timebase(FrameRate rate)
{
    return std::max(1, int(std::lround(double(rate.num) / double(rate.den))));
}

std::string timecode(std::int64_t frames, int fps)
{
    frames = std::max<std::int64_t>(frames, 0);
    char text[16];
    std::snprintf(text, sizeof text, "%02lld:%02lld:%02lld:%02lld", (long long)(frames / (3600LL * fps)),
                  (long long)(frames / (60LL * fps) % 60), (long long)(frames / fps % 60), (long long)(frames % fps));
    return text;
}

const char* colourName(MarkerColor color)
{
    switch (color) {
    case MarkerColor::Red: return "RED";
    case MarkerColor::Orange: return "ORANGE";
    case MarkerColor::Yellow: return "YELLOW";
    case MarkerColor::Green: return "GREEN";
    case MarkerColor::Blue: return "BLUE";
    case MarkerColor::Purple: return "MAGENTA";
    }
    return "BLUE";
}

std::string clean(const std::string& text)
{
    std::string out;
    for (char c : text)
        out += c == '\n' || c == '\r' ? ' ' : c;
    return out;
}

} // namespace

std::string writeEdl(const Timeline& timeline, Notes& notes)
{
    const int fps = timebase(timeline.rate);
    const std::int64_t record = 3600LL * fps; // 01:00:00:00
    const auto f = [&](Ticks t) { return framesOf(t, timeline.rate); };
    const auto tc = [&](std::int64_t frames) { return timecode(frames, fps); };

    const Track* main = nullptr;
    for (const Track& track : timeline.tracks) {
        if (!track.audio && !main)
            main = &track;
        else if (!track.items.empty())
            notes.addLine(TVE_TR("core", "Tracks other than the main video track (an EDL holds one)"));
    }
    std::ostringstream edl;
    edl << "TITLE: " << clean(timeline.name) << "\nFCM: NON-DROP FRAME\n";
    if (timeline.rate.den != 1)
        notes.addLine("Frame counts at " + std::to_string(fps) + " fps, non-drop (the EDL's own timecode)");
    int event = 0;
    const Item* previous = nullptr;
    // Source time shown at a timeline time inside an item.
    const auto sourceAt = [](const Item& it, Ticks time) {
        if (it.freeze)
            return it.sourceIn;
        const Ticks into = Ticks(std::llround(double(time - it.start) * it.speed));
        return it.reversed ? it.sourceIn + it.sourceSpan() - into : it.sourceIn + into;
    };
    for (const Item* it = main && !main->items.empty() ? &main->items.front() : nullptr; it && it <= &main->items.back(); ++it) {
        if (it->kind != Item::Kind::Media) {
            notes.add(TVE_TR("core", "Titles (an EDL holds media only)"));
            previous = nullptr;
            continue;
        }
        const std::string kind = it->hasAudio && !it->still ? "AA/V" : "V   ";
        char number[8];
        std::snprintf(number, sizeof number, "%03d", ++event);
        const auto line = [&](const std::string& transition, Ticks srcIn, Ticks srcOut, Ticks recIn, Ticks recOut) {
            char text[160];
            std::snprintf(text, sizeof text, "%s  AX       %s  %-8s %s %s %s %s\n", number, kind.c_str(), transition.c_str(),
                          tc(f(srcIn)).c_str(), tc(f(srcOut)).c_str(), tc(record + f(recIn)).c_str(),
                          tc(record + f(recOut)).c_str());
            edl << "\n" << text;
        };
        const Ticks recIn = it->start;
        // Up to where the next clip's dissolve begins (the dissolve event plays both).
        Ticks recOut = it->start + it->duration;
        if (const Item* next = it + 1; next <= &main->items.back() && next->kind == Item::Kind::Media && next->transitionIn
                                       && next->start == recOut)
            recOut -= next->transitionIn->before;
        const bool dissolve = it->transitionIn && previous && previous->start + previous->duration == it->start;
        Ticks eventSourceIn = 0; // (for its M2 line)
        if (dissolve) {
            // From the outgoing clip where the dissolve starts, into this one.
            const Transition& t = *it->transitionIn;
            const Ticks begin = it->start - t.before;
            const Ticks outgoing = sourceAt(*previous, begin);
            edl << "\n" << number << "  AX       " << (previous->hasAudio ? "AA/V" : "V   ") << "  C        "
                << tc(f(outgoing)) << " " << tc(f(outgoing)) << " " << tc(record + f(begin)) << " " << tc(record + f(begin))
                << "\n";
            char frames[16];
            std::snprintf(frames, sizeof frames, "D    %03lld", (long long)f(t.before + t.after));
            char text[160];
            const Ticks srcIn = it->reversed ? sourceAt(*it, it->start) + Ticks(std::llround(double(t.before) * it->speed))
                                             : it->sourceIn - Ticks(std::llround(double(t.before) * (it->freeze ? 0.0 : it->speed)));
            eventSourceIn = std::max<Ticks>(srcIn, 0);
            std::snprintf(text, sizeof text, "%s  AX       %s  %-8s %s %s %s %s\n", number, kind.c_str(), frames,
                          tc(f(std::max<Ticks>(srcIn, 0))).c_str(), tc(f(sourceAt(*it, recOut))).c_str(),
                          tc(record + f(begin)).c_str(), tc(record + f(recOut)).c_str());
            edl << text;
        } else {
            // The source range forward (reversed or not: M2 says which way); a freeze frame's
            // as long as it is held.
            const Ticks srcIn = it->reversed ? sourceAt(*it, recOut) : sourceAt(*it, recIn);
            const Ticks srcOut = srcIn + (it->freeze ? recOut - recIn : Ticks(std::llround(double(recOut - recIn) * it->speed)));
            line("C", srcIn, srcOut, recIn, recOut);
            eventSourceIn = srcIn;
        }
        if (it->freeze || it->reversed || it->speed != 1.0) {
            char text[96];
            const double speedFps = it->freeze ? 0.0 : (it->reversed ? -1.0 : 1.0) * it->speed * fps;
            std::snprintf(text, sizeof text, "M2   AX       %05.1f                %s\n", speedFps, tc(f(eventSourceIn)).c_str());
            edl << text;
        }
        edl << "* FROM CLIP NAME: " << clean(it->name) << "\n* SOURCE FILE: " << clean(it->path) << "\n";
        previous = it;
    }
    if (event > 0)
        notes.addLine(TVE_TR("core", "Source timecodes count from each file's start"));
    for (const Marker& m : timeline.markers)
        edl << "\n* LOC: " << tc(record + f(m.time)) << " " << colourName(m.color) << " " << clean(m.note) << "\n";
    // Transitions on clips after a title or gap have no outgoing clip in the EDL.
    for (const Item& it : main ? main->items : std::vector<Item>{})
        if (it.transitionIn && &it != &main->items.front() && (&it - 1)->kind != Item::Kind::Media)
            notes.add(TVE_TR("core", "Transitions from titles (left out of the EDL)"));
    return edl.str();
}

} // namespace tve::interchange
