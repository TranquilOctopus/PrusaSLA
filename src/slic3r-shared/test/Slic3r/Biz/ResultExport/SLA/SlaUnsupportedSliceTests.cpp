#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "Slic3r/Biz/SlaFixture.hpp"
#include "Slic3r/Domain/ConfigDefsSLA.hpp"

// Slicing a model without supports is part of the planned workflow: an unsupported model is
// sliced and the user is warned, not refused. With the pad around the object, the pad and
// support slicing steps run although the support tree step left no mesh; they used to
// dereference that null mesh (SIGSEGV).
//
// Supports off with a pad *under* the object (raft Full) still fails, with NoPadGenerated as in
// upstream: there is nothing for that pad to hold. M2.17a has to decide what an unsupported
// model gets in that case.
TEST_CASE("SLA slicing with supports disabled", "[slicing][sla][supports]")
{
    using Slic3r::Domain::sla::RaftType;
    const RaftType raft = GENERATE(RaftType::AroundObject, RaftType::None);
    const bool pad_enable = raft != RaftType::None;
    CAPTURE(pad_enable);

    Slic3r::Test::SlaSlicingFixture fixture;
    auto model = Slic3r::Test::generate_cubes(1, 1);

    auto config = Slic3r::Domain::ConfigPackSLA{};
    config.sla_printer_settings.items.opt("sla_archive_format").set(std::string("goo"));
    config.sla_printer_settings.items.opt("display_pixels_x").set(1440);
    config.sla_printer_settings.items.opt("display_pixels_y").set(800);
    config.sla_printer_settings.items.opt("display_width").set(144.0);
    config.sla_printer_settings.items.opt("display_height").set(80.0);
    config.sla_print_settings.items.opt("layer_height").set(0.05);
    config.sla_material_settings.items.opt("initial_layer_height").set(0.05);
    config.sla_print_settings.items.opt("supports_enable").set(false);
    config.sla_print_settings.items.opt("pad_enable").set(pad_enable);
    config.sla_print_settings.items.opt("raft_type").set(raft);

    auto sla_result = fixture.slice_sla_model(model, config);
    REQUIRE(sla_result != nullptr);
    // The 20 mm cube sits on the plate either way (a pad around the object does not lift it),
    // so there are about 400 layers at 0.05 mm.
    REQUIRE(sla_result->files.data.size() >= 390);
}

// Slicing must never generate support points on its own. The generator runs ONLY when the
// slice was requested with SliceUntilStep{slaposSupportPoints, <that object's id}> (the
// support tool's "Generate" button path). Every other slice uses the model's own points as
// they are. A model with no points gets no support points (an empty list), not generated ones.
// With this change, an unsupported cube sits on the plate (no elevation), so layer 10 is inside the cube.
TEST_CASE("SLA slicing never generates support points", "[slicing][sla][supports]")
{
    using Slic3r::Domain::sla::RaftType;

    Slic3r::Test::SlaSlicingFixture fixture;
    auto model = Slic3r::Test::generate_cubes(1, 1);
    // Model has no support points (sla_support_points is empty).

    auto config = Slic3r::Domain::ConfigPackSLA{};
    config.sla_printer_settings.items.opt("sla_archive_format").set(std::string("goo"));
    config.sla_printer_settings.items.opt("display_pixels_x").set(1440);
    config.sla_printer_settings.items.opt("display_pixels_y").set(800);
    config.sla_printer_settings.items.opt("display_width").set(144.0);
    config.sla_printer_settings.items.opt("display_height").set(80.0);
    config.sla_print_settings.items.opt("layer_height").set(0.05);
    config.sla_material_settings.items.opt("initial_layer_height").set(0.05);
    config.sla_print_settings.items.opt("supports_enable").set(true);
    config.sla_print_settings.items.opt("pad_enable").set(false);
    config.sla_print_settings.items.opt("raft_type").set(RaftType::None);

    auto sla_result = fixture.slice_sla_model(model, config);
    REQUIRE(sla_result != nullptr);
    REQUIRE(sla_result->files.data.size() > 20);
    // Unsupported cube now sits on the plate: no elevation gap, no pillars.
    CHECK(sla_result->files.data.size() < 450);
}

// An object without supports sits on the build plate:
// - elevation 0;
// - no raft under it, unless the raft is "around the object" (zero elevation mode);
// - slicing succeeds (no NoPadGenerated for such an object).
TEST_CASE("SLA slicing an unsupported model with the default raft", "[slicing][sla][supports]")
{
    using Slic3r::Domain::sla::RaftType;

    Slic3r::Test::SlaSlicingFixture fixture;
    auto model = Slic3r::Test::generate_cubes(1, 1);
    // Model has no support points (sla_support_points is empty).

    auto config = Slic3r::Domain::ConfigPackSLA{};
    config.sla_printer_settings.items.opt("sla_archive_format").set(std::string("goo"));
    config.sla_printer_settings.items.opt("display_pixels_x").set(1440);
    config.sla_printer_settings.items.opt("display_pixels_y").set(800);
    config.sla_printer_settings.items.opt("display_width").set(144.0);
    config.sla_printer_settings.items.opt("display_height").set(80.0);
    config.sla_print_settings.items.opt("layer_height").set(0.05);
    config.sla_material_settings.items.opt("initial_layer_height").set(0.05);
    config.sla_print_settings.items.opt("supports_enable").set(true);
    config.sla_print_settings.items.opt("pad_enable").set(true);
    config.sla_print_settings.items.opt("raft_type").set(RaftType::Full);

    auto sla_result = fixture.slice_sla_model(model, config);
    REQUIRE(sla_result != nullptr);          // no NoPadGenerated
    // The 20 mm cube now sits on the plate: about 400 layers at 0.05 mm, not 400 + the
    // 5 mm support elevation (about 500).
    CHECK(sla_result->files.data.size() < 450);
    CHECK(sla_result->files.data.size() >= 390);
}
