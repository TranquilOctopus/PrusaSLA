#include "Slic3r/App/Plater/SlaHeightBandMeshes.hpp"

#include "Slic3r/Domain/ModelObject.hpp"
#include "Slic3r/Domain/ModelVolume.hpp"
#include "libslic3r/SLAResult.hpp"

using namespace Slic3r;

namespace Slic3r::App::Plater {

std::vector<Scene::ExtraMesh> collect_height_band_meshes(
    const Domain::BedInstance&   bed_instance,
    const Domain::SlicingId&     slicing_id,
    const Biz::SLAObjectCache&   sla_object_cache,
    const Domain::ModelInstance* selected_instance
)
{
    std::vector<Scene::ExtraMesh> meshes;

    // The model instances are placed the way the plater places them, see PlaterScenePresenter.
    const Domain::Transform3d bed_trafo = bed_instance.transformation.get_matrix();

    for (const Domain::ModelInstance* instance : bed_instance.model_instances) {
        if (instance == nullptr || !instance->is_printable())
            continue;

        const Domain::ModelObject* object = instance->get_object();
        if (object == nullptr)
            continue;

        if (instance != selected_instance) {
            for (const Domain::ModelVolume* volume : object->volumes) {
                if (volume == nullptr || !volume->is_model_part())
                    continue;
                meshes.push_back(Scene::ExtraMesh{
                    volume->mesh_ptr(), instance->get_matrix() * volume->get_matrix()
                });
            }
        }

        // The sliced support tree and the raft, as the Preview draws them. They exist only once the
        // bed has been sliced, and they are shared by all instances of the object.
        const Biz::SLAObjectCache::Key key{slicing_id, object->id()};
        const Biz::SLAObjectOptRef cached = sla_object_cache.get_instance(key);
        if (!cached)
            continue;

        const Biz::Slicing::Sla::Object& sla_object = cached->get();
        for (const auto& [cached_instance_id, trafo] : sla_object.instance_trafos) {
            // The tree and the pad are shared by all instances, so one copy per instance placement.
            // The cached meshes sit in the print frame, in which the model is lifted by the support
            // elevation. The plater draws the model unlifted, so drop the lift again.
            Domain::Transform3d world_trafo = bed_trafo * trafo;
            world_trafo.translation().z()     = instance->get_matrix().translation().z();

            if (sla_object.support_structure && !sla_object.support_structure->empty()) {
                meshes.push_back(Scene::ExtraMesh{sla_object.support_structure, world_trafo});
            }
            if (sla_object.pad && !sla_object.pad->empty()) {
                meshes.push_back(Scene::ExtraMesh{sla_object.pad, world_trafo});
            }
        }    }

    return meshes;
}

} // namespace Slic3r::App::Plater
