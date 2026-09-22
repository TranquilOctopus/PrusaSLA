#include "Slic3r/Biz/ResinProfile/ChituboxCfgReader.hpp"

#include "Slic3r/Biz/ResinProfile/ForeignResinProfile.hpp"
#include <boost/filesystem/fstream.hpp>
#include <boost/filesystem/operations.hpp>
#include <algorithm>
#include <cctype>
#include <string>
#include <tl/expected.hpp>
#include <vector>

namespace Slic3r::Biz::ResinProfile {

namespace {

// A reader must not depend on the registry that dispatches to it, so the limit lives here.
// ResinProfileReaderRegistry applies the same ceiling before it ever picks a reader.
constexpr std::size_t MAX_CFG_FILE_SIZE = 8 * 1024 * 1024;

/// Trim whitespace from both ends of a string.
static std::string trim(const std::string& s)
{
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) {
        ++start;
    }
    size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) {
        --end;
    }
    return s.substr(start, end - start);
}

/// Remove UTF-8 BOM if present at the start of the string.
static std::string remove_bom(std::string s)
{
    if (s.size() >= 3 && static_cast<unsigned char>(s[0]) == 0xEF &&
        static_cast<unsigned char>(s[1]) == 0xBB && static_cast<unsigned char>(s[2]) == 0xBF) {
        s.erase(0, 3);
    }
    return s;
}

/// Normalize line endings: replace CRLF with LF.
static std::string normalize_line_endings(std::string s)
{
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\r') {
            if (i + 1 < s.size() && s[i + 1] == '\n') {
                // CRLF -> skip CR, LF will be added in next iteration
                continue;
            }
            // Lone CR -> treat as LF
            out.push_back('\n');
        } else {
            out.push_back(s[i]);
        }
    }
    return out;
}

/// Check if a line is a key:value pair (not empty, not comment).
static bool is_key_value_line(const std::string& line)
{
    std::string t = trim(line);
    if (t.empty()) {
        return false;
    }
    if (t[0] == '#' || t[0] == ';') {
        return false;
    }
    return t.find(':') != std::string::npos;
}

/// Split a "key:value" line into key and value at the FIRST colon.
/// Value may contain colons (e.g. G-code blocks).
static std::pair<std::string, std::string> split_key_value(const std::string& line)
{
    size_t colon_pos = line.find(':');
    if (colon_pos == std::string::npos) {
        return {trim(line), ""};
    }
    std::string key = trim(line.substr(0, colon_pos));
    std::string value = trim(line.substr(colon_pos + 1));
    return {key, value};
}

/// Heuristic: does the value look like a number? Used for warnings only.
static bool looks_like_number(const std::string& s)
{
    std::string t = trim(s);
    if (t.empty()) {
        return false;
    }
    size_t i = 0;
    if (t[i] == '+' || t[i] == '-') {
        ++i;
    }
    bool has_digit = false;
    while (i < t.size() && std::isdigit(static_cast<unsigned char>(t[i]))) {
        has_digit = true;
        ++i;
    }
    if (i < t.size() && t[i] == '.') {
        ++i;
        while (i < t.size() && std::isdigit(static_cast<unsigned char>(t[i]))) {
            has_digit = true;
            ++i;
        }
    }
    return has_digit && i == t.size();
}

/// Known Chitubox keys that are expected to be numeric.
static bool is_known_numeric_key(const std::string& key)
{
    static const std::vector<std::string> numeric_keys = {
        "normalExposureTime",
        "layerHeight",
        "bottomLayerHeight",
        "exposureTime",
        "bottomExposureTime",
        "lightPWM",
        "bottomLightPWM",
        "liftDistance",
        "bottomLiftDistance",
        "liftSpeed",
        "bottomLiftSpeed",
        "retractSpeed",
        "bottomRetractSpeed",
        "restTimeAfterLift",
        "restTimeAfterRetract",
        "bottomLayers",
        "transitionLayers",
        "antiAliasingLevel",
        "blurLevel",
        "contrast",
        "saturation",
        "gamma",
        "machineWidth",
        "machineHeight",
        "machineDepth",
        "resolutionX",
        "resolutionY",
        "pixelSize",
        "offTime",
        "bottomOffTime"
    };
    return std::find(numeric_keys.begin(), numeric_keys.end(), key) != numeric_keys.end();
}

} // anonymous namespace

bool ChituboxCfgReader::sniff(const std::string& head) const
{
    // Look for a distinctive Chitubox key in the first 4 KB
    // normalExposureTime is the most characteristic key.
    // The BOM has to be stripped here too: with it still attached the first key reads as
    // "\xEF\xBB\xBFnormalExposureTime", so a valid file is rejected as unrecognised and read()
    // (which does strip it) never runs.
    std::string normalized = normalize_line_endings(remove_bom(head));
    std::string::size_type pos = 0;
    while (pos < normalized.size()) {
        std::string::size_type nl = normalized.find('\n', pos);
        std::string line = normalized.substr(pos, nl - pos);
        if (is_key_value_line(line)) {
            auto [key, value] = split_key_value(line);
            if (key == "normalExposureTime" || key == "layerHeight" || key == "bottomExposureTime") {
                return true;
            }
        }
        if (nl == std::string::npos) {
            break;
        }
        pos = nl + 1;
    }
    return false;
}

tl::expected<ForeignResinProfile, std::string> ChituboxCfgReader::read(const boost::filesystem::path& path) const
{
    ForeignResinProfile profile;
    profile.source_format = format_id();
    profile.source_path = path.string();

    // Read entire file
    boost::system::error_code ec;
    const auto file_size = boost::filesystem::file_size(path, ec);
    if (ec) {
        return tl::make_unexpected(std::string("Cannot get file size: ") + ec.message());
    }
    if (file_size > static_cast<boost::uintmax_t>(MAX_CFG_FILE_SIZE)) {
        return tl::make_unexpected("File too large (max 8 MB)");
    }

    boost::filesystem::ifstream file(path, std::ios::binary);
    if (!file) {
        return tl::make_unexpected("Failed to open file");
    }

    std::string content;
    content.resize(static_cast<size_t>(file_size));
    file.read(content.data(), static_cast<std::streamsize>(file_size));
    if (!file) {
        return tl::make_unexpected("Failed to read file content");
    }

    // Handle BOM and normalize line endings
    content = remove_bom(content);
    content = normalize_line_endings(content);

    // Parse line by line
    std::string::size_type pos = 0;
    size_t line_number = 0;
    while (pos < content.size()) {
        std::string::size_type nl = content.find('\n', pos);
        std::string line = content.substr(pos, nl - pos);
        ++line_number;
        const size_t start_line = line_number;

        if (is_key_value_line(line)) {
            auto [key, value] = split_key_value(line);

            // A quoted value may span several lines: Chitubox stores G-code blocks that way.
            // Without this the value is truncated at the first newline and every continuation
            // line is then reported as an unrecognised line.
            if (!value.empty() && value.front() == '"' &&
                !(value.size() >= 2 && value.back() == '"')) {
                bool closed = false;
                while (nl != std::string::npos) {
                    const std::string::size_type next = content.find('
', nl + 1);
                    const std::string continuation = next == std::string::npos
                        ? content.substr(nl + 1)
                        : content.substr(nl + 1, next - (nl + 1));
                    ++line_number;
                    value += '
';
                    value += continuation;
                    nl = next;
                    const std::string trimmed = trim(continuation);
                    if (!trimmed.empty() && trimmed.back() == '"') {
                        closed = true;
                        break;
                    }
                }
                if (!closed) {
                    profile.warnings.push_back(
                        "Unterminated quoted value for key '" + key + "' starting on line " +
                        std::to_string(start_line) + "; took the rest of the file");
                }
            }

            // Check for duplicate keys (keep both spellings)
            if (profile.raw_values.find(key) != profile.raw_values.end()) {
                profile.warnings.push_back("Duplicate key on line " + std::to_string(line_number) + ": " + key);
            }

            // Store raw value
            profile.raw_values[key] = value;

            // Heuristic: try to extract printer hint from known keys
            if (!profile.printer_hint.has_value()) {
                if (key == "machineName" || key == "printerName" || key == "name") {
                    profile.printer_hint = value;
                }
            }

            // Warn if a known numeric key has a non-numeric value
            if (is_known_numeric_key(key) && !looks_like_number(value)) {
                profile.warnings.push_back("Key '" + key + "' on line " + std::to_string(line_number) +
                                           " has non-numeric value: " + value);
            }
        } else if (!trim(line).empty() && trim(line)[0] != '#' && trim(line)[0] != ';') {
            // Non-empty, non-comment line that isn't key:value
            profile.warnings.push_back("Unrecognized line format on line " + std::to_string(line_number) + ": " + trim(line));
        }

        if (nl == std::string::npos) {
            break;
        }
        pos = nl + 1;
    }

    return profile;
}

} // namespace Slic3r::Biz::ResinProfile