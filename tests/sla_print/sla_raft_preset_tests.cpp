#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "Slic3r/Domain/SLA/RaftPreset.hpp"
#include "Slic3r/Domain/ConfigDefsSLA.hpp"

using namespace Slic3r;
using namespace Slic3r::Domain;
using namespace Slic3r::Domain::SLA;
using Catch::Approx;

TEST_CASE("RaftPreset: None disables pad", "[SLA][RaftPreset]")
{
    RaftPadValues vals = raft_preset_to_pad_values(
        sla::RaftType::None,
        2.0,   // wall_height_mm
        2.0,   // wall_thickness_mm
        1.6,   // expansion_mm
        90.0,  // slope_deg
        1.0,   // object_gap_mm
        0.0,   // edge_taper_mm
        {},    // infill
        0.0,   // floor_thickness_mm
    );

    CHECK(vals.pad_enable == false);
    CHECK(vals.pad_around_object == false);
    // Other values should still be set from inputs
    CHECK(vals.pad_wall_height_mm == Approx(2.0));
    CHECK(vals.pad_wall_thickness_mm == Approx(2.0));
    CHECK(vals.pad_brim_size_mm == Approx(1.6));
    CHECK(vals.pad_wall_slope_deg == Approx(90.0));
    CHECK(vals.pad_object_gap_mm == Approx(1.0));
}

TEST_CASE("RaftPreset: Full enables plate-wide pad", "[SLA][RaftPreset]")
{
    RaftPadValues vals = raft_preset_to_pad_values(
        sla::RaftType::Full,
        2.0,   // wall_height_mm
        2.0,   // wall_thickness_mm
        1.6,   // expansion_mm
        90.0,  // slope_deg
        1.0,   // object_gap_mm
        0.0,   // edge_taper_mm
        {},    // infill
        0.0,   // floor_thickness_mm
    );

    CHECK(vals.pad_enable == true);
    CHECK(vals.pad_around_object == false);
    CHECK(vals.pad_wall_height_mm == Approx(2.0));
    CHECK(vals.pad_wall_thickness_mm == Approx(2.0));
    CHECK(vals.pad_brim_size_mm == Approx(1.6));
    CHECK(vals.pad_wall_slope_deg == Approx(90.0));
    CHECK(vals.pad_object_gap_mm == Approx(1.0));
}

TEST_CASE("RaftPreset: AroundObject sets pad_around_object flag", "[SLA][RaftPreset]")
{
    RaftPadValues vals = raft_preset_to_pad_values(
        sla::RaftType::AroundObject,
        2.0,   // wall_height_mm
        2.0,   // wall_thickness_mm
        1.6,   // expansion_mm
        90.0,  // slope_deg
        1.0,   // object_gap_mm
        0.0,   // edge_taper_mm
        {},    // infill
        0.0,   // floor_thickness_mm
    );

    CHECK(vals.pad_enable == true);
    CHECK(vals.pad_around_object == true);
    CHECK(vals.pad_wall_height_mm == Approx(2.0));
    CHECK(vals.pad_wall_thickness_mm == Approx(2.0));
    CHECK(vals.pad_brim_size_mm == Approx(1.6));
    CHECK(vals.pad_wall_slope_deg == Approx(90.0));
    CHECK(vals.pad_object_gap_mm == Approx(1.0));
}

TEST_CASE("RaftPreset: Skate differs from AroundObject in brim and slope", "[SLA][RaftPreset]")
{
    RaftPadValues vals_around = raft_preset_to_pad_values(
        sla::RaftType::AroundObject,
        2.0,   // wall_height_mm
        2.0,   // wall_thickness_mm
        1.6,   // expansion_mm
        90.0,  // slope_deg
        1.0,   // object_gap_mm
        0.0,   // edge_taper_mm
        {},    // infill
        0.0,   // floor_thickness_mm
    );

    RaftPadValues vals_skate = raft_preset_to_pad_values(
        sla::RaftType::Skate,
        2.0,   // wall_height_mm
        2.0,   // wall_thickness_mm
        1.6,   // expansion_mm
        90.0,  // slope_deg
        1.0,   // object_gap_mm
        0.0,   // edge_taper_mm
        {},    // infill
        0.0,   // floor_thickness_mm
    );

    CHECK(vals_skate.pad_enable == true);
    CHECK(vals_skate.pad_around_object == true);
    CHECK(vals_skate.pad_wall_height_mm == Approx(2.0));
    CHECK(vals_skate.pad_wall_thickness_mm == Approx(2.0));
    // Skate has half the brim
    CHECK(vals_skate.pad_brim_size_mm == Approx(1.6 * 0.5));
    // Skate has steeper slope (70 deg)
    CHECK(vals_skate.pad_wall_slope_deg == Approx(70.0));
    CHECK(vals_skate.pad_object_gap_mm == Approx(1.0));

    // Verify they differ as expected
    CHECK(vals_skate.pad_brim_size_mm != vals_around.pad_brim_size_mm);
    CHECK(vals_skate.pad_wall_slope_deg != vals_around.pad_wall_slope_deg);
}

TEST_CASE("RaftPreset: Shared knobs pass through unchanged for Full", "[SLA][RaftPreset]")
{
    RaftPadValues vals = raft_preset_to_pad_values(
        sla::RaftType::Full,
        3.5,   // wall_height_mm
        2.5,   // wall_thickness_mm
        2.0,   // expansion_mm
        80.0,  // slope_deg
        1.5,   // object_gap_mm
        0.0,   // edge_taper_mm
        {},    // infill
        0.0,   // floor_thickness_mm
    );

    CHECK(vals.pad_wall_height_mm == Approx(3.5));
    CHECK(vals.pad_wall_thickness_mm == Approx(2.5));
    CHECK(vals.pad_brim_size_mm == Approx(2.0));
    CHECK(vals.pad_wall_slope_deg == Approx(80.0));
    CHECK(vals.pad_object_gap_mm == Approx(1.5));
}

TEST_CASE("RaftPreset: Shared knobs pass through unchanged for AroundObject", "[SLA][RaftPreset]")
{
    RaftPadValues vals = raft_preset_to_pad_values(
        sla::RaftType::AroundObject,
        3.5,   // wall_height_mm
        2.5,   // wall_thickness_mm
        2.0,   // expansion_mm
        80.0,  // slope_deg
        1.5,   // object_gap_mm
        0.0,   // edge_taper_mm
        {},    // infill
        0.0,   // floor_thickness_mm
    );

    CHECK(vals.pad_wall_height_mm == Approx(3.5));
    CHECK(vals.pad_wall_thickness_mm == Approx(2.5));
    CHECK(vals.pad_brim_size_mm == Approx(2.0));
    CHECK(vals.pad_wall_slope_deg == Approx(80.0));
    CHECK(vals.pad_object_gap_mm == Approx(1.5));
}

TEST_CASE("RaftPreset: Shared knobs pass through for Skate (except brim/slope)", "[SLA][RaftPreset]")
{
    RaftPadValues vals = raft_preset_to_pad_values(
        sla::RaftType::Skate,
        3.5,   // wall_height_mm
        2.5,   // wall_thickness_mm
        2.0,   // expansion_mm
        80.0,  // slope_deg
        1.5,   // object_gap_mm
        0.0,   // edge_taper_mm
        {},    // infill
        0.0,   // floor_thickness_mm
    );

    CHECK(vals.pad_wall_height_mm == Approx(3.5));
    CHECK(vals.pad_wall_thickness_mm == Approx(2.5));
    // Brim is halved
    CHECK(vals.pad_brim_size_mm == Approx(2.0 * 0.5));
    // Slope is fixed to 70
    CHECK(vals.pad_wall_slope_deg == Approx(70.0));
    CHECK(vals.pad_object_gap_mm == Approx(1.5));
}

TEST_CASE("RaftPreset: the edge taper is a shared knob, no type replaces it", "[SLA][RaftPreset]")
{
    // The taper is a bevel on the top edge of the raft, so every printed raft reads it. How deep
    // it should be for a skate is a tuning question, so no type decides it on its own.
    for (const sla::RaftType type : {sla::RaftType::None,
                                     sla::RaftType::Full,
                                     sla::RaftType::AroundObject,
                                     sla::RaftType::Skate}) {
        INFO("raft type " << static_cast<int>(type));
        RaftPadValues vals = raft_preset_to_pad_values(
            type,
            0.0,   // wall_height_mm
            2.0,   // wall_thickness_mm
            1.6,   // expansion_mm
            90.0,  // slope_deg
            1.0,   // object_gap_mm
            1.2,   // edge_taper_mm
            {},    // infill
            0.0,   // floor_thickness_mm
        );

        CHECK(vals.raft_edge_taper_mm == Approx(1.2));
    }
}

TEST_CASE("RaftPreset: the infill is a shared knob, no type replaces it", "[SLA][RaftPreset]")
{
    // Which pattern goes into the raft is a tuning question against Lychee and Chitubox, like the
    // skate expansion and slope, so no raft type brings its own.
    RaftInfill grid;
    grid.type = sla::RaftInfillType::Grid;
    grid.spacing_mm = 3.;
    grid.wall_mm = 0.6;
    grid.skin_mm = 0.8;

    for (const sla::RaftType type : {sla::RaftType::None,
                                     sla::RaftType::Full,
                                     sla::RaftType::AroundObject,
                                     sla::RaftType::Skate}) {
        INFO("raft type " << static_cast<int>(type));
        RaftPadValues vals = raft_preset_to_pad_values(
            type,
            0.0,   // wall_height_mm
            2.0,   // wall_thickness_mm
            1.6,   // expansion_mm
            90.0,  // slope_deg
            1.0,   // object_gap_mm
            0.0,   // edge_taper_mm
            grid,  // infill
            0.0    // floor_thickness_mm
        );

        CHECK(vals.raft_infill.type == sla::RaftInfillType::Grid);
        CHECK(vals.raft_infill.spacing_mm == Approx(3.0));
        CHECK(vals.raft_infill.wall_mm == Approx(0.6));
        CHECK(vals.raft_infill.skin_mm == Approx(0.8));
    }
}

TEST_CASE("RaftPreset: no infill is the solid raft of today", "[SLA][RaftPreset]")
{
    // Nothing asked for a pattern, so there is none: the raft stays the solid slab the
    // generator has always built.
    RaftPadValues vals = raft_preset_to_pad_values(
        sla::RaftType::Full,
        0.0,   // wall_height_mm
        2.0,   // wall_thickness_mm
        1.6,   // expansion_mm
        90.0,  // slope_deg
        1.0,   // object_gap_mm
        0.0,   // edge_taper_mm
        {},    // infill
        0.0     // floor_thickness_mm
    );

    CHECK(vals.raft_infill.type == sla::RaftInfillType::None);
}

TEST_CASE("RaftPreset: the floor thickness is a shared knob, no type replaces it", "[SLA][RaftPreset]")
{
    // The floor is the slab on the build plate the raft stands on. How thick it should be for a
    // skate is a tuning question against Lychee and Chitubox, so no raft type decides it, and zero
    // keeps it as thick as the wall.
    for (const sla::RaftType type : {sla::RaftType::None,
                                     sla::RaftType::Full,
                                     sla::RaftType::AroundObject,
                                     sla::RaftType::Skate}) {
        INFO("raft type " << static_cast<int>(type));
        RaftPadValues vals = raft_preset_to_pad_values(
            type,
            0.0,   // wall_height_mm
            2.0,   // wall_thickness_mm
            1.6,   // expansion_mm
            90.0,  // slope_deg
            1.0,   // object_gap_mm
            0.0,   // edge_taper_mm
            {},    // infill
            0.8    // floor_thickness_mm
        );

        CHECK(vals.raft_floor_thickness_mm == Approx(0.8));
        // The wall thickness is untouched: the floor is a knob of its own.
        CHECK(vals.pad_wall_thickness_mm == Approx(2.0));
    }
}

TEST_CASE("RaftPreset: no floor thickness is a floor as thick as the wall", "[SLA][RaftPreset]")
{
    RaftPadValues vals = raft_preset_to_pad_values(
        sla::RaftType::Full,
        1.0,   // wall_height_mm
        2.0,   // wall_thickness_mm
        1.6,   // expansion_mm
        90.0,  // slope_deg
        1.0,   // object_gap_mm
        0.0,   // edge_taper_mm
        {},    // infill
        0.0    // floor_thickness_mm
    );

    CHECK(vals.raft_floor_thickness_mm == Approx(0.));
}
