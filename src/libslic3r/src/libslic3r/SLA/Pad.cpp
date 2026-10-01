#include <libslic3r/SLA/Pad.hpp>
#include <libslic3r/SLA/SpatIndex.hpp>
//#include <libslic3r/SLA/Contour3D.hpp>
#include <libslic3r/TriangleMeshSlicer.hpp>
#include <Slic3r/Log.hpp>
#include <algorithm>
#include <utility>
#include <cstdlib>

#include "Slic3r/Biz/Algorithms/Polygon.hpp"
#include "ConcaveHull.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "Slic3r/Biz/Algorithms/Tesselate.hpp"
#include "libslic3r/MTUtils.hpp"
#include "libslic3r/TriangulateWall.hpp"
#include "libslic3r/I18N_private.hpp"
#include "admesh/stl.h"
#include "libslic3r/Point.hpp"
#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"
#include "libslic3r/libslic3r.h"

#ifndef NDEBUG
#include "Slic3r/Biz/Algorithms/SVG.hpp"
#endif

using namespace Slic3r::Biz;
using Slic3r::Domain::Index3;
using Slic3r::Domain::its_merge;

namespace Slic3r { namespace sla {

using Slic3r::Biz::Algorithms::Tesselate::triangulate_expolygon_3d;
using Slic3r::Biz::Algorithms::Tesselate::triangulate_expolygons_3d;
using Slic3r::Biz::Algorithms::Tesselate::NORMALS_UP;
using Slic3r::Biz::Algorithms::Tesselate::NORMALS_DOWN;

namespace {

indexed_triangle_set walls(
    const Polygon &lower,
    const Polygon &upper,
    double         lower_z_mm,
    double         upper_z_mm)
{
    indexed_triangle_set w;
    triangulate_wall(w.vertices, w.indices, lower, upper, lower_z_mm,
                     upper_z_mm);
    
    return w;
}

// Same as walls() but with identical higher and lower polygons.
inline indexed_triangle_set straight_walls(const Polygon &plate,
                                           double         lo_z,
                                           double         hi_z)
{
    using Slic3r::Biz::Algorithms::Tesselate::wall_strip;
    return wall_strip(plate, hi_z, lo_z); //walls(plate, plate, lo_z, hi_z);
}

// Function to cut tiny connector cavities for a given polygon. The input poly
// will be offsetted by "padding" and small rectangle shaped cavities will be
// inserted along the perimeter in every "stride" distance. The stick rectangles
// will have a with about "stick_width". The input dimensions are in world
// measure, not the scaled clipper units.
void breakstick_holes(Points& pts,
                      double padding,
                      double stride,
                      double stick_width,
                      double penetration)
{
    if(stride <= EPSILON || stick_width <= EPSILON || padding <= EPSILON)
        return;

    // Biz::Algorithms::SVG::SVG svg("bridgestick_plate.svg");
    // svg.draw(poly);

    // The connector stick will be a small rectangle with dimensions
    // stick_width x (penetration + padding) to have some penetration
    // into the input polygon.

    Points out;
    out.reserve(2 * pts.size()); // output polygon points

    // stick bottom and right edge dimensions
    double sbottom = scaled(stick_width);
    double sright  = scaled(penetration + padding);

    // scaled stride distance
    double sstride = scaled(stride);
    double t       = 0;

    // process pairs of vertices as an edge, start with the last and
    // first point
    for (size_t i = pts.size() - 1, j = 0; j < pts.size(); i = j, ++j) {
        // Get vertices and the direction vectors
        const Point &a = pts[i], &b = pts[j];
        Vec2d        dir = b.cast<double>() - a.cast<double>();
        double       nrm = dir.norm();
        dir /= nrm;
        Vec2d dirp(-dir(Y), dir(X));

        // Insert start point
        out.emplace_back(a);

        // dodge the start point, do not make sticks on the joins
        while (t < sbottom) t += sbottom;
        double tend = nrm - sbottom;

        while (t < tend) { // insert the stick on the polygon perimeter

            // calculate the stick rectangle vertices and insert them
            // into the output.
            Point p1 = a + (t * dir).cast<coord_t>();
            Point p2 = p1 + (sright * dirp).cast<coord_t>();
            Point p3 = p2 + (sbottom * dir).cast<coord_t>();
            Point p4 = p3 + (sright * -dirp).cast<coord_t>();
            out.insert(out.end(), {p1, p2, p3, p4});

            // continue along the perimeter
            t += sstride;
        }

        t = t - nrm;

        // Insert edge endpoint
        out.emplace_back(b);
    }

    // move the new points
    out.shrink_to_fit();
    pts.swap(out);
}

template<class...Args>
ExPolygons breakstick_holes(const ExPolygons &input, Args...args)
{
    ExPolygons ret = input;
    for (ExPolygon &p : ret) {
        breakstick_holes(p.contour.points, args...);
        for (auto &h : p.holes) breakstick_holes(h.points, args...);
    }

    return ret;
}

static inline coord_t get_waffle_offset(const PadConfig &c)
{
    return scaled(c.brim_size_mm + c.wing_distance());
}

static inline double get_merge_distance(const PadConfig &c)
{
    return 2. * (1.8 * c.wall_thickness_mm) + c.max_merge_dist_mm;
}

// Part of the pad configuration that is used for 3D geometry generation
struct PadConfig3D {
    double thickness, height, wing_height, slope, edge_taper;
    PadConfig::Infill infill;

    explicit PadConfig3D(const PadConfig &cfg2d)
        : thickness{cfg2d.wall_thickness_mm}
        , height{cfg2d.full_height()}
        , wing_height{cfg2d.wall_height_mm}
        , slope{cfg2d.wall_slope}
        , edge_taper{cfg2d.edge_taper_mm}
        , infill{cfg2d.infill}
    {}

    inline double bottom_offset() const
    {
        return (thickness + wing_height) / std::tan(slope);
    }

    /// The height over which the outer wall slopes, which is the wall thickness plus the height
    /// of the cavity. A floor that is thicker than the wall is a straight prism below this, so
    /// the raft grows towards the build plate without its wall moving.
    inline double sloped_height() const
    {
        return thickness + wing_height;
    }

    /// The z where the open cells of the infill begin, that is, the top of the cells. The object
    /// rests on the solid skin under the top face and, in a raft with a cavity, on the floor of
    /// the cavity, so the cells may not reach into either of them, which is the lower of the two.
    /// The skin is what is left of the raft height after the cells.
    inline double infill_cells_top_z() const
    {
        return -std::max(infill.skin_mm, wing_height);
    }
};

// Outer part of the skeleton is used to generate the waffled edges of the pad.
// Inner parts will not be waffled or offsetted. Inner parts are only used if
// pad is generated around the object and correspond to holes and inner polygons
// in the model blueprint.
struct PadSkeleton { ExPolygons inner, outer; };

PadSkeleton divide_blueprint(const ExPolygons &bp)
{
    ClipperLib::PolyTree ptree = union_pt(bp);

    PadSkeleton ret;
    ret.inner.reserve(size_t(ptree.Total()));
    ret.outer.reserve(size_t(ptree.Total()));

    for (ClipperLib::PolyTree::PolyNode *node : ptree.Childs) {
        ExPolygon poly;
        poly.contour.points = std::move(node->Contour);
        for (ClipperLib::PolyTree::PolyNode *child : node->Childs) {
            poly.holes.emplace_back(std::move(child->Contour));

            traverse_pt(child->Childs, &ret.inner);
        }

        ret.outer.emplace_back(poly);
    }
    
    return ret;
}

// A helper class for storing polygons and maintaining a spatial index of their
// bounding boxes.
class Intersector {
    BoxIndex       m_index;
    ExPolygons     m_polys;

public:

    // Add a new polygon to the index
    void add(const ExPolygon &ep)
    {
        m_polys.emplace_back(ep);
        m_index.insert(get_extents(ep), unsigned(m_index.size()));
    }

    // Check an arbitrary polygon for intersection with the indexed polygons
    bool intersects(const ExPolygon &poly)
    {
        // Create a suitable query bounding box.
        auto bb = Algorithms::Polygon::get_bounding_box(poly.contour);

        std::vector<BoxIndexEl> qres = m_index.query(bb, BoxIndex::qtIntersects);

        // Now check intersections on the actual polygons (not just the boxes)
        bool is_overlap = false;
        auto qit        = qres.begin();
        while (!is_overlap && qit != qres.end())
            is_overlap = is_overlap || Algorithms::ExPolygon::overlaps(poly, m_polys[(qit++)->second]);

        return is_overlap;
    }
};

// This dummy intersector to implement the "force pad everywhere" feature
struct DummyIntersector
{
    inline void add(const ExPolygon &) {}
    inline bool intersects(const ExPolygon &) { return true; }
};

template<class _Intersector>
class _AroundPadSkeleton : public PadSkeleton
{
    // A spatial index used to be able to efficiently find intersections of
    // support polygons with the model polygons.
    _Intersector m_intersector;

public:
    _AroundPadSkeleton(const ExPolygons &support_blueprint,
                       const ExPolygons &model_blueprint,
                       const PadConfig & cfg,
                       ThrowOnCancel     thr)
    {
        // We need to merge the support and the model contours in a special
        // way in which the model contours have to be substracted from the
        // support contours. The pad has to have a hole in which the model can
        // fit perfectly (thus the substraction -- diff_ex). Also, the pad has
        // to be eliminated from areas where there is no need for a pad, due
        // to missing supports.

        add_supports_to_index(support_blueprint);

        auto model_bp_offs =
            offset_ex(model_blueprint,
                      scaled<float>(cfg.embed_object.object_gap_mm),
                      ClipperLib::jtMiter, 1);

        ExPolygons fullcvh =
            wafflized_concave_hull(support_blueprint, model_bp_offs, cfg, thr);

        auto model_bp_sticks =
            breakstick_holes(model_bp_offs, cfg.embed_object.object_gap_mm,
                             cfg.embed_object.stick_stride_mm,
                             cfg.embed_object.stick_width_mm,
                             cfg.embed_object.stick_penetration_mm);

        ExPolygons fullpad = diff_ex(fullcvh, model_bp_sticks);

        PadSkeleton divided = divide_blueprint(fullpad);
        
        remove_redundant_parts(divided.outer);
        remove_redundant_parts(divided.inner);

        outer = std::move(divided.outer);
        inner = std::move(divided.inner);
    }

private:

    // Add the support blueprint to the search index to be queried later
    void add_supports_to_index(const ExPolygons &supp_bp)
    {
        for (auto &ep : supp_bp) m_intersector.add(ep);
    }

    // Create the wafflized pad around all object in the scene. This pad doesnt
    // have any holes yet.
    ExPolygons wafflized_concave_hull(const ExPolygons &supp_bp,
                                       const ExPolygons &model_bp,
                                       const PadConfig  &cfg,
                                       ThrowOnCancel     thr)
    {
        auto allin = reserve_vector<ExPolygon>(supp_bp.size() + model_bp.size());

        for (auto &ep : supp_bp) allin.emplace_back(ep.contour);
        for (auto &ep : model_bp) allin.emplace_back(ep.contour);

        ConcaveHull cchull{allin, get_merge_distance(cfg), thr};
        return offset_waffle_style_ex(cchull, get_waffle_offset(cfg));
    }

    // To remove parts of the pad skeleton which do not host any supports
    void remove_redundant_parts(ExPolygons &parts)
    {
        auto endit = std::remove_if(parts.begin(), parts.end(),
                                    [this](const ExPolygon &p) {
                                        return !m_intersector.intersects(p);
                                    });

        parts.erase(endit, parts.end());
    }
};

using AroundPadSkeleton = _AroundPadSkeleton<Intersector>;
using BrimPadSkeleton   = _AroundPadSkeleton<DummyIntersector>;

class BelowPadSkeleton : public PadSkeleton
{
public:
    BelowPadSkeleton(const ExPolygons &support_blueprint,
                     const ExPolygons &model_blueprint,
                     const PadConfig & cfg,
                     ThrowOnCancel     thr)
    {
        outer.reserve(support_blueprint.size() + model_blueprint.size());

        for (auto &ep : support_blueprint) outer.emplace_back(ep.contour);
        for (auto &ep : model_blueprint) outer.emplace_back(ep.contour);

        ConcaveHull ochull{outer, get_merge_distance(cfg), thr};

        outer = offset_waffle_style_ex(ochull, get_waffle_offset(cfg));
    }
};

// Offset the contour only, leave the holes untouched
template<class...Args>
ExPolygon offset_contour_only(const ExPolygon &poly, coord_t delta, Args...args)
{
    Polygons tmp = offset(poly.contour, float(delta), args...);

    if (tmp.empty()) return {};

    Polygons holes = poly.holes;
    for (auto &h : holes) h.reverse();

    ExPolygons tmp2 = diff_ex(tmp, holes);

    // An offset that splits the part is not an outline of that part any more. Taking one of the
    // pieces would leave the wall and the face built from this single polygon with a hole in
    // them, so the part is dropped instead: the caller either keeps the sharp edge or skips it.
    if (tmp2.size() != 1) return {};

    return std::move(tmp2.front());
}

// A pattern finer than this would be a hole too small to print and would take the boolean ops
// down with it, so a raft that needs more cells than this is printed solid.
constexpr size_t MAX_INFILL_CELLS = 4096;

// The open cells of the raft infill, cut out of the given region of the raft. Both patterns are
// lattices of cells with the infill wall left between two neighbours, so what is left of the
// region between the cells is the ribs of the pattern. Grid is a lattice of squares, honeycomb the
// hexagons of a hexagonal lattice.
ExPolygons infill_cells(const ExPolygon &region, const PadConfig3D &cfg)
{
    ExPolygons cells;

    const coord_t cell = scaled(cfg.infill.spacing_mm);
    const coord_t wall = scaled(cfg.infill.wall_mm);

    // A pattern without open cells would fill the raft back up to solid and one without material
    // between the cells would be a raft full of holes, so neither is built.
    if (cell <= 0 || wall <= 0)
        return cells;

    const Points &pts = region.contour.points;
    if (pts.empty())
        return cells;

    // The lattice is generated over the bounding box of the region and clipped to it afterwards,
    // so a cell that reaches over the edge of the region is trimmed instead of guessed at.
    coord_t min_x = pts.front().x(), max_x = min_x;
    coord_t min_y = pts.front().y(), max_y = min_y;
    for (const Point &p : pts) {
        min_x = std::min(min_x, p.x());
        max_x = std::max(max_x, p.x());
        min_y = std::min(min_y, p.y());
        max_y = std::max(max_y, p.y());
    }

    Polygons lattice;

    switch (cfg.infill.type) {
    case Domain::sla::RaftInfillType::None:
        break;

    case Domain::sla::RaftInfillType::Grid: {
        const coord_t period = cell + wall;
        const long    cols   = (max_x - min_x) / period + 1;
        const long    rows   = (max_y - min_y) / period + 1;

        if (cols <= 0 || rows <= 0)
            break;

        if (size_t(cols) * size_t(rows) > MAX_INFILL_CELLS) {
            SPDLOG_ERROR("Raft infill spacing is too small for this raft, printing it solid.");
            break;
        }

        for (coord_t x = min_x; x < max_x; x += period)
            for (coord_t y = min_y; y < max_y; y += period)
                lattice.emplace_back(Points{{x, y},
                                             {x + cell, y},
                                             {x + cell, y + cell},
                                             {x, y + cell}});
        break;
    }

    case Domain::sla::RaftInfillType::Honeycomb: {
        // The cells are hexagons on a lattice that keeps the wall between two neighbours, so the
        // clear width of a cell is the spacing.
        const double r_cell = cfg.infill.spacing_mm / 2.;
        const double r_lat  = (cfg.infill.spacing_mm + cfg.infill.wall_mm) / 2.;
        const coord_t col_dx = scaled(std::sqrt(3.) * r_lat);
        const coord_t row_dy = scaled(2. * r_lat);
        const coord_t reach  = scaled(2. * r_cell / std::sqrt(3.));

        if (col_dx > 0 && row_dy > 0 && reach > 0) {
            const int first_col = int(std::floor(double(min_x - reach) / col_dx));
            const int last_col  = int(std::ceil(double(max_x + reach) / col_dx));
            const int first_row = int(std::floor(double(min_y - reach) / row_dy));
            const int last_row  = int(std::ceil(double(max_y + reach) / row_dy));

            const long cols = long(last_col) - long(first_col) + 1;
            const long rows = long(last_row) - long(first_row) + 1;

            if (cols > 0 && rows > 0 &&
                size_t(cols) * size_t(rows) <= MAX_INFILL_CELLS) {
                for (int col = first_col; col <= last_col; ++col) {
                    const coord_t cx = coord_t(col) * col_dx;
                    for (int row = first_row; row <= last_row; ++row) {
                        // Every other column of a hexagonal lattice sits half a row higher.
                        const coord_t cy = coord_t(row) * row_dy + ((col & 1) ? row_dy / 2 : 0);

                        Points hexagon;
                        hexagon.reserve(6);
                        for (int i = 0; i < 6; ++i) {
                            const double angle = i * PI / 3.;
                            hexagon.emplace_back(coord_t(cx + double(reach) * std::cos(angle)),
                                                 coord_t(cy + double(reach) * std::sin(angle)));
                        }

                        lattice.emplace_back(std::move(hexagon));
                    }
                }
            } else {
                SPDLOG_ERROR("Raft infill spacing is too small for this raft, printing it solid.");
            }
        }
        break;
    }
    }

    if (lattice.empty())
        return cells;

    return intersection_ex(ExPolygons{region}, lattice);
}

// What the raft infill leaves of one raft part: its interior without the solid rim, the open
// cells inside that and the ribs around the cells. All three are empty for a raft that is printed
// solid.
struct PadInfill {
    ExPolygons interior;
    ExPolygons cells;
    ExPolygons ribs;
};

// Cut the pattern of the infill out of the raft part. The part is given as the outline it has
// where it is narrowest, so the cells are inside the raft at every height.
PadInfill cut_infill_cells(const ExPolygon &outline, const PadConfig3D &cfg)
{
    PadInfill infill;

    // No pattern, or a skin that is the whole raft and leaves nothing to open up.
    if (!cfg.infill || cfg.infill_cells_top_z() <= -cfg.height)
        return infill;

    // The pattern stops at a solid rim, which is as thick as the material between two cells.
    for (const ExPolygon &region : offset_ex(outline, -scaled<float>(cfg.infill.wall_mm))) {
        ExPolygons cells = infill_cells(region, cfg);

        // An interior without a cell in it is left solid. Cutting the pattern out of it anyway
        // would leave a closed pocket in the raft, which no printer can fill and no post
        // processing can empty.
        if (cells.empty())
            continue;

        infill.interior.emplace_back(region);
        infill.cells.insert(infill.cells.end(), cells.begin(), cells.end());

        ExPolygons ribs = diff_ex(ExPolygons{region}, cells);
        infill.ribs.insert(infill.ribs.end(), ribs.begin(), ribs.end());
    }

    return infill;
}

// The open cells of the infill: the walls around them and the flat ceiling that closes them at the
// top. What stands above the ceiling is the solid skin under the top face of the raft, which is
// what the object rests on.
indexed_triangle_set create_infill_geometry(const PadInfill & infill,
                                            const PadConfig3D & cfg,
                                            ThrowOnCancel       thr)
{
    indexed_triangle_set ret;

    if (infill.cells.empty())
        return ret;

    const double z_floor = -cfg.height, z_ceiling = cfg.infill_cells_top_z();

    for (const ExPolygon &ribs : infill.ribs) {
        thr();

        // The boundary of what is left of the interior around the cells is what the walls are
        // built on: the rim along the wall of the raft and the ribs between the cells.
        its_merge(ret, straight_walls(ribs.contour, z_ceiling, z_floor));
        for (const Polygon &h : ribs.holes)
            its_merge(ret, straight_walls(h, z_ceiling, z_floor));
    }

    // The cells are closed at the top by a ceiling, which is the underside of the skin above them,
    // so no cell ever reaches under the object and the raft is still one closed solid. What stands
    // on top of the ribs is the skin itself, which needs no face of its own there.
    thr();
    its_merge(ret, triangulate_expolygons_3d(infill.cells, z_ceiling, NORMALS_DOWN));

    return ret;
}

// The bottom face of a raft part, which the cells of the infill go through: the solid rim around
// the infill and the ribs around the cells, so the walls of the cells stand on its edges.
indexed_triangle_set create_infill_floor(const ExPolygon & outline,
                                         const PadInfill & infill,
                                         double             z)
{
    indexed_triangle_set ret;

    if (infill.cells.empty()) {
        its_merge(ret, triangulate_expolygon_3d(outline, z, NORMALS_DOWN));
        return ret;
    }

    for (const ExPolygon &rim : diff_ex(ExPolygons{outline}, infill.interior))
        its_merge(ret, triangulate_expolygon_3d(rim, z, NORMALS_DOWN));

    for (const ExPolygon &ribs : infill.ribs)
        its_merge(ret, triangulate_expolygon_3d(ribs, z, NORMALS_DOWN));

    return ret;
}

bool add_cavity(indexed_triangle_set &pad,
                ExPolygon &           top_poly,
                const PadConfig3D &   cfg,
                ThrowOnCancel         thr)
{
    auto logerr = []{SPDLOG_ERROR("Could not create pad cavity");};

    double    wing_distance = cfg.wing_height / std::tan(cfg.slope);
    coord_t   delta_inner   = -scaled(cfg.thickness + wing_distance);
    coord_t   delta_middle  = -scaled(cfg.thickness);
    ExPolygon inner_base    = offset_contour_only(top_poly, delta_inner);
    ExPolygon middle_base   = offset_contour_only(top_poly, delta_middle);

    if (inner_base.empty() || middle_base.empty()) { logerr(); return false; }

    ExPolygons pdiff = diff_ex(top_poly, middle_base.contour);

    if (pdiff.size() != 1) { logerr(); return false; }

    top_poly = pdiff.front();

    double z_min = -cfg.wing_height, z_max = 0;
    its_merge(pad, walls(inner_base.contour, middle_base.contour, z_min, z_max));
    thr();

    its_merge(pad, triangulate_expolygon_3d(inner_base, z_min, NORMALS_UP));

    return true;
}

indexed_triangle_set create_outer_pad_geometry(const ExPolygons & skeleton,
                                               const PadConfig3D &cfg,
                                               ThrowOnCancel      thr)
{
    indexed_triangle_set ret;

    // The top edge of the pad may be bevelled: the rim is pulled in over the top of the wall, which
    // leaves a thin lip a spatula can get under, so the pad can be pried off the build plate. The
    // bevel runs at 45 degrees, as deep as it is wide. A bevel that would eat the whole wall is not
    // a bevel, so then the pad keeps its sharp edge.
    const double taper = cfg.edge_taper < cfg.sloped_height() ? cfg.edge_taper : 0.;

    for (const ExPolygon &pad_part : skeleton) {
        // The rim at z = 0 and, with a bevel, the point where the bevel meets the sloped wall.
        ExPolygon top_poly{pad_part}, bevel_poly{pad_part};
        double      taper_z = 0;

        if (taper > EPSILON) {
            ExPolygon rim  = offset_contour_only(pad_part, -scaled(taper));
            // The wall keeps the pad slope below the bevel, so it starts from the skeleton pulled
            // in by whatever is left of the height.
            ExPolygon wall = offset_contour_only(
                pad_part, -scaled((cfg.sloped_height() - taper) / std::tan(cfg.slope)));

            // A bevel the offset cannot deliver would leave a hole in the pad, so it is dropped
            // and the part gets the sharp edge.
            if (!rim.empty() && !wall.empty()) {
                top_poly   = std::move(rim);
                bevel_poly = std::move(wall);
                taper_z    = -taper;
            }
        }

        ExPolygon bottom_poly =
            offset_contour_only(pad_part, -scaled(cfg.bottom_offset()));

        if (bottom_poly.empty()) continue;
        thr();

        // The cells are cut out of the part between the bottom face and the solid skin under the
        // top face, so the wall and the top face of the raft stay where they were.
        const PadInfill infill = cut_infill_cells(bottom_poly, cfg);
        its_merge(ret, create_infill_geometry(infill, cfg, thr));

        double z_min = -cfg.height, z_max = 0;
        if (taper_z < 0) {
            // The rim is the first polygon of the bevel wall and the outline the bevel runs into
            // is the second, the same way round as the wall below it: triangulate_wall() walks
            // the ring of its first polygon one way and the ring of its second one the other
            // way, so the two walls that share the outline have to take it as the same argument
            // to close the raft. The other way round leaves the raft open along the rim and
            // along the seam where the bevel meets the wall.
            its_merge(ret, walls(top_poly.contour, bevel_poly.contour, z_max, taper_z));
            z_max = taper_z;
        }

        // The wall slopes over the top of the raft and stands straight below it where the floor is
        // thicker than the wall, so the floor grows towards the build plate and the outline on it,
        // the wall and the top face are where they were. A floor as thick as the wall reaches the
        // bottom already, which is the pad of today.
        const double z_slope_min = std::max(z_min, -cfg.sloped_height());
        its_merge(ret, walls(bevel_poly.contour, bottom_poly.contour, z_max, z_slope_min));
        if (z_slope_min > z_min)
            its_merge(ret, straight_walls(bottom_poly.contour, z_slope_min, z_min));

        if (cfg.wing_height > 0. && add_cavity(ret, top_poly, cfg, thr))
            z_max = -cfg.wing_height;

        // The bevelled rim closes the top face at z = 0, so then the holes run the full height.
        const double hole_z_max = taper_z < 0 ? 0. : z_max;
        for (auto &h : bottom_poly.holes)
            its_merge(ret, straight_walls(h, hole_z_max, z_min));

        its_merge(ret, create_infill_floor(bottom_poly, infill, z_min));
        its_merge(ret, triangulate_expolygon_3d(top_poly, NORMALS_UP));
    }

    return ret;
}

indexed_triangle_set create_inner_pad_geometry(const ExPolygons & skeleton,
                                               const PadConfig3D &cfg,
                                               ThrowOnCancel      thr)
{
    indexed_triangle_set ret;

    double z_max = 0., z_min = -cfg.height;
    for (const ExPolygon &pad_part : skeleton) {
        thr();
        // An inner part is a straight prism, so its narrowest cross section is its own outline.
        const PadInfill infill = cut_infill_cells(pad_part, cfg);
        its_merge(ret, create_infill_geometry(infill, cfg, thr));
        its_merge(ret, straight_walls(pad_part.contour, z_max, z_min));

        for (auto &h : pad_part.holes)
            its_merge(ret, straight_walls(h, z_max, z_min));

        its_merge(ret, create_infill_floor(pad_part, infill, z_min));
        its_merge(ret, triangulate_expolygon_3d(pad_part, z_max, NORMALS_UP));
    }

    return ret;
}

indexed_triangle_set create_pad_geometry(const PadSkeleton &skelet,
                                         const PadConfig &  cfg,
                                         ThrowOnCancel      thr)
{
#ifndef NDEBUG
    Biz::Algorithms::SVG::SVG svg("pad_skeleton.svg");
    svg.draw(skelet.outer, "green");
    svg.draw(skelet.inner, "blue");
    svg.Close();
#endif

    PadConfig3D cfg3d(cfg);
    auto pg = create_outer_pad_geometry(skelet.outer, cfg3d, thr);
    its_merge(pg, create_inner_pad_geometry(skelet.inner, cfg3d, thr));

    return pg;
}

indexed_triangle_set create_pad_geometry(const ExPolygons &supp_bp,
                                         const ExPolygons &model_bp,
                                         const PadConfig & cfg,
                                         ThrowOnCancel     thr)
{
    PadSkeleton skelet;

    if (cfg.embed_object.enabled) {
        if (cfg.embed_object.everywhere)
            skelet = BrimPadSkeleton(supp_bp, model_bp, cfg, thr);
        else
            skelet = AroundPadSkeleton(supp_bp, model_bp, cfg, thr);
    } else
        skelet = BelowPadSkeleton(supp_bp, model_bp, cfg, thr);

    return create_pad_geometry(skelet, cfg, thr);
}

} // namespace

void pad_blueprint(const indexed_triangle_set &mesh,
                   ExPolygons &                output,
                   const std::vector<float> &  heights,
                   ThrowOnCancel               thrfn)
{
    if (mesh.empty()) return;

    std::vector<ExPolygons> out = slice_mesh_ex(mesh, heights, thrfn);

    size_t count = 0;
    for(auto& o : out) count += o.size();

    // Unification is expensive, a simplify also speeds up the pad generation
    auto tmp = reserve_vector<ExPolygon>(count);
    for(ExPolygons& o : out)
        for(ExPolygon& e : o) {
            // A slicing plane that only grazes the mesh - the bottom of a ball standing on the
            // plate is one - leaves a contour with no area in it or none at all, and the
            // simplifier reads the first point of the contour.
            if (e.contour.points.size() < 3)
                continue;

            auto&& exss = Algorithms::ExPolygon::simplify(e, scaled<double>(0.1));
            for(ExPolygon& ep : exss) tmp.emplace_back(std::move(ep));
        }

    ExPolygons utmp = union_ex(tmp);

    for(auto& o : utmp) {
        auto&& smp = Algorithms::ExPolygon::simplify(o, scaled<double>(0.1));
        output.insert(output.end(), smp.begin(), smp.end());
    }
}

void pad_blueprint(const indexed_triangle_set &mesh,
                   ExPolygons &                output,
                   float                       h,
                   float                       layerh,
                   ThrowOnCancel               thrfn)
{
    float gnd = float(Domain::bounding_box(mesh).min(Z));

    std::vector<float> slicegrid = grid(gnd, gnd + h, layerh);
    pad_blueprint(mesh, output, slicegrid, thrfn);
}

void create_pad(const ExPolygons &    sup_blueprint,
                const ExPolygons &    model_blueprint,
                indexed_triangle_set &out,
                const PadConfig &     cfg,
                ThrowOnCancel         thr)
{
    auto t = create_pad_geometry(sup_blueprint, model_blueprint, cfg, thr);
    its_merge(out, t);
}

std::string PadConfig::validate() const
{
    static const double constexpr MIN_BRIM_SIZE_MM = .1;

    if (brim_size_mm < MIN_BRIM_SIZE_MM ||
        bottom_offset() > brim_size_mm + wing_distance() ||
        get_waffle_offset(*this) <= MIN_BRIM_SIZE_MM)
        return _u8L("Pad brim size is too small for the current configuration.");

    return "";
}

}} // namespace Slic3r::sla
