#include <catch2/catch_test_macros.hpp>

#include "Slic3r/Domain/SLA/SupportPoint.hpp"
#include "Slic3r/Domain/Types.hpp"

#include <Eigen/Geometry>

using Slic3r::Domain::SLA::SupportPoint;
using Slic3r::Domain::SLA::SupportPointType;
using Slic3r::Domain::Transform3d;
using Slic3r::Domain::Vec3f;
using Slic3r::Domain::ObjectID;

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
        // Rotated 90 degrees around Z: (10, 0) -> (0, 10)
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
}
