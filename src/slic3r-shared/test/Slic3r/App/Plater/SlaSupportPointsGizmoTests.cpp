#include <catch2/catch_test_macros.hpp>

#include "Slic3r/Domain/SLA/SupportPoint.hpp"
#include "Slic3r/Domain/Types.hpp"

#include <Eigen/Geometry>

using Slic3r::Domain::SLA::SupportPoint;
using Slic3r::Domain::SLA::SupportPointType;
using Slic3r::Domain::Transform3d;
using Slic3r::Domain::Vec3f;
using Slic3r::Domain::Vec3d;

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
        }
    }

    void move_point(size_t idx, const Vec3d& mesh_pos)
    {
        if (idx < points.size()) {
            points[idx].pos = mesh_pos.cast<float>();
        }
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