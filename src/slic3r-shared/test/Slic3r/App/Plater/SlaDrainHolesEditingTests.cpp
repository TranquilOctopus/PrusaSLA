#include <catch2/catch_test_macros.hpp>

#include "Slic3r/App/Plater/SlaDrainHolesEditing.hpp"
#include "Slic3r/Domain/SLA/DrainHole.hpp"
#include "Slic3r/Domain/Types.hpp"

#include <unordered_set>

using Slic3r::Domain::SLA::DrainHole;
using Slic3r::Domain::SLA::DrainHoles;
using Slic3r::Domain::Vec3d;
using Slic3r::Domain::Vec3f;
using Slic3r::App::Plater::SlaDrainHolesEditing;

TEST_CASE("SlaDrainHolesEditing - pure editing logic", "[SlaDrainHolesEditing][editing]")
{
    SECTION("Add hole creates hole with correct radius and height")
    {
        SlaDrainHolesEditing editor;
        editor.hole_radius_mm = 3.0;
        editor.hole_height_mm = 7.0;

        editor.add_hole(Vec3d{10.0, 20.0, 5.0}, Vec3d{0.0, 0.0, 1.0});

        REQUIRE(editor.holes.size() == 1);
        REQUIRE(editor.holes[0].pos == Vec3f{10.0f, 20.0f, 5.0f});
        REQUIRE(editor.holes[0].normal == Vec3f{0.0f, 0.0f, 1.0f});
        REQUIRE(editor.holes[0].radius == 3.0f);
        REQUIRE(editor.holes[0].height == 7.0f);
        REQUIRE(editor.holes[0].failed == false);
    }

    SECTION("Find nearest returns index within radius")
    {
        SlaDrainHolesEditing editor;
        editor.holes.push_back({Vec3f{10.0f, 20.0f, 5.0f}, Vec3f{0,0,1}, 5.0f, 10.0f, false});
        editor.holes.push_back({Vec3f{15.0f, 25.0f, 6.0f}, Vec3f{0,0,1}, 5.0f, 10.0f, false});

        auto idx = editor.find_nearest_hole(Vec3d{10.1, 20.1, 5.1}, 1.0);

        REQUIRE(idx.has_value());
        REQUIRE(*idx == 0);
    }

    SECTION("Find nearest returns nullopt outside radius")
    {
        SlaDrainHolesEditing editor;
        editor.holes.push_back({Vec3f{10.0f, 20.0f, 5.0f}, Vec3f{0,0,1}, 5.0f, 10.0f, false});

        auto idx = editor.find_nearest_hole(Vec3d{100.0, 200.0, 50.0}, 1.0);

        REQUIRE(!idx.has_value());
    }

    SECTION("Find nearest returns closest point when multiple in radius")
    {
        SlaDrainHolesEditing editor;
        editor.holes.push_back({Vec3f{10.0f, 20.0f, 5.0f}, Vec3f{0,0,1}, 5.0f, 10.0f, false});
        editor.holes.push_back({Vec3f{10.5f, 20.5f, 5.5f}, Vec3f{0,0,1}, 5.0f, 10.0f, false});

        auto idx = editor.find_nearest_hole(Vec3d{10.1, 20.1, 5.1}, 2.0);

        REQUIRE(idx.has_value());
        REQUIRE(*idx == 0);
    }

    SECTION("Remove hole removes correct index")
    {
        SlaDrainHolesEditing editor;
        editor.holes.push_back({Vec3f{10.0f, 20.0f, 5.0f}, Vec3f{0,0,1}, 5.0f, 10.0f, false});
        editor.holes.push_back({Vec3f{15.0f, 25.0f, 6.0f}, Vec3f{0,0,1}, 5.0f, 10.0f, false});
        editor.holes.push_back({Vec3f{5.0f, 30.0f, 4.0f}, Vec3f{0,0,1}, 5.0f, 10.0f, false});

        editor.remove_hole(1);

        REQUIRE(editor.holes.size() == 2);
        REQUIRE(editor.holes[0].pos == Vec3f{10.0f, 20.0f, 5.0f});
        REQUIRE(editor.holes[1].pos == Vec3f{5.0f, 30.0f, 4.0f});
    }

    SECTION("Remove hole handles out of bounds gracefully")
    {
        SlaDrainHolesEditing editor;
        editor.holes.push_back({Vec3f{10.0f, 20.0f, 5.0f}, Vec3f{0,0,1}, 5.0f, 10.0f, false});

        editor.remove_hole(5);

        REQUIRE(editor.holes.size() == 1);
    }

    SECTION("Remove hole adjusts selected indices correctly")
    {
        SlaDrainHolesEditing editor;
        editor.holes.push_back({Vec3f{10.0f, 20.0f, 5.0f}, Vec3f{0,0,1}, 5.0f, 10.0f, false});
        editor.holes.push_back({Vec3f{15.0f, 25.0f, 6.0f}, Vec3f{0,0,1}, 5.0f, 10.0f, false});
        editor.holes.push_back({Vec3f{5.0f, 30.0f, 4.0f}, Vec3f{0,0,1}, 5.0f, 10.0f, false});

        editor.selected_hole_indices.insert(1);
        editor.selected_hole_indices.insert(2);
        editor.remove_hole(0);

        REQUIRE(editor.holes.size() == 2);
        REQUIRE(editor.selected_hole_indices.count(0) == 1);
        REQUIRE(editor.selected_hole_indices.count(1) == 1);
    }

    SECTION("Move hole updates position and normal")
    {
        SlaDrainHolesEditing editor;
        editor.holes.push_back({Vec3f{10.0f, 20.0f, 5.0f}, Vec3f{0,0,1}, 5.0f, 10.0f, false});

        editor.move_hole(0, Vec3d{12.0, 22.0, 7.0}, Vec3d{1.0, 0.0, 0.0});

        REQUIRE(editor.holes.size() == 1);
        REQUIRE(editor.holes[0].pos == Vec3f{12.0f, 22.0f, 7.0f});
        REQUIRE(editor.holes[0].normal == Vec3f{1.0f, 0.0f, 0.0f});
        REQUIRE(editor.holes[0].radius == 5.0f);
        REQUIRE(editor.holes[0].height == 10.0f);
    }

    SECTION("Move hole handles out of bounds gracefully")
    {
        SlaDrainHolesEditing editor;
        editor.holes.push_back({Vec3f{10.0f, 20.0f, 5.0f}, Vec3f{0,0,1}, 5.0f, 10.0f, false});

        editor.move_hole(5, Vec3d{12.0, 22.0, 7.0}, Vec3d{1.0, 0.0, 0.0});

        REQUIRE(editor.holes.size() == 1);
        REQUIRE(editor.holes[0].pos == Vec3f{10.0f, 20.0f, 5.0f});
    }

    SECTION("Radius and height affect new holes only")
    {
        SlaDrainHolesEditing editor;
        editor.hole_radius_mm = 3.0;
        editor.hole_height_mm = 6.0;
        editor.add_hole(Vec3d{0, 0, 0}, Vec3d{0,0,1});

        editor.hole_radius_mm = 5.0;
        editor.hole_height_mm = 10.0;
        editor.add_hole(Vec3d{1, 1, 1}, Vec3d{0,0,1});

        REQUIRE(editor.holes[0].radius == 3.0f);
        REQUIRE(editor.holes[0].height == 6.0f);
        REQUIRE(editor.holes[1].radius == 5.0f);
        REQUIRE(editor.holes[1].height == 10.0f);
    }

    SECTION("Multiple edits sequence: add, move, remove")
    {
        SlaDrainHolesEditing editor;
        editor.hole_radius_mm = 2.0;
        editor.hole_height_mm = 4.0;

        editor.add_hole(Vec3d{0, 0, 0}, Vec3d{0,0,1});
        editor.add_hole(Vec3d{10, 10, 10}, Vec3d{0,0,1});
        REQUIRE(editor.holes.size() == 2);

        editor.move_hole(0, Vec3d{1, 1, 1}, Vec3d{0,0,1});
        REQUIRE(editor.holes[0].pos == Vec3f{1.0f, 1.0f, 1.0f});

        editor.remove_hole(0);
        REQUIRE(editor.holes.size() == 1);
        REQUIRE(editor.holes[0].pos == Vec3f{10.0f, 10.0f, 10.0f});
    }
}

TEST_CASE("SlaDrainHolesEditing - selection logic", "[SlaDrainHolesEditing][selection]")
{
    SECTION("Select single hole")
    {
        SlaDrainHolesEditing editor;
        editor.holes.push_back({Vec3f{10.0f, 20.0f, 5.0f}, Vec3f{0,0,1}, 5.0f, 10.0f, false});
        editor.holes.push_back({Vec3f{15.0f, 25.0f, 6.0f}, Vec3f{0,0,1}, 5.0f, 10.0f, false});

        editor.select_hole(0);
        REQUIRE(editor.selected_hole_indices.size() == 1);
        REQUIRE(editor.selected_hole_indices.count(0) == 1);
    }

    SECTION("Shift+click adds to selection (add_to_selection=true)")
    {
        SlaDrainHolesEditing editor;
        editor.holes.push_back({Vec3f{10.0f, 20.0f, 5.0f}, Vec3f{0,0,1}, 5.0f, 10.0f, false});
        editor.holes.push_back({Vec3f{15.0f, 25.0f, 6.0f}, Vec3f{0,0,1}, 5.0f, 10.0f, false});
        editor.holes.push_back({Vec3f{5.0f, 30.0f, 4.0f}, Vec3f{0,0,1}, 5.0f, 10.0f, false});

        editor.select_hole(0);
        editor.select_hole(1, true);

        REQUIRE(editor.selected_hole_indices.size() == 2);
        REQUIRE(editor.selected_hole_indices.count(0) == 1);
        REQUIRE(editor.selected_hole_indices.count(1) == 1);
    }

    SECTION("Select without add_to_selection clears previous selection")
    {
        SlaDrainHolesEditing editor;
        editor.holes.push_back({Vec3f{10.0f, 20.0f, 5.0f}, Vec3f{0,0,1}, 5.0f, 10.0f, false});
        editor.holes.push_back({Vec3f{15.0f, 25.0f, 6.0f}, Vec3f{0,0,1}, 5.0f, 10.0f, false});

        editor.select_hole(0);
        editor.select_hole(1, false);

        REQUIRE(editor.selected_hole_indices.size() == 1);
        REQUIRE(editor.selected_hole_indices.count(1) == 1);
    }

    SECTION("Deselect hole removes from selection")
    {
        SlaDrainHolesEditing editor;
        editor.holes.push_back({Vec3f{10.0f, 20.0f, 5.0f}, Vec3f{0,0,1}, 5.0f, 10.0f, false});
        editor.holes.push_back({Vec3f{15.0f, 25.0f, 6.0f}, Vec3f{0,0,1}, 5.0f, 10.0f, false});

        editor.select_hole(0);
        editor.select_hole(1, true);
        editor.deselect_hole(0);

        REQUIRE(editor.selected_hole_indices.size() == 1);
        REQUIRE(editor.selected_hole_indices.count(1) == 1);
    }

    SECTION("Toggle hole adds when not selected")
    {
        SlaDrainHolesEditing editor;
        editor.holes.push_back({Vec3f{10.0f, 20.0f, 5.0f}, Vec3f{0,0,1}, 5.0f, 10.0f, false});
        editor.holes.push_back({Vec3f{15.0f, 25.0f, 6.0f}, Vec3f{0,0,1}, 5.0f, 10.0f, false});

        editor.toggle_hole(0);
        REQUIRE(editor.selected_hole_indices.size() == 1);
        REQUIRE(editor.selected_hole_indices.count(0) == 1);
    }

    SECTION("Toggle hole removes when selected")
    {
        SlaDrainHolesEditing editor;
        editor.holes.push_back({Vec3f{10.0f, 20.0f, 5.0f}, Vec3f{0,0,1}, 5.0f, 10.0f, false});
        editor.holes.push_back({Vec3f{15.0f, 25.0f, 6.0f}, Vec3f{0,0,1}, 5.0f, 10.0f, false});

        editor.select_hole(0);
        editor.select_hole(1, true);
        editor.toggle_hole(0);

        REQUIRE(editor.selected_hole_indices.size() == 1);
        REQUIRE(editor.selected_hole_indices.count(1) == 1);
    }

    SECTION("Toggle hole handles out of bounds gracefully")
    {
        SlaDrainHolesEditing editor;
        editor.holes.push_back({Vec3f{10.0f, 20.0f, 5.0f}, Vec3f{0,0,1}, 5.0f, 10.0f, false});

        editor.toggle_hole(5);
        REQUIRE(editor.selected_hole_indices.empty());
    }

    SECTION("Select all selects all holes")
    {
        SlaDrainHolesEditing editor;
        editor.holes.push_back({Vec3f{10.0f, 20.0f, 5.0f}, Vec3f{0,0,1}, 5.0f, 10.0f, false});
        editor.holes.push_back({Vec3f{15.0f, 25.0f, 6.0f}, Vec3f{0,0,1}, 5.0f, 10.0f, false});
        editor.holes.push_back({Vec3f{5.0f, 30.0f, 4.0f}, Vec3f{0,0,1}, 5.0f, 10.0f, false});

        editor.select_all_holes();

        REQUIRE(editor.selected_hole_indices.size() == 3);
        REQUIRE(editor.selected_hole_indices.count(0) == 1);
        REQUIRE(editor.selected_hole_indices.count(1) == 1);
        REQUIRE(editor.selected_hole_indices.count(2) == 1);
    }

    SECTION("Select all on empty holes does nothing")
    {
        SlaDrainHolesEditing editor;
        editor.select_all_holes();
        REQUIRE(editor.selected_hole_indices.empty());
    }

    SECTION("Clear selection removes all selected holes")
    {
        SlaDrainHolesEditing editor;
        editor.holes.push_back({Vec3f{10.0f, 20.0f, 5.0f}, Vec3f{0,0,1}, 5.0f, 10.0f, false});
        editor.holes.push_back({Vec3f{15.0f, 25.0f, 6.0f}, Vec3f{0,0,1}, 5.0f, 10.0f, false});

        editor.select_hole(0);
        editor.select_hole(1, true);
        editor.clear_selection();

        REQUIRE(editor.selected_hole_indices.empty());
    }

    SECTION("Delete selected removes selected holes")
    {
        SlaDrainHolesEditing editor;
        editor.holes.push_back({Vec3f{10.0f, 20.0f, 5.0f}, Vec3f{0,0,1}, 5.0f, 10.0f, false});
        editor.holes.push_back({Vec3f{15.0f, 25.0f, 6.0f}, Vec3f{0,0,1}, 5.0f, 10.0f, false});
        editor.holes.push_back({Vec3f{5.0f, 30.0f, 4.0f}, Vec3f{0,0,1}, 5.0f, 10.0f, false});

        editor.select_hole(0);
        editor.select_hole(2, true);
        editor.delete_selected_holes();

        REQUIRE(editor.holes.size() == 1);
        REQUIRE(editor.holes[0].pos == Vec3f{15.0f, 25.0f, 6.0f});
        REQUIRE(editor.selected_hole_indices.empty());
    }

    SECTION("Delete selected does nothing when no holes selected")
    {
        SlaDrainHolesEditing editor;
        editor.holes.push_back({Vec3f{10.0f, 20.0f, 5.0f}, Vec3f{0,0,1}, 5.0f, 10.0f, false});

        editor.delete_selected_holes();

        REQUIRE(editor.holes.size() == 1);
    }

    SECTION("Delete selected clears selection")
    {
        SlaDrainHolesEditing editor;
        editor.holes.push_back({Vec3f{10.0f, 20.0f, 5.0f}, Vec3f{0,0,1}, 5.0f, 10.0f, false});
        editor.holes.push_back({Vec3f{15.0f, 25.0f, 6.0f}, Vec3f{0,0,1}, 5.0f, 10.0f, false});

        editor.select_hole(0);
        editor.delete_selected_holes();

        REQUIRE(editor.selected_hole_indices.empty());
    }

    SECTION("Apply radius to selected updates only selected holes")
    {
        SlaDrainHolesEditing editor;
        editor.hole_radius_mm = 3.0;
        editor.holes.push_back({Vec3f{10.0f, 20.0f, 5.0f}, Vec3f{0,0,1}, 2.0f, 10.0f, false});
        editor.holes.push_back({Vec3f{15.0f, 25.0f, 6.0f}, Vec3f{0,0,1}, 2.0f, 10.0f, false});
        editor.holes.push_back({Vec3f{5.0f, 30.0f, 4.0f}, Vec3f{0,0,1}, 2.0f, 10.0f, false});

        editor.select_hole(0);
        editor.select_hole(2, true);
        editor.apply_radius_to_selected();

        REQUIRE(editor.holes[0].radius == 3.0f);
        REQUIRE(editor.holes[1].radius == 2.0f);
        REQUIRE(editor.holes[2].radius == 3.0f);
    }

    SECTION("Apply height to selected updates only selected holes")
    {
        SlaDrainHolesEditing editor;
        editor.hole_height_mm = 8.0;
        editor.holes.push_back({Vec3f{10.0f, 20.0f, 5.0f}, Vec3f{0,0,1}, 5.0f, 5.0f, false});
        editor.holes.push_back({Vec3f{15.0f, 25.0f, 6.0f}, Vec3f{0,0,1}, 5.0f, 5.0f, false});
        editor.holes.push_back({Vec3f{5.0f, 30.0f, 4.0f}, Vec3f{0,0,1}, 5.0f, 5.0f, false});

        editor.select_hole(0);
        editor.select_hole(2, true);
        editor.apply_height_to_selected();

        REQUIRE(editor.holes[0].height == 8.0f);
        REQUIRE(editor.holes[1].height == 5.0f);
        REQUIRE(editor.holes[2].height == 8.0f);
    }

    SECTION("Apply radius/height to selected handles out of bounds indices gracefully")
    {
        SlaDrainHolesEditing editor;
        editor.hole_radius_mm = 3.0;
        editor.hole_height_mm = 8.0;
        editor.holes.push_back({Vec3f{10.0f, 20.0f, 5.0f}, Vec3f{0,0,1}, 2.0f, 5.0f, false});
        editor.selected_hole_indices.insert(5);

        editor.apply_radius_to_selected();
        editor.apply_height_to_selected();

        REQUIRE(editor.holes[0].radius == 2.0f);
        REQUIRE(editor.holes[0].height == 5.0f);
    }
}