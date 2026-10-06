#pragma once

#include "velcal/profile.hpp"
#include "MidiEngine.hpp"
#include "PluginState.hpp"
#include "Theme.hpp"

#include <JuceHeader.h>

#include <memory>
#include <optional>

class MainComponent final : public juce::Component,
                            private juce::ComboBox::Listener,
                            private juce::Button::Listener,
                            private juce::Slider::Listener,
                            private juce::ScrollBar::Listener,
                            private juce::Timer {
public:
    explicit MainComponent(PluginState* plugin = nullptr);
    ~MainComponent() override;
    void requestClose(std::function<void()> close);

    void paint(juce::Graphics& graphics) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;

private:
    friend struct MainComponentTestAccess;
    void comboBoxChanged(juce::ComboBox* comboBox) override;
    void buttonClicked(juce::Button* button) override;
    void sliderValueChanged(juce::Slider* slider) override;
    void scrollBarMoved(juce::ScrollBar* scrollBar, double newRangeStart) override;
    void mouseWheelMove(
        const juce::MouseEvent& event,
        const juce::MouseWheelDetails& wheel) override;
    void timerCallback() override;
    void refreshUpdateStatus();
    void refreshMidiInputs();
    void refreshMidiOutputs();
    void loadAppState();
    void saveAppState() const;
    void loadAppearance(bool restoreTab = false);
    bool saveAppearancePreference(const char* key, const std::string& value);
    void applyAccent(juce::uint32 colour);
    void chooseAccent(std::size_t index);
    void showThemePalette();
    void restoreMidiSelections(
        const juce::String& inputIdentifier,
        const juce::String& inputName,
        const juce::String& outputIdentifier,
        const juce::String& outputName,
        bool useVirtualOutput);
    void refreshProfileList();
    void selectProfileInList(const juce::File& file);
    void updateRouting();
    void updateCapture();
    void showCaptureGuide();
    void beginSectionCapture();
    void finishSectionCapture();
    void updateCaptureControls();
    void chooseProfile();
    void loadProfile(const juce::File& file);
    void loadProfileConfirmed(const juce::File& file);
    void confirmDiscardUnsaved(std::function<void()> action);
    void markProfileDirty();
    void deleteSelectedProfile();
    void deleteProfileConfirmed(const juce::File& file);
    void createNewProfile();
    void replaceWithNewProfile();
    void clearMeasurements();
    void clearMeasurementsConfirmed();
    void saveCurrentProfile();
    void writeProfile(const juce::File& file);
    void setActiveTab(bool globalCurveTab, bool remember = false);
    void refreshCurvePresets();
    void applySelectedCurvePreset();
    void saveCurvePreset();
    void updateEffectiveMaps();
    void publishPluginState() const;
    void syncPluginState();
    juce::File profileDirectory() const;
    void updateEditingControls();
    void ensureEditableCurve();
    std::vector<velcal::VelocityCurvePoint> sampledCurrentCurve() const;
    std::vector<velcal::VelocityCurvePoint>& editableCurvePoints();
    bool& editableCurveSmooth();
    juce::Rectangle<float> curvePlotBounds() const;
    void paintCurveAxes(juce::Graphics& graphics, juce::Rectangle<float> plot);
    void paintCurveHandles(
        juce::Graphics& graphics,
        juce::Rectangle<float> plot,
        const std::vector<velcal::VelocityCurvePoint>& points);
    void updateLabels();
    void paintKeyboard(juce::Graphics& graphics, juce::Rectangle<float> bounds);
    void paintCurve(juce::Graphics& graphics, juce::Rectangle<float> bounds);
    void paintGlobalCurve(juce::Graphics& graphics, juce::Rectangle<float> bounds);
    void paintMetric(
        juce::Graphics& graphics,
        juce::Rectangle<float> bounds,
        const juce::String& label,
        const juce::String& value,
        juce::Colour accent);
    std::optional<std::uint8_t> noteAtPosition(juce::Point<float> position) const;
    static juce::String midiNoteName(std::uint8_t note);

    velcal_ui::Theme theme{this};
    std::shared_ptr<UpdateCheck> updateCheck;
    juce::Image brandIcon;
    juce::Label titleLabel;
    juce::TextButton perKeyTabButton{"Per-key calibration"};
    juce::TextButton globalTabButton{"Global curve"};
    juce::Label deviceLabel;
    juce::ComboBox midiInputBox;
    juce::Label outputLabel;
    juce::ComboBox midiOutputBox;
    juce::ToggleButton routingToggle{"Route MIDI"};
    juce::Label keyGroupLabel;
    juce::ComboBox keyGroupBox;
    juce::TextButton captureButton{"Start section"};
    juce::TextButton newProfileButton{"New profile"};
    juce::TextButton clearProfileButton{"Clear data"};
    juce::TextButton openProfileButton{"Open profile"};
    juce::TextButton saveProfileButton{"Save profile"};
    juce::TextButton themeButton;
    juce::TextButton deleteProfileButton{"Delete"};
    juce::Label keyAdjustmentLabel;
    juce::Slider keyAdjustmentSlider;
    juce::TextButton resetKeyButton{"Reset key"};
    juce::Label presetLabel;
    juce::ComboBox globalPresetBox;
    juce::Label curvatureLabel;
    juce::Slider curvatureSlider;
    juce::Label minimumVelocityLabel;
    juce::Slider minimumVelocitySlider;
    juce::Label maximumVelocityLabel;
    juce::Slider maximumVelocitySlider;
    juce::TextButton savePresetButton{"Save preset"};
    juce::TextButton resetGlobalButton{"Reset curve"};
    juce::ToggleButton smoothCurveToggle{"Smooth"};
    juce::ScrollBar keyboardScrollBar{false};
    juce::ComboBox profileBox;
    juce::Label selectedNoteLabel;
    juce::Label statusLabel;
    juce::Label updateStatusLabel;
    std::unique_ptr<juce::FileChooser> fileChooser;
    std::unique_ptr<juce::Component> captureGuide;
    std::unique_ptr<juce::Component> themePalette;
    std::unique_ptr<juce::CallOutBox> themePopup;
    juce::uint32 green{velcal_ui::accent};
    int appearancePollTicks{};
    juce::Array<juce::MidiDeviceInfo> midiInputs;
    juce::Array<juce::MidiDeviceInfo> midiOutputs;
    juce::Array<juce::File> profileFiles;
    PluginState* pluginState{};
    mutable std::uint64_t pluginRevision{};
    bool pluginStateLoaded{};
    std::unique_ptr<MidiEngine> ownedMidiEngine;
    MidiEngine& midiEngine;
    std::optional<velcal::CalibrationProfile> profile;
    juce::File profileFile;
    std::uint64_t captureStartMessageCount{};
    std::uint8_t selectedNote{60};
    bool showingGlobalCurve{};
    bool updatingControls{};
    bool updatingProfileList{};
    bool profileDirty{};
    bool discardPromptOpen{};
    std::optional<std::size_t> activeCurvePoint;
    juce::Rectangle<float> keyboardBounds;
    juce::Rectangle<float> keyboardKeyBounds;
    juce::Rectangle<float> curveBounds;
    juce::Rectangle<float> metricsBounds;
    juce::Rectangle<float> adjustmentBounds;
    juce::TooltipWindow tooltipWindow{this, 700};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
