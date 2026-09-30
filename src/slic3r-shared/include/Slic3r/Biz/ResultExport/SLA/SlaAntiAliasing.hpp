#pragma once

#include "Slic3r/Domain/Config.hpp"

#include <cstdint>

namespace Slic3r::Biz::PrintHost::Sla {

/// M4.13b. Whether the layers of an exported print are anti-aliased. One setting decides it:
/// `gamma_correction`, which the rasterizer turns into the agg gamma function
/// (sla::create_raster_grayscale_aa, Format/SL1.cpp and the other three). Zero thresholds the
/// raster to 0 or 255 and anything above zero anti-aliases it, so a header that states the
/// anti-aliasing of the file states that setting and nothing else.
///
/// A config view that does not carry the setting is one that never got a printer, and the setting's
/// own default is 1 (ConfigDefsSLA.cpp), so that case reads as anti-aliased: it leaves every export
/// that did not ask for a binary image byte for byte as it was.
bool sla_raster_anti_aliased(const Domain::ConfigView& cfg);

/// The value of an anti-aliasing level or flag for a layer that is anti-aliased, and the "no
/// anti-aliasing" value of the same field for a layer that was thresholded. The first is what every
/// one of these writers has always written, so an anti-aliased export does not move by a byte.
constexpr std::uint32_t SLA_AA_LEVEL_ANTI_ALIASED = 1;
constexpr std::uint32_t SLA_AA_LEVEL_BINARY       = 0;

/// The grey depth of a layer image, counted in bits. The nibble encoders of .goo and
/// .pwmo/.pwmx/.pwms write four of them (16 greys) for an anti-aliased layer; a thresholded raster
/// has one, lit or not.
constexpr std::uint32_t SLA_GREY_BITS_ANTI_ALIASED = 4;
constexpr std::uint32_t SLA_GREY_BITS_BINARY       = 1;

/// Unverified: there is no sample for any of the three containers that carry these fields. The
/// Photon Workshop .pm5 sample records 16 grey levels, which matches the encoder, but it says
/// nothing about the anti-aliasing flag of .pwmo/.pwmx/.pwms at the same offset, and no .goo or
/// .ctb file has been compared against its writer. See
/// doc/sla-fork/profiling/aa-and-z-correction.md for the per-container reading of each field.

} // namespace Slic3r::Biz::PrintHost::Sla
