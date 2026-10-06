#include <catch2/catch_test_macros.hpp>

#include "Slic3r/Biz/Scene/BedTracking.hpp"
#include "Slic3r/Biz/Algorithms/Bed.hpp"
#include "Slic3r/Biz/Algorithms/BoundingBox.hpp"
#include "Slic3r/Domain/BedContainer.hpp"
#include "Slic3r/Domain/BedInstance.hpp"

#include <memory>

using Slic3r::Biz::BedTracking;
namespace BoundingBox = Slic3r::Biz::Algorithms::BoundingBox;
using Slic3r::Biz::Algorithms::Bed::bed_contour_as_its;
using Slic3r::Biz::Algorithms::Bed::BedContainmentState;
using Slic3r::Domain::Bed;
using Slic3r::Domain::BedContainer;
using Slic3r::Domain::BedCreationData;
using Slic3r::Domain::BedInstance;
using Slic3r::Domain::BedType;
using Slic3r::Domain::BoundingBox2d;
using Slic3r::Domain::Vec2ds;

namespace {

Bed make_rectangular_bed(const Vec2ds& contour)
{
    return Bed::create(BedCreationData{BedType::Rectangle, contour, bed_contour_as_its(contour), 250.0f});
}

const Vec2ds small_contour = {{-50, -50}, {50, -50}, {50, 50}, {-50, 50}};
const Vec2ds large_contour = {{-500, -500}, {500, -500}, {500, 500}, {-500, 500}};

// A 10 x 10 mm square at the origin, which both beds of the case contain.
const Vec2ds middle_hull = {{-5, -5}, {5, -5}, {5, 5}, {-5, 5}};

// A 10 x 10 mm square 200 mm from the origin, which only the large bed contains. Its answer is
// therefore decided by the contour the acceleration structure was built over: the structure of the
// small contour finds no bed under it, the structure of the large contour finds one.
const Vec2ds far_hull = {{-205, -205}, {-195, -205}, {-195, -195}, {-205, -195}};

} // namespace

// M0.15: the cached bed contour's AABBMesh is keyed by bed id, and an AABBMesh is a view on the
// contour mesh of a bed rather than a copy of it, so an entry may only be used while the bed it was
// built from is the bed that is alive. An id alone does not prove that: BedContainer::copy() copies
// the beds by value and keeps their ids, which is what the copy constructor of a project does, and
// the two beds are then different objects with different contour meshes. An entry that names
// another bed than the one being asked about is therefore a stale one and is rebuilt.
TEST_CASE("BedTracking - a cached bed contour is rebuilt when the bed id names another bed",
          "[BedTracking][bed-containment]")
{
    BedTracking tracking;

    BedContainer original;
    original.beds().push_back(std::make_unique<Bed>(make_rectangular_bed(small_contour)));
    REQUIRE(original.beds_count() == 1u);
    const Bed& small_bed = *original.beds().front();

    // The same id, a different bed object, a different contour mesh. This is what a copied project
    // puts on the workbench next to the project it was copied from.
    BedContainer copied = original.copy();
    REQUIRE(copied.beds_count() == 1u);
    Bed& large_bed = *copied.beds().front();
    REQUIRE(large_bed.id().id == small_bed.id().id);
    REQUIRE(&large_bed != &small_bed);

    const BoundingBox2d middle_box = BoundingBox::construct(middle_hull);
    const BoundingBox2d far_box    = BoundingBox::construct(far_hull);

    const BedInstance small_instance{small_bed};
    const BedInstance large_instance{large_bed};

    SECTION("A bed is answered from its own contour")
    {
        CHECK(tracking.check_containment_2d(small_bed, small_instance, middle_box, middle_hull)
              == BedContainmentState::Inside);
        CHECK(tracking.check_containment_2d(large_bed, large_instance, far_box, far_hull)
              == BedContainmentState::Inside);
    }

    SECTION("A far point is not answered from the contour of the other bed that has the same id")
    {
        // The small bed is asked about first, so its contour is what the entry under this id holds.
        CHECK(tracking.check_containment_2d(small_bed, small_instance, middle_box, middle_hull)
              == BedContainmentState::Inside);

        // The same id now names the large bed, whose contour the cached structure was not built
        // over. The far point is inside the large bed, so only a structure built over its contour
        // can say so.
        CHECK(tracking.check_containment_2d(large_bed, large_instance, far_box, far_hull)
              == BedContainmentState::Inside);
    }

    SECTION("A bed asked about before is still answered from its own contour afterwards")
    {
        CHECK(tracking.check_containment_2d(small_bed, small_instance, middle_box, middle_hull)
              == BedContainmentState::Inside);
        CHECK(tracking.check_containment_2d(large_bed, large_instance, far_box, far_hull)
              == BedContainmentState::Inside);

        // The entry belongs to the large bed now; asking about the small one rebuilds it again.
        CHECK(tracking.check_containment_2d(small_bed, small_instance, middle_box, middle_hull)
              == BedContainmentState::Inside);
        CHECK(tracking.check_containment_2d(large_bed, large_instance, far_box, far_hull)
              == BedContainmentState::Inside);
    }
}