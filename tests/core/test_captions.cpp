#include "commands/CommandStack.h"
#include "commands/StructureCommands.h"
#include "io/ProjectSerializer.h"
#include "model/Captions.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <sstream>

using namespace tve;

namespace {

constexpr Ticks kMs = kTicksPerSecond / 1000;

// Words one after another, each `length` ms, `gap` ms apart.
std::vector<TimedWord> spoken(const std::string& sentence, Ticks from = 0, int length = 300, int gap = 50)
{
    std::vector<TimedWord> words;
    std::istringstream in(sentence);
    std::string word;
    Ticks at = from;
    while (in >> word) {
        words.push_back({at, at + length * kMs, word});
        at += (length + gap) * kMs;
    }
    return words;
}

} // namespace

TEST_CASE("words become captions at pauses, sentence ends and length limits", "[captions]")
{
    // A pause of a second splits; so does a sentence end once a caption is half full.
    auto words = spoken("The quick brown fox jumps over the lazy dog.");
    auto later = spoken("We edit our videos on the timeline every day.", words.back().end + 1500 * kMs);
    words.insert(words.end(), later.begin(), later.end());
    const auto captions = layoutCaptions(words);
    REQUIRE(captions.size() == 2);
    CHECK(captions[0].text == "The quick brown fox\njumps over the lazy dog.");
    CHECK(captions[0].words.size() == 9);
    CHECK(captions[0].start == 0);
    CHECK(captions[0].end == words[8].end);
    CHECK(captions[1].words.front().text == "We");

    // Long speech without pauses splits by length: lines of at most 42 characters, two lines.
    std::string run;
    for (int i = 0; i < 30; ++i)
        run += "word" + std::to_string(i) + " ";
    const auto many = layoutCaptions(spoken(run, 0, 200, 20));
    REQUIRE(many.size() > 1);
    for (const CaptionBlock& c : many) {
        INFO(c.text);
        CHECK(std::ranges::count(c.text, '\n') <= 1);
        std::istringstream lines(c.text);
        for (std::string line; std::getline(lines, line);)
            CHECK(line.size() <= 42);
        CHECK(c.end - c.start <= 6 * kTicksPerSecond);
    }
    // Two lines are balanced: the break is near the middle.
    const auto two = layoutCaptions(spoken("one two three four five six seven eight nine ten eleven twelve"));
    REQUIRE(two.size() == 1);
    CHECK(two[0].text == "one two three four five six seven\neight nine ten eleven twelve"); // 33 / 28

    // A lone short word stays up at least a second.
    const auto lone = layoutCaptions({{0, 200 * kMs, "Hi."}});
    CHECK(lone[0].end == kTicksPerSecond);
}

TEST_CASE("SRT and WebVTT round trip, and foreign files read", "[captions]")
{
    const std::vector<CaptionBlock> captions{{0, 1500 * kMs, "Hello there.", {}},
                                             {Ticks(3723) * kTicksPerSecond + 45 * kMs, Ticks(3725) * kTicksPerSecond, "Two\nlines", {}}};
    const std::string srt = writeSrt(captions);
    CHECK(srt == "1\n00:00:00,000 --> 00:00:01,500\nHello there.\n\n2\n01:02:03,045 --> 01:02:05,000\nTwo\nlines\n\n");
    const std::string vtt = writeVtt(captions);
    CHECK(vtt.starts_with("WEBVTT\n\n00:00:00.000 --> 00:00:01.500\nHello there.\n"));
    CHECK(readSubtitles(srt) == captions);
    CHECK(readSubtitles(vtt) == captions);

    // Windows line ends, a byte-order mark, cue settings, identifiers, styling tags, short times.
    const std::string foreign = "\xEF\xBB\xBFWEBVTT\r\nKind: captions\r\n\r\nintro\r\n00:01.000 --> 00:02.500 align:start position:10%\r\n"
                                "<v Roger>Hello <i>there</i></v>\r\n\r\nNOTE a comment\r\n\r\n00:03.000 --> 00:04.000\r\n<c.yellow>Bye</c>\r\n";
    std::string error;
    const auto read = readSubtitles(foreign, &error);
    INFO(error);
    REQUIRE(read);
    REQUIRE(read->size() == 2);
    CHECK((*read)[0] == CaptionBlock{kTicksPerSecond, 2500 * kMs, "Hello there", {}});
    CHECK((*read)[1].text == "Bye");
    CHECK_FALSE(readSubtitles("just some text\nwith no times", &error));
    CHECK(error == "no captions found in the file");
}

TEST_CASE("captions go on a caption track, replacing what it held", "[captions]")
{
    Project project;
    CommandStack stack(project);
    auto words = spoken("Hello world.");
    REQUIRE_FALSE(stack.push(std::make_unique<SetCaptionsCommand>(layoutCaptions(words), "Generate Captions")));
    REQUIRE(project.tracks.size() == 1);
    const Track& track = project.tracks[0];
    CHECK(track.captions);
    CHECK(track.kind == TrackKind::Title);
    CHECK(track.name == "Captions");
    REQUIRE(track.clips.size() == 1);
    const Clip& clip = track.clips[0];
    REQUIRE(clip.text);
    CHECK(clip.text->text == "Hello world.");
    CHECK(clip.text->words.size() == 2);
    CHECK(clip.text->words[1].start == 350 * kMs); // from the clip's start
    CHECK(clip.text->shape.kind == TitleShapeKind::Rounded);
    CHECK(clip.visual.transform.y > 0.3);

    // Again: the same track, new contents; overlaps are trimmed.
    REQUIRE_FALSE(stack.push(std::make_unique<SetCaptionsCommand>(
        std::vector<CaptionBlock>{{0, 2 * kTicksPerSecond, "A", {}}, {kTicksPerSecond, 3 * kTicksPerSecond, "B", {}}})));
    REQUIRE(project.tracks.size() == 1);
    REQUIRE(project.tracks[0].clips.size() == 2);
    CHECK(project.tracks[0].clips[0].duration == kTicksPerSecond);
    const Project after = project;
    stack.undo();
    CHECK(project.tracks[0].clips[0].text->text == "Hello world.");
    stack.redo();
    CHECK(project == after);

    // Saved and read back with the words and the flag.
    const Project loaded = projectFromJson(projectToJson(project));
    CHECK(loaded == project);
    stack.undo();
    stack.undo();
    CHECK(project.tracks.empty());
}
