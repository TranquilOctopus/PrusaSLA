#include "Slic3r/App/Plater/PaintSupportsRules.hpp"

#include "Slic3r/Biz/I18N/I18N.hpp"

namespace Slic3r::App::Plater {

PaintSupportsStrings paint_supports_strings(const Domain::PrinterTechnology technology)
{
    if (technology == Domain::PrinterTechnology::SLA) {
        return PaintSupportsStrings{
            /* tool_name */ Biz::_u8L("Paint supports"),
            /* paint */ Biz::_u8L("Paint supports"),
            /* block */ Biz::_u8L("Block supports"),
            /* remove */ Biz::_u8L("Remove paint"),
            /* block_hint */
            Biz::_u8L(
                "A blocked region gets no automatic support point. A floating island keeps its "
                "point anyway: it falls off the build without one. Run Auto support to place the "
                "painted support points, painting itself never slices."
            )
        };
    }

    return PaintSupportsStrings{
        /* tool_name */ Biz::_u8L("Paint-on supports"),
        /* paint */ Biz::_u8L("Paint"),
        /* block */ Biz::_u8L("Block"),
        /* remove */ Biz::_u8L("Remove"),
        /* block_hint */ {}
    };
}

bool paint_supports_automatic_painting_visible(const Domain::PrinterTechnology technology)
{
    return technology != Domain::PrinterTechnology::SLA;
}

} // namespace Slic3r::App::Plater