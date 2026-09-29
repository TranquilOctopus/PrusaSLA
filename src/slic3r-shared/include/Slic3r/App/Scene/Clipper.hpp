#pragma once

#include "Slic3r/Biz/Utils/MeshClipper.hpp"
#include "Slic3r/App/Scene/ClipperPresenterHelper.hpp"
#include "Slic3r/App/Scene/HeightBand.hpp"
#include "Slic3r/Domain/Model.hpp"

namespace Slic3r::Domain {
class ModelVolume;
class ModelObject;
class ModelInstance;
class TriangleMesh;
} // namespace Slic3r::Domain

namespace Slic3r::App::Scene {

class Camera;
struct Ray;

/**
 * @brief A mesh cut and capped next to the selected object's volumes.
 *
 * The height band shows the whole print between two heights, not just the model the user happens to
 * have selected: every other printable instance on the build plate and the SLA support tree and raft
 * come in here. Unlike the selected object's volumes, which are placed through the instance
 * transform, an extra mesh carries its own world transform.
 */
struct ExtraMesh
{
    /// Shared, so that editing the model or re-slicing cannot pull the mesh away from the clipper.
    std::shared_ptr<const Domain::TriangleMesh> mesh;
    Domain::Transform3d trafo{Domain::Transform3d::Identity()};
};

class Clipper
{
public:
    explicit Clipper() {};

    void set_camera(const Camera* camera);
    void set_normal(const Domain::Vec3d& dir);

    double get_position() const
    {
        return m_clp_ratio;
    }

    const Biz::ClippingPlane& get_clipping_plane(bool ignore_hide_clipped = false) const;
    void set_position_by_ratio(double pos, bool keep_normal);
    void set_range_and_pos(const Domain::Vec3d& cpl_normal, double cpl_offset, double pos);
    void set_limiting_plane(const Domain::Vec3d& plane_normal, double plane_offset);
    void set_behavior(bool hide_clipped, bool fill_cut, double contour_width);

    // The lower limit of the band becomes the clipper plane, the upper limit is only a shader side plane.
    void set_height_band(const HeightBand& band);
    const HeightBand& height_band() const { return m_height_band; }

    int get_number_of_contours() const;
    std::map<MeshClipperContourId, Domain::Vec3d> point_per_contour() const;

    MeshClipperContourId get_mesh_clipper_contour_id_from_projection(
        const Domain::Vec3d& point_in
    ) const;
    bool has_valid_contour() const;

    void update(
        const Domain::ModelObject* selected_object,
        const Domain::ModelInstance* selected_instance,
        double sla_shift                = 0.,
        bool force_clipper_regeneration = false
    );
    void release();
    void recalculate_object_clippers();

    bool unproject_on_cut_plane(
        const Ray& ray,
        Domain::Vec3d& pos_world,
        MeshClipperContourId& contour_id
    );

    const std::vector<Domain::ModelVolume*>& volumes();

    using MeshesWithTransform =
        std::vector<std::pair<std::unique_ptr<Biz::MeshClipper>, Domain::Transformation>>;
    MeshesWithTransform object_clippers;

    // Meshes cut next to the selected object, placed by their own world transform - see ExtraMesh.
    void set_extra_meshes(std::vector<ExtraMesh> meshes);
    [[nodiscard]] const std::vector<ExtraMesh>& extra_meshes() const { return m_extra_meshes; }
    [[nodiscard]] const std::vector<std::pair<std::unique_ptr<Biz::MeshClipper>, Domain::Transformation>>& extra_clippers() const
    {
        return m_extra_clippers;
    }

private:
    void set_extra_clippers();
    // Place one mesh clipper in the current cutting plane and cut it.
    void set_mesh_clipper_plane(Biz::MeshClipper& mesh_clipper, const Domain::Transformation& trafo);

private:
    std::vector<const Domain::TriangleMesh*> m_old_meshes;
    // std::unique_ptr<MeshClipper> m_supports_clipper;
    // std::unique_ptr<MeshClipper> m_pad_clipper;
    std::vector<ExtraMesh> m_extra_meshes;
    std::vector<std::pair<std::unique_ptr<Biz::MeshClipper>, Domain::Transformation>> m_extra_clippers;
    std::unique_ptr<Biz::ClippingPlane> m_clp;
    double m_clp_ratio             = 0.;
    double m_active_inst_bb_radius = 0.;
    bool m_hide_clipped            = true;

    const Camera* m_camera;
    const Domain::ModelObject* m_selected_object{nullptr};
    const Domain::ModelInstance* m_selected_instance{nullptr};
    double m_sla_shift{0.};
    Biz::ClippingPlane m_limiting_plane{Domain::Vec3d::UnitZ(), -Domain::SINKING_Z_THRESHOLD};
    HeightBand m_height_band;
    // Cached cut behaviour, so meshes added after set_behavior() are cut the same way.
    bool m_fill_cut{true};
    double m_contour_width{0.};
};

} // namespace Slic3r::App::Scene
