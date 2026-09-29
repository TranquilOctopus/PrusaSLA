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

} // namespace

double sla_effective_layer_height(const ConfigView& cfg)
{
    const double resin_layer_height{lenient_layer_height(cfg, "resin_layer_height")};
    return resin_layer_height > 0. ? resin_layer_height : lenient_layer_height(cfg, "layer_height");
}

} // namespace Slic3r::Domain
