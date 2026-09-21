#include <catch2/catch_test_macros.hpp>

#include "Slic3r/App/Undo/ModelSerialize.hpp"
#include "Slic3r/Domain/Model.hpp"
#include "Slic3r/Domain/ModelObject.hpp"
#include "Slic3r/Domain/ModelInstance.hpp"
#include "Slic3r/Domain/SLA/SupportPoint.hpp"
#include "Slic3r/Domain/SLA/DrainHole.hpp"
#include "Slic3r/Domain/Types.hpp"

#include <Eigen/Geometry>

using Slic3r::Domain::Model;
using Slic3r::Domain::ModelObject;
using Slic3r::Domain::ModelInstance;
using Slic3r::Domain::SLA::SupportPoint;
using Slic3r::Domain::SLA::SupportPointType;
using Slic3r::Domain::SLA::DrainHole;
using Slic3r::Domain::Vec3f;
using Slic3r::Domain::Transform3d;
using Slic3r::App::Undo::serialize_model;
using Slic3r::App::Undo::load_serialized_model;
using Slic3r::App::Undo::SerializedData;

TEST_CASE("ModelSerialize - roundtrip with SLA support points and drain holes", "[ModelSerialize][SLA][undo]")
{
    // Create a model with an object that has support points and drain holes
    Model original_model;

    auto* model_object = original_model.add_object();
    model_object->name = "TestObject";

    // Add support points (with per-point overrides for the last one)
    model_object->sla_support_points.push_back({Vec3f{10.0f, 20.0f, 5.0f}, 0.5f, SupportPointType::manual_add});
    model_object->sla_support_points.push_back({Vec3f{15.0f, 25.0f, 6.0f}, 0.6f, SupportPointType::island});
    model_object->sla_support_points.push_back({Vec3f{5.0f, 30.0f, 4.0f}, 0.4f, SupportPointType::slope});
    // Point with per-point overrides
    model_object->sla_support_points.push_back({Vec3f{20.0f, 20.0f, 10.0f}, 1.2f, SupportPointType::manual_add, 1.8f, 3.5f, 1.2f});
    model_object->sla_points_status = Slic3r::Domain::SLA::PointsStatus::UserModified;

    // Add drain holes
    model_object->sla_drain_holes.push_back({
        Vec3f{12.0f, 22.0f, 0.0f},   // pos
        Vec3f{0.0f, 0.0f, 1.0f},     // normal (upward)
        3.0f,                         // radius
        5.0f,                         // height
        false                         // failed
    });
    model_object->sla_drain_holes.push_back({
        Vec3f{20.0f, 30.0f, 0.0f},
        Vec3f{0.0f, 0.0f, 1.0f},
        2.5f,
        4.0f,
        false
    });


    // Serialize
    SerializedData empty_previous;
    SerializedData snapshot = serialize_model(original_model, empty_previous);

    // Deserialize
    Model recovered_model = load_serialized_model(snapshot);

    // Verify the recovered model
    REQUIRE(recovered_model.objects.size() == 1);
    ModelObject* recovered_object = recovered_model.objects[0];
    REQUIRE(recovered_object->name == "TestObject");

    // Verify support points
    REQUIRE(recovered_object->sla_support_points.size() == 4);
    REQUIRE(recovered_object->sla_points_status == Slic3r::Domain::SLA::PointsStatus::UserModified);

    REQUIRE(recovered_object->sla_support_points[0].pos == Vec3f{10.0f, 20.0f, 5.0f});
    REQUIRE(recovered_object->sla_support_points[0].head_front_radius == 0.5f);
    REQUIRE(recovered_object->sla_support_points[0].pillar_diameter == 0.f);
    REQUIRE(recovered_object->sla_support_points[0].base_diameter == 0.f);
    REQUIRE(recovered_object->sla_support_points[0].base_height == 0.f);
    REQUIRE(recovered_object->sla_support_points[0].type == SupportPointType::manual_add);

    REQUIRE(recovered_object->sla_support_points[1].pos == Vec3f{15.0f, 25.0f, 6.0f});
    REQUIRE(recovered_object->sla_support_points[1].head_front_radius == 0.6f);
    REQUIRE(recovered_object->sla_support_points[1].pillar_diameter == 0.f);
    REQUIRE(recovered_object->sla_support_points[1].base_diameter == 0.f);
    REQUIRE(recovered_object->sla_support_points[1].base_height == 0.f);
    REQUIRE(recovered_object->sla_support_points[1].type == SupportPointType::island);

    REQUIRE(recovered_object->sla_support_points[2].pos == Vec3f{5.0f, 30.0f, 4.0f});
    REQUIRE(recovered_object->sla_support_points[2].head_front_radius == 0.4f);
    REQUIRE(recovered_object->sla_support_points[2].pillar_diameter == 0.f);
    REQUIRE(recovered_object->sla_support_points[2].base_diameter == 0.f);
    REQUIRE(recovered_object->sla_support_points[2].base_height == 0.f);
    REQUIRE(recovered_object->sla_support_points[2].type == SupportPointType::slope);

    // Point with per-point overrides
    REQUIRE(recovered_object->sla_support_points[3].pos == Vec3f{20.0f, 20.0f, 10.0f});
    REQUIRE(recovered_object->sla_support_points[3].head_front_radius == 1.2f);
    REQUIRE(recovered_object->sla_support_points[3].pillar_diameter == 1.8f);
    REQUIRE(recovered_object->sla_support_points[3].base_diameter == 3.5f);
    REQUIRE(recovered_object->sla_support_points[3].base_height == 1.2f);
    REQUIRE(recovered_object->sla_support_points[3].type == SupportPointType::manual_add);

    // Verify drain holes
    REQUIRE(recovered_object->sla_drain_holes.size() == 2);

    REQUIRE(recovered_object->sla_drain_holes[0].pos == Vec3f{12.0f, 22.0f, 0.0f});
    REQUIRE(recovered_object->sla_drain_holes[0].normal == Vec3f{0.0f, 0.0f, 1.0f});
    REQUIRE(recovered_object->sla_drain_holes[0].radius == 3.0f);
    REQUIRE(recovered_object->sla_drain_holes[0].height == 5.0f);
    REQUIRE(recovered_object->sla_drain_holes[0].failed == false);

    REQUIRE(recovered_object->sla_drain_holes[1].pos == Vec3f{20.0f, 30.0f, 0.0f});
    REQUIRE(recovered_object->sla_drain_holes[1].normal == Vec3f{0.0f, 0.0f, 1.0f});
    REQUIRE(recovered_object->sla_drain_holes[1].radius == 2.5f);
    REQUIRE(recovered_object->sla_drain_holes[1].height == 4.0f);
    REQUIRE(recovered_object->sla_drain_holes[1].failed == false);
}

TEST_CASE("ModelSerialize - roundtrip with empty SLA data", "[ModelSerialize][SLA][undo]")
{
    // Create a model with an object that has no SLA data
    Model original_model;

    auto* model_object = original_model.add_object();
    model_object->name = "EmptySLAObject";
    // sla_support_points and sla_drain_holes are empty by default
    model_object->sla_points_status = Slic3r::Domain::SLA::PointsStatus::NoPoints;


    // Serialize
    SerializedData empty_previous;
    SerializedData snapshot = serialize_model(original_model, empty_previous);

    // Deserialize
    Model recovered_model = load_serialized_model(snapshot);

    // Verify
    REQUIRE(recovered_model.objects.size() == 1);
    ModelObject* recovered_object = recovered_model.objects[0];
    REQUIRE(recovered_object->name == "EmptySLAObject");
    REQUIRE(recovered_object->sla_support_points.empty());
    REQUIRE(recovered_object->sla_drain_holes.empty());
    REQUIRE(recovered_object->sla_points_status == Slic3r::Domain::SLA::PointsStatus::NoPoints);
}

TEST_CASE("ModelSerialize - roundtrip with multiple objects", "[ModelSerialize][SLA][undo]")
{
    Model original_model;

    // Object 1: with support points only
    auto* obj1 = original_model.add_object();
    obj1->name = "ObjectWithSupports";
    obj1->sla_support_points.push_back({Vec3f{0.0f, 0.0f, 10.0f}, 0.3f, SupportPointType::manual_add});
    obj1->sla_points_status = Slic3r::Domain::SLA::PointsStatus::AutoGenerated;

    // Object 2: with drain holes only
    auto* obj2 = original_model.add_object();
    obj2->name = "ObjectWithDrainHoles";
    obj2->sla_drain_holes.push_back({
        Vec3f{5.0f, 5.0f, 0.0f},
        Vec3f{0.0f, 0.0f, 1.0f},
        2.0f,
        3.0f,
        true // failed
    });

    // Object 3: with both
    auto* obj3 = original_model.add_object();
    obj3->name = "ObjectWithBoth";
    obj3->sla_support_points.push_back({Vec3f{10.0f, 10.0f, 10.0f}, 0.4f, SupportPointType::slope});
    obj3->sla_drain_holes.push_back({
        Vec3f{15.0f, 15.0f, 0.0f},
        Vec3f{0.0f, 0.0f, 1.0f},
        1.5f,
        2.5f,
        false
    });
    obj3->sla_points_status = Slic3r::Domain::SLA::PointsStatus::UserModified;


    // Serialize
    SerializedData empty_previous;
    SerializedData snapshot = serialize_model(original_model, empty_previous);

    // Deserialize
    Model recovered_model = load_serialized_model(snapshot);

    // Verify
    REQUIRE(recovered_model.objects.size() == 3);

    // Object 1
    ModelObject* r1 = recovered_model.objects[0];
    REQUIRE(r1->name == "ObjectWithSupports");
    REQUIRE(r1->sla_support_points.size() == 1);
    REQUIRE(r1->sla_support_points[0].pos == Vec3f{0.0f, 0.0f, 10.0f});
    REQUIRE(r1->sla_support_points[0].head_front_radius == 0.3f);
    REQUIRE(r1->sla_support_points[0].pillar_diameter == 0.f);
    REQUIRE(r1->sla_support_points[0].base_diameter == 0.f);
    REQUIRE(r1->sla_support_points[0].base_height == 0.f);
    REQUIRE(r1->sla_drain_holes.empty());
    REQUIRE(r1->sla_points_status == Slic3r::Domain::SLA::PointsStatus::AutoGenerated);

    // Object 2
    ModelObject* r2 = recovered_model.objects[1];
    REQUIRE(r2->name == "ObjectWithDrainHoles");
    REQUIRE(r2->sla_support_points.empty());
    REQUIRE(r2->sla_drain_holes.size() == 1);
    REQUIRE(r2->sla_drain_holes[0].failed == true);

    // Object 3
    ModelObject* r3 = recovered_model.objects[2];
    REQUIRE(r3->name == "ObjectWithBoth");
    REQUIRE(r3->sla_support_points.size() == 1);
    REQUIRE(r3->sla_support_points[0].pos == Vec3f{10.0f, 10.0f, 10.0f});
    REQUIRE(r3->sla_support_points[0].head_front_radius == 0.4f);
    REQUIRE(r3->sla_support_points[0].pillar_diameter == 0.f);
    REQUIRE(r3->sla_support_points[0].base_diameter == 0.f);
    REQUIRE(r3->sla_support_points[0].base_height == 0.f);
    REQUIRE(r3->sla_drain_holes.size() == 1);
    REQUIRE(r3->sla_points_status == Slic3r::Domain::SLA::PointsStatus::UserModified);
}