#pragma once

#include "Slic3r/App/Yoga/Dialog.hpp"
#include "Slic3r/Biz/ResinProfile/ResinDatasheetForm.hpp"
#include "Slic3r/Biz/ResinProfile/ForeignResinProfile.hpp"

#include <functional>
#include <string>

namespace Slic3r::App {

class Navigator;

namespace Yoga {
class InputTextField;
class LayoutButton;
class Text;
} // namespace Yoga

/**
 * @brief The "New resin from datasheet" form of M3.11: the values a resin datasheet states, typed
 * in, and nothing else.
 *
 * A vendor datasheet is a table of numbers, and for a resin whose profile the user does not have
 * this is the way in: there is no file to import, so the values are the input. "Next" turns the form
 * into a ForeignResinProfile and hands it to the import review dialog, which is the same dialog a
 * profile read from a file gets: the same base picker, the same mapping table, the same save. What
 * the form does not do is decide anything - what each value becomes is the mapper's work, and the
 * review table shows it.
 *
 * The fields are plain text and the numbers are not read here: what the form accepts is
 * validate_datasheet(), a pure function of the form, so the same rule is what the tests check and
 * what the dialog enforces on "Next".
 */
class ResinDatasheetDialog : public Yoga::Dialog
{
public:
    /// @brief Called with the profile the form describes when the user goes on to the review.
    using NextCallback = std::function<void(const Biz::ResinProfile::ForeignResinProfile&)>;

    ResinDatasheetDialog(Navigator& navigator);

    /// @brief Who to hand the finished profile to, which is the review dialog.
    void set_next_callback(NextCallback callback);

    /**
     * @brief Show the form with the last values still in it and open it.
     *
     * What the values were is kept, so coming back to the form to fix one number does not mean
     * typing the other five again. The dialog navigation shows the form through the Popup::open()
     * behind this, which is why this one is a new name and not a new behaviour of that.
     */
    void open();

protected:
    void close_action() override;

private:
    /// @brief The values of the fields, as typed.
    Biz::ResinProfile::ResinDatasheet typed_datasheet() const;

    /// @brief Check the form and, when it is filled in, hand the profile over to the review dialog.
    void next();

    /// @brief Show @p error, or hide the line when it is empty.
    void set_error(const std::string& error);

private:
    Navigator& m_navigator;
    NextCallback m_next_callback;

    Yoga::Text* m_error_line{nullptr};
    Yoga::InputTextField* m_name_input{nullptr};
    Yoga::InputTextField* m_vendor_input{nullptr};
    Yoga::InputTextField* m_layer_height_input{nullptr};
    Yoga::InputTextField* m_normal_exposure_input{nullptr};
    Yoga::InputTextField* m_bottom_exposure_input{nullptr};
    Yoga::InputTextField* m_bottom_layers_input{nullptr};
    Yoga::InputTextField* m_light_off_delay_input{nullptr};
    Yoga::InputTextField* m_price_input{nullptr};
    Yoga::InputTextField* m_bottle_volume_input{nullptr};
    Yoga::InputTextField* m_lift_distance_input{nullptr};
    Yoga::InputTextField* m_lift_speed_input{nullptr};
    Yoga::InputTextField* m_retract_speed_input{nullptr};
    Yoga::InputTextField* m_transition_layers_input{nullptr};
    Yoga::LayoutButton* m_next_button{nullptr};
};

} // namespace Slic3r::App
