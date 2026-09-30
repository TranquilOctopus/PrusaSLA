#include <catch2/catch_test_macros.hpp>

#include "Slic3r/App/Plater/SlaSupportPreviewService.hpp"
#include "Slic3r/Domain/SLA/SupportPoint.hpp"
#include "Slic3r/Domain/Types.hpp"

#include <unordered_map>

using Slic3r::App::Plater::SlaSupportPreviewCandidate;
using Slic3r::App::Plater::SlaSupportPreviewDiff;
using Slic3r::App::Plater::SlaSupportPreviewKey;
using Slic3r::App::Plater::diff_sla_support_previews;
using Slic3r::App::Plater::hash_support_points;
using Slic3r::App::Plater::make_sla_support_preview_key;
using Slic3r::Domain::ObjectID;
using Slic3r::Domain::SLA::SupportPoint;
using Slic3r::Domain::SLA::SupportPointType;
using Slic3r::Domain::SLA::SupportPoints;
using Slic3r::Domain::Transform3d;
using Slic3r::Domain::Vec3f;

namespace {

SupportPoints make_points(std::size_t count)
{
    SupportPoints points;
    points.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        points.push_back(SupportPoint{Vec3f{float(i), 1.f, 2.f}, 0.4f, SupportPointType::slope});
    }
    return points;
}

Transform3d translated(double x, double y, double z)
{
    Transform3d trafo = Transform3d::Identity();
    trafo.pretranslate(Slic3r::Domain::Vec3d{x, y, z});
    return trafo;
}

SlaSupportPreviewCandidate candidate(ObjectID id, bool wants, SlaSupportPreviewKey key = {})
{
    SlaSupportPreviewCandidate result;
    result.object_id     = id;
    result.wants_preview = wants;
    result.key           = wants ? key : SlaSupportPreviewKey{};
    return result;
}

} // namespace

TEST_CASE("SlaSupportPreviewService - hash_support_points", "[SlaSupportPreviewService]")
{
    SECTION("An empty point list hashes to a stable value")
    {
        REQUIRE(hash_support_points({}) == hash_support_points({}));
    }

    SECTION("Equal point lists hash equally")
    {
        REQUIRE(hash_support_points(make_points(5)) == hash_support_points(make_points(5)));
    }

    SECTION("A moved point changes the hash")
    {
        SupportPoints a = make_points(3);
        SupportPoints b = make_points(3);
        b[1].pos.x() += 0.001f;

        REQUIRE(hash_support_points(a) != hash_support_points(b));
    }

    SECTION("A changed head radius changes the hash")
    {
        SupportPoints a = make_points(3);
        SupportPoints b = make_points(3);
        b[2].head_front_radius = 0.8f;

        REQUIRE(hash_support_points(a) != hash_support_points(b));
    }

    SECTION("More points change the hash")
    {
        REQUIRE(hash_support_points(make_points(3)) != hash_support_points(make_points(4)));
    }

    SECTION("A changed pillar or base size changes the hash")
    {
        SupportPoints a = make_points(3);
        SupportPoints b = make_points(3);
        b[2].pillar_diameter = 1.8f;
        REQUIRE(hash_support_points(a) != hash_support_points(b));

        SupportPoints c = make_points(3);
        c[1].base_diameter = 4.f;
        c[1].base_height   = 1.f;
        REQUIRE(hash_support_points(a) != hash_support_points(c));
    }

    SECTION("A changed tip shape or stem geometry changes the hash")
    {
        // These four reach the support tree mesh since M2.16b, so a project that
        // carries them has to refresh the preview geometry.
        SupportPoints a = make_points(3);

        SupportPoints shape = make_points(3);
        shape[2].tip_shape = SupportPoint::TipShape::Cone;
        REQUIRE(hash_support_points(a) != hash_support_points(shape));

        SupportPoints knot = make_points(3);
        knot[1].knot_radius = 0.8f;
        REQUIRE(hash_support_points(a) != hash_support_points(knot));

        SupportPoints sides = make_points(3);
        sides[0].stem_sides = 6;
        REQUIRE(hash_support_points(a) != hash_support_points(sides));

        SupportPoints taper = make_points(3);
        taper[2].stem_taper = 0.2f;
        REQUIRE(hash_support_points(a) != hash_support_points(taper));
    }
}

TEST_CASE("SlaSupportPreviewService - make_sla_support_preview_key", "[SlaSupportPreviewService]")
{
    const SupportPoints points = make_points(4);

    SECTION("The same inputs give the same key")
    {
        const SlaSupportPreviewKey a =
            make_sla_support_preview_key(points, translated(1, 2, 3), 11, 22, true);
        const SlaSupportPreviewKey b =
            make_sla_support_preview_key(points, translated(1, 2, 3), 11, 22, true);
        REQUIRE(a == b);
    }

    SECTION("A moved instance changes the key")
    {
        const SlaSupportPreviewKey a =
            make_sla_support_preview_key(points, translated(1, 2, 3), 11, 22, true);
        const SlaSupportPreviewKey b =
            make_sla_support_preview_key(points, translated(1, 2, 4), 11, 22, true);
        REQUIRE(!(a == b));
    }

    SECTION("A changed printer configuration changes the key")
    {
        const SlaSupportPreviewKey a =
            make_sla_support_preview_key(points, translated(0, 0, 0), 11, 22, true);
        const SlaSupportPreviewKey b =
            make_sla_support_preview_key(points, translated(0, 0, 0), 12, 22, true);
        REQUIRE(!(a == b));
    }

    SECTION("A changed object configuration changes the key")
    {
        const SlaSupportPreviewKey a =
            make_sla_support_preview_key(points, translated(0, 0, 0), 11, 22, true);
        const SlaSupportPreviewKey b =
            make_sla_support_preview_key(points, translated(0, 0, 0), 11, 23, true);
        REQUIRE(!(a == b));
    }

    SECTION("Turned off supports change the key")
    {
        const SlaSupportPreviewKey a =
            make_sla_support_preview_key(points, translated(0, 0, 0), 11, 22, true);
        const SlaSupportPreviewKey b =
            make_sla_support_preview_key(points, translated(0, 0, 0), 11, 22, false);
        REQUIRE(!(a == b));
    }

    SECTION("The key carries the point count")
    {
        const SlaSupportPreviewKey key =
            make_sla_support_preview_key(points, translated(0, 0, 0), 1, 2, true);
        REQUIRE(key.point_count == points.size());
    }
}

TEST_CASE("SlaSupportPreviewService - diff_sla_support_previews", "[SlaSupportPreviewService]")
{
    const SlaSupportPreviewKey key_a = make_sla_support_preview_key(make_points(3), translated(0, 0, 0), 1, 2, true);
    const SlaSupportPreviewKey key_b = make_sla_support_preview_key(make_points(9), translated(5, 0, 0), 1, 2, true);

    SECTION("A new object is built")
    {
        std::unordered_map<std::size_t, SlaSupportPreviewKey> current;
        const SlaSupportPreviewDiff diff = diff_sla_support_previews({candidate(ObjectID{7}, true, key_a)}, current);

        REQUIRE(diff.to_recompute == std::vector<ObjectID>{ObjectID{7}});
        REQUIRE(diff.to_remove.empty());
        REQUIRE(current.at(7) == key_a);
    }

    SECTION("An unchanged object is left alone")
    {
        std::unordered_map<std::size_t, SlaSupportPreviewKey> current{{7, key_a}};
        const SlaSupportPreviewDiff diff = diff_sla_support_previews({candidate(ObjectID{7}, true, key_a)}, current);

        REQUIRE(diff.to_recompute.empty());
        REQUIRE(diff.to_remove.empty());
        REQUIRE(current.at(7) == key_a);
    }

    SECTION("A changed key rebuilds the object")
    {
        std::unordered_map<std::size_t, SlaSupportPreviewKey> current{{7, key_a}};
        const SlaSupportPreviewDiff diff = diff_sla_support_previews({candidate(ObjectID{7}, true, key_b)}, current);

        REQUIRE(diff.to_recompute == std::vector<ObjectID>{ObjectID{7}});
        REQUIRE(diff.to_remove.empty());
        REQUIRE(current.at(7) == key_b);
    }

    SECTION("An object without points loses its preview")
    {
        std::unordered_map<std::size_t, SlaSupportPreviewKey> current{{7, key_a}};
        const SlaSupportPreviewDiff diff = diff_sla_support_previews({candidate(ObjectID{7}, false)}, current);

        REQUIRE(diff.to_recompute.empty());
        REQUIRE(diff.to_remove == std::vector<ObjectID>{ObjectID{7}});
        REQUIRE(current.find(7) == current.end());
    }

    SECTION("An object that lost its preview is removed even with another key")
    {
        std::unordered_map<std::size_t, SlaSupportPreviewKey> current{{7, key_a}};
        const SlaSupportPreviewDiff diff = diff_sla_support_previews({candidate(ObjectID{7}, false, key_b)}, current);

        REQUIRE(diff.to_recompute.empty());
        REQUIRE(diff.to_remove == std::vector<ObjectID>{ObjectID{7}});
        REQUIRE(current.empty());
    }

    SECTION("An object that left the plate loses its preview")
    {
        std::unordered_map<std::size_t, SlaSupportPreviewKey> current{{7, key_a}, {8, key_b}};
        const SlaSupportPreviewDiff diff = diff_sla_support_previews({candidate(ObjectID{7}, true, key_a)}, current);

        REQUIRE(diff.to_recompute.empty());
        REQUIRE(diff.to_remove == std::vector<ObjectID>{ObjectID{8}});
        REQUIRE(current.size() == 1);
    }

    SECTION("An empty plate clears everything")
    {
        std::unordered_map<std::size_t, SlaSupportPreviewKey> current{{7, key_a}};
        const SlaSupportPreviewDiff diff = diff_sla_support_previews({}, current);

        REQUIRE(diff.to_recompute.empty());
        REQUIRE(diff.to_remove == std::vector<ObjectID>{ObjectID{7}});
        REQUIRE(current.empty());
    }
}
