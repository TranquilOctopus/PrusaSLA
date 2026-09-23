#include <catch2/catch_test_macros.hpp>

#include "Slic3r/Biz/Preset/IO/PresetSaver.hpp"
#include "Slic3r/Domain/Preset/EvaluatedPreset.hpp"

using Slic3r::Domain::Preset::EvaluatedMaterialPreset;
using Slic3r::Domain::Preset::PresetKind;
using Slic3r::Domain::SLAMaterialSettings;

namespace {
EvaluatedMaterialPreset::Preset make_resin(std::vector<std::string> conditions)
{
    EvaluatedMaterialPreset::Preset preset;
    preset.kind = PresetKind::SlaMaterial;
    preset.root_id = "community_sla_material_generic";
    preset.id = "community_sla_material_generic";
    preset.name = "Generic Resin";
    preset.values = SLAMaterialSettings{};
    preset.conditions = std::move(conditions);
    return preset;
}
} // namespace

// Saving an edited resin crashed the app: the community SLA resins have no condition, and the
// saver asserted that every preset has at least one.
TEST_CASE("Saving a preset without a condition keeps it unconditional", "[preset][saver]")
{
    const auto saved = Slic3r::Biz::Preset::IO::transform_for_saving(make_resin({}), static_cast<const EvaluatedMaterialPreset::Preset*>(nullptr), {});
    REQUIRE(saved.variants.size() == 1);
    CHECK_FALSE(saved.variants[0].condition.has_value());
}

TEST_CASE("Saving a preset with conditions keeps them", "[preset][saver]")
{
    const auto saved = Slic3r::Biz::Preset::IO::transform_for_saving(
        make_resin({"printer.model == \"Photon Mono M5\""}), static_cast<const EvaluatedMaterialPreset::Preset*>(nullptr), {}
    );
    REQUIRE(saved.variants.size() == 1);
    REQUIRE(saved.variants[0].condition.has_value());
    CHECK(saved.variants[0].condition->expr_str.find("Photon Mono M5") != std::string::npos);
}
