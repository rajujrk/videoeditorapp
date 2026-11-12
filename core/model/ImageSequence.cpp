#include "model/ImageSequence.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <map>
#include <mutex>

namespace tve {

namespace fs = std::filesystem;

namespace {

// A file name split around the last run of digits in its stem: "shot_" "0012" ".png".
struct NumberedName {
    std::string prefix;
    std::string digits;
    std::string suffix;
};

std::optional<NumberedName> splitNumber(const std::string& name)
{
    const std::size_t dot = name.find_last_of('.');
    const std::size_t stemEnd = dot == std::string::npos || dot == 0 ? name.size() : dot;
    std::size_t end = stemEnd;
    while (end > 0 && !std::isdigit(static_cast<unsigned char>(name[end - 1])))
        --end;
    if (end == 0)
        return std::nullopt;
    std::size_t start = end;
    while (start > 0 && std::isdigit(static_cast<unsigned char>(name[start - 1])))
        --start;
    // A number of more than 12 digits is not a frame number (a date and time, an id…).
    if (end - start > 12)
        return std::nullopt;
    return NumberedName{name.substr(0, start), name.substr(start, end - start), name.substr(end)};
}

// "%" in a file name would be read as a field: written as "%%".
std::string escapePercent(const std::string& text)
{
    std::string out;
    for (char c : text) {
        out += c;
        if (c == '%')
            out += '%';
    }
    return out;
}

std::string unescapePercent(const std::string& text)
{
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        out += text[i];
        if (text[i] == '%' && i + 1 < text.size() && text[i + 1] == '%')
            ++i;
    }
    return out;
}

// The pattern's parts: what comes before the field, its width (0: not padded) and after.
struct PatternParts {
    std::string prefix;
    int width = 0;
    std::string suffix;
};

std::optional<PatternParts> splitPattern(const std::string& pattern)
{
    for (std::size_t i = 0; i < pattern.size(); ++i) {
        if (pattern[i] != '%')
            continue;
        if (i + 1 < pattern.size() && pattern[i + 1] == '%') {
            ++i;
            continue;
        }
        std::size_t j = i + 1;
        int width = 0;
        if (j < pattern.size() && pattern[j] == '0') {
            ++j;
            while (j < pattern.size() && std::isdigit(static_cast<unsigned char>(pattern[j])))
                width = width * 10 + (pattern[j++] - '0');
        }
        if (j >= pattern.size() || pattern[j] != 'd')
            return std::nullopt;
        return PatternParts{unescapePercent(pattern.substr(0, i)), width, unescapePercent(pattern.substr(j + 1))};
    }
    return std::nullopt;
}

std::mutex& registryMutex()
{
    static std::mutex mutex;
    return mutex;
}

std::map<std::string, RegisteredSequence>& registry()
{
    static std::map<std::string, RegisteredSequence> sequences;
    return sequences;
}

} // namespace

std::optional<ImageSequence> findImageSequence(const std::string& framePath)
{
    const fs::path path(framePath);
    const auto chosen = splitNumber(path.filename().string());
    if (!chosen)
        return std::nullopt;
    std::error_code error;
    std::vector<std::string> candidates; // the numbers of the files named like it
    for (const fs::directory_entry& entry : fs::directory_iterator(path.parent_path(), error)) {
        if (!entry.is_regular_file(error))
            continue;
        const auto other = splitNumber(entry.path().filename().string());
        if (other && other->prefix == chosen->prefix && other->suffix == chosen->suffix)
            candidates.push_back(other->digits);
    }
    // Padded names ("0012") are all as wide; unpadded ones ("12") are as wide as the number.
    // The chosen one says which, or (as wide as any padded one, like "1000") the others do.
    const auto leadingZero = [](const std::string& digits) { return digits.size() > 1 && digits.front() == '0'; };
    const bool padded = leadingZero(chosen->digits)
                        || std::any_of(candidates.begin(), candidates.end(), [&](const std::string& digits) {
                               return leadingZero(digits) && digits.size() == chosen->digits.size();
                           });
    std::vector<std::int64_t> numbers;
    for (const std::string& digits : candidates)
        if (padded ? digits.size() == chosen->digits.size() : !leadingZero(digits))
            numbers.push_back(std::stoll(digits));
    if (numbers.size() < 2)
        return std::nullopt;
    std::sort(numbers.begin(), numbers.end());
    ImageSequence sequence;
    sequence.pattern = escapePercent(chosen->prefix)
                       + (padded ? "%0" + std::to_string(chosen->digits.size()) + "d" : std::string("%d"))
                       + escapePercent(chosen->suffix);
    sequence.first = numbers.front();
    sequence.last = numbers.back();
    return sequence;
}

std::string sequenceFrameFile(const std::string& folder, const ImageSequence& sequence, std::int64_t number)
{
    const auto parts = splitPattern(sequence.pattern);
    if (!parts)
        return {};
    std::string digits = std::to_string(number);
    if (parts->width > 0 && digits.size() < std::size_t(parts->width))
        digits.insert(0, std::size_t(parts->width) - digits.size(), '0');
    return (fs::path(folder) / (parts->prefix + digits + parts->suffix)).string();
}

std::vector<std::string> sequenceFrameFiles(const std::string& firstFramePath, const ImageSequence& sequence)
{
    const std::string folder = fs::path(firstFramePath).parent_path().string();
    std::vector<std::string> files;
    files.reserve(std::size_t(std::max<std::int64_t>(sequence.length(), 0)));
    std::size_t firstThere = std::string::npos;
    for (std::int64_t n = sequence.first; n <= sequence.last; ++n) {
        std::string file = sequenceFrameFile(folder, sequence, n);
        std::error_code error;
        if (fs::is_regular_file(file, error)) {
            if (firstThere == std::string::npos)
                firstThere = files.size();
            files.push_back(std::move(file));
        } else {
            files.push_back(files.empty() ? std::string() : files.back());
        }
    }
    if (firstThere == std::string::npos)
        return {};
    for (std::size_t i = 0; i < firstThere; ++i)
        files[i] = files[firstThere];
    return files;
}

std::string sequenceDisplayName(const ImageSequence& sequence)
{
    const auto parts = splitPattern(sequence.pattern);
    if (!parts)
        return sequence.pattern;
    return parts->prefix + std::string(std::size_t(std::max(parts->width, 1)), '#') + parts->suffix;
}

void registerImageSequence(const std::string& firstFramePath, const ImageSequence& sequence, FrameRate rate)
{
    std::lock_guard lock(registryMutex());
    registry()[firstFramePath] = RegisteredSequence{sequence, rate};
}

std::optional<RegisteredSequence> registeredImageSequence(const std::string& path)
{
    std::lock_guard lock(registryMutex());
    const auto found = registry().find(path);
    if (found == registry().end())
        return std::nullopt;
    return found->second;
}

} // namespace tve
