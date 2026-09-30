#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <algorithm>
#include <string>
#include <vector>

#include "Slic3r/Domain/ExPolygon.hpp"
#include "Slic3r/Domain/ObjectID.hpp"
#include "Slic3r/Domain/Point.hpp"
#include "Slic3r/Biz/Algorithms/Scaling.hpp"
#include "libslic3r/SLA/ObjectResinUse.hpp"

using namespace Slic3r;
using Catch::Approx;
using Slic3r::Biz::Algorithms::Scaling::scaled;
using Slic3r::Biz::Algorithms::Scaling::SCALING_FACTOR;
using Slic3r::Domain::ExPolygon;
using Slic3r::Domain::ExPolygons;
using Slic3r::Domain::ObjectID;
using Slic3r::Domain::Point;
using Slic3r::SLA::object_resin_use;
using Slic3r::SLA::ObjectLayerUse;
using Slic3r::SLA::ObjectResinUse;

namespace {

// The squared scaling factor that turns the scaled coordinates of the plate into mm², which is what
// merge_slices_and_eval_stats() hands to object_resin_use().
const double scaling_sq = SCALING_FACTOR * SCALING_FACTOR;

const double layer_height_mm = 0.05;

/// A square of @p size_mm on a layer.
ExPolygon square(double size_mm, double center_x = 0., double center_y = 0.)
{
    const double half = size_mm / 2.;
    ExPolygon poly;
    poly.contour.points = {
        Point(scaled(center_x - half), scaled(center_y - half)),
        Point(scaled(center_x + half), scaled(center_y - half)),
        Point(scaled(center_x + half), scaled(center_y + half)),
        Point(scaled(center_x - half), scaled(center_y + half)),
        Point(scaled(center_x - half), scaled(center_y - half)),
    };
    return poly;
}

ObjectID object_id(uint16_t id)
{
    ObjectID out;
    out.id = id;
    return out;
}

/**
 * @brief One entry of one model on one layer, the way the layer loop of merge_slices_and_eval_stats
 * hands it over: the body, the support tree without the raft, and the raft.
 *
 * The raft stands where it stands on the plate, which is not where the body stands: two models that
 * share one raft hand over the same outline twice.
 */
ObjectLayerUse layer_of(
    size_t layer_index,
    const ObjectID& id,
    const std::string& name,
    double body_size_mm,
    double body_center_x,
    double support_size_mm = 0.,
    double raft_size_mm    = 0.,
    double raft_center_x   = 0.
)
{
    ObjectLayerUse use;
    use.layer_index     = layer_index;
    use.layer_height_mm = layer_height_mm;
    use.object_id       = id;
    use.name            = name;
    if (body_size_mm > 0.)
        use.model.emplace_back(square(body_size_mm, body_center_x));
    if (support_size_mm > 0.)
        use.support.emplace_back(square(support_size_mm, body_center_x));
    if (raft_size_mm > 0)
        use.raft.emplace_back(square(raft_size_mm, raft_center_x));
    return use;
}

/// A column of @p layers of one model: the same body on every layer.
std::vector<ObjectLayerUse> column(
    const ObjectID& id,
    const std::string& name,
    double body_size_mm,
    double body_center_x,
    size_t layers
)
{
    std::vector<ObjectLayerUse> out;
    for (size_t i = 0; i < layers; ++i)
        out.emplace_back(layer_of(i, id, name, body_size_mm, body_center_x));
    return out;
}

const ObjectResinUse& find(const std::vector<ObjectResinUse>& use, const ObjectID& id)
{
    const auto it = std::find_if(
        use.begin(),
        use.end(),
        [&id](const ObjectResinUse& u) { return u.object_id == id; }
    );
    if (it == use.end()) {
        FAIL("the model is not in the result");
    }
    return *it;
}

double total_volume(const std::vector<ObjectResinUse>& use)
{
    double volume = 0.;
    for (const ObjectResinUse& u : use)
        volume += u.volume_mm3();
    return volume;
}

} // namespace

TEST_CASE("object_resin_use - the models add up to the print", "[object_resin_use]")
{
    SECTION("nothing sliced")
    {
        REQUIRE(object_resin_use({}, scaling_sq).empty());
        // A scaling factor that would turn every area into nothing is refused, not used.
        REQUIRE(object_resin_use(column(object_id(1), "cube", 10., 0., 4), 0.).empty());
    }

    SECTION("one model on the plate")
    {
        // 10 x 10 mm on 4 layers of 0.05 mm.
        const std::vector<ObjectResinUse> use =
            object_resin_use(column(object_id(1), "cube", 10., 0., 4), scaling_sq);
        REQUIRE(use.size() == 1);
        CHECK(use.front().name == "cube");
        CHECK(use.front().model_volume_mm3 == Approx(100. * 0.05 * 4.));
        CHECK(use.front().support_volume_mm3 == Approx(0.));
        CHECK(use.front().raft_volume_mm3 == Approx(0.));
        CHECK(use.front().footprint_mm2 == Approx(100.));
        CHECK(use.front().volume_mm3() == Approx(20.));
    }

    SECTION("two cubes of different size keep the ratio of their volumes")
    {
        const ObjectID small = object_id(1);
        const ObjectID big   = object_id(2);

        std::vector<ObjectLayerUse> layers       = column(small, "small", 10., -10., 4);
        const std::vector<ObjectLayerUse> bigger = column(big, "big", 20., 10., 4);
        layers.insert(layers.end(), bigger.begin(), bigger.end());

        const std::vector<ObjectResinUse> use = object_resin_use(layers, scaling_sq);
        REQUIRE(use.size() == 2);

        // The order of the plate is kept: the small one is sliced first.
        CHECK(use.front().object_id == small);
        CHECK(use.back().object_id == big);

        CHECK(find(use, big).volume_mm3() / find(use, small).volume_mm3() == Approx(4.));
        CHECK(total_volume(use) == Approx(20. + 80.));
    }

    SECTION("a body standing on another model is counted once")
    {
        // The big cube is 20 x 20 and the small one of 10 x 10 stands on top of it, so the layer
        // holds 400 mm2 of resin and not 500.
        const ObjectID big   = object_id(1);
        const ObjectID small = object_id(2);

        std::vector<ObjectLayerUse> layers{layer_of(0, big, "big", 20., 0.)};
        layers.emplace_back(layer_of(1, small, "small", 10., 0.));

        const std::vector<ObjectResinUse> use = object_resin_use(layers, scaling_sq);
        REQUIRE(use.size() == 2);
        CHECK(find(use, big).model_volume_mm3 == Approx(400. * layer_height_mm));
        CHECK(find(use, small).model_volume_mm3 == Approx(0.));
        CHECK(total_volume(use) == Approx(400. * layer_height_mm));
    }

    SECTION("the support tree around a body is not resin twice")
    {
        // A model with a body standing in the middle of its own support tree: the tree is counted
        // around it, the plate counts it the same way.
        const ObjectID vase = object_id(1);

        const std::vector<ObjectLayerUse> layers{layer_of(0, vase, "vase", 10., 0., 20.)};

        const std::vector<ObjectResinUse> use = object_resin_use(layers, scaling_sq);
        REQUIRE(use.size() == 1);
        CHECK(find(use, vase).model_volume_mm3 == Approx(100. * layer_height_mm));
        CHECK(find(use, vase).support_volume_mm3 == Approx(300. * layer_height_mm));
    }
}

TEST_CASE("object_resin_use - the raft is shared by footprint", "[object_resin_use]")
{
    SECTION("one raft under two models is counted once and split by footprint")
    {
        // Two models standing in one raft of 40 x 20 mm, which both of them hands over whole: that
        // is what a shared pad looks like in the slices.
        const ObjectID small = object_id(1);
        const ObjectID big   = object_id(2);

        std::vector<ObjectLayerUse> layers{
            layer_of(0, small, "small", 10., -10., 0., 40.),
            layer_of(0, big, "big", 20., 10., 0., 40.)
        };

        const std::vector<ObjectResinUse> use = object_resin_use(layers, scaling_sq);
        REQUIRE(use.size() == 2);

        // The bodies stand in the raft, so what is left of it is the 800 mm2 of it less their own
        // 100 and 400.
        const double raft_mm3 = 300. * layer_height_mm;
        CHECK(
            find(use, small).raft_volume_mm3 + find(use, big).raft_volume_mm3 == Approx(raft_mm3)
        );

        // 100 mm2 of footprint against 400, so a quarter of the raft and three quarters of it.
        CHECK(find(use, small).footprint_mm2 == Approx(100.));
        CHECK(find(use, big).footprint_mm2 == Approx(400.));
        CHECK(find(use, small).raft_volume_mm3 == Approx(raft_mm3 / 4.));
        CHECK(find(use, big).raft_volume_mm3 == Approx(raft_mm3 * 3. / 4.));
        CHECK(total_volume(use) == Approx(raft_mm3 + 500. * layer_height_mm));
    }

    SECTION("a raft under one model is all of it")
    {
        const ObjectID cube = object_id(1);
        const std::vector<ObjectLayerUse> layers{layer_of(0, cube, "cube", 10., 0., 0., 30.)};

        const std::vector<ObjectResinUse> use = object_resin_use(layers, scaling_sq);
        REQUIRE(use.size() == 1);
        CHECK(find(use, cube).raft_volume_mm3 == Approx((900. - 100.) * layer_height_mm));
        CHECK(find(use, cube).volume_mm3() == Approx(900. * layer_height_mm));
    }

    SECTION("the widest layer is the footprint, not the first one")
    {
        // A model that tapers: its first layer is a 4 x 4 point, the widest one is 20 x 20.
        const ObjectID vase = object_id(1);
        std::vector<ObjectLayerUse>
            layers{layer_of(0, vase, "vase", 4., 0., 0., 30.), layer_of(1, vase, "vase", 20., 0.)};

        const std::vector<ObjectResinUse> use = object_resin_use(layers, scaling_sq);
        REQUIRE(use.size() == 1);
        CHECK(find(use, vase).footprint_mm2 == Approx(400.));
        CHECK(find(use, vase).model_volume_mm3 == Approx((16. + 400.) * layer_height_mm));
        CHECK(find(use, vase).raft_volume_mm3 == Approx((900. - 16.) * layer_height_mm));
    }

    SECTION("a model with no body of its own takes an even share")
    {
        // Nothing on the plate has a footprint, so the two rafts are split evenly. They stand far
        // enough apart to be two rafts and not one.
        const ObjectID a = object_id(1);
        const ObjectID b = object_id(2);
        std::vector<ObjectLayerUse> layers{
            layer_of(0, a, "a", 0., -25., 10., 20., -25.),
            layer_of(0, b, "b", 0., 25., 10., 20., 25.)
        };

        const std::vector<ObjectResinUse> use = object_resin_use(layers, scaling_sq);
        REQUIRE(use.size() == 2);
        CHECK(find(use, a).raft_volume_mm3 == Approx(400. * layer_height_mm));
        CHECK(find(use, b).raft_volume_mm3 == Approx(400. * layer_height_mm));
        CHECK(find(use, a).footprint_mm2 == Approx(0.));
        // The two support trees and the two rafts, all of it added up.
        CHECK(total_volume(use) == Approx(1000. * layer_height_mm));
    }

    SECTION("the models add up to the plate, the raft of a shared pad included")
    {
        // Three layers, one raft of 20 x 20 under both models, which stand in it: 4 mm2 of footprint
        // against 16, so a fifth of everything the raft holds and four fifths.
        const ObjectID small = object_id(1);
        const ObjectID big   = object_id(2);

        std::vector<ObjectLayerUse> layers;
        for (size_t i = 0; i < 3; ++i) {
            layers.emplace_back(layer_of(i, small, "small", 2., -5., 0., 20.));
            layers.emplace_back(layer_of(i, big, "big", 4., 5., 0., 20.));
        }

        const std::vector<ObjectResinUse> use = object_resin_use(layers, scaling_sq);
        REQUIRE(use.size() == 2);

        const double bodies = 20. * 3. * layer_height_mm;
        const double raft   = (400. - 20.) * 3. * layer_height_mm;
        CHECK(find(use, small).model_volume_mm3 == Approx(bodies / 5.));
        CHECK(find(use, big).model_volume_mm3 == Approx(bodies * 4. / 5.));
        CHECK(find(use, small).raft_volume_mm3 == Approx(raft / 5.));
        CHECK(find(use, big).raft_volume_mm3 == Approx(raft * 4. / 5.));
        CHECK(total_volume(use) == Approx(bodies + raft));
    }

    SECTION("the entries may come in any order")
    {
        const ObjectID a = object_id(1);
        const ObjectID b = object_id(2);
        std::vector<ObjectLayerUse> layers{column(b, "b", 10., 0., 2), column(a, "a", 20., 30., 2)};
        std::reverse(layers.begin(), layers.end());

        const std::vector<ObjectResinUse> use = object_resin_use(layers, scaling_sq);
        REQUIRE(use.size() == 2);
        // The order of the plate is the order the models first show up in it, not the order of their
        // ids.
        CHECK(use.front().name == "b");
        CHECK(use.back().name == "a");
        CHECK(
            total_volume(use) == Approx(2. * 100. * layer_height_mm + 2. * 400. * layer_height_mm)
        );
    }
}
