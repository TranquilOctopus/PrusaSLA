#include "Slic3r/Biz/ResultExport/SLA/SlaAntiAliasing.hpp"

namespace Slic3r::Biz::PrintHost::Sla {

bool sla_raster_anti_aliased(const Domain::ConfigView& cfg)
{
    // ConfigView::get asserts on a missing key, so the setting is looked up in the resolved values
    // the way the writers do it for the rest of their fields.
    const auto it = cfg.values().find("gamma_correction");
    if (it == cfg.values().end())
        return true;
    if (it->second.holds_alternative<double>())
        return it->second.get<double>() > 0.0;
    if (it->second.holds_alternative<int>())
        return it->second.get<int>() > 0;
    return true;
}

} // namespace Slic3r::Biz::PrintHost::Sla
