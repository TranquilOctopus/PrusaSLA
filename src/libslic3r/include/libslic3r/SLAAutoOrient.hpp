#pragma once

#include "Slic3r/Domain/Point.hpp"
#include "Slic3r/Domain/TriangleMesh.hpp"

#include <functional>

namespace Slic3r::Domain {
class ModelObject;
} // namespace Slic3r::Domain

namespace Slic3r::sla {

// Public entry point to the SLA auto-orientation (the engine's SLA/Rotfinder, which is private to
// libslic3r).
//
// Returns rotations in radians about X (x()) and Y (y()) for the object's mesh with its scaling and
// mirroring applied but no rotation. The rotation is R = Ry(y) * Rx(x): rotate about X first, then
// about Y (the order Rotfinder's to_transform3f uses). Callers that apply it to an instance that is
// already rotated must first undo the instance's current rotation.
//
// `status` is called with progress 0..100 (or -1 to only ask whether to continue) and returns false
// to cancel. The default never cancels.
using AutoOrientStatus = std::function<bool(int)>;

// What the auto orientation optimizes for.
enum class AutoOrientGoal
{
    // Lay the object down so it is as low as possible on the build plate (fewest layers).
    MinHeight,
    // Lay the object down on the face that needs the least support material.
    LeastSupports,
    // Lay the object down so the biggest cross section on its way up is as small as possible: the
    // peak peel force of the print (PLAN B7). Estimated from coarse slices of the rotated mesh, so
    // it needs no print config. This is not the same as the lowest pose: a thin plate standing on
    // its edge is the easy peel, even though it is the tall print.
    LeastPeel,
    // Lay the object down so no layer of it holds a suction cup (PLAN B7, PLAN B5b): a pocket that
    // is enclosed in its layer and stays enclosed going up until a layer closes it seals against
    // the vat film and pins a vacuum there on every peel it is open for. Estimated by running the
    // cavity detection of M4.8e on coarse slices of the rotated mesh, so it needs no print config
    // either. It is a rotation and not a drain hole: a cup of resin that is already in the vat
    // cannot be peeled off, only avoided.
    NoCups,
    // Print a pre-supported head, a helmet or a bust the way miniature printers print them: the
    // flat face the piece is glued to a body by (the cut of a neck, the underside of a head, the
    // flat back of a bust) down on the plate, the detail side up so the layers cut across the face
    // and not along it, and the piece leaned over by a fixed angle so the cross sections ramp in
    // from the narrow cut instead of standing up as a full width disc.
    //
    // This one is not a search over poses: the flat face is found in the mesh and pointed at the
    // plate, and only the azimuth of the lean is searched, so the piece keeps the pose it was
    // loaded in around the vertical axis. It does not ask what the piece looks like and it does not
    // count supports: LeastSupports is wrong for a miniature, because a face that is exposed to
    // print is a face with many islands on it and so with many support points, while the way a
    // miniature is supported is a fat support in the neck that the body hides and small ones where
    // the detail needs them. A piece with no flat face at all keeps the pose it was loaded in and
    // is only leaned over.
    Miniature
};

// The rotation for the given goal. Same angle convention as auto_orient_min_height() below.
// An object with no instances or no geometry has nothing to rotate, "no rotation" is returned
// for it instead of searching.
Domain::Vec2d auto_orient(const Domain::ModelObject& object, AutoOrientGoal goal, AutoOrientStatus status = {});

// The rotation that makes the object as low as possible on the build plate (fewest layers).
// Shorthand for auto_orient(object, AutoOrientGoal::MinHeight).
Domain::Vec2d auto_orient_min_height(const Domain::ModelObject& object, AutoOrientStatus status = {});

// The mesh auto_orient() searches: the mesh of the object with the scaling and mirroring of its
// first instance applied and no rotation. An object with no instances or no geometry has no mesh to
// search, an empty one is returned for it.
//
// Take it out of the model to search a rotation where the model is not at hand, off the UI thread
// for instance, and pass it to the auto_orient() overload below.
Domain::TriangleMesh auto_orient_mesh(const Domain::ModelObject& object);

// The rotation for the given goal, on a mesh taken with auto_orient_mesh(). The same search as
// above, without the model: the goal, the progress and the cancel all work as they do here. An
// empty mesh has nothing to rotate, "no rotation" is returned for it.
Domain::Vec2d auto_orient(
    const Domain::TriangleMesh& mesh, AutoOrientGoal goal, AutoOrientStatus status = {}
);

} // namespace Slic3r::sla
