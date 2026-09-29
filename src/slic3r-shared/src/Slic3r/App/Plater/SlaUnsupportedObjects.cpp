#include "Slic3r/App/Plater/SlaUnsupportedObjects.hpp"

#include <algorithm>

#include "Slic3r/Domain/ModelInstance.hpp"

namespace Slic3r::App::Plater {

using namespace Slic3r;

std::vector<const Domain::ModelObject*> collect_unsupported_objects(
    const Biz::SLAObjectCache& sla_object_cache,
    const Domain::SlicingId&     slicing_id,
    const Domain::BedInstance&    bed_instance,
    const Domain::Project&        project)
{
    std::vector<const Domain::ModelObject*> unsupported_objects;
    for (const Domain::ModelInstance* instance : bed_instance.model_instances) {
        if (!instance || !instance->is_printable()) {
            continue;
        }
        const Domain::ModelObject* model_object = project.find_object_by_id(instance->get_object()->id().id);
        if (!model_object) {
            continue;
        }
        // Skip if already added
        if (std::find(unsupported_objects.begin(), unsupported_objects.end(), model_object) != unsupported_objects.end()) {
            continue;
        }
        // Check if the object has no support structure in the slice result
        const Biz::SLAObjectCache::Key key{ slicing_id, model_object->id() };
        const Biz::SLAObjectOptRef     opt_ref = sla_object_cache.get_instance(key);
        bool unsupported = !opt_ref.has_value() || !opt_ref->get().support_structure || opt_ref->get().support_structure->empty();
        if (unsupported) {
            unsupported_objects.push_back(model_object);
        }
    }
    return unsupported_objects;
}

} // namespace Slic3r::App::Plater
