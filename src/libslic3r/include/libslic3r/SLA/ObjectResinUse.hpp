#pragma once

#include <string>
#include <vector>

#include "Slic3r/Domain/ExPolygon.hpp"
#include "Slic3r/Domain/ObjectID.hpp"

namespace Slic3r::SLA {

/**
 * @brief What one model object occupies on one layer, in the same scaled plate coordinates as the
 * layers the printer input is built of.
 *
 * The polygons are the per object slices of the print, so the model is the body as it was sliced
 * (hollowing and drain holes already cut into it) and the supports are the support tree. The raft
 * (the pad the generator builds on the plate) is part of the support slices, and it is handed over
 * separately: the pad is shared, so two models that stand close enough are printed on one raft,
 * and the volumes of their support slices would count that raft twice.
 */
struct ObjectLayerUse
{
    size_t layer_index     = 0; //< the print layer the polygons belong to
    double layer_height_mm = 0.; //< the thickness of that layer, which turns an area into a volume
    Domain::ObjectID object_id{};
    std::string name;
    Domain::ExPolygons model;
    Domain::ExPolygons support; //< the support tree, the raft left out
    Domain::ExPolygons raft; //< the outline of the raft this model was printed on
};

/**
 * @brief The resin one model object cures on a sliced plate: its body, its support tree and its
 * share of the raft.
 *
 * The shares add up to the volume of the raft, and the volumes add up to the volume of the print,
 * which is what PrintStatistics counts for the whole plate.
 */
struct ObjectResinUse
{
    Domain::ObjectID object_id{};
    std::string name;
    double model_volume_mm3   = 0.;
    double support_volume_mm3 = 0.;
    double raft_volume_mm3    = 0.;
    /// The footprint the raft was split by, see object_resin_use().
    double footprint_mm2 = 0.;

    double volume_mm3() const
    {
        return model_volume_mm3 + support_volume_mm3 + raft_volume_mm3;
    }
};

/**
 * @brief Sum the per layer polygons of a sliced plate into the resin each model object cures.
 *
 * The areas are the ones of the merged layers of the whole plate, so the result adds up to
 * PrintStatistics: a model is only counted where no other model covers the same resin, and a
 * support is only counted where no model stands on it, which is how the plate counts them.
 *
 * THE RAFT RULE: the pad is generated around the models that stand close enough to share one, so
 * its volume is counted once for the print and then split between those models by FOOTPRINT: the
 * share of a model is its widest layer, the area its body presents on the plate (in mm²), because
 * that is what decides how much raft the generator grows around it. Models are not asked how much
 * raft they sit on, since one raft under two models would be counted for both of them and the
 * total would no longer be the volume of the print. A model whose body has no layer at all takes
 * no footprint and gets no raft; when no model of the plate has one either, the raft is split
 * evenly between them all. The same split is applied to the whole raft of the print, because two
 * rafts never overlap.
 *
 * The entries may come in any order: the layers are grouped by layer_index, and the models come
 * back in the order they first appear on the plate. The rafts of a layer are taken as one, whether
 * they are one shared raft handed over twice or two rafts that grow into each other, and the split
 * is then applied to the raft of the whole print at once.
 *
 * @param layers the per layer polygons of every model object
 * @param scaling_sq the squared scaling factor that turns the scaled coordinates into mm², which
 * is sqr(SCALING_FACTOR). The volumes are mm³.
 */
std::vector<ObjectResinUse>
object_resin_use(const std::vector<ObjectLayerUse>& layers, double scaling_sq);

} // namespace Slic3r::SLA
