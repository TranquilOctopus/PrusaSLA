#include "Slic3r/App/SlaIssueRows.hpp"

#include "Slic3r/Biz/I18N/I18N.hpp"

#include "fmt/format.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <string_view>

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
    default:
        // TRN: Kind of an issue in the SLA sidebar issues list of a kind without a name yet.
        return Biz::_u8L("Issue");
    }
}

/// Islands before cups, so that the rows of a layer read in that order.
int kind_order(SlaIssue::Kind kind)
{
    switch (kind) {
    case SlaIssue::Kind::Island:
        return 0;
    case SlaIssue::Kind::Cup:
        return 1;
    default:
        return 2;
    }
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
        // Only islands and cups are listed for now. The other kinds (trapped resin) have no
        // meaning in the sidebar yet, but they still count, so the header does not lie.
        if (issue.kind != SlaIssue::Kind::Island && issue.kind != SlaIssue::Kind::Cup) {
            continue;
        }
        if (issue.kind == SlaIssue::Kind::Island) {
            out.island_count++;
        } else {
            out.cup_count++;
        }
        out.rows.push_back(
            SlaIssueRow{issue.kind, issue.layer, sla_issue_area_mm2(issue.note), issue.object_name}
        );
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
            if (a.area_mm2.has_value() != b.area_mm2.has_value()) {
                // An issue of a known area is the more useful one, so it comes first.
                return a.area_mm2.has_value();
            }
            if (a.area_mm2 && b.area_mm2) {
                // The biggest piece of a layer is the one most likely to fall off.
                return *a.area_mm2 > *b.area_mm2;
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
    }
    return text;
}

} // namespace Slic3r::App
