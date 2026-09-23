///|/ Copyright (c) Prusa Research 2020 - 2021 Tomáš Mészáros @tamasmeszaros
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
#ifndef SLA_ROTFINDER_HPP
#define SLA_ROTFINDER_HPP

#include <Slic3r/Domain/Point.hpp>
#include <functional>
#include <array>
#include <utility>

namespace Slic3r::Domain {
class ModelObject;
} // namespace Slic3r::Domain

namespace Slic3r {

class SLAPrintObject;

namespace sla {

using RotOptimizeStatusCB = std::function<bool(int)>;

class RotOptimizeParams {
    float m_accuracy = 1.;
    RotOptimizeStatusCB m_statuscb = [](int) { return true; };

public:

    RotOptimizeParams &accuracy(float a) { m_accuracy = a; return *this; }
    RotOptimizeParams &statucb(RotOptimizeStatusCB cb)
    {
        m_statuscb = std::move(cb);
        return *this;
    }

    float accuracy() const { return m_accuracy; }
    const RotOptimizeStatusCB &statuscb() const { return m_statuscb; }
};

/**
  * The function should find the best rotation for SLA upside down printing.
  *
  * @param modelobj The model object representing the 3d mesh.
  * @param accuracy The optimization accuracy from 0.0f to 1.0f. Currently,
  * the nlopt genetic optimizer is used and the number of iterations is
  * accuracy * 100000. This can change in the future.
  * @param statuscb A status indicator callback called with the int
  * argument spanning from 0 to 100. May not reach 100 if the optimization finds
  * an optimum before max iterations are reached. It should return a boolean
  * signaling if the operation may continue (true) or not (false). A status
  * value lower than 0 shall not update the status but still return a valid
  * continuation indicator.
  *
  * @return Returns the rotations around each axis (x, y, z)
  */
Vec2d find_best_misalignment_rotation(const Domain::ModelObject &modelobj,
                                      const RotOptimizeParams & = {});

Vec2d find_min_z_height_rotation(const Domain::ModelObject &mo,
                                 const RotOptimizeParams &params = {});

// Helper to convert XY rotation angles to a Transform3f
Transform3f rotation_angles_to_transform(const Vec2d &angles);

} // namespace sla
} // namespace Slic3r

#endif // SLAROTFINDER_HPP