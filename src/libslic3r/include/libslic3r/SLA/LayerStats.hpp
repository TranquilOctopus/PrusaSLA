#pragma once

#include <cstddef>
#include <string_view>
#include <vector>

#include "Slic3r/Domain/ConfigDefsSLA.hpp"
#include "Slic3r/Domain/ExPolygon.hpp"
#include "Slic3r/Biz/Algorithms/Scaling.hpp"

namespace Slic3r::SLA {

/// The coefficients of the peel force model, one set per vat film type.
/// F_peel(layer) = area_coefficient_n_per_mm2 * cured area + perimeter_coefficient_n_per_mm *
/// boundary length + suction_coefficient_n_per_mm2 * cup opening area on that layer.
/// The numbers are rough order-of-magnitude defaults from the SLA peel force literature, not
/// measurements; see doc/sla-fork/profiling/peel-force.md for where they came from and how to
/// tune them. Never read them as a prediction of what a printer will do.
struct PeelForceCoefficients
{
    double area_coefficient_n_per_mm2     = 0.; //< k_area, the area term
    double perimeter_coefficient_n_per_mm = 0.; //< k_perimeter, the boundary term
    double suction_coefficient_n_per_mm2  = 0.; //< k_suction, the cup opening term
    double warning_n                      = 0.; //< peel_force_warning default of this film
};

/// The coefficients a vat film type brings, before the override keys are applied.
PeelForceCoefficients peel_force_coefficients(Domain::sla::VatFilmType film);

/// The name of a vat film, spelled as the vat_film_type combo box spells it. These are chemical
/// abbreviations rather than words to translate, so the name is handed out as it is.
std::string_view vat_film_name(Domain::sla::VatFilmType film);

/// What the slicer was told about the printer for the peel force model: the vat film, which
/// picks the coefficients, and the two overrides of them. The suction term and the warning
/// follow the film and are not overridden.
struct PeelForceSettings
{
    Domain::sla::VatFilmType film                 = Domain::sla::VatFilmType::FEP;
    double area_coefficient                      = 0.; //< peel_area_coefficient, 0 for the film value
    double perimeter_coefficient                 = 0.; //< peel_perimeter_coefficient, 0 for the film

    /// The coefficients this print uses. A zero override keeps the value of the film.
    PeelForceCoefficients coefficients() const;
};

/// The peel force warning threshold in N a print uses: @p warning_setting when it is positive
/// (zero turns the warning off), the default of the film when it is negative.
double peel_force_warning_n(double warning_setting, const PeelForceCoefficients& coefficients);

/// Compute the exposed area (in mm²) for each layer.
/// @param layers Vector of ExPolygons per layer (in scaled coordinates).
/// @return Vector of areas in mm², one per layer.
std::vector<float> layer_areas_mm2(const std::vector<Domain::ExPolygons>& layers);

/// Compute the boundary length (in mm) for each layer: the sum of the contour and of every hole
/// length, which is the length the film has to be peeled along. Same layers as layer_areas_mm2().
std::vector<float> layer_perimeters_mm(const std::vector<Domain::ExPolygons>& layers);

/// The three numbers of one layer the peel force model multiplies with the coefficients.
struct LayerPeelInput
{
    float area_mm2         = 0.f;
    float perimeter_mm     = 0.f;
    float suction_area_mm2 = 0.f;
};

/// Estimate the peel force of every layer, in N.
std::vector<float> peel_force_estimate(const std::vector<LayerPeelInput>& layers,
                                       const PeelForceCoefficients& coefficients);

/// The whole model on sliced layers: the peel force of every layer in N. @p suction_area_mm2 is
/// one entry per layer, the area of the cup openings open on it, as
/// SLA::cup_suction_area_mm2() of CavityDetection.hpp returns it. It may be empty, which is the
/// same as no cup on any layer.
std::vector<float> peel_force_estimate(const std::vector<Domain::ExPolygons>& layers,
                                       const std::vector<float>& suction_area_mm2,
                                       const PeelForceCoefficients& coefficients);

/// The layers whose peel force is above @p threshold_n, ascending. A threshold of zero or less
/// reports nothing, which is how a peel_force_warning of zero turns the check off.
std::vector<size_t> layers_over_peel_force(const std::vector<float>& peel_force_n, double threshold_n);

/**
 * @brief The smallest dimension of a cross section, in mm: the caliper of the thinnest part.
 *
 * This is the width a wall has to fit into to hollow a model, so it answers "can this shape take
 * a wall of this thickness at all". Every polygon of the layer is measured on its own and the
 * smallest of them is the answer, because a model of several separate blobs on a layer is as thin
 * as its thinnest blob. The caliper of a polygon is the smallest distance between two parallel
 * lines that enclose it, computed over the edges of its convex hull, so a concave region is as
 * thin as its narrowest neck. Zero when the layer holds no polygon with a shape, and the empty
 * layer therefore reads as "no cross section", never as a thin one.
 */
double min_cross_section_mm(const Domain::ExPolygons& slices);

} // namespace Slic3r::SLA