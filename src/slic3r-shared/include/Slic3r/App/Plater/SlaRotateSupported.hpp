#pragma once

#include <cstddef>
#include <functional>
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

/// Result of checking which selected objects have support points.
struct SlaRotateSupportedCheck
{
    /// The objects from the selection that have non-empty sla_support_points.
    std::vector<const Domain::ModelObject*> objects_with_points;

    /// Total number of support points across all those objects.
    std::size_t point_count{0};

    bool empty() const { return objects_with_points.empty(); }
};

/// Check which of the currently selected objects have support points.
/// @param project_interactor Used to get the current selection and project.
/// @return A check result listing the objects with points and the total point count.
SlaRotateSupportedCheck sla_rotate_supported_check(Biz::ProjectInteractor& project_interactor);

/// The yes/no question text for the rotate-supported-part dialog.
/// @param check The result of sla_rotate_supported_check (must not be empty).
/// @return The formatted question string, or empty string if check is empty.
std::string sla_rotate_supported_question(const SlaRotateSupportedCheck& check);

/// Type for the rotation callback: applies the rotation transform to the selection.
using SlaRotateCallback = std::function<void()>;

/// Ask the user whether to rotate and discard supports, then apply the rotation if yes.
/// Only asks in SLA mode (is_sla_active). In FFF mode or when no selected objects have
/// supports, the callback is invoked directly without a dialog.
///
/// @param project_interactor Used for the dialog, undo, and SLA mode check.
/// @param check The result of sla_rotate_supported_check (if empty, callback runs directly).
/// @param rotation_callback The rotation to apply (e.g. a lambda that calls transform_selection
///                          and takes the undo snapshot). Called only on Yes (or directly if no dialog).
void sla_rotate_supported_ask_then_apply(
    Biz::ProjectInteractor& project_interactor,
    const SlaRotateSupportedCheck& check,
    SlaRotateCallback rotation_callback
);

} // namespace Slic3r::App::Plater