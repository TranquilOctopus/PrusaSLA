#include <catch2/catch_test_macros.hpp>

#include "Slic3r/Domain/SLA/SupportPoint.hpp"
#include "Slic3r/Domain/Types.hpp"

#include <Eigen/Geometry>
#include <unordered_set>

using Slic3r::Domain::SLA::SupportPoint;
using Slic3r::Domain::SLA::SupportPointType;
using Slic3r::Domain::Transform3d;
using Slic3r::Domain::Vec3f;
using Slic3r::Domain::Vec3d;
using Slic3r::Domain::Vec2d;
using Slic3r::Domain::Vec4d;

namespace {

// Helper function that applies a transform to support points
// This mimics the world->mesh conversion done by SlaSupportPointsRequest
Slic3r::Domain::SLA::SupportPoints transform_support_points(
    const Slic3r::Domain::SLA::SupportPoints& points,
    const Transform3d& transform)
{
    Slic3r::Domain::SLA::SupportPoints result;
    result.reserve(points.size());
    for (const auto& sp : points) {
        SupportPoint transformed = sp;
        transformed.pos = (transform * sp.pos.cast<double>()).cast<float>();
        result.push_back(transformed);
    }
    return result;
}

// Pure editing logic helpers (GPU/window free)
struct SupportPointEditor
{
    Slic3r::Domain::SLA::SupportPoints points;
    double head_diameter_mm = 0.4;
    std::unordered_set<size_t> selected_point_indices;
    bool lock_island_supports = false;

    std::optional<size_t> find_nearest(const Vec3d& mesh_pos, double max_distance_mm) const
    {
        std::optional<size_t> nearest_idx;
        double nearest_dist_sq = max_distance_mm * max_distance_mm;

        for (size_t i = 0; i < points.size(); ++i) {
            const Vec3d point_pos = points[i].pos.cast<double>();
            double dist_sq = (point_pos - mesh_pos).squaredNorm();
            if (dist_sq < nearest_dist_sq) {
                nearest_dist_sq = dist_sq;
                nearest_idx = i;
            }
        }
        return nearest_idx;
    }

    void add_point(const Vec3d& mesh_pos)
    {
        SupportPoint new_point;
        new_point.pos = mesh_pos.cast<float>();
        new_point.head_front_radius = static_cast<float>(head_diameter_mm / 2.0);
        new_point.type = SupportPointType::manual_add;
        points.push_back(new_point);
    }

    void remove_point(size_t idx)
    {
        if (idx < points.size()) {
            points.erase(points.begin() + idx);
            selected_point_indices.erase(idx);
            // Adjust indices > idx
            std::unordered_set<size_t> new_selected;
            for (size_t s : selected_point_indices) {
                if (s > idx) new_selected.insert(s - 1);
                else if (s < idx) new_selected.insert(s);
            }
            selected_point_indices = std::move(new_selected);
        }
    }

    void move_point(size_t idx, const Vec3d& mesh_pos)
    {
        if (idx < points.size()) {
            points[idx].pos = mesh_pos.cast<float>();
        }
    }

    void select_point(size_t idx, bool add_to_selection = false)
    {
        if (idx >= points.size()) return;
        if (!add_to_selection) selected_point_indices.clear();
        selected_point_indices.insert(idx);
    }

    void deselect_point(size_t idx)
    {
        selected_point_indices.erase(idx);
    }

    void select_all_points()
    {
        selected_point_indices.clear();
        for (size_t i = 0; i < points.size(); ++i) {
            if (!lock_island_supports || !points[i].is_island()) {
                selected_point_indices.insert(i);
            }
        }
    }

    void clear_selection()
    {
        selected_point_indices.clear();
    }

    void delete_selected_points()
    {
        if (selected_point_indices.empty()) return;

        std::vector<size_t> indices_to_delete(selected_point_indices.begin(), selected_point_indices.end());
        std::sort(indices_to_delete.rbegin(), indices_to_delete.rend());

        bool any_deleted = false;
        for (size_t idx : indices_to_delete) {
            if (idx < points.size()) {
                const bool is_island = points[idx].is_island();
                if (!lock_island_supports || !is_island) {
                    points.erase(points.begin() + idx);
                    any_deleted = true;
                }
            }
        }

        if (any_deleted) {
            selected_point_indices.clear();
        }
    }

    void apply_head_diameter_to_selected()
    {
        const float new_radius = static_cast<float>(head_diameter_mm / 2.0);
        for (size_t idx : selected_point_indices) {
            if (idx < points.size()) {
                points[idx].head_front_radius = new_radius;
            }
        }
    }
};

struct SupportPointSelectionTestHelper
{
    // Projects 3D points to 2D screen using a simple orthographic camera
    static std::vector<Vec2d> project_points(const Slic3r::Domain::SLA::SupportPoints& points,
                                              const Transform3d& view_matrix,
                                              const Vec2d& screen_size)
    {
        std::vector<Vec2d> result;
        result.reserve(points.size());
        
        for (const auto& point : points) {
            Vec4d homogeneous = view_matrix * Vec4d(point.pos.x(), point.pos.y(), point.pos.z(), 1.0);
            if (std::abs(homogeneous.w()) > 1e-9) {
                homogeneous /= homogeneous.w();
            }
            // NDC to screen space
            double x = (homogeneous.x() + 1.0) * 0.5 * screen_size.x();
            double y = (1.0 - homogeneous.y()) * 0.5 * screen_size.y();
            result.emplace_back(x, y);
        }
        return result;
    }

    // Returns indices of points within the screen-space rectangle
    static std::vector<size_t> points_in_rectangle(const std::vector<Vec2d>& screen_positions,
                                                    const Vec2d& rect_min,
                                                    const Vec2d& rect_max)
    {
        std::vector<size_t> result;
        for (size_t i = 0; i < screen_positions.size(); ++i) {
            const Vec2d& pos = screen_positions[i];
            if (pos.x() >= rect_min.x() && pos.x() <= rect_max.x() &&
                pos.y() >= rect_min.y() && pos.y() <= rect_max.y()) {
                result.push_back(i);
            }
        }
        return result;
    }
};

} // namespace

TEST_CASE("SlaSupportPointsGizmo - transform_support_points", "[SlaSupportPointsGizmo]")
{
    SECTION("Transforms empty points to empty points")
    {
        Slic3r::Domain::SLA::SupportPoints input;
        Transform3d transform = Transform3d::Identity();

        auto result = transform_support_points(input, transform);

        REQUIRE(result.empty());
    }

    SECTION("Identity transform preserves points")
    {
        Slic3r::Domain::SLA::SupportPoints input;
        input.push_back({Vec3f{10.0f, 20.0f, 5.0f}, 0.5f, SupportPointType::island});
        input.push_back({Vec3f{15.0f, 25.0f, 6.0f}, 0.6f, SupportPointType::slope});
        Transform3d transform = Transform3d::Identity();

        auto result = transform_support_points(input, transform);

        REQUIRE(result.size() == 2);
        REQUIRE(result[0].pos == Vec3f{10.0f, 20.0f, 5.0f});
        REQUIRE(result[0].head_front_radius == 0.5f);
        REQUIRE(result[0].type == SupportPointType::island);
        REQUIRE(result[1].pos == Vec3f{15.0f, 25.0f, 6.0f});
        REQUIRE(result[1].head_front_radius == 0.6f);
        REQUIRE(result[1].type == SupportPointType::slope);
    }

    SECTION("Translation transform shifts points")
    {
        Slic3r::Domain::SLA::SupportPoints input;
        input.push_back({Vec3f{10.0f, 20.0f, 5.0f}, 0.5f, SupportPointType::island});
        Transform3d transform = Transform3d::Identity();
        transform.translate(Vec3d{5.0, -3.0, 2.0});

        auto result = transform_support_points(input, transform);

        REQUIRE(result.size() == 1);
        REQUIRE(result[0].pos == Vec3f{15.0f, 17.0f, 7.0f});
        REQUIRE(result[0].head_front_radius == 0.5f);
        REQUIRE(result[0].type == SupportPointType::island);
    }

    SECTION("Scaling transform scales points")
    {
        Slic3r::Domain::SLA::SupportPoints input;
        input.push_back({Vec3f{10.0f, 20.0f, 5.0f}, 0.5f, SupportPointType::island});
        Transform3d transform = Transform3d::Identity();
        transform.scale(Vec3d{2.0, 0.5, 1.0});

        auto result = transform_support_points(input, transform);

        REQUIRE(result.size() == 1);
        REQUIRE(result[0].pos == Vec3f{20.0f, 10.0f, 5.0f});
        REQUIRE(result[0].head_front_radius == 0.5f);
        REQUIRE(result[0].type == SupportPointType::island);
    }

    SECTION("Rotation transform rotates points around Z")
    {
        Slic3r::Domain::SLA::SupportPoints input;
        input.push_back({Vec3f{10.0f, 0.0f, 5.0f}, 0.5f, SupportPointType::island});
        Transform3d transform = Transform3d::Identity();
        transform.rotate(Eigen::AngleAxisd(M_PI / 2, Vec3d::UnitZ()));

        auto result = transform_support_points(input, transform);

        REQUIRE(result.size() == 1);
        REQUIRE(std::abs(result[0].pos.x()) < 0.001f);
        REQUIRE(std::abs(result[0].pos.y() - 10.0f) < 0.001f);
        REQUIRE(result[0].pos.z() == 5.0f);
        REQUIRE(result[0].head_front_radius == 0.5f);
        REQUIRE(result[0].type == SupportPointType::island);
    }

    SECTION("Multiple points transformed correctly")
    {
        Slic3r::Domain::SLA::SupportPoints input;
        input.push_back({Vec3f{10.0f, 20.0f, 5.0f}, 0.5f, SupportPointType::island});
        input.push_back({Vec3f{15.0f, 25.0f, 6.0f}, 0.6f, SupportPointType::slope});
        input.push_back({Vec3f{5.0f, 30.0f, 4.0f}, 0.4f, SupportPointType::manual_add});
        Transform3d transform = Transform3d::Identity();
        transform.translate(Vec3d{1.0, 2.0, 3.0});

        auto result = transform_support_points(input, transform);

        REQUIRE(result.size() == 3);
        REQUIRE(result[0].pos == Vec3f{11.0f, 22.0f, 8.0f});
        REQUIRE(result[0].head_front_radius == 0.5f);
        REQUIRE(result[0].type == SupportPointType::island);
        REQUIRE(result[1].pos == Vec3f{16.0f, 27.0f, 9.0f});
        REQUIRE(result[1].head_front_radius == 0.6f);
        REQUIRE(result[1].type == SupportPointType::slope);
        REQUIRE(result[2].pos == Vec3f{6.0f, 32.0f, 7.0f});
        REQUIRE(result[2].head_front_radius == 0.4f);
        REQUIRE(result[2].type == SupportPointType::manual_add);
    }

    SECTION("Inverse transform recovers original points within tolerance")
    {
        Slic3r::Domain::SLA::SupportPoints input;
        input.push_back({Vec3f{10.0f, 20.0f, 5.0f}, 0.5f, SupportPointType::island});
        input.push_back({Vec3f{15.0f, 25.0f, 6.0f}, 0.6f, SupportPointType::slope});
        input.push_back({Vec3f{5.0f, 30.0f, 4.0f}, 0.4f, SupportPointType::manual_add});
        Transform3d transform = Transform3d::Identity();
        transform.translate(Vec3d{1.0, 2.0, 3.0});
        transform.rotate(Eigen::AngleAxisd(M_PI / 4, Vec3d::UnitZ()));
        transform.scale(Vec3d{1.2, 0.8, 1.0});

        auto transformed = transform_support_points(input, transform);
        auto recovered = transform_support_points(transformed, transform.inverse());

        REQUIRE(recovered.size() == input.size());
        for (size_t i = 0; i < input.size(); ++i) {
            REQUIRE(std::abs(recovered[i].pos.x() - input[i].pos.x()) < 1e-5f);
            REQUIRE(std::abs(recovered[i].pos.y() - input[i].pos.y()) < 1e-5f);
            REQUIRE(std::abs(recovered[i].pos.z() - input[i].pos.z()) < 1e-5f);
            REQUIRE(recovered[i].head_front_radius == input[i].head_front_radius);
            REQUIRE(recovered[i].type == input[i].type);
        }
    }
}

TEST_CASE("SupportPointEditor - pure editing logic", "[SlaSupportPointsGizmo][editing]")
{
    SECTION("Add point creates manual_add point with correct head radius")
    {
        SupportPointEditor editor;
        editor.head_diameter_mm = 0.6;

        editor.add_point(Vec3d{10.0, 20.0, 5.0});

        REQUIRE(editor.points.size() == 1);
        REQUIRE(editor.points[0].pos == Vec3f{10.0f, 20.0f, 5.0f});
        REQUIRE(editor.points[0].head_front_radius == 0.3f);
        REQUIRE(editor.points[0].type == SupportPointType::manual_add);
    }

    SECTION("Find nearest returns index within radius")
    {
        SupportPointEditor editor;
        editor.points.push_back({Vec3f{10.0f, 20.0f, 5.0f}, 0.5f, SupportPointType::manual_add});
        editor.points.push_back({Vec3f{15.0f, 25.0f, 6.0f}, 0.6f, SupportPointType::island});

        auto idx = editor.find_nearest(Vec3d{10.1, 20.1, 5.1}, 1.0);

        REQUIRE(idx.has_value());
        REQUIRE(*idx == 0);
    }

    SECTION("Find nearest returns nullopt outside radius")
    {
        SupportPointEditor editor;
        editor.points.push_back({Vec3f{10.0f, 20.0f, 5.0f}, 0.5f, SupportPointType::manual_add});

        auto idx = editor.find_nearest(Vec3d{100.0, 200.0, 50.0}, 1.0);

        REQUIRE(!idx.has_value());
    }

    SECTION("Find nearest returns closest point when multiple in radius")
    {
        SupportPointEditor editor;
        editor.points.push_back({Vec3f{10.0f, 20.0f, 5.0f}, 0.5f, SupportPointType::manual_add});
        editor.points.push_back({Vec3f{10.5f, 20.5f, 5.5f}, 0.6f, SupportPointType::island});

        auto idx = editor.find_nearest(Vec3d{10.1, 20.1, 5.1}, 2.0);

        REQUIRE(idx.has_value());
        REQUIRE(*idx == 0);
    }

    SECTION("Remove point removes correct index")
    {
        SupportPointEditor editor;
        editor.points.push_back({Vec3f{10.0f, 20.0f, 5.0f}, 0.5f, SupportPointType::manual_add});
        editor.points.push_back({Vec3f{15.0f, 25.0f, 6.0f}, 0.6f, SupportPointType::island});
        editor.points.push_back({Vec3f{5.0f, 30.0f, 4.0f}, 0.4f, SupportPointType::slope});

        editor.remove_point(1);

        REQUIRE(editor.points.size() == 2);
        REQUIRE(editor.points[0].pos == Vec3f{10.0f, 20.0f, 5.0f});
        REQUIRE(editor.points[1].pos == Vec3f{5.0f, 30.0f, 4.0f});
    }

    SECTION("Remove point handles out of bounds gracefully")
    {
        SupportPointEditor editor;
        editor.points.push_back({Vec3f{10.0f, 20.0f, 5.0f}, 0.5f, SupportPointType::manual_add});

        editor.remove_point(5);

        REQUIRE(editor.points.size() == 1);
    }

    SECTION("Move point updates position")
    {
        SupportPointEditor editor;
        editor.points.push_back({Vec3f{10.0f, 20.0f, 5.0f}, 0.5f, SupportPointType::manual_add});

        editor.move_point(0, Vec3d{12.0, 22.0, 7.0});

        REQUIRE(editor.points.size() == 1);
        REQUIRE(editor.points[0].pos == Vec3f{12.0f, 22.0f, 7.0f});
        REQUIRE(editor.points[0].head_front_radius == 0.5f);
        REQUIRE(editor.points[0].type == SupportPointType::manual_add);
    }

    SECTION("Move point handles out of bounds gracefully")
    {
        SupportPointEditor editor;
        editor.points.push_back({Vec3f{10.0f, 20.0f, 5.0f}, 0.5f, SupportPointType::manual_add});

        editor.move_point(5, Vec3d{12.0, 22.0, 7.0});

        REQUIRE(editor.points.size() == 1);
        REQUIRE(editor.points[0].pos == Vec3f{10.0f, 20.0f, 5.0f});
    }

    SECTION("Head diameter affects new points only")
    {
        SupportPointEditor editor;
        editor.head_diameter_mm = 0.4;
        editor.add_point(Vec3d{0, 0, 0});

        editor.head_diameter_mm = 1.0;
        editor.add_point(Vec3d{1, 1, 1});

        REQUIRE(editor.points[0].head_front_radius == 0.2f);
        REQUIRE(editor.points[1].head_front_radius == 0.5f);
    }

    SECTION("Multiple edits sequence: add, move, remove")
    {
        SupportPointEditor editor;
        editor.head_diameter_mm = 0.5;

        editor.add_point(Vec3d{0, 0, 0});
        editor.add_point(Vec3d{10, 10, 10});
        REQUIRE(editor.points.size() == 2);

        editor.move_point(0, Vec3d{1, 1, 1});
        REQUIRE(editor.points[0].pos == Vec3f{1.0f, 1.0f, 1.0f});

        editor.remove_point(0);
        REQUIRE(editor.points.size() == 1);
        REQUIRE(editor.points[0].pos == Vec3f{10.0f, 10.0f, 10.0f});
    }
}

TEST_CASE("SupportPointEditor - selection logic", "[SlaSupportPointsGizmo][selection]")
{
    SECTION("Select single point")
    {
        SupportPointEditor editor;
        editor.points.push_back({Vec3f{10.0f, 20.0f, 5.0f}, 0.5f, SupportPointType::manual_add});
        editor.points.push_back({Vec3f{15.0f, 25.0f, 6.0f}, 0.6f, SupportPointType::island});

        editor.select_point(0);
        REQUIRE(editor.selected_point_indices.size() == 1);
        REQUIRE(editor.selected_point_indices.count(0) == 1);
    }

    SECTION("Shift+click adds to selection")
    {
        SupportPointEditor editor;
        editor.points.push_back({Vec3f{10.0f, 20.0f, 5.0f}, 0.5f, SupportPointType::manual_add});
        editor.points.push_back({Vec3f{15.0f, 25.0f, 6.0f}, 0.6f, SupportPointType::island});
        editor.points.push_back({Vec3f{5.0f, 30.0f, 4.0f}, 0.4f, SupportPointType::slope});

        editor.select_point(0);
        editor.select_point(1, true); // add to selection

        REQUIRE(editor.selected_point_indices.size() == 2);
        REQUIRE(editor.selected_point_indices.count(0) == 1);
        REQUIRE(editor.selected_point_indices.count(1) == 1);
    }

    SECTION("Shift+click removes from selection")
    {
        SupportPointEditor editor;
        editor.points.push_back({Vec3f{10.0f, 20.0f, 5.0f}, 0.5f, SupportPointType::manual_add});
        editor.points.push_back({Vec3f{15.0f, 25.0f, 6.0f}, 0.6f, SupportPointType::island});

        editor.select_point(0);
        editor.select_point(1, true);
        editor.select_point(0, true); // toggle off

        REQUIRE(editor.selected_point_indices.size() == 1);
        REQUIRE(editor.selected_point_indices.count(1) == 1);
    }

    SECTION("Select all selects all non-locked points")
    {
        SupportPointEditor editor;
        editor.points.push_back({Vec3f{10.0f, 20.0f, 5.0f}, 0.5f, SupportPointType::manual_add});
        editor.points.push_back({Vec3f{15.0f, 25.0f, 6.0f}, 0.6f, SupportPointType::island});
        editor.points.push_back({Vec3f{5.0f, 30.0f, 4.0f}, 0.4f, SupportPointType::slope});
        editor.lock_island_supports = true;

        editor.select_all_points();

        REQUIRE(editor.selected_point_indices.size() == 2);
        REQUIRE(editor.selected_point_indices.count(0) == 1); // manual_add
        REQUIRE(editor.selected_point_indices.count(2) == 1); // slope
        REQUIRE(editor.selected_point_indices.count(1) == 0); // island (locked)
    }

    SECTION("Select all selects all points when not locked")
    {
        SupportPointEditor editor;
        editor.points.push_back({Vec3f{10.0f, 20.0f, 5.0f}, 0.5f, SupportPointType::manual_add});
        editor.points.push_back({Vec3f{15.0f, 25.0f, 6.0f}, 0.6f, SupportPointType::island});
        editor.points.push_back({Vec3f{5.0f, 30.0f, 4.0f}, 0.4f, SupportPointType::slope});
        editor.lock_island_supports = false;

        editor.select_all_points();

        REQUIRE(editor.selected_point_indices.size() == 3);
    }

    SECTION("Clear selection removes all selected points")
    {
        SupportPointEditor editor;
        editor.points.push_back({Vec3f{10.0f, 20.0f, 5.0f}, 0.5f, SupportPointType::manual_add});
        editor.points.push_back({Vec3f{15.0f, 25.0f, 6.0f}, 0.6f, SupportPointType::island});

        editor.select_point(0);
        editor.select_point(1, true);
        editor.clear_selection();

        REQUIRE(editor.selected_point_indices.empty());
    }

    SECTION("Delete selected removes selected non-locked points")
    {
        SupportPointEditor editor;
        editor.points.push_back({Vec3f{10.0f, 20.0f, 5.0f}, 0.5f, SupportPointType::manual_add});
        editor.points.push_back({Vec3f{15.0f, 25.0f, 6.0f}, 0.6f, SupportPointType::island});
        editor.points.push_back({Vec3f{5.0f, 30.0f, 4.0f}, 0.4f, SupportPointType::slope});
        editor.lock_island_supports = true;

        editor.select_point(0);
        editor.select_point(2, true); // select manual_add and slope
        editor.delete_selected_points();

        REQUIRE(editor.points.size() == 1);
        REQUIRE(editor.points[0].type == SupportPointType::island); // only island remains
        REQUIRE(editor.selected_point_indices.empty());
    }

    SECTION("Delete selected with lock off removes all selected including islands")
    {
        SupportPointEditor editor;
        editor.points.push_back({Vec3f{10.0f, 20.0f, 5.0f}, 0.5f, SupportPointType::manual_add});
        editor.points.push_back({Vec3f{15.0f, 25.0f, 6.0f}, 0.6f, SupportPointType::island});
        editor.points.push_back({Vec3f{5.0f, 30.0f, 4.0f}, 0.4f, SupportPointType::slope});
        editor.lock_island_supports = false;

        editor.select_point(0);
        editor.select_point(1, true);
        editor.select_point(2, true);
        editor.delete_selected_points();

        REQUIRE(editor.points.empty());
        REQUIRE(editor.selected_point_indices.empty());
    }

    SECTION("Delete selected does nothing when no points selected")
    {
        SupportPointEditor editor;
        editor.points.push_back({Vec3f{10.0f, 20.0f, 5.0f}, 0.5f, SupportPointType::manual_add});

        editor.delete_selected_points();

        REQUIRE(editor.points.size() == 1);
    }

    SECTION("Apply head diameter to selected updates only selected points")
    {
        SupportPointEditor editor;
        editor.head_diameter_mm = 0.4;
        editor.points.push_back({Vec3f{10.0f, 20.0f, 5.0f}, 0.2f, SupportPointType::manual_add});
        editor.points.push_back({Vec3f{15.0f, 25.0f, 6.0f}, 0.3f, SupportPointType::island});
        editor.points.push_back({Vec3f{5.0f, 30.0f, 4.0f}, 0.4f, SupportPointType::slope});

        editor.select_point(0);
        editor.select_point(2, true);
        editor.head_diameter_mm = 1.0;
        editor.apply_head_diameter_to_selected();

        REQUIRE(editor.points[0].head_front_radius == 0.5f); // updated
        REQUIRE(editor.points[1].head_front_radius == 0.3f); // not selected, unchanged
        REQUIRE(editor.points[2].head_front_radius == 0.5f); // updated
    }

    SECTION("Remove point adjusts selected indices correctly")
    {
        SupportPointEditor editor;
        editor.points.push_back({Vec3f{10.0f, 20.0f, 5.0f}, 0.5f, SupportPointType::manual_add});
        editor.points.push_back({Vec3f{15.0f, 25.0f, 6.0f}, 0.6f, SupportPointType::island});
        editor.points.push_back({Vec3f{5.0f, 30.0f, 4.0f}, 0.4f, SupportPointType::slope});

        editor.select_point(1);
        editor.select_point(2, true);
        editor.remove_point(0); // remove first point

        REQUIRE(editor.points.size() == 2);
        // Selected indices should shift down by 1
        REQUIRE(editor.selected_point_indices.count(0) == 1); // was index 1
        REQUIRE(editor.selected_point_indices.count(1) == 1); // was index 2
    }
}

TEST_CASE("SupportPointSelectionTestHelper - rectangle selection", "[SlaSupportPointsGizmo][rectangle]")
{
    SECTION("Points inside rectangle are selected")
    {
        SupportPointEditor editor;
        editor.points.push_back({Vec3f{10.0f, 20.0f, 5.0f}, 0.5f, SupportPointType::manual_add});
        editor.points.push_back({Vec3f{15.0f, 25.0f, 6.0f}, 0.6f, SupportPointType::island});
        editor.points.push_back({Vec3f{5.0f, 30.0f, 4.0f}, 0.4f, SupportPointType::slope});

        // Simple orthographic view matrix
        Transform3d view = Transform3d::Identity();
        view(0,0) = 10.0; // scale X
        view(1,1) = 10.0; // scale Y
        view(2,2) = 1.0;
        view(0,3) = 0.0;
        view(1,3) = 0.0;
        view(2,3) = 0.0;

        auto screen_pos = SupportPointSelectionTestHelper::project_points(editor.points, view, Vec2d{800, 600});

        // Rectangle covering first two points
        Vec2d rect_min{50, 50};
        Vec2d rect_max{200, 300};
        auto indices = SupportPointSelectionTestHelper::points_in_rectangle(screen_pos, rect_min, rect_max);

        REQUIRE(indices.size() >= 1);
    }

    SECTION("Points outside rectangle are not selected")
    {
        SupportPointEditor editor;
        editor.points.push_back({Vec3f{100.0f, 100.0f, 0.0f}, 0.5f, SupportPointType::manual_add});
        editor.points.push_back({Vec3f{200.0f, 200.0f, 0.0f}, 0.6f, SupportPointType::island});

        Transform3d view = Transform3d::Identity();
        view(0,0) = 1.0;
        view(1,1) = 1.0;

        auto screen_pos = SupportPointSelectionTestHelper::project_points(editor.points, view, Vec2d{800, 600});

        // Rectangle far away from points
        Vec2d rect_min{10, 10};
        Vec2d rect_max{20, 20};
        auto indices = SupportPointSelectionTestHelper::points_in_rectangle(screen_pos, rect_min, rect_max);

        REQUIRE(indices.empty());
    }

    SECTION("Rectangle selection works with partial overlap")
    {
        SupportPointEditor editor;
        editor.points.push_back({Vec3f{10.0f, 10.0f, 0.0f}, 0.5f, SupportPointType::manual_add});
        editor.points.push_back({Vec3f{50.0f, 50.0f, 0.0f}, 0.6f, SupportPointType::island});
        editor.points.push_back({Vec3f{100.0f, 100.0f, 0.0f}, 0.4f, SupportPointType::slope});

        Transform3d view = Transform3d::Identity();
        view(0,0) = 4.0; // maps 0-200 to 0-800
        view(1,1) = 4.0;

        auto screen_pos = SupportPointSelectionTestHelper::project_points(editor.points, view, Vec2d{800, 600});

        // Rectangle covering first point only
        Vec2d rect_min{30, 30};
        Vec2d rect_max{60, 60};
        auto indices = SupportPointSelectionTestHelper::points_in_rectangle(screen_pos, rect_min, rect_max);

        REQUIRE(indices.size() == 1);
        REQUIRE(indices[0] == 0);
    }
}