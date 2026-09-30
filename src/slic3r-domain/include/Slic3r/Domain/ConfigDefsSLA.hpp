#pragma once

#include "Slic3r/Domain/ConfigDef.hpp"
namespace Slic3r::Domain {

// Next, define all enums that should be used in the config.
enum class SLADisplayOrientation {
    sladoLandscape,
    sladoPortrait
};

enum SLAMaterialSpeed { slamsSlow, slamsFast, slamsHighViscosity };

namespace sla {
    enum class SupportTreeType { Default, Branching, Organic };
    enum class PillarConnectionMode { zigzag, cross, dynamic };
    enum class RaftType { None, Full, AroundObject, Skate };
    // What the inside of the raft is filled with between the top skin and the build plate.
    enum class RaftInfillType { None, Grid, Honeycomb };
    // The shape of a support tip where it touches the model, as support_tip_shape stores it. The
    // same three values live on the point itself (SLA::SupportPoint::TipShape), which is where the
    // value is used once it is placed.
    enum class SupportTipShape { Default, Cone, Ball };
    // The material of the film at the bottom of the vat. It is the hardest pull a layer puts on
    // the film, so it picks the coefficients of the peel force estimate, see the vat_film_type
    // key and doc/sla-fork/profiling/peel-force.md.
    enum class VatFilmType { FEP, nFEP, PFA, ACF };
}

enum TowerSpeeds : int {
    tsLayer1,
    tsLayer2,
    tsLayer3,
    tsLayer4,
    tsLayer5,
    tsLayer8,
    tsLayer11,
    tsLayer14,
    tsLayer18, 
    tsLayer22,
    tsLayer24,
};

enum TiltSpeeds : int {
    tsMove120,
    tsLayer200,
    tsMove300,
    tsLayer400,
    tsLayer600,
    tsLayer800,
    tsLayer1000,
    tsLayer1250,
    tsLayer1500,
    tsLayer1750,
    tsLayer2000,
    tsLayer2250,
    tsMove5120,
    tsMove8000,
};

const ConfigDefinitions& get_defs_sla();

} // namespace Slic3r::Domain
