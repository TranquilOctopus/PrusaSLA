#include <libslic3r/SLA/ObjectResinUse.hpp>

#include <libslic3r/ClipperUtils.hpp>

#include "Slic3r/Biz/Algorithms/ExPolygon.hpp"

#include <algorithm>
#include <numeric>

namespace Slic3r::SLA {

namespace {

namespace ExPolygon = Biz::Algorithms::ExPolygon;

/// A model that has resin on one layer, and the row of the result it is added up in.
struct LayerModel
{
    Domain::ObjectID object_id{};
    size_t use = 0; //< index into the returned vector of ObjectResinUse
};

/// The total area of the polygons in the scaled coordinates of the plate.
double area_of(const Domain::ExPolygons& polygons)
{
    double area = 0.;
    for (const Domain::ExPolygon& polygon : polygons) {
        area += ExPolygon::area(polygon);
    }
    return area;
}

/// The part of @p polygons that no other of the @p models on the layer covers, so that two models
/// which overlap are not both charged for the resin in the overlap. With one model on the layer,
/// which is the usual case, there is nothing to take away and the polygons are used as they are.
Domain::ExPolygons
exclusive_of(const Domain::ExPolygons& polygons, const Domain::ExPolygons& merged, size_t models)
{
    if (models < 2 || polygons.empty())
        return polygons;

    const Domain::ExPolygons others = diff_ex(merged, polygons);
    return others.empty() ? polygons : diff_ex(polygons, others);
}

/// The part of @p polygons no model of the layer stands on, which is how the plate counts the
/// supports: a body standing on its own support tree is not resin twice.
Domain::ExPolygons
outside_of(const Domain::ExPolygons& polygons, const Domain::ExPolygons& merged_models)
{
    return merged_models.empty() ? polygons : diff_ex(polygons, merged_models);
}

} // namespace

std::vector<ObjectResinUse>
object_resin_use(const std::vector<ObjectLayerUse>& layers, double scaling_sq)
{
    std::vector<ObjectResinUse> out;
    if (layers.empty() || scaling_sq <= 0.)
        return out;

    // The models come back in the order they first appear on the plate, the layers are walked in
    // printing order.
    std::vector<size_t> order(layers.size());
    std::iota(order.begin(), order.end(), size_t(0));
    std::stable_sort(
        order.begin(),
        order.end(),
        [&layers](size_t a, size_t b) { return layers[a].layer_index < layers[b].layer_index; }
    );

    // The volume of every raft of the print, counted once here and shared between the models at
    // the end.
    double raft_mm3 = 0.;

    size_t at = 0;
    while (at < order.size()) {
        const size_t layer_index = layers[order[at]].layer_index;
        size_t last              = at;
        while (last < order.size() && layers[order[last]].layer_index == layer_index) {
            ++last;
        }

        const double layer_height_mm = layers[order[at]].layer_height_mm;
        const double layer_mm3       = scaling_sq * layer_height_mm;

        // The models on this layer, the bodies of all of them merged into one (which is the shape
        // the plate counts as its model volume) and their rafts.
        std::vector<LayerModel> layer_models;
        Domain::ExPolygons merged_models;
        Domain::ExPolygons merged_raft;

        for (size_t i = at; i < last; ++i) {
            const ObjectLayerUse& layer = layers[order[i]];
            // A model can be on a layer without having any resin on it: an instance above another
            // one that does. Such an entry has nothing to add and nothing to share.
            if (layer.model.empty() && layer.support.empty() && layer.raft.empty()) {
                continue;
            }

            merged_models.insert(merged_models.end(), layer.model.begin(), layer.model.end());
            merged_raft.insert(merged_raft.end(), layer.raft.begin(), layer.raft.end());

            if (std::any_of(
                    layer_models.begin(),
                    layer_models.end(),
                    [&layer](const LayerModel& m) { return m.object_id == layer.object_id; }
                ))
            {
                continue;
            }

            const auto it = std::find_if(
                out.begin(),
                out.end(),
                [&layer](const ObjectResinUse& use) { return use.object_id == layer.object_id; }
            );
            if (it == out.end()) {
                layer_models.push_back(LayerModel{.object_id = layer.object_id, .use = out.size()});
                out.push_back(ObjectResinUse{.object_id = layer.object_id, .name = layer.name});
            } else {
                layer_models.push_back(
                    LayerModel{.object_id = layer.object_id, .use = size_t(it - out.begin())}
                );
            }
        }

        // The bodies of the layer merged into one, which is the shape the plate counts as its model
        // volume. Everything below compares a model against it.
        if (!merged_models.empty()) {
            merged_models = union_ex(merged_models);
        }

        if (!layer_models.empty()) {
            for (const LayerModel& layer_model : layer_models) {
                Domain::ExPolygons model_polys;
                Domain::ExPolygons support_polys;
                for (size_t i = at; i < last; ++i) {
                    const ObjectLayerUse& layer = layers[order[i]];
                    if (layer.object_id != layer_model.object_id) {
                        continue;
                    }
                    model_polys.insert(model_polys.end(), layer.model.begin(), layer.model.end());
                    support_polys
                        .insert(support_polys.end(), layer.support.begin(), layer.support.end());
                }

                // The instances of one model share the layer, so they are merged before the areas
                // are compared: two copies of a part standing on each other are printed once.
                if (!model_polys.empty()) {
                    model_polys = union_ex(model_polys);
                }

                ObjectResinUse& use = out[layer_model.use];
                // The footprint is the widest layer of the body, measured on the body as it was
                // sliced and not on the part of it no other model covers.
                use.footprint_mm2 = std::max(use.footprint_mm2, area_of(model_polys) * scaling_sq);

                use.model_volume_mm3 +=
                    area_of(exclusive_of(model_polys, merged_models, layer_models.size()))
                    * layer_mm3;

                if (!support_polys.empty()) {
                    use.support_volume_mm3 +=
                        area_of(outside_of(support_polys, merged_models)) * layer_mm3;
                }
            }
        }

        // The raft of the layer is counted once for the whole print, see the rule in the header.
        if (!merged_raft.empty()) {
            raft_mm3 += area_of(outside_of(merged_raft, merged_models)) * layer_mm3;
        }

        at = last;
    }

    // The raft split between the models by their footprint, see the rule in the header. When no
    // model of the plate has a footprint at all, every one of them takes an equal share.
    double footprint_mm2 = 0.;
    for (const ObjectResinUse& use : out) {
        footprint_mm2 += use.footprint_mm2;
    }
    const double even_share = out.empty() ? 0. : 1. / double(out.size());
    for (ObjectResinUse& use : out) {
        const double share  = footprint_mm2 > 0. ? use.footprint_mm2 / footprint_mm2 : even_share;
        use.raft_volume_mm3 = raft_mm3 * share;
    }

    return out;
}

} // namespace Slic3r::SLA
