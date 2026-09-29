#pragma once

#include <vector>

#include "Slic3r/Biz/SLAObjectCache.hpp"
#include "Slic3r/Domain/BedInstance.hpp"
#include "Slic3r/Domain/Model.hpp"
#include "Slic3r/Domain/Project.hpp"
#include "Slic3r/Domain/SlicingId.hpp"

namespace Slic3r::App::Plater {

/**
 * @brief Model objects of @p bed_instance that were sliced without any support structure.
 *
 * An object counts as unsupported when the SLA object cache has no entry for it under
 * @p slicing_id, or the entry carries an empty support structure. Instances that are not
 * printable are ignored and every object is reported at most once, in bed order.
 *
 * Shared by the post-slice notification and the pre-export checklist so both agree on
 * what "sliced without supports" means.
 */
std::vector<const Domain::ModelObject*> collect_unsupported_objects(
    const Biz::SLAObjectCache& sla_object_cache,
    const Domain::SlicingId&     slicing_id,
    const Domain::BedInstance&    bed_instance,
    const Domain::Project&        project);

} // namespace Slic3r::App::Plater
