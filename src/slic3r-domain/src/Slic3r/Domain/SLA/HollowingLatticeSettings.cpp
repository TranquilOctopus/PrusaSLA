#include "Slic3r/Domain/SLA/HollowingLatticeSettings.hpp"

#include <algorithm>

namespace Slic3r::Domain::SLA {

const std::vector<std::string>& hollowing_infill_visible_settings(sla::HollowingInfillType infill)
{
    // The two knobs that cut the pattern out of the cavity: how far apart two neighbouring struts
    // stand and how thick a strut is. Nothing of them is read while the cavity stays empty.
    static const std::vector<std::string> none{"hollowing_infill"};
    static const std::vector<std::string> patterned{
        "hollowing_infill",
        "hollowing_infill_spacing",
        "hollowing_infill_strut",
    };

    return infill == sla::HollowingInfillType::None ? none : patterned;
}

bool hollowing_infill_uses_setting(sla::HollowingInfillType infill, const std::string& key)
{
    const std::vector<std::string>& settings = hollowing_infill_visible_settings(infill);
    return std::ranges::find(settings, key) != settings.end();
}

bool is_hollowing_infill_setting(const std::string& key)
{
    // A patterned cavity reads every key of the group, so its list is the union over the patterns.
    return hollowing_infill_uses_setting(sla::HollowingInfillType::Grid, key);
}

} // namespace Slic3r::Domain::SLA
