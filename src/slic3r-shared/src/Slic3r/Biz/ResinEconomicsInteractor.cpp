#include "Slic3r/Biz/ResinEconomicsInteractor.hpp"

#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/SLAResultCache.hpp"
#include "Slic3r/Domain/ConfigContainer.hpp"
#include "Slic3r/Domain/FindById.hpp"
#include "Slic3r/Domain/Project.hpp"
#include "Slic3r/Domain/SLA/PrintStatistics.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace Slic3r::Biz {

BedResinEconomics ResinEconomicsInteractor::compute_bed_economics(
    Domain::SelectionId project_id,
    Domain::SelectionId bed_instance_id
) const
{
    BedResinEconomics result;
    result.bed_instance_id = bed_instance_id;

    // Get the project
    const Domain::Project& project = m_project_interactor.project(project_id);

    // Find the config container for this bed instance
    const Domain::ConfigContainer* config_container = project.find_config_container_by_bed_instance_id(bed_instance_id);
    if (!config_container) {
        result.economics.summary = "Config container not found";
        return result;
    }

    // Find the bed instance
    const Domain::BedInstance* bed_instance = Domain::find_by_id(config_container->bed_instances(), bed_instance_id);
    if (!bed_instance) {
        result.economics.summary = "Bed instance not found";
        return result;
    }
    result.bed_name = bed_instance->name();

    // Get the slicing ID for this bed
    const Domain::SlicingId slicing_id = m_project_interactor.slicing_interactor().get_process_id(bed_instance_id);
    // SlicingId is a pair of ids, not a scalar: compare against a default-constructed one.
    if (slicing_id == Domain::SlicingId{}) {
        result.economics.summary = "No slicing ID";
        return result;
    }

    // Get the cached SLA result
    const SLAResultCache& sla_cache = m_project_interactor.sla_result_cache();
    const std::optional<SLAResultRef> sla_result_opt = sla_cache.get_result(slicing_id);

    if (!sla_result_opt) {
        result.economics.summary = "No SLA result cached";
        return result;
    }

    const Slicing::SLAResult& sla_result = sla_result_opt.value().get();
    if (!sla_result.export_data || !sla_result.export_data->print_statistics.has_value()) {
        result.economics.summary = "No print statistics";
        return result;
    }

    const Domain::SLA::PrintStatistics& stats = sla_result.export_data->print_statistics.value();
    const Domain::ConfigView& config = sla_result.export_data->config;

    // Compute economics
    result.economics = ResinEconomics::calculate(stats, config);
    result.has_result = true;

    return result;
}

ProjectResinEconomics ResinEconomicsInteractor::compute_project_economics(Domain::SelectionId project_id) const
{
    ProjectResinEconomics result;

    const Domain::Project& project = m_project_interactor.project(project_id);

    // Iterate over all config containers
    for (const auto& config_container_ptr : project.config_containers()) {
        const Domain::ConfigContainer& config_container = *config_container_ptr;

        // Iterate over all bed instances in this config container
        for (const auto& bed_instance_ptr : config_container.bed_instances()) {
            const Domain::BedInstance& bed_instance = *bed_instance_ptr;

            BedResinEconomics bed_economics = compute_bed_economics(project_id, bed_instance.id().id);

            if (bed_economics.has_result) {
                result.beds_with_results++;
            } else {
                result.beds_skipped++;
            }

            result.beds.push_back(std::move(bed_economics));
        }
    }

    // Compute project total by summing up beds with results
    double total_ml = 0.0;
    double total_g = 0.0;
    double total_cost = 0.0;
    double total_bottles = 0.0;
    bool has_ml = false, has_g = false, has_cost = false, has_bottles = false;

    for (const auto& bed : result.beds) {
        if (!bed.has_result) continue;

        if (bed.economics.millilitres.has_value()) {
            total_ml += *bed.economics.millilitres;
            has_ml = true;
        }
        if (bed.economics.grams.has_value()) {
            total_g += *bed.economics.grams;
            has_g = true;
        }
        if (bed.economics.cost.has_value()) {
            total_cost += *bed.economics.cost;
            has_cost = true;
        }
        if (bed.economics.bottles_fraction.has_value()) {
            total_bottles += *bed.economics.bottles_fraction;
            has_bottles = true;
        }
    }

    if (has_ml) result.total.millilitres = total_ml;
    if (has_g) result.total.grams = total_g;
    if (has_cost) result.total.cost = total_cost;
    if (has_bottles) result.total.bottles_fraction = total_bottles;

    // Build summary for total
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2);

    if (has_ml) {
        ss << total_ml << " ml";
    }
    if (has_g) {
        if (!ss.str().empty()) ss << ", ";
        ss << total_g << " g";
    }
    if (has_cost) {
        if (!ss.str().empty()) ss << ", ";
        ss << total_cost;
    }
    if (has_bottles) {
        if (!ss.str().empty()) ss << ", ";
        ss << total_bottles << " bottles";
    }

    result.total.summary = ss.str();
    if (result.total.summary.empty()) {
        result.total.summary = "No data";
    }

    return result;
}

} // namespace Slic3r::Biz