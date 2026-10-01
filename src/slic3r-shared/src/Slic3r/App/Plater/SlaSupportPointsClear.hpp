#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "Slic3r/Domain/ElementRef.hpp"

namespace Slic3r::Biz {
class ProjectInteractor;
} // namespace Slic3r::Biz

namespace Slic3r::Domain {
class ModelObject;
} // namespace Slic3r::Domain

namespace Slic3r::App::Plater {

/// What one "remove the support points" action would do (M2.32), worked out before anything is
/// asked or written: the models it covers, the points they carry between them and the question it
/// puts to the user.
struct SlaSupportPointsClearPlan
{
    /// The models of the caller's list that have support points, in the order the caller gave them.
    /// A model with none is left out of it, so an action never touches a model it has nothing to do
    /// with, and @p empty tells the caller there is nothing to do at all.
    std::vector<Domain::ElementRef> object_refs;

    /// How many support points go away, over every model of @p object_refs.
    std::size_t point_count{0};

    bool empty() const
    {
        return object_refs.empty();
    }
};

/// What removing the support points of @p models would do. @p models are the objects the action can
/// reach, e.g. the selected models or every printable model on the build plate; the ones that carry
/// no points are not in the plan.
SlaSupportPointsClearPlan
sla_support_points_clear_plan(const std::vector<const Domain::ModelObject*>& models);

/// The body of the yes/no dialog the action asks with: how many support points of how many models
/// go away, and that one undo brings them back. Empty for an empty plan, which asks nothing.
std::string sla_support_points_clear_question(const SlaSupportPointsClearPlan& plan);

/// Removes the support points of every model of @p plan, in ONE undo snapshot, and leaves each of
/// them at PointsStatus::NoPoints so the M2.21 support preview drops their tree and their lift by
/// itself. Nothing here slices: the points reach the project as slicing input and the Slice button
/// is what turns them into layers.
///
/// @return how many models were taken, which is the size of the plan.
std::size_t clear_sla_support_points(
    Biz::ProjectInteractor& project_interactor,
    const SlaSupportPointsClearPlan& plan
);

} // namespace Slic3r::App::Plater
