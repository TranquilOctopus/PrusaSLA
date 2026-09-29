#include "Slic3r/Domain/SlaLayerHeight.hpp"

namespace Slic3r::Domain {

namespace {

// ConfigView::get() asserts on unknown keys, but a config view may carry only a subset of
// the options (a printer-only view has no resin settings, a result export may be given a
// trimmed view), so option values are looked up leniently, as the exporters do.
double lenient_layer_height(const ConfigView& cfg, const std::string& key)
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

} // namespace

double sla_effective_layer_height(const ConfigView& cfg)
{
    const double resin_layer_height{lenient_layer_height(cfg, "resin_layer_height")};
    return resin_layer_height > 0. ? resin_layer_height : lenient_layer_height(cfg, "layer_height");
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

} // namespace Slic3r::Domain
