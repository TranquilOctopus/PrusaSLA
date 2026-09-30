#include "Slic3r/App/SlaObjectUseRows.hpp"

#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Biz/ResinEconomics.hpp"
#include "Slic3r/Domain/SLA/PrintStatistics.hpp"

#include "fmt/format.h"

#include <string_view>

namespace Slic3r::App {

namespace {

/// The UTF-8 bytes of U+2014, the dash the SLA sidebar shows where a value is not available: the
/// same one the plate figures of M1.11a carry before a print has been sliced. Spelled as bytes
/// rather than "—" because this project is not compiled with /utf-8, so the encoding of a non
/// ASCII character in a narrow literal depends on the code page of the compiler, and this string is
/// compared by the tests.
constexpr std::string_view no_value{"\xE2\x80\x94"};

/// The resin and the money of @p volume_mm3 as the resin preset of @p config prices them: the same
/// ResinEconomics the plate figure goes through, so a row cannot disagree with it.
SlaObjectUseRow priced(double volume_mm3, const Domain::ConfigView& config)
{
    SlaObjectUseRow row;
    row.volume_mm3 = volume_mm3;

    Domain::SLA::PrintStatistics stats;
    stats.objects_used_material               = volume_mm3;
    const Biz::ResinEconomicsResult economics = Biz::ResinEconomics::calculate(stats, config);
    row.millilitres                           = economics.millilitres;
    row.cost                                  = economics.cost;

    return row;
}

} // namespace

std::string SlaObjectUseRows::title() const
{
    // TRN: Header of the per model table in the SLA sidebar. {0} counts the models on the plate.
    return fmt::format(fmt::runtime(Biz::_u8L("Per model ({0})")), rows.size());
}

SlaObjectUseRows build_sla_object_use_rows(
    const std::vector<SLA::ObjectResinUse>& use,
    const Domain::ConfigView& config
)
{
    SlaObjectUseRows out;
    out.rows.reserve(use.size());

    for (const SLA::ObjectResinUse& model : use) {
        SlaObjectUseRow row = priced(model.volume_mm3(), config);
        row.object_id       = model.object_id;
        row.object_name     = model.name;
        out.volume_mm3 += row.volume_mm3;
        if (row.millilitres) {
            out.millilitres = out.millilitres.value_or(0.) + *row.millilitres;
        }
        if (row.cost) {
            out.cost = out.cost.value_or(0.) + *row.cost;
        }
        out.rows.emplace_back(std::move(row));
    }

    return out;
}

SlaObjectUseRows unsliced_sla_object_use_rows(const std::vector<std::string>& model_names)
{
    SlaObjectUseRows out;
    out.rows.reserve(model_names.size());
    for (const std::string& name : model_names) {
        SlaObjectUseRow row;
        row.object_name = name;
        out.rows.emplace_back(std::move(row));
    }
    return out;
}

std::string sla_object_use_value_text(const SlaObjectUseRow& row)
{
    // No value, no figures: the plate has not been sliced, or the model has no resin of its own on
    // it. The dash is what the plate figures carry in the same state.
    if (!row.millilitres) {
        return std::string(no_value);
    }

    // The units are spelled outside the translatable part, like the " ml" of the plate figures,
    // because the translation lookup converts a narrow string through the UI locale and drops the
    // bytes above 0x7F.
    std::string text = fmt::format("{0:.1f} ml", *row.millilitres);
    if (row.cost) {
        text += fmt::format(", {0:.2f}", *row.cost);
    }
    return text;
}

std::string sla_object_use_row_text(const SlaObjectUseRow& row)
{
    if (row.object_name.empty()) {
        return sla_object_use_value_text(row);
    }
    return row.object_name + "  " + sla_object_use_value_text(row);
}

} // namespace Slic3r::App
