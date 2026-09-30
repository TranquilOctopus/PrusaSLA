#pragma once

#include <optional>
#include <string>
#include <vector>

#include "libslic3r/SLAResult.hpp"

namespace Slic3r::App {

/// One issue of a sliced build plate, as a row of the sidebar issues list (M1.11b, W4).
struct SlaIssueRow
{
    Biz::Slicing::Sla::SlaIssue::Kind kind = Biz::Slicing::Sla::SlaIssue::Kind::Other;
    size_t layer{0}; //< 0 based layer index, the way the slicer reports it
    std::optional<double> area_mm2;
    std::string object_name; //< the model the issue was found on, empty when unknown
};

/// The issues of a sliced build plate, ready to be listed: sorted, capped, and counted.
struct SlaIssueRows
{
    /// At most max_rows entries, see build_sla_issue_rows().
    std::vector<SlaIssueRow> rows;
    size_t total_count{0}; //< every issue found, the ones left out of rows included
    size_t island_count{0};
    size_t cup_count{0};
    size_t hidden_count{0}; //< total_count - rows.size()

    bool empty() const
    {
        return total_count == 0;
    }

    /// The header of the list, naming how many issues were found.
    std::string title() const;

    /// The line under the rows when the list is capped, empty when nothing was left out.
    std::string more_text() const;
};

/// How many rows the list shows before it only counts the rest.
inline constexpr size_t sla_issue_rows_default_max = 20;

/**
 * @brief Turn the issues of a slice result into the rows of the sidebar issues list.
 *
 * Islands and cups are listed, the other kinds are counted out: nothing else has a place in
 * the list yet (M4.8b/M4.8d only report islands). The rows are ordered by layer, then by kind
 * (islands before cups) and by descending area, so the issue most likely to fall off comes
 * first. At most @p max_rows rows are kept and the rest is counted into hidden_count.
 */
SlaIssueRows build_sla_issue_rows(
    const std::vector<Biz::Slicing::Sla::SlaIssue>& issues,
    size_t max_rows = sla_issue_rows_default_max
);

/// The text of one row, localized, with the layer number the way the user counts layers. The
/// model an issue was found on is named in front of the layer, when the slicer could tell.
std::string sla_issue_row_text(const SlaIssueRow& row);

/**
 * @brief The area in mm² the slicer wrote into the note of an issue.
 *
 * The note reads e.g. "island, 4.20 mm2". Empty when it carries no number, in which case the
 * row is shown without an area.
 */
std::optional<double> sla_issue_area_mm2(const std::string& note);

} // namespace Slic3r::App
