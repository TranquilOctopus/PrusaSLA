#pragma once

#include "Slic3r/Domain/Point.hpp"

#include <cfloat>

namespace Slic3r::App::Scene {

/**
 * @brief A Z interval limiting what the 3D view shows, like Chitubox's height band.
 *
 * The lower limit becomes the single plane of Scene::Clipper (everything below it is dropped and the
 * cut face gets capped by the mesh clipper). The upper limit has no counterpart in Scene::Clipper,
 * so it is passed to the shaders as a second clipping plane - see height_band_upper_plane_data().
 */
struct HeightBand
{
    /// False when the band covers the whole print height, i.e. nothing is clipped.
    bool active{false};
    double z_min{0.};
    double z_max{0.};

    /**
     * @brief Clamp both limits into [0, max_height], order them and decide whether they narrow the view.
     * @param max_height The printer's max print height. Anything not positive disables the band.
     */
    static HeightBand make(double z_min, double z_max, double max_height);

    [[nodiscard]] bool contains(double z) const
    {
        return !active || (z >= z_min && z <= z_max);
    }
};

/// vec4 in the convention of Scene::get_clipping_plane_data() - negated normal plus offset. Clips nothing when inactive.
Domain::Vec4f height_band_upper_plane_data(const HeightBand& band);

} // namespace Slic3r::App::Scene
