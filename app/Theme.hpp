#pragma once

#include <JuceHeader.h>
#include <array>

namespace velcal_ui {
constexpr auto background = 0xff10191e;
constexpr auto surface = 0xff1b2932;
constexpr auto border = 0xff3c5360;
constexpr auto text = 0xfff0f5fa;
constexpr auto muted = 0xffafc3d1;
constexpr auto accent = 0xff2bd9b0;
struct AccentOption { const char* name; juce::uint32 colour; };
inline constexpr std::array<AccentOption, 16> accents{{
    {"Teal", accent}, {"Green", 0xff79dc85}, {"Lime", 0xffc5df64}, {"Yellow", 0xffffdc70},
    {"Amber", 0xffffbd66}, {"Orange", 0xffff9c70}, {"Coral", 0xffff8c88}, {"Red", 0xffff747f},
    {"Rose", 0xffed93b6}, {"Pink", 0xffed8edc}, {"Lavender", 0xffc19aff}, {"Violet", 0xffac9bff},
    {"Blue", 0xff83b0ff}, {"Sky", 0xff6fcfff}, {"Cyan", 0xff62dddd}, {"Silver", 0xffc4d1dc},
}};

enum class Icon { none, folder, save, trash, file, play, reset, keyboard, brush, menu };

inline void drawIcon(juce::Graphics& g, Icon icon, juce::Rectangle<float> bounds)
{
    juce::Path p;
    switch (icon) {
        case Icon::folder:
            p.startNewSubPath(2, 5); p.lineTo(8, 5); p.lineTo(10, 8);
            p.lineTo(22, 8); p.lineTo(22, 21); p.lineTo(2, 21); p.closeSubPath();
            break;
        case Icon::save:
            p.startNewSubPath(3, 2); p.lineTo(18, 2); p.lineTo(22, 6);
            p.lineTo(22, 22); p.lineTo(3, 22); p.closeSubPath();
            p.addRectangle(7, 2, 9, 7); p.addRectangle(7, 14, 11, 8);
            break;
        case Icon::trash:
            p.startNewSubPath(3, 6); p.lineTo(21, 6);
            p.startNewSubPath(9, 6); p.lineTo(9, 3); p.lineTo(15, 3); p.lineTo(15, 6);
            p.startNewSubPath(5, 6); p.lineTo(6, 22); p.lineTo(18, 22); p.lineTo(19, 6);
            p.startNewSubPath(10, 10); p.lineTo(10, 18);
            p.startNewSubPath(14, 10); p.lineTo(14, 18);
            break;
        case Icon::file:
            p.startNewSubPath(5, 2); p.lineTo(14, 2); p.lineTo(20, 8);
            p.lineTo(20, 22); p.lineTo(5, 22); p.closeSubPath();
            p.startNewSubPath(14, 2); p.lineTo(14, 8); p.lineTo(20, 8);
            break;
        case Icon::play:
            p.addTriangle(6, 3, 21, 12, 6, 21);
            break;
        case Icon::reset:
            p.addCentredArc(12, 12, 9, 9, 0, -2.4f, 2.8f, true);
            p.startNewSubPath(2, 4); p.lineTo(2, 10); p.lineTo(8, 10);
            break;
        case Icon::keyboard:
            p.addRectangle(2, 3, 20, 18);
            for (int key = 0; key < 4; ++key) {
                const auto x = 6.0f + key * 4.0f;
                p.startNewSubPath(x, 3); p.lineTo(x, 21);
            }
            break;
        case Icon::brush:
            p.startNewSubPath(9, 13); p.lineTo(18, 3); p.quadraticTo(22, 0, 23, 4);
            p.lineTo(13, 16); p.closeSubPath();
            p.startNewSubPath(12, 16); p.quadraticTo(12, 22, 3, 22);
            p.quadraticTo(6, 20, 5, 17); p.quadraticTo(6, 12, 9, 13);
            break;
        case Icon::menu:
            for (const auto y : {5.0f, 12.0f, 19.0f}) {
                p.startNewSubPath(3, y); p.lineTo(21, y);
            }
            break;
        case Icon::none: return;
    }
    const auto transform = juce::AffineTransform::scale(bounds.getWidth() / 24.0f,
        bounds.getHeight() / 24.0f).translated(bounds.getX(), bounds.getY());
    if (icon == Icon::play)
        g.fillPath(p, transform);
    else
        g.strokePath(p, juce::PathStrokeType(1.7f, juce::PathStrokeType::curved,
            juce::PathStrokeType::rounded), transform);
}

class Theme final : public juce::LookAndFeel_V4 {
public:
    explicit Theme(juce::Component* owner = nullptr) : popupParent(owner)
    {
        setColour(juce::TextButton::buttonColourId, juce::Colour(surface));
        setColour(juce::TextButton::textColourOffId, juce::Colour(text));
        setColour(juce::ComboBox::backgroundColourId, juce::Colour(surface));
        setColour(juce::ComboBox::textColourId, juce::Colour(text));
        setColour(juce::ComboBox::outlineColourId, juce::Colour(border));
        setColour(juce::PopupMenu::backgroundColourId, juce::Colour(surface));
        setColour(juce::PopupMenu::textColourId, juce::Colour(text));
        setColour(juce::PopupMenu::highlightedBackgroundColourId, juce::Colour(0xff176c5d));
        setColour(juce::PopupMenu::highlightedTextColourId, juce::Colour(text));
        setColour(juce::Slider::textBoxOutlineColourId, juce::Colour(border));
        setColour(juce::TooltipWindow::backgroundColourId, juce::Colour(surface));
        setColour(juce::TooltipWindow::textColourId, juce::Colour(text));
        setColour(juce::TooltipWindow::outlineColourId, juce::Colour(border));
    }

    static Icon buttonIcon(const juce::Button& button)
    {
        return static_cast<Icon>(static_cast<int>(button.getProperties()["velcalIcon"]));
    }

    void setAccent(juce::uint32 colour)
    {
        currentAccent = colour;
        setColour(juce::PopupMenu::highlightedBackgroundColourId, juce::Colour(colour).darker(0.65f));
    }

    void drawButtonBackground(juce::Graphics& g, juce::Button& button,
        const juce::Colour& colour, bool hover, bool down) override
    {
        auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
        auto fill = colour;
        if (down) fill = fill.darker(0.15f);
        else if (hover) fill = fill.brighter(0.12f);
        if (!button.isEnabled()) fill = fill.withMultipliedAlpha(0.45f);
        g.setColour(fill);
        g.fillRoundedRectangle(bounds, 6);
        const auto primary = static_cast<bool>(button.getProperties()["velcalPrimary"]);
        if (static_cast<bool>(button.getProperties()["velcalSwatch"])) {
            const auto focused = button.hasKeyboardFocus(true);
            g.setColour(primary || focused ? juce::Colour(text) : juce::Colour(border));
            g.drawRoundedRectangle(bounds.reduced(1), 5, primary ? 3.0f : focused ? 2.0f : 1.0f);
            return;
        }
        g.setColour(juce::Colour(primary || button.hasKeyboardFocus(true) ? currentAccent : border)
            .withMultipliedAlpha(button.isEnabled() ? 1.0f : 0.4f));
        g.drawRoundedRectangle(bounds, 6, primary ? 1.5f : 1.0f);
    }

    void drawButtonText(juce::Graphics& g, juce::TextButton& button, bool, bool) override
    {
        const auto icon = buttonIcon(button);
        auto bounds = button.getLocalBounds().toFloat().reduced(10, 0);
        g.setColour(button.findColour(juce::TextButton::textColourOffId)
            .withMultipliedAlpha(button.isEnabled() ? 1.0f : 0.45f));
        if (icon != Icon::none) {
            drawIcon(g, icon, juce::Rectangle<float>(button.getButtonText().isEmpty()
                ? bounds.getCentreX() - 9 : bounds.getX(), bounds.getCentreY() - 9, 18, 18));
            bounds.removeFromLeft(26);
        }
        g.setFont(juce::FontOptions(button.getHeight() >= 42 ? 15.0f : 13.0f));
        g.drawFittedText(button.getButtonText(), bounds.toNearestInt(), juce::Justification::centred, 1);
    }

    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& button, bool hover, bool) override
    {
        auto bounds = button.getLocalBounds().toFloat();
        const auto routing = static_cast<bool>(button.getProperties()["velcalRouting"]);
        const auto switchX = routing ? bounds.getRight() - 46 : bounds.getX();
        auto track = juce::Rectangle<float>(switchX, bounds.getCentreY() - 11, 44, 22);
        g.setColour((button.getToggleState() ? juce::Colour(currentAccent).darker(0.35f) : juce::Colour(border))
            .withMultipliedAlpha(button.isEnabled() ? 1.0f : 0.4f));
        g.fillRoundedRectangle(track, 11);
        const auto thumb = juce::Colour(button.getToggleState() ? text : muted)
            .withMultipliedAlpha(button.isEnabled() ? 1.0f : 0.4f);
        g.setColour(hover ? thumb.brighter(0.1f) : thumb);
        g.fillEllipse(track.getX() + (button.getToggleState() ? 24 : 3), track.getY() + 3, 16, 16);
        if (routing) bounds.removeFromRight(56);
        else bounds.removeFromLeft(54);
        g.setColour(juce::Colour(text).withMultipliedAlpha(button.isEnabled() ? 1.0f : 0.45f));
        g.setFont(14);
        g.drawFittedText(button.getButtonText(), bounds.toNearestInt(), juce::Justification::centredLeft, 1);
    }

    void drawComboBox(juce::Graphics& g, int width, int height, bool,
        int, int, int, int, juce::ComboBox& box) override
    {
        auto bounds = juce::Rectangle<float>(0, 0, static_cast<float>(width), static_cast<float>(height)).reduced(0.5f);
        g.setColour(box.findColour(juce::ComboBox::backgroundColourId));
        g.fillRoundedRectangle(bounds, 6);
        g.setColour(box.hasKeyboardFocus(true) ? juce::Colour(currentAccent) : juce::Colour(border));
        g.drawRoundedRectangle(bounds, 6, 1);
        if (static_cast<bool>(box.getProperties()["velcalKeyboard"])) {
            g.setColour(juce::Colour(text));
            drawIcon(g, Icon::keyboard, {12, height * 0.5f - 10, 20, 20});
        }
        juce::Path arrow;
        arrow.startNewSubPath(static_cast<float>(width - 26), height * 0.45f);
        arrow.lineTo(static_cast<float>(width - 20), height * 0.58f);
        arrow.lineTo(static_cast<float>(width - 14), height * 0.45f);
        g.setColour(juce::Colour(text));
        g.strokePath(arrow, juce::PathStrokeType(1.7f));
    }

    juce::Font getComboBoxFont(juce::ComboBox&) override { return juce::FontOptions(15.0f); }
    juce::PopupMenu::Options getOptionsForComboBoxPopupMenu(juce::ComboBox& box, juce::Label& label) override
    {
        return juce::LookAndFeel_V4::getOptionsForComboBoxPopupMenu(box, label)
            .withParentComponent(popupParent != nullptr ? popupParent : box.getTopLevelComponent())
            .withItemThatMustBeVisible(0)
            .withPreferredPopupDirection(juce::PopupMenu::Options::PopupDirection::downwards)
            .withStandardItemHeight(36);
    }
    void positionComboBoxText(juce::ComboBox& box, juce::Label& label) override
    {
        const auto inset = static_cast<bool>(box.getProperties()["velcalKeyboard"]) ? 42 : 12;
        label.setBounds(inset, 1, juce::jmax(0, box.getWidth() - inset - 32), box.getHeight() - 2);
        label.setFont(getComboBoxFont(box));
    }

private:
    juce::Component* popupParent{};
    juce::uint32 currentAccent{accent};
};
} // namespace velcal_ui
