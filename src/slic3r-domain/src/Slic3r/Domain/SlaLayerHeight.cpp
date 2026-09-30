#include "Slic3r/Domain/SlaLayerHeight.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace Slic3r::Domain {

namespace {

// ConfigView::get() asserts on unknown keys, but a config view may carry only a subset of
// the options (a printer-only view has no resin settings, a result export may be given a
// trimmed view), so option values are looked up leniently, as the exporters do.
double lenient_double(const ConfigView& cfg, const std::string& key)
{
    const auto& values{cfg.values()};
    const auto  it{values.find(key)};
    if (it == values.end() || !it->second.holds_alternative<double>())
        return 0.;
    return it->second.get<double>();
}

int lenient_int(const ConfigView& cfg, const std::string& key, int fallback = 0)
{
    const auto& values{cfg.values()};
    const auto  it{values.find(key)};
    if (it == values.end() || !it->second.holds_alternative<int>())
        return fallback;
    return it->second.get<int>();
}

// How far a division of two millimetre values may miss a whole layer before the miss counts as a
// layer of its own. The division of two values neither of which is a whole number of layers is
// rarely exact (2 mm at 0.05 mm is 40 layers, or 40.00000000000001 of them), and a raft that ends
// on a layer boundary must not grow a layer of nothing because of it.
constexpr double layer_epsilon = 1e-6;

} // namespace

double sla_effective_layer_height(const ConfigView& cfg)
{
    const double resin_layer_height{lenient_double(cfg, "resin_layer_height")};
    return resin_layer_height > 0. ? resin_layer_height : lenient_double(cfg, "layer_height");
}

int sla_effective_faded_layers(const ConfigView& cfg)
{
    // -1 (the unset value) means the print preset owns the transition layer count.
    const int resin_faded_layers{lenient_int(cfg, "resin_faded_layers", -1)};
    return resin_faded_layers >= 0 ? resin_faded_layers : lenient_int(cfg, "faded_layers");
}

int sla_bottom_layer_count(const ConfigView& cfg)
{
    const int bottom_layer_count{lenient_int(cfg, "bottom_layer_count")};
    if (bottom_layer_count > 0)
        return bottom_layer_count;
    // SLAPrint::Steps::merge_slices_and_eval_stats() exposes the first layer at
    // initial_exposure_time and fades to exposure_time over the transition layers, so the
    // elevated exposure covers the transition layers plus that first one.
    return sla_effective_faded_layers(cfg) + 1;
}

RaftInterface raft_interface_band(double raft_height_mm,
                                  double interface_thickness_mm,
                                  double layer_height_mm)
{
    RaftInterface ret;

    if (raft_height_mm <= 0. || interface_thickness_mm <= 0. || layer_height_mm <= 0.)
        return ret;

    // The raft stands on the build plate, so it covers the first layers of the print, as many as
    // its height takes, and the interface is the top of them. An interface thinner than half a
    // layer still gets the one layer it rounds to, so a positive thickness is never a setting that
    // silently does nothing.
    const int raft_layers = int(std::ceil(raft_height_mm / layer_height_mm - layer_epsilon));
    const int interface_layers =
        std::max(1, int(std::lround(interface_thickness_mm / layer_height_mm - layer_epsilon)));

    ret.first_layer = std::max(0, raft_layers - interface_layers);
    ret.last_layer  = raft_layers - 1;

    return ret;
}

RaftInterface sla_raft_interface(const ConfigView& cfg, int layer_count)
{
    // The height of the raft is the thickness of its walls plus the height of the cavity the object
    // sits in, which is what the pad generator builds (PadConfig::full_height()).
    RaftInterface ret = raft_interface_band(
        lenient_double(cfg, "pad_wall_height") + lenient_double(cfg, "pad_wall_thickness"),
        lenient_double(cfg, "raft_interface_thickness"),
        sla_effective_layer_height(cfg));

    if (ret.empty())
        return ret;

    // The bottom layers are the burn-in of the print, so a layer that is both a bottom layer and an
    // interface layer is exposed at the bottom exposure, not the interface one.
    const int bottom_layers = sla_bottom_layer_count(cfg);
    if (ret.first_layer < bottom_layers) {
        if (bottom_layers > ret.last_layer)
            return {};
        ret.first_layer = bottom_layers;
    }

    // A print shorter than the band is cut to the layers it has.
    if (layer_count >= 0)
        ret.last_layer = std::min(ret.last_layer, layer_count - 1);

    if (ret.empty())
        return ret;

    ret.exposure_s = lenient_double(cfg, "raft_interface_exposure");

    return ret;
}

} // namespace Slic3r::Domain
