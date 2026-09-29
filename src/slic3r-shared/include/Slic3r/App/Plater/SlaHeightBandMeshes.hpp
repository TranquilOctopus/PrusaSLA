#pragma once

#include <vector>

#include "Slic3r/App/Scene/Clipper.hpp"
#include "Slic3r/Biz/SLAObjectCache.hpp"
#include "Slic3r/Domain/BedInstance.hpp"
#include "Slic3r/Domain/ModelInstance.hpp"
#include "Slic3r/Domain/Project.hpp"
#include "Slic3r/Domain/SlicingId.hpp"

namespace Slic3r::App::Plater {

/**
 * @brief The meshes the height band clips and caps next to the model the clipper already draws.
 *
 * The band is meant to show the whole print between two heights, so every other printable instance
 * on @p bed_instance is collected, together with the support trees and the rafts the slicer produced
 * for it. Instances that are not printable are skipped, @p selected_instance is skipped because the
 * clipper cuts it already, and only the model parts of an object are taken.
 *
 * The support tree and the pad of the SLA object cache are drawn in the print frame, in which the
 * model is lifted by the support elevation. The plater draws the model unlifted, so the lift is
 * dropped again to keep the supports attached to their model.
 */
std::vector<Scene::ExtraMesh> collect_height_band_meshes(
    const Domain::BedInstance&   bed_instance,
    const Domain::SlicingId&     slicing_id,
    const Biz::SLAObjectCache&   sla_object_cache,
    const Domain::ModelInstance* selected_instance
);

} // namespace Slic3r::App::Plater
