#include "Slic3r/App/Hints/SlaHints.hpp"

#include "Slic3r/Biz/I18N/I18N.hpp"

// Shorthand for the translator of the Biz layer.
using Slic3r::Biz::_u8L;

namespace Slic3r::App::Hints {

const std::vector<PlaterHint>& all_hints()
{
    // Only hints about features that actually exist in the app belong here. No FFF-only hint may
    // be reachable while an SLA printer is selected, so those carry HintAudience::Fff.
    static const std::vector<PlaterHint> hints = {
        // Shown once, when the first model lands on an SLA build plate.
        {_u8L("Next: add supports in the support tool, then press Slice."), HintAudience::Sla},
        {_u8L("Slicing does not add supports. Select a model, take the SLA Support Points tool and place points by hand, or press Generate or Auto support all."),
         HintAudience::Sla},
        {_u8L("Placing a support point builds its supports right away, on the build plate. Nothing is sliced until you press Slice."),
         HintAudience::Sla},
        {_u8L("Light, Medium and Heavy set the size of new supports. Fine-tune the sizes in Supports & raft."),
         HintAudience::Sla},
        {_u8L("The Layer image window in Preview shows the layer images the printer's screen shows."),
         HintAudience::Sla},
        {_u8L("Export writes the printer's own format (.sl1, .goo, .pm5 and others), so there is nothing to convert."),
         HintAudience::Sla},
        {_u8L("Pick the printer's removable drive as the export destination. When the export finishes, press Eject before you unplug it."),
         HintAudience::Sla},
    };
    return hints;
}

std::vector<PlaterHint> hints_for(Domain::PrinterTechnology technology)
{
    const HintAudience wanted{technology == Domain::PrinterTechnology::SLA ? HintAudience::Sla : HintAudience::Fff};

    std::vector<PlaterHint> ret;
    for (const PlaterHint& hint : all_hints()) {
        if (hint.audience == HintAudience::Any || hint.audience == wanted) {
            ret.push_back(hint);
        }
    }
    return ret;
}

const PlaterHint& first_model_on_sla_plate_hint()
{
    // all_hints() starts with the SLA next-step hint.
    return all_hints().front();
}

} // namespace Slic3r::App::Hints
