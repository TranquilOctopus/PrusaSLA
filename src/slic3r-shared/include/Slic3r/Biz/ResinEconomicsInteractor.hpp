#pragma once

#include "Slic3r/Biz/ResinEconomics.hpp"
#include "Slic3r/Biz/SLAResultCache.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Domain/SelectionId.hpp"

#include <optional>
#include <string>
#include <vector>

namespace Slic3r::Biz {

/**
 * @brief Per-bed resin economics result with bed identification.
 */
struct BedResinEconomics
{
    Domain::SelectionId bed_instance_id;
    std::string bed_name;
    ResinEconomicsResult economics;
    bool has_result = false;
};

/**
 * @brief Project-level resin economics summary.
 */
struct ProjectResinEconomics
{
    std::vector<BedResinEconomics> beds;
    ResinEconomicsResult total;
    size_t beds_with_results = 0;
    size_t beds_skipped = 0;
};

/**
 * @brief Interactor-level helper for computing resin economics across project beds.
 * Reads cached SLA results from SLAResultCache and computes economics per bed and project total.
 */
class ResinEconomicsInteractor
{
public:
    explicit ResinEconomicsInteractor(ProjectInteractor& project_interactor)
        : m_project_interactor(project_interactor)
    {}

    /**
     * @brief Compute resin economics for all beds in a project.
     * @param project_id Project to analyze
     * @return ProjectResinEconomics with per-bed results and project total
     */
    ProjectResinEconomics compute_project_economics(Domain::SelectionId project_id) const;

    /**
     * @brief Compute resin economics for a single bed instance.
     * @param project_id Project containing the bed
     * @param bed_instance_id Bed instance to analyze
     * @return BedResinEconomics result, has_result=false if no cached SLA result
     */
    BedResinEconomics compute_bed_economics(Domain::SelectionId project_id, Domain::SelectionId bed_instance_id) const;

private:
    ProjectInteractor& m_project_interactor;
};

} // namespace Slic3r::Biz