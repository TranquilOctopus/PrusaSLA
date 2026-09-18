#include <catch2/catch_test_macros.hpp>

#include "Slic3r/Biz/Slicing/GeneratedSupportPoints.hpp"
#include "Slic3r/Domain/ModelObject.hpp"
#include "Slic3r/Domain/SLA/SupportPoint.hpp"
#include "Slic3r/Domain/Types.hpp"

using Slic3r::Biz::Slicing::GeneratedSupportPoint;
using Slic3r::Biz::Slicing::ObjectSupportPoints;
using Slic3r::Domain::SLA::SupportPoint;
using Slic3r::Domain::SLA::SupportPointType;
using Slic3r::Domain::Transform3d;
using Slic3r::Domain::Vec3f;
using Slic3r::Domain::ObjectID;

namespace {

// Helper function that converts generated support points to domain support points
// This is the core logic that can be tested without GPU/window
Slic3r::Domain::SLA::SupportPoints convert_generated_to_domain_points(const ObjectSupportPoints& object_support_points)
{
    Slic3r::Domain::SLA::SupportPoints domain_points;
    domain_points.reserve(object_support_points.support_points.size());

    for (const GeneratedSupportPoint& gp : object_support_points.support_points) {
        SupportPoint sp;
        sp.pos = gp.position;
        sp.head_front_radius = gp.spot_radius;
        sp.type = SupportPointType::island;
        domain_points.push_back(sp);
    }
    return domain_points;
}

} // namespace

TEST_CASE("SlaSupportPointsGizmo - convert_generated_to_domain_points", "[SlaSupportPointsGizmo]")
{
    SECTION("Converts empty generated points to empty domain points")
    {
        ObjectSupportPoints input;
        input.model_object_id = ObjectID(1);
        input.object_transform = Transform3d::Identity();
        input.support_points.clear();

        auto result = convert_generated_to_domain_points(input);

        REQUIRE(result.empty());
    }

    SECTION("Converts single generated point to domain point")
    {
        ObjectSupportPoints input;
        input.model_object_id = ObjectID(1);
        input.object_transform = Transform3d::Identity();
        input.support_points.push_back({Vec3f{10.0f, 20.0f, 5.0f}, 0.5f});

        auto result = convert_generated_to_domain_points(input);

        REQUIRE(result.size() == 1);
        REQUIRE(result[0].pos == Vec3f{10.0f, 20.0f, 5.0f});
        REQUIRE(result[0].head_front_radius == 0.5f);
        REQUIRE(result[0].type == SupportPointType::island);
    }

    SECTION("Converts multiple generated points to domain points")
    {
        ObjectSupportPoints input;
        input.model_object_id = ObjectID(1);
        input.object_transform = Transform3d::Identity();
        input.support_points.push_back({Vec3f{10.0f, 20.0f, 5.0f}, 0.5f});
        input.support_points.push_back({Vec3f{15.0f, 25.0f, 6.0f}, 0.6f});
        input.support_points.push_back({Vec3f{5.0f, 30.0f, 4.0f}, 0.4f});

        auto result = convert_generated_to_domain_points(input);

        REQUIRE(result.size() == 3);
        REQUIRE(result[0].pos == Vec3f{10.0f, 20.0f, 5.0f});
        REQUIRE(result[0].head_front_radius == 0.5f);
        REQUIRE(result[1].pos == Vec3f{15.0f, 25.0f, 6.0f});
        REQUIRE(result[1].head_front_radius == 0.6f);
        REQUIRE(result[2].pos == Vec3f{5.0f, 30.0f, 4.0f});
        REQUIRE(result[2].head_front_radius == 0.4f);
        for (const auto& sp : result) {
            REQUIRE(sp.type == SupportPointType::island);
        }
    }

    SECTION("Preserves spot_radius as head_front_radius")
    {
        ObjectSupportPoints input;
        input.model_object_id = ObjectID(1);
        input.object_transform = Transform3d::Identity();
        input.support_points.push_back({Vec3f{0.0f, 0.0f, 0.0f}, 1.25f});

        auto result = convert_generated_to_domain_points(input);

        REQUIRE(result[0].head_front_radius == 1.25f);
    }
}