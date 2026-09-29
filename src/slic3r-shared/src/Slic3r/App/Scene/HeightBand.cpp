#include "Slic3r/App/Scene/HeightBand.hpp"

#include <algorithm>
#include <cmath>

namespace Slic3r::App::Scene {

static double sanitize(double value, double fallback)
{
    return std::isfinite(value) ? value : fallback;
}

HeightBand HeightBand::make(double z_min, double z_max, double max_height)
{
    HeightBand band;
    if (!std::isfinite(max_height) || max_height <= 0.)
        return band;

    double lo = std::clamp(sanitize(z_min, 0.), 0., max_height);
    double hi = std::clamp(sanitize(z_max, max_height), 0., max_height);
    if (lo > hi)
        std::swap(lo, hi);

    band.z_min  = lo;
    band.z_max  = hi;
    band.active = lo > 0. || hi < max_height;
    return band;
}

Domain::Vec4f height_band_upper_plane_data(const HeightBand& band)
{
    // Same "clips nothing" sentinel as Scene::get_clipping_plane_data().
    if (!band.active)
        return Domain::Vec4f(0.f, 0.f, 1.f, FLT_MAX);

    // Biz::ClippingPlane((0, 0, -1), -z_max) keeps everything below z_max, negated normal for OpenGL.
    return Domain::Vec4f(0.f, 0.f, 1.f, float(-band.z_max));
}

} // namespace Slic3r::App::Scene
