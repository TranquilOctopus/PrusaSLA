#pragma once

#include <optional>
#include <string>
#include <vector>

#include "Slic3r/Domain/Config.hpp"
#include "Slic3r/Domain/ObjectID.hpp"
#include "libslic3r/SLA/ObjectResinUse.hpp"

namespace Slic3r::App {

/// One model of the plate with the resin it cures: a row of the per model table of the SLA sidebar
/// summary, and the second line (as a tooltip) of a model in the objects list (M1.11d).
struct SlaObjectUseRow
{
    Domain::ObjectID object_id{};
    std::string object_name;
    /// What the model cures in mm³: its body, its support tree and its share of the raft.
    double volume_mm3 = 0.;
    /// The same volume in the units the resin preset is priced in, empty where there is no value
    /// to show (a plate that has not been sliced yet), which is the en dash case.
    std::optional<double> millilitres;
    std::optional<double> cost;
};

/// The per model resin use of a build plate, ready to be listed: one row per model, in plate order.
struct SlaObjectUseRows
{
    std::vector<SlaObjectUseRow> rows;
    /// What the models add up to. It is the volume of the print the plate figures show, because the
    /// raft shares are split so that they add up to the raft.
    double volume_mm3 = 0.;
    std::optional<double> millilitres;
    std::optional<double> cost;

    bool empty() const
    {
        return rows.empty();
    }

    /// The table is only worth the room it takes when more than one model is on the plate: with a
    /// single model its row would say what the resin figure above it already says.
    bool show() const
    {
        return rows.size() > 1;
    }

    /// The header of the table, naming how many models it holds.
    std::string title() const;
};

/**
 * @brief Turn the per model resin use of a slice result into the rows of the table.
 *
 * The volumes come from the slicer, which splits them the way SLA::object_resin_use() documents,
 * so the rows add up to the plate figures. The millilitres and the cost are the ones the resin
 * preset prices: the same ResinEconomics the plate figure uses, so a row can never disagree with
 * the figure it is a breakdown of. The cost carries no currency symbol, because no config key says
 * which currency a resin is sold in (bottle_cost is a plain money amount), and the plate cost is
 * shown the same way.
 */
SlaObjectUseRows build_sla_object_use_rows(
    const std::vector<SLA::ObjectResinUse>& use,
    const Domain::ConfigView& config
);

/**
 * @brief The rows of a plate that has not been sliced yet.
 *
 * Every model of the bed gets a row and none of them has a value, so the table reads as the en
 * dash it is before there is a print to break down. The names come from the bed, so the list is
 * the same before and after the slice.
 */
SlaObjectUseRows unsliced_sla_object_use_rows(const std::vector<std::string>& model_names);

/// The text of one row: the name of the model, then its figures.
std::string sla_object_use_row_text(const SlaObjectUseRow& row);

/// The figures of one row alone, e.g. "12.4 ml, 0.62", or the en dash where there is no value.
std::string sla_object_use_value_text(const SlaObjectUseRow& row);

} // namespace Slic3r::App
