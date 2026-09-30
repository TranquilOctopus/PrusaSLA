#include "Slic3r/App/SlaIssueRows.hpp"

#include "Slic3r/Biz/I18N/I18N.hpp"

#include "fmt/format.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <string_view>
#include <utility>

namespace Slic3r::App {

namespace {

using SlaIssue = Biz::Slicing::Sla::SlaIssue;

/// The UTF-8 bytes of U+2026. It is kept out of every translatable string: the translation
/// lookup converts a narrow string through the UI locale, which drops the bytes above 0x7F, so
/// an ellipsis inside a _u8L() string never reaches the output. Spelled as bytes rather than
/// "\u2026" because this project is not compiled with /utf-8, so a "\u2026" in a narrow literal
/// would be encoded in the execution code page instead and arrive as a different character.
constexpr std::string_view ellipsis{"\xE2\x80\xA6"};

/// The UTF-8 bytes of U+00B2, the superscript two of "mm²", see ellipsis above.
constexpr std::string_view squared{"\xC2\xB2"};

/// The UTF-8 bytes of U+00B3, the superscript three of "mm³", see ellipsis above.
constexpr std::string_view cubed{"\xC2\xB3"};

std::string kind_name(SlaIssue::Kind kind)
{
    switch (kind) {
    case SlaIssue::Kind::Island:
        // TRN: Kind of an issue in the SLA sidebar issues list, a piece of a layer that touches
        // nothing below it and can fall off during printing.
        return Biz::_u8L("Island");
    case SlaIssue::Kind::Cup:
        // TRN: Kind of an issue in the SLA sidebar issues list, a hole in a printed layer.
        return Biz::_u8L("Cup");
    case SlaIssue::Kind::TrappedResin:
        // TRN: Kind of an issue in the SLA sidebar issues list, resin in a cavity of the print
        // that has no way out.
        return Biz::_u8L("Trapped resin");
    case SlaIssue::Kind::HighPeelForce:
        // TRN: Kind of an issue in the SLA sidebar issues list, a layer the peel force model
        // calls too hard to peel off the film.
        return Biz::_u8L("High peel force");
    default:
        // TRN: Kind of an issue in the SLA sidebar issues list of a kind without a name yet.
        return Biz::_u8L("Issue");
    }
}

/// Islands before cups before trapped resin before the layers that are hard to peel, so that the
/// rows of a layer read in that order.
int kind_order(SlaIssue::Kind kind)
{
    switch (kind) {
    case SlaIssue::Kind::Island:
        return 0;
    case SlaIssue::Kind::Cup:
        return 1;
    case SlaIssue::Kind::TrappedResin:
        return 2;
    case SlaIssue::Kind::HighPeelForce:
        return 3;
    default:
        return 4;
    }
}

/// How big the issue is, in whichever unit its note carried: the biggest piece of a layer is the
/// one most likely to fall off, the biggest pocket of resin the one that wastes the most and the
/// hardest layer to peel the one that can rip the print off the plate.
std::optional<double> size_of(const SlaIssueRow& row)
{
    if (row.area_mm2) {
        return row.area_mm2;
    }
    if (row.volume_mm3) {
        return row.volume_mm3;
    }
    return row.peel_force_n;
}

/// Parse the half open range [@p begin, @p end) of @p note as a number. Empty when it is not one.
std::optional<double> parse_number(const std::string& note, size_t begin, size_t end)
{
    double value      = 0.;
    const auto parsed = std::from_chars(note.data() + begin, note.data() + end, value);
    if (parsed.ec != std::errc{} || parsed.ptr != note.data() + end) {
        return std::nullopt;
    }
    return value;
}

/// Whether @p c can be part of the decimal number a note carries.
bool part_of_number(char c)
{
    return (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '+';
}

/// The number in front of @p unit in @p note, e.g. 21.0 in "layers 3-5, 21.00 mm3" for "mm3".
/// Empty when there is none, so a note that only names a layer is not read as a size.
std::optional<double> number_before_unit(const std::string& note, std::string_view unit)
{
    const size_t unit_at = note.rfind(unit);
    if (unit_at == std::string::npos || unit_at == 0) {
        return std::nullopt;
    }

    // The slicer separates the number from the unit with a space, so the scan starts past it.
    size_t begin = unit_at;
    while (begin > 0 && (note[begin - 1] == ' ' || note[begin - 1] == '\t')) {
        --begin;
    }

    // From there the number ends, and it reaches back over the digits, the dot and the sign in
    // front of them.
    const size_t number_end = begin;
    while (begin > 0 && part_of_number(note[begin - 1])) {
        --begin;
    }
    if (begin == number_end) {
        return std::nullopt;
    }

    return parse_number(note, begin, number_end);
}

/// The number that starts at @p begin in @p note and runs until the first character that cannot be
/// part of it. Empty when there is none there, so a word is never read as a size.
std::optional<double> number_after(const std::string& note, size_t begin)
{
    size_t end = begin;
    while (end < note.size() && part_of_number(note[end])) {
        ++end;
    }
    if (end == begin) {
        return std::nullopt;
    }

    return parse_number(note, begin, end);
}

} // namespace

std::optional<double> sla_issue_area_mm2(const std::string& note)
{
    size_t begin = note.find_first_of("0123456789");
    if (begin == std::string::npos)
        return std::nullopt;

    size_t end = begin;
    while (end < note.size()
           && (std::isdigit(static_cast<unsigned char>(note[end])) || note[end] == '.'))
        ++end;
    if (end == begin)
        return std::nullopt;

    double area       = 0.;
    const auto parsed = std::from_chars(note.data() + begin, note.data() + end, area);
    if (parsed.ec != std::errc{} || parsed.ptr != note.data() + end)
        return std::nullopt;

    return area;
}

std::optional<double> sla_issue_volume_mm3(const std::string& note)
{
    return number_before_unit(note, "mm3");
}

std::optional<double> sla_issue_peel_force_n(const std::string& note)
{
    // The note of a high peel layer is "peel force 12.30 N over 8.00 N": the force is the number
    // behind the name, the limit behind it is not read. Found by its prefix rather than by its
    // unit, because the unit is in front of both numbers.
    const size_t number_at = note.find("peel force ");
    if (number_at == std::string::npos) {
        return std::nullopt;
    }
    return number_after(note, number_at + std::string_view("peel force ").size());
}

std::string SlaIssueRows::title() const
{
    // TRN: Header of the issues list in the SLA sidebar. {0} counts every issue the slicer found.
    return fmt::format(fmt::runtime(Biz::_u8L("Issues ({0})")), total_count);
}

std::string SlaIssueRows::more_text() const
{
    if (hidden_count == 0) {
        return {};
    }
    // TRN: Last line of the capped issues list in the SLA sidebar. {0} counts the issues that are
    // not listed. The ellipsis is not translated, see ellipsis above.
    return fmt::format(fmt::runtime(Biz::_u8L("and {0} more")), hidden_count)
        + std::string(ellipsis);
}

SlaIssueRows build_sla_issue_rows(const std::vector<SlaIssue>& issues, size_t max_rows)
{
    SlaIssueRows out;
    for (const SlaIssue& issue : issues) {
        // Islands, the two kinds of cavity and the layers the peel force model calls high are
        // listed. The other kinds have no meaning in the sidebar yet, but they still count, so
        // the header does not lie.
        if (issue.kind != SlaIssue::Kind::Island
            && issue.kind != SlaIssue::Kind::Cup
            && issue.kind != SlaIssue::Kind::TrappedResin
            && issue.kind != SlaIssue::Kind::HighPeelForce)
        {
            continue;
        }
        if (issue.kind == SlaIssue::Kind::Island) {
            out.island_count++;
        } else if (issue.kind == SlaIssue::Kind::Cup) {
            out.cup_count++;
        } else if (issue.kind == SlaIssue::Kind::TrappedResin) {
            out.trapped_resin_count++;
        } else {
            out.high_peel_force_count++;
        }

        SlaIssueRow row;
        row.kind        = issue.kind;
        row.layer       = issue.layer;
        row.position    = issue.position;
        row.object_name = issue.object_name;
        // Every kind reports its size in its own unit and reads it out of a note written for it:
        // a pocket of trapped resin has no area and its note starts with the layer range, and the
        // note of a high peel layer names the force and then the limit it was compared against.
        if (issue.kind == SlaIssue::Kind::TrappedResin) {
            row.volume_mm3 = sla_issue_volume_mm3(issue.note);
        } else if (issue.kind == SlaIssue::Kind::HighPeelForce) {
            row.peel_force_n = sla_issue_peel_force_n(issue.note);
        } else {
            row.area_mm2 = sla_issue_area_mm2(issue.note);
        }
        out.rows.push_back(std::move(row));
    }
    out.total_count = out.rows.size();

    std::sort(
        out.rows.begin(),
        out.rows.end(),
        [](const SlaIssueRow& a, const SlaIssueRow& b)
        {
            if (a.layer != b.layer)
                return a.layer < b.layer;
            if (a.kind != b.kind)
                return kind_order(a.kind) < kind_order(b.kind);
            const std::optional<double> size_a = size_of(a);
            const std::optional<double> size_b = size_of(b);
            if (size_a.has_value() != size_b.has_value()) {
                // An issue of a known size is the more useful one, so it comes first.
                return size_a.has_value();
            }
            if (size_a && size_b) {
                // The biggest piece of a layer is the one most likely to fall off.
                return *size_a > *size_b;
            }
            return false;
        }
    );

    if (out.rows.size() > max_rows) {
        out.rows.resize(max_rows);
        out.hidden_count = out.total_count - out.rows.size();
    }

    return out;
}

std::string sla_issue_row_text(const SlaIssueRow& row)
{
    std::string text;
    if (row.object_name.empty()) {
        // TRN: One row of the issues list in the SLA sidebar. {0} is the kind of the issue and {1}
        // the number of the layer it was found on, counted from one as the user counts layers.
        text = fmt::
            format(fmt::runtime(Biz::_u8L("{0}, layer {1}")), kind_name(row.kind), row.layer + 1);
    } else {
        // TRN: One row of the issues list in the SLA sidebar, for an issue the slicer could put on
        // a model. {0} is the kind of the issue, {1} the name of the model it was found on and
        // {2} the number of the layer, counted from one as the user counts layers.
        text = fmt::format(
            fmt::runtime(Biz::_u8L("{0} on {1}, layer {2}")),
            kind_name(row.kind),
            row.object_name,
            row.layer + 1
        );
    }
    if (row.area_mm2) {
        // The unit is spelled outside the translatable part, for the same reason the ellipsis is,
        // see ellipsis above.
        text += fmt::format("  {0:.1f} mm", *row.area_mm2) + std::string(squared);
    } else if (row.volume_mm3) {
        text += fmt::format("  {0:.1f} mm", *row.volume_mm3) + std::string(cubed);
    } else if (row.peel_force_n) {
        // TRN: The peel force of a layer in the issues list of the SLA sidebar, the force the
        // model calls high. {0} is the force in newtons, one decimal.
        text += fmt::format(fmt::runtime(Biz::_u8L("  {0:.1f} N")), *row.peel_force_n);
    }
    return text;
}

} // namespace Slic3r::App
