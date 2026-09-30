// The raft edge taper (raft_edge_taper) is a bevel on the top edge of the raft's outer wall: the
// rim is pulled in over the top of the wall, which leaves a thin lip a spatula can get under, so
// the raft can be pried off the build plate instead of having to be flexed or cut off. It is not
// the raft slope (pad_wall_slope): that tilts the whole wall over the whole raft height, so it
// cannot make a local bevel. The default of 0 leaves the sharp edge, which is the raft of today.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>

#include "sla_test_utils.hpp"

#include "Slic3r/Domain/ConfigBoxesSLA.hpp"
#include "Slic3r/Domain/ConfigDefsSLA.hpp"
#include "Slic3r/Domain/FullConfigSLA.hpp"
#include "Slic3r/Domain/SLA/RaftPreset.hpp"

#include "libslic3r/ConfigViews.hpp"
#include "libslic3r/SLAPrint.hpp"

namespace {

// A config view over the given object settings, the way the engine builds one for a print object.
Slic3r::SLAPrintObjectConfigView make_config_view(Slic3r::Domain::SLAObjectSettings& settings)
{
    Slic3r::Domain::FullConfigSLAPtr full{std::make_shared<const Slic3r::Domain::FullConfigSLA>(
        Slic3r::Domain::FullConfigSLA::defaults())};
    Slic3r::Domain::PartialObjectConfigSLAPtr object{
        std::make_shared<const Slic3r::Domain::PartialObjectConfigSLA>(settings, full->hw_config())};
    return Slic3r::SLAPrintObjectConfigView{full, object};
}

// The XY footprint of the raft vertices that sit on one of its two ends, in mm.
struct Footprint {
    double min_x = 0.;
    double max_x = 0.;
    double min_y = 0.;
    double max_y = 0.;
    size_t vertices = 0;
};

// The raft is built with its top face at the top of the mesh and its footprint on the plate at
// the bottom, so the two ends are the mesh bounding box.
struct RaftEnds {
    Footprint top;
    Footprint bottom;
};

Footprint footprint_at_z(const indexed_triangle_set& raft, double z, double tolerance = 0.01)
{
    Footprint fp;

    for (const auto& v : raft.vertices) {
        if (std::abs(double(v.z()) - z) > tolerance)
            continue;

        if (fp.vertices == 0) {
            fp.min_x = fp.max_x = v.x();
            fp.min_y = fp.max_y = v.y();
        } else {
            fp.min_x = std::min(fp.min_x, double(v.x()));
            fp.max_x = std::max(fp.max_x, double(v.x()));
            fp.min_y = std::min(fp.min_y, double(v.y()));
            fp.max_y = std::max(fp.max_y, double(v.y()));
        }
        ++fp.vertices;
    }

    return fp;
}

RaftEnds raft_ends(const Slic3r::Domain::TriangleMesh& raft)
{
    const Slic3r::Domain::BoundingBox3d bb = raft.bounding_box();

    return {footprint_at_z(raft.its, bb.max.z()), footprint_at_z(raft.its, bb.min.z())};
}

// A straight walled raft with no cavity, so the only thing that can make its top footprint
// smaller than its bottom footprint is the edge taper.
sla::PadConfig straight_walled_raft(double edge_taper_mm)
{
    sla::PadConfig cfg;
    cfg.wall_thickness_mm = 2.;
    cfg.wall_height_mm = 0.;
    cfg.wall_slope = PI / 2;
    cfg.brim_size_mm = 1.6;
    cfg.edge_taper_mm = edge_taper_mm;
    return cfg;
}

} // namespace

TEST_CASE("The raft edge taper reaches the pad config", "[SLA][RaftEdgeTaper]")
{
    Slic3r::Domain::SLAObjectSettings settings;
    settings.overrides.set("raft_type", Slic3r::Domain::sla::RaftType::Full);

    SECTION("a config without the key gets the sharp edge it has always had")
    {
        CHECK(Slic3r::make_pad_cfg(make_config_view(settings)).edge_taper_mm == Approx(0.));
    }

    SECTION("the key is what the raft is built with")
    {
        settings.overrides.set("raft_edge_taper", 1.5);
        CHECK(Slic3r::make_pad_cfg(make_config_view(settings)).edge_taper_mm == Approx(1.5));
    }
}

TEST_CASE("The raft footprint at its top is the taper smaller than at its bottom",
          "[SLA][RaftEdgeTaper]")
{
    const double taper_mm = 1.;

    PadByproducts sharp, bevelled;
    test_pad("20mm_cube.obj", straight_walled_raft(0.), sharp);
    test_pad("20mm_cube.obj", straight_walled_raft(taper_mm), bevelled);

    const RaftEnds sharp_ends = raft_ends(sharp.mesh);
    const RaftEnds bevelled_ends = raft_ends(bevelled.mesh);
    REQUIRE(bevelled_ends.top.vertices > 2);
    REQUIRE(bevelled_ends.bottom.vertices > 2);

    // Without a taper the two ends are the same size, which is the raft of today.
    CHECK(sharp_ends.bottom.max_x - sharp_ends.top.max_x == Approx(0.).margin(0.001));
    CHECK(sharp_ends.top.min_x - sharp_ends.bottom.min_x == Approx(0.).margin(0.001));

    // The taper takes the same amount off each side of the top.
    CHECK(bevelled_ends.bottom.max_x - bevelled_ends.top.max_x == Approx(taper_mm).margin(0.01));
    CHECK(bevelled_ends.top.min_x - bevelled_ends.bottom.min_x == Approx(taper_mm).margin(0.01));
    CHECK(bevelled_ends.bottom.max_y - bevelled_ends.top.max_y == Approx(taper_mm).margin(0.01));
    CHECK(bevelled_ends.top.min_y - bevelled_ends.bottom.min_y == Approx(taper_mm).margin(0.01));

    // The footprint on the build plate, which is what the raft is stuck down with, did not move.
    CHECK(bevelled_ends.bottom.max_x - sharp_ends.bottom.max_x == Approx(0.).margin(0.001));
    CHECK(bevelled_ends.bottom.max_y - sharp_ends.bottom.max_y == Approx(0.).margin(0.001));
}
