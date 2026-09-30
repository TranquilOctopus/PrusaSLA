#include "PointCloud.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <random>
#include <vector>

#include "Slic3r/Biz/Algorithms/Tesselate.hpp"
#include "libslic3r/SLA/SupportTreeUtils.hpp"
#include "admesh/stl.h"
#include "libslic3r/BranchingTree/BranchingTree.hpp"
#include "libslic3r/SLA/Pad.hpp"

namespace Slic3r { namespace branchingtree {

using Domain::Index3;

namespace {

// FNV-1a over the bytes it is fed. The bytes of the numbers are hashed, never a
// whole struct, so the seed of the sampler depends on nothing but the numbers.
uint64_t fnv1a(uint64_t seed, const void *data, size_t len)
{
    const auto *bytes = static_cast<const unsigned char *>(data);

    for (size_t i = 0; i < len; ++i) {
        seed ^= bytes[i];
        seed *= 1099511628211ull;
    }

    return seed;
}

// What the sampler of a mesh draws from: a hash of the mesh itself, of the radius
// the points are drawn at and of what is being sampled, so that the same input
// always draws the same points and two different inputs never draw the same ones.
uint64_t sampling_seed(const indexed_triangle_set &its, double radius, uint64_t of)
{
    uint64_t seed = 14695981039346656037ull;

    seed = fnv1a(seed, &of, sizeof(of));
    seed = fnv1a(seed, &radius, sizeof(radius));
    seed = fnv1a(seed, its.vertices.data(), its.vertices.size() * sizeof(Vec3f));
    seed = fnv1a(seed, its.indices.data(), its.indices.size() * sizeof(Index3));

    return seed;
}

// The generator of the sampler. It used to be std::rand, through Eigen, which is
// what made the tree of two runs of the same model two different trees. An
// mt19937_64 gives the same sequence for a seed on every platform the slicer is
// built on, and the number below is arithmetic rather than a uniform
// distribution, whose output the standard does not fix.
class Sampler
{
    std::mt19937_64 m_gen;

public:
    explicit Sampler(uint64_t seed) : m_gen{seed} {}

    // A number in [0, 1).
    double next()
    {
        return double(m_gen() >> 11) * 0x1p-53;
    }
};

} // namespace

std::optional<Vec3f> find_merge_pt(const Vec3f &A, const Vec3f &B, float max_slope)
{
    return sla::find_merge_pt(A, B, max_slope);
}

void to_eigen_mesh(const indexed_triangle_set &its,
                   Eigen::MatrixXd            &V,
                   Eigen::MatrixXi            &F)
{
    V.resize(its.vertices.size(), 3);
    F.resize(its.indices.size(), 3);
    for (unsigned int i = 0; i < its.indices.size(); ++i){
        F.row(i)[0] = its.indices[i][0];
        F.row(i)[1] = its.indices[i][1];
        F.row(i)[2] = its.indices[i][2];
    }

    for (unsigned int i = 0; i < its.vertices.size(); ++i)
        V.row(i) = its.vertices[i].cast<double>();
}

// The places the tree merges branches between: the surface of the model and the
// bed, at the sampling radius of the properties. Turk's method, as libigl drew
// it: a triangle is picked with the probability of its share of the surface, then
// a point is picked uniformly inside that triangle. Drawn from the generator of
// sampling_seed() rather than from the one of the whole process, so the points
// are a function of the mesh and the radius alone.
std::vector<Node> sample_mesh(const indexed_triangle_set &its, double radius)
{
    std::vector<Node> ret;

    // The share of the surface every triangle is worth, accumulated, so that one
    // upper bound per draw picks the triangle a point falls into.
    std::vector<double> cum;
    cum.reserve(its.indices.size());

    double surface_area = 0.;
    for (const Index3 &face : its.indices) {
        const Vec3f U = its.vertices[face[1]] - its.vertices[face[0]];
        const Vec3f V = its.vertices[face[2]] - its.vertices[face[0]];

        surface_area += 0.5 * double(U.cross(V).norm());
        cum.push_back(surface_area);
    }

    if (!(surface_area > 0.) || !(radius > 0.))
        return ret;

    int N = surface_area / (PI * radius * radius);

    for (double &c : cum)
        c /= surface_area;

    Sampler rng{sampling_seed(its, radius, 1)};

    ret.reserve(size_t(N));
    for (int i = 0; i < N; ++i) {
        const double r = rng.next();

        size_t face_id = size_t(std::upper_bound(cum.begin(), cum.end(), r) - cum.begin());

        if (face_id >= its.indices.size())
            continue;

        Index3 face = its.indices[face_id];

        if (face[0] >= int(its.vertices.size()) ||
            face[1] >= int(its.vertices.size()) ||
            face[2] >= int(its.vertices.size()))
            continue;

        // The barycentric coordinates of a point uniformly inside a triangle: the
        // square root of a number in [0, 1) is distributed over the square of it
        // the same way as the number itself, so the root and what is left of one
        // are two coordinates of a uniform point and one minus the root the third.
        const double s = rng.next(), t = rng.next();
        const double sqt = std::sqrt(t);

        Vec3f c = float(1. - sqt) * its.vertices[face[0]]
            + float((1. - s) * sqt) * its.vertices[face[1]]
            + float(s * sqt) * its.vertices[face[2]];

        ret.emplace_back(c);
    }

    return ret;
}

std::vector<Node> sample_bed(const ExPolygons &bed, float z, double radius)
{
    using Slic3r::Biz::Algorithms::Tesselate::triangulate_expolygons_3d;
    auto triangles = triangulate_expolygons_3d(bed, z);
    indexed_triangle_set its;
    its.vertices.reserve(triangles.size());

    for (size_t i = 0; i < triangles.size(); i += 3) {
        its.vertices.emplace_back(triangles[i].cast<float>());
        its.vertices.emplace_back(triangles[i + 1].cast<float>());
        its.vertices.emplace_back(triangles[i + 2].cast<float>());

        its.indices.emplace_back(Domain::Index3{
            static_cast<int>(i),
            static_cast<int>(i + 1),
            static_cast<int>(i + 2)
        });
    }

    return sample_mesh(its, radius);
}

PointCloud::PointCloud(const indexed_triangle_set &M,
                       std::vector<Node>           support_leafs,
                       const Properties           &props)
    : PointCloud{sample_mesh(M, props.sampling_radius()),
                 sample_bed(props.bed_shape(),
                            props.ground_level(),
                            props.sampling_radius()),
                 std::move(support_leafs), props}
{}

PointCloud::PointCloud(std::vector<Node> meshpts,
                       std::vector<Node> bedpts,
                       std::vector<Node> support_leafs,
                       const Properties &props)
    : m_leafs{std::move(support_leafs)}
    , m_meshpoints{std::move(meshpts)}
    , m_bedpoints{std::move(bedpts)}
    , m_props{props}
    , cos2bridge_slope{std::cos(props.max_slope()) *
                       std::abs(std::cos(props.max_slope()))}
    , MESHPTS_BEGIN{m_bedpoints.size()}
    , LEAFS_BEGIN{MESHPTS_BEGIN + m_meshpoints.size()}
    , JUNCTIONS_BEGIN{LEAFS_BEGIN + m_leafs.size()}
    , m_searchable_indices(JUNCTIONS_BEGIN + m_junctions.size(), true)
    , m_queue_indices(JUNCTIONS_BEGIN + m_junctions.size(), Unqueued)
    , m_reachable_cnt{JUNCTIONS_BEGIN + m_junctions.size()}
{
    for (size_t i = 0; i < m_bedpoints.size(); ++i) {
        m_bedpoints[i].id = int(i);
        m_ktree.insert({m_bedpoints[i].pos, i});
    }
    
    for (size_t i = 0; i < m_meshpoints.size(); ++i) {
        Node &n = m_meshpoints[i];
        n.id = int(MESHPTS_BEGIN + i);
        m_ktree.insert({n.pos, n.id});
    }
    
    for (size_t i = 0; i < m_leafs.size(); ++i) {
        Node &n = m_leafs[i];
        n.id    = int(LEAFS_BEGIN + i);
        n.left  = Node::ID_NONE;
        n.right = Node::ID_NONE;

        m_ktree.insert({n.pos, n.id});
    }
}

float PointCloud::get_distance(const Vec3f &p, size_t node_id) const
{
    auto t = get_type(node_id);
    auto ret = std::numeric_limits<float>::infinity();
    const auto &node = get(node_id);
    
    switch (t) {
    case MESH:
    case BED: {
        // Points of mesh or bed which are outside of the support cone of
        // 'pos' must be discarded.
        if (is_outside_support_cone(p, node.pos))
            ret = std::numeric_limits<float>::infinity();
        else
            ret  = (node.pos - p).norm();
        
        break;
    }
    case LEAF:
    case JUNCTION:{
        auto mergept = find_merge_pt(p, node.pos, m_props.max_slope());
        double maxL2 = m_props.max_branch_length() * m_props.max_branch_length();

        if (!mergept || mergept->z() < (m_props.ground_level() + 2 * node.Rmin))
            ret = std::numeric_limits<float>::infinity();
        else if (double a = (node.pos - *mergept).squaredNorm(),
                 b        = (p - *mergept).squaredNorm();
                 a < maxL2 && b < maxL2)
            ret = std::sqrt(b);

        break;
    }
    case NONE:
        ;
    }
    
    // Setting the ret val to infinity will effectively discard this
    // connection of nodes. max_branch_length property is used here
    // to discard node=>node and node=>mesh connections longer than this
    // property.
    if (t != BED && ret > m_props.max_branch_length())
        ret = std::numeric_limits<float>::infinity();
    
    return ret;
}

}} // namespace Slic3r::branchingtree
