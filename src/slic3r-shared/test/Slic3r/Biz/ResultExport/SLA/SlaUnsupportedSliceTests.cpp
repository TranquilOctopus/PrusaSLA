#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "Slic3r/Biz/SlaFixture.hpp"
#include "Slic3r/Domain/ConfigDefsSLA.hpp"
#include "Slic3r/Domain/Model.hpp"
#include "Slic3r/Domain/SLA/SupportPoint.hpp"

using Catch::Approx;

// Slicing a model without supports is part of the planned workflow: an unsupported model is
// sliced and the user is warned, not refused. With the pad around the object, the pad and
// support slicing steps run although the support tree step left no mesh; they used to
// dereference that null mesh (SIGSEGV).
//
// A raft *under* the model (raft Full) has nothing to hold, because the model without supports
// stands on the plate: such a model gets no raft at all instead of failing the whole bed with
// NoPadGenerated (M2.17j). The case below is the same for every raft type then, so the whole
// set is generated.
TEST_CASE("SLA slicing with supports disabled", "[slicing][sla][supports]")
{
    using Slic3r::Domain::sla::RaftType;
    const RaftType raft = GENERATE(RaftType::AroundObject, RaftType::None, RaftType::Full);
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

// The same with supports off and a raft *under* the model (raft Full, the default), which is
// the case the todo names: such a raft has nothing to hold, because the model without supports
// stands on the plate, so the model gets no raft at all and the bed slices. The first layer is
// the model's own 20 x 20 mm footprint, so neither a raft under it nor a raft around it was
// printed: both would put their own area into that layer (the first under it, the second around
// it), and a raft under it would lift the model off the plate as well.
TEST_CASE("SLA slicing an unsupported model with a raft under it", "[slicing][sla][supports]")
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
    config.sla_print_settings.items.opt("supports_enable").set(false);
    config.sla_print_settings.items.opt("pad_enable").set(true);
    config.sla_print_settings.items.opt("raft_type").set(RaftType::Full);

    auto sla_result = fixture.slice_sla_model(model, config);
    REQUIRE(sla_result != nullptr);          // no NoPadGenerated
    REQUIRE_FALSE(sla_result->layer_areas.empty());

    // The model stands on the plate, so the print is as tall as the model itself: about 400
    // layers at 0.05 mm, with no raft below it and no support elevation above it.
    CHECK(sla_result->files.data.size() >= 390);
    CHECK(sla_result->files.data.size() < 450);

    // The first layer is the 20 x 20 mm bottom face of the model and nothing else.
    CHECK(sla_result->layer_areas[0] == Approx(400.f).margin(20.f));
}

// One model on the bed cannot take the others down with it: a bed with a supported and an
// unsupported model under a raft that can hold it slices both, the supported one lifted by its
// supports (about 500 layers at 0.05 mm) and the unsupported one standing on the plate without
// a raft of its own.
TEST_CASE("SLA slicing a bed with a supported and an unsupported model", "[slicing][sla][supports]")
{
    using Slic3r::Domain::sla::RaftType;
    using Slic3r::Domain::SLA::SupportPoint;
    using Slic3r::Domain::SLA::SupportPointType;
    using Slic3r::Domain::Vec3f;

    Slic3r::Test::SlaSlicingFixture fixture;
    auto model = Slic3r::Test::generate_cubes(2, 2);

    // Four support points on the bottom face (z = 0) of the first cube only. The second one
    // stays unsupported, so it is the one that used to fail the bed.
    Slic3r::Domain::ModelObject* obj = model.objects.front();
    obj->sla_support_points.emplace_back(SupportPoint{Vec3f{5.0f, 5.0f, 0.0f}, 0.2f, SupportPointType::island});
    obj->sla_support_points.emplace_back(SupportPoint{Vec3f{15.0f, 5.0f, 0.0f}, 0.2f, SupportPointType::island});
    obj->sla_support_points.emplace_back(SupportPoint{Vec3f{5.0f, 15.0f, 0.0f}, 0.2f, SupportPointType::island});
    obj->sla_support_points.emplace_back(SupportPoint{Vec3f{15.0f, 15.0f, 0.0f}, 0.2f, SupportPointType::island});

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
    REQUIRE(sla_result != nullptr);
    REQUIRE_FALSE(sla_result->layer_areas.empty());

    // The bed is as tall as the supported model with its 5 mm of elevation: the unsupported one
    // prints on the plate, it does not lift the whole bed or fail it.
    CHECK(sla_result->files.data.size() > 450);
    // The unsupported cube's own 20 x 20 mm bottom face (400 mm2) is in the first layer, so it
    // stands on the plate: a raft under it would have lifted it away from z = 0 and put the raft
    // under the four pillars in that layer alone.
    CHECK(sla_result->layer_areas[0] >= 400.f);
}

// A model with support points placed on it (type island, not manual_add) gets those points used
// during slicing, the model is lifted by support elevation, and slicing succeeds with more layers.
TEST_CASE("SLA slicing uses every support point on the model", "[slicing][sla][supports]")
{
    using Slic3r::Domain::sla::RaftType;
    using Slic3r::Domain::SLA::SupportPoint;
    using Slic3r::Domain::SLA::SupportPointType;
    using Slic3r::Domain::Vec3f;

    Slic3r::Test::SlaSlicingFixture fixture;
    auto model = Slic3r::Test::generate_cubes(1, 1);
    // Give the cube four support points on its bottom face (z = 0), inside the 20 x 20 mm face.
    Slic3r::Domain::ModelObject* obj = model.objects.front();
    obj->sla_support_points.clear();
    obj->sla_support_points.emplace_back(SupportPoint{Vec3f{5.0f, 5.0f, 0.0f}, 0.2f, SupportPointType::island});
    obj->sla_support_points.emplace_back(SupportPoint{Vec3f{15.0f, 5.0f, 0.0f}, 0.2f, SupportPointType::island});
    obj->sla_support_points.emplace_back(SupportPoint{Vec3f{5.0f, 15.0f, 0.0f}, 0.2f, SupportPointType::island});
    obj->sla_support_points.emplace_back(SupportPoint{Vec3f{15.0f, 15.0f, 0.0f}, 0.2f, SupportPointType::island});

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
    REQUIRE(sla_result != nullptr);
    // The cube is lifted by support elevation, so there are more layers than the unsupported cube (~400).
    REQUIRE(sla_result->files.data.size() > 450);
}

// Slicing an unsupported model with supports enabled and raft Full twice in a row
// (simulating a support generation run followed by a normal slice) must succeed both times.
// This tests the fix for M2.17g where the first slice would lift the object (generation mode)
// and the second slice would reuse the lifted slices without supports under them.
TEST_CASE("SLA slicing after a support generation run", "[slicing][sla][supports]")
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

    // First slice (simulates support generation run with m_generate_support_points_for set)
    // The fixture doesn't expose a way to set m_generate_support_points_for directly,
    // so we test the simpler invariant: slice twice with the same config.
    // If the fixture cannot slice twice, the second call will fail and we'll know.
    auto sla_result1 = fixture.slice_sla_model(model, config);
    REQUIRE(sla_result1 != nullptr);

    // Second slice (normal slice, no generation mode)
    auto sla_result2 = fixture.slice_sla_model(model, config);
    REQUIRE(sla_result2 != nullptr);

    // Both results should be valid (non-null) and have similar layer counts
    CHECK(sla_result1->files.data.size() >= 390);
    CHECK(sla_result1->files.data.size() < 450);
    CHECK(sla_result2->files.data.size() >= 390);
    CHECK(sla_result2->files.data.size() < 450);
}
