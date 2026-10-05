#include "MainComponent.hpp"
#include "DataPaths.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>

#include <nlohmann/json.hpp>

namespace {

constexpr auto background = 0xff16191d;
constexpr auto panel = 0xff20252a;
constexpr auto panelRaised = 0xff282e34;
constexpr auto textPrimary = 0xfff1f3f4;
constexpr auto textMuted = 0xff9ea7ad;
constexpr auto green = 0xff4fc58b;
constexpr auto amber = 0xffe8ad4b;
constexpr auto cyan = 0xff62b7c9;
constexpr auto red = 0xffdc6b69;
constexpr auto selection = 0xffff4fd8;
constexpr int firstPianoNote = 21;
constexpr int lastPianoNote = 108;
constexpr int pianoWhiteKeyCount = 52;
constexpr float minimumWhiteKeyWidth = 18.0f;
constexpr int profileComboBaseId = 1000;

class CaptureGuide final : public juce::Component {
public:
    explicit CaptureGuide(const juce::String& groupName)
    {
        setName("Calibrate a key section");
        setWantsKeyboardFocus(true);
        title.setText(getName(), juce::dontSendNotification);
        title.setFont(juce::FontOptions(18.0f, juce::Font::bold));
        title.setColour(juce::Label::textColourId, juce::Colour(textPrimary));
        addAndMakeVisible(title);
        instructions.setText(
            "1. Place the bar over one continuous section of " + groupName
                + " keys.\n\n2. Press the bar down steadily and fully release it each time. Keep the same keys covered each time.\n\n3. Make soft, medium, and firm bar presses. VelCal will watch the weakest covered key and tell you which strength is still needed.\n\n4. Select Finish section when VelCal says the bar section is ready.",
            juce::dontSendNotification);
        instructions.setFont(juce::FontOptions(14.0f));
        instructions.setJustificationType(juce::Justification::topLeft);
        instructions.setColour(juce::Label::textColourId, juce::Colour(textPrimary));
        addAndMakeVisible(instructions);
        begin.onClick = [this] { exitModalState(1); };
        cancel.onClick = [this] { exitModalState(0); };
        begin.addShortcut(juce::KeyPress(juce::KeyPress::returnKey));
        cancel.addShortcut(juce::KeyPress(juce::KeyPress::escapeKey));
        addAndMakeVisible(begin);
        addAndMakeVisible(cancel);
    }

    void paint(juce::Graphics& graphics) override
    {
        graphics.fillAll(juce::Colours::black.withAlpha(0.45f));
        graphics.setColour(juce::Colour(panelRaised));
        graphics.fillRoundedRectangle(dialogBounds.toFloat(), 6.0f);
        graphics.setColour(juce::Colour(textMuted));
        graphics.drawRoundedRectangle(dialogBounds.toFloat(), 6.0f, 1.0f);
    }

    void resized() override
    {
        dialogBounds = getLocalBounds().withSizeKeepingCentre(
            juce::jmin(480, getWidth() - 32), juce::jmin(390, getHeight() - 32));
        auto content = dialogBounds.reduced(24);
        title.setBounds(content.removeFromTop(30));
        content.removeFromTop(12);
        auto buttons = content.removeFromBottom(38);
        cancel.setBounds(buttons.removeFromRight(88));
        buttons.removeFromRight(12);
        begin.setBounds(buttons.removeFromRight(140));
        content.removeFromBottom(16);
        instructions.setBounds(content);
    }

private:
    juce::Rectangle<int> dialogBounds;
    juce::Label title;
    juce::Label instructions;
    juce::TextButton begin{"Begin capture"};
    juce::TextButton cancel{"Cancel"};
};

juce::File standaloneProfileDirectory()
{
    return velcalProfileDirectory();
}

juce::File appStateFile()
{
    return standaloneProfileDirectory().getChildFile(".velcal-app-state.json");
}

juce::String profileDisplayName(const juce::File& file)
{
    auto name = file.getFileNameWithoutExtension();
    if (name.endsWithIgnoreCase(".velcal"))
        name = name.dropLastCharacters(7);
    return name;
}

std::filesystem::path juceFilePath(const juce::File& file)
{
    return std::filesystem::u8path(file.getFullPathName().toRawUTF8());
}

const std::array<velcal::VelocityCurveSettings, 5>& defaultCurvePresets()
{
    static const std::array<velcal::VelocityCurveSettings, 5> presets{{
        {"Linear", 0.0, 1, 127},
        {"Soft touch", -0.35, 1, 127},
        {"Firm touch", 0.35, 1, 127},
        {"Compressed", 0.0, 18, 110},
        {"Wide dynamics", 0.18, 1, 127},
    }};
    return presets;
}

void styleSlider(juce::Slider& slider)
{
    slider.setSliderStyle(juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 52, 24);
    slider.setColour(juce::Slider::trackColourId, juce::Colour(green));
    slider.setColour(juce::Slider::backgroundColourId, juce::Colour(0xff394147));
    slider.setColour(juce::Slider::thumbColourId, juce::Colour(textPrimary));
    slider.setColour(juce::Slider::textBoxTextColourId, juce::Colour(textPrimary));
    slider.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(panelRaised));
    slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colour(0xff414a51));
}

double evaluateParametricCurve(
    const velcal::VelocityCurveSettings& curve,
    const double input)
{
    const auto normalized = std::clamp((input - 1.0) / 126.0, 0.0, 1.0);
    const auto curvature = std::clamp(curve.curvature, -1.0, 1.0);
    const auto exponent = curvature >= 0.0
        ? 1.0 + curvature * 3.0
        : 1.0 / (1.0 - curvature * 3.0);
    return curve.minimumOutput
        + std::pow(normalized, exponent)
            * (curve.maximumOutput - curve.minimumOutput);
}

juce::Colour responseColour(const double bias, const double confidence)
{
    const auto neutral = juce::Colour(0xff69737a);
    if (confidence <= 0.0)
        return neutral.withAlpha(0.45f);
    const auto intensity = static_cast<float>(std::min(1.0, std::abs(bias) / 8.0));
    const auto target = bias > 0.0 ? juce::Colour(red) : juce::Colour(cyan);
    return neutral.interpolatedWith(target, intensity).withMultipliedAlpha(
        static_cast<float>(0.55 + 0.45 * confidence));
}

bool hasCompleteRegionalCoverage(
    const velcal::NoteCalibrationStats& stats,
    const std::size_t target)
{
    return std::all_of(
        stats.samplesUsedByRegion.begin(), stats.samplesUsedByRegion.end(),
        [target](const auto count) { return count >= target; });
}

juce::String localMidiNoteName(const std::uint8_t note)
{
    static constexpr const char* names[] = {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    return juce::String(names[note % 12]) + juce::String(static_cast<int>(note) / 12 - 1);
}

juce::String regionName(const std::size_t region)
{
    if (region == 0)
        return "soft";
    if (region == 1)
        return "medium";
    return "firm";
}

struct CaptureGuidance {
    bool ready{};
    juce::String countsText;
    juce::String statusText;
    juce::String warningText;
};

CaptureGuidance makeCaptureGuidance(
    const velcal::SectionCaptureAnalysis& analysis,
    const velcal::KeyGroup group,
    const velcal::CalibrationConfig& config,
    const std::vector<velcal::CalibrationPress>* existingPresses)
{
    CaptureGuidance guidance;
    if (!analysis.rangeInferred) {
        guidance.statusText = "Press and release the same key section at least 3 times";
        return guidance;
    }

    std::vector<velcal::CalibrationPress> trialPresses;
    if (existingPresses != nullptr)
        trialPresses = *existingPresses;
    trialPresses.insert(trialPresses.end(), analysis.presses.begin(), analysis.presses.end());
    const auto trialResult = velcal::calibrate(trialPresses, config);
    const auto target = std::max<std::size_t>(1, config.desiredSamplesPerRegion);

    std::array<std::size_t, 3> weakestCounts{
        std::numeric_limits<std::size_t>::max(),
        std::numeric_limits<std::size_t>::max(),
        std::numeric_limits<std::size_t>::max()};
    std::array<std::uint8_t, 3> weakestNotes{};
    std::size_t coveredKeys = 0;
    for (int note = analysis.lowestNote; note <= analysis.highestNote; ++note) {
        const auto midiNote = static_cast<std::uint8_t>(note);
        if (!velcal::belongsToGroup(midiNote, group))
            continue;
        ++coveredKeys;
        const auto& stats = trialResult.noteStats[static_cast<std::size_t>(midiNote)];
        for (std::size_t region = 0; region < weakestCounts.size(); ++region) {
            const auto count = stats.samplesUsedByRegion[region];
            if (count < weakestCounts[region]) {
                weakestCounts[region] = count;
                weakestNotes[region] = midiNote;
            }
        }
    }

    if (coveredKeys == 0) {
        guidance.statusText = "No matching keys found in this section";
        return guidance;
    }

    guidance.countsText = localMidiNoteName(analysis.lowestNote) + "-"
        + localMidiNoteName(analysis.highestNote)
        + "  |  Soft "
        + juce::String(static_cast<int>(std::min(weakestCounts[0], target))) + "/"
        + juce::String(static_cast<int>(target))
        + "  |  Medium "
        + juce::String(static_cast<int>(std::min(weakestCounts[1], target))) + "/"
        + juce::String(static_cast<int>(target))
        + "  |  Firm "
        + juce::String(static_cast<int>(std::min(weakestCounts[2], target))) + "/"
        + juce::String(static_cast<int>(target));

    std::size_t neededRegion = 0;
    std::size_t neededSamples = 0;
    for (std::size_t region = 0; region < weakestCounts.size(); ++region) {
        if (weakestCounts[region] >= target)
            continue;
        const auto missing = target - weakestCounts[region];
        if (missing > neededSamples) {
            neededSamples = missing;
            neededRegion = region;
        }
    }

    if (neededSamples == 0) {
        guidance.ready = true;
        guidance.statusText = "Ready to finish this bar section";
        return guidance;
    }

    guidance.statusText = "Need "
        + juce::String(static_cast<int>(neededSamples)) + " more "
        + regionName(neededRegion) + " bar "
        + (neededSamples == 1 ? "press" : "presses")
        + "  |  weakest key "
        + localMidiNoteName(weakestNotes[neededRegion]);
    guidance.warningText = "This section still needs "
        + juce::String(static_cast<int>(neededSamples)) + " more usable "
        + regionName(neededRegion) + " bar "
        + (neededSamples == 1 ? "press" : "presses")
        + ". Finish anyway?";
    return guidance;
}

} // namespace

MainComponent::MainComponent(PluginState* plugin)
    : pluginState(plugin), ownedMidiEngine(plugin ? nullptr : std::make_unique<MidiEngine>()),
      midiEngine(plugin ? plugin->midi : *ownedMidiEngine)
{
    setOpaque(true);
    setWantsKeyboardFocus(true);

    titleLabel.setText("VelCal", juce::dontSendNotification);
    titleLabel.setFont(juce::FontOptions(24.0f, juce::Font::bold));
    titleLabel.setColour(juce::Label::textColourId, juce::Colour(textPrimary));
    addAndMakeVisible(titleLabel);

    for (auto* button : {&perKeyTabButton, &globalTabButton}) {
        button->addListener(this);
        button->setClickingTogglesState(false);
        addAndMakeVisible(*button);
    }

    deviceLabel.setText("MIDI input", juce::dontSendNotification);
    deviceLabel.setColour(juce::Label::textColourId, juce::Colour(textMuted));
    addAndMakeVisible(deviceLabel);

    midiInputBox.addListener(this);
    midiInputBox.setColour(juce::ComboBox::backgroundColourId, juce::Colour(panelRaised));
    midiInputBox.setColour(juce::ComboBox::textColourId, juce::Colour(textPrimary));
    midiInputBox.setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff414a51));
    addAndMakeVisible(midiInputBox);

    outputLabel.setText("MIDI output", juce::dontSendNotification);
    outputLabel.setColour(juce::Label::textColourId, juce::Colour(textMuted));
    addAndMakeVisible(outputLabel);

    midiOutputBox.addListener(this);
    midiOutputBox.setColour(juce::ComboBox::backgroundColourId, juce::Colour(panelRaised));
    midiOutputBox.setColour(juce::ComboBox::textColourId, juce::Colour(textPrimary));
    midiOutputBox.setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff414a51));
    addAndMakeVisible(midiOutputBox);

    routingToggle.addListener(this);
    routingToggle.setColour(juce::ToggleButton::textColourId, juce::Colour(textPrimary));
    routingToggle.setColour(juce::ToggleButton::tickColourId, juce::Colour(green));
    addAndMakeVisible(routingToggle);

    keyGroupLabel.setText("Calibration keys", juce::dontSendNotification);
    keyGroupLabel.setColour(juce::Label::textColourId, juce::Colour(textMuted));
    addAndMakeVisible(keyGroupLabel);

    keyGroupBox.addItem("White keys", 1);
    keyGroupBox.addItem("Black keys", 2);
    keyGroupBox.setSelectedId(1, juce::dontSendNotification);
    keyGroupBox.setColour(juce::ComboBox::backgroundColourId, juce::Colour(panelRaised));
    keyGroupBox.setColour(juce::ComboBox::textColourId, juce::Colour(textPrimary));
    keyGroupBox.setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff414a51));
    addAndMakeVisible(keyGroupBox);

    captureButton.addListener(this);
    captureButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff3c765d));
    captureButton.setColour(juce::TextButton::textColourOffId, juce::Colour(textPrimary));
    addAndMakeVisible(captureButton);

    newProfileButton.addListener(this);
    newProfileButton.setColour(juce::TextButton::buttonColourId, juce::Colour(panelRaised));
    newProfileButton.setColour(juce::TextButton::textColourOffId, juce::Colour(textPrimary));
    addAndMakeVisible(newProfileButton);

    clearProfileButton.addListener(this);
    clearProfileButton.setColour(juce::TextButton::buttonColourId, juce::Colour(panelRaised));
    clearProfileButton.setColour(juce::TextButton::textColourOffId, juce::Colour(textPrimary));
    addAndMakeVisible(clearProfileButton);

    openProfileButton.addListener(this);
    openProfileButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff3c765d));
    openProfileButton.setColour(juce::TextButton::textColourOffId, juce::Colour(textPrimary));
    addAndMakeVisible(openProfileButton);

    saveProfileButton.addListener(this);
    saveProfileButton.setColour(juce::TextButton::buttonColourId, juce::Colour(panelRaised));
    saveProfileButton.setColour(juce::TextButton::textColourOffId, juce::Colour(textPrimary));
    addAndMakeVisible(saveProfileButton);

    deleteProfileButton.addListener(this);
    deleteProfileButton.setColour(juce::TextButton::buttonColourId, juce::Colour(panelRaised));
    deleteProfileButton.setColour(juce::TextButton::textColourOffId, juce::Colour(textPrimary));
    addAndMakeVisible(deleteProfileButton);

    keyAdjustmentLabel.setText("Selected-key adjustment", juce::dontSendNotification);
    keyAdjustmentLabel.setColour(juce::Label::textColourId, juce::Colour(textMuted));
    addAndMakeVisible(keyAdjustmentLabel);
    styleSlider(keyAdjustmentSlider);
    keyAdjustmentSlider.setRange(-24.0, 24.0, 1.0);
    keyAdjustmentSlider.setDoubleClickReturnValue(true, 0.0);
    keyAdjustmentSlider.addListener(this);
    addAndMakeVisible(keyAdjustmentSlider);
    resetKeyButton.addListener(this);
    addAndMakeVisible(resetKeyButton);

    presetLabel.setText("Preset", juce::dontSendNotification);
    presetLabel.setColour(juce::Label::textColourId, juce::Colour(textMuted));
    addAndMakeVisible(presetLabel);
    globalPresetBox.addListener(this);
    globalPresetBox.setColour(juce::ComboBox::backgroundColourId, juce::Colour(panelRaised));
    globalPresetBox.setColour(juce::ComboBox::textColourId, juce::Colour(textPrimary));
    globalPresetBox.setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff414a51));
    addAndMakeVisible(globalPresetBox);

    curvatureLabel.setText("Touch curve", juce::dontSendNotification);
    minimumVelocityLabel.setText("Minimum output", juce::dontSendNotification);
    maximumVelocityLabel.setText("Maximum output", juce::dontSendNotification);
    for (auto* label : {&curvatureLabel, &minimumVelocityLabel, &maximumVelocityLabel}) {
        label->setColour(juce::Label::textColourId, juce::Colour(textMuted));
        addAndMakeVisible(*label);
    }
    styleSlider(curvatureSlider);
    curvatureSlider.setRange(-1.0, 1.0, 0.01);
    curvatureSlider.setDoubleClickReturnValue(true, 0.0);
    styleSlider(minimumVelocitySlider);
    minimumVelocitySlider.setRange(1.0, 127.0, 1.0);
    styleSlider(maximumVelocitySlider);
    maximumVelocitySlider.setRange(1.0, 127.0, 1.0);
    for (auto* slider : {&curvatureSlider, &minimumVelocitySlider, &maximumVelocitySlider}) {
        slider->addListener(this);
        addAndMakeVisible(*slider);
    }
    savePresetButton.addListener(this);
    resetGlobalButton.addListener(this);
    addAndMakeVisible(savePresetButton);
    addAndMakeVisible(resetGlobalButton);

    keyboardScrollBar.addListener(this);
    keyboardScrollBar.setAutoHide(true);
    keyboardScrollBar.setColour(juce::ScrollBar::thumbColourId, juce::Colour(0xff69737a));
    keyboardScrollBar.setColour(juce::ScrollBar::trackColourId, juce::Colour(panel));
    addAndMakeVisible(keyboardScrollBar);

    smoothCurveToggle.addListener(this);
    smoothCurveToggle.setColour(juce::ToggleButton::textColourId, juce::Colour(textPrimary));
    smoothCurveToggle.setColour(juce::ToggleButton::tickColourId, juce::Colour(green));
    smoothCurveToggle.setTooltip("Use smooth monotonic interpolation between editable curve points");
    addAndMakeVisible(smoothCurveToggle);

    profileBox.addListener(this);
    profileBox.setTextWhenNothingSelected("No profile loaded");
    profileBox.setColour(juce::ComboBox::backgroundColourId, juce::Colour(panelRaised));
    profileBox.setColour(juce::ComboBox::textColourId, juce::Colour(textPrimary));
    profileBox.setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff414a51));
    addAndMakeVisible(profileBox);

    selectedNoteLabel.setColour(juce::Label::textColourId, juce::Colour(textPrimary));
    selectedNoteLabel.setFont(juce::FontOptions(18.0f, juce::Font::bold));
    addAndMakeVisible(selectedNoteLabel);

    statusLabel.setColour(juce::Label::textColourId, juce::Colour(textMuted));
    statusLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(statusLabel);

    const auto profileDirectoryResult = profileDirectory().createDirectory();
    if (!pluginState) {
        refreshMidiInputs();
        refreshMidiOutputs();
    } else {
        for (auto* control : std::array<juce::Component*, 5>{
                &deviceLabel, &midiInputBox, &outputLabel, &midiOutputBox, &routingToggle})
            control->setVisible(false);
        pluginRevision = pluginState->revision();
        syncPluginState();
    }
    refreshProfileList();
    if (!pluginState)
        loadAppState();
    startTimerHz(12);

    if (!profile && !pluginState) {
        const auto defaultProfile = profileDirectory().getChildFile("kawai-white-partial.velcal.json");
        if (defaultProfile.existsAsFile())
            loadProfile(defaultProfile);
    }
    if (!profile)
        updateLabels();
    setActiveTab(false);
    updateCaptureControls();
    if (profileDirectoryResult.failed())
        statusLabel.setText("Could not create profile folder: " + profileDirectoryResult.getErrorMessage(),
            juce::dontSendNotification);
}

MainComponent::~MainComponent()
{
    stopTimer();
    midiInputBox.removeListener(this);
    midiOutputBox.removeListener(this);
    globalPresetBox.removeListener(this);
    routingToggle.removeListener(this);
    captureButton.removeListener(this);
    newProfileButton.removeListener(this);
    clearProfileButton.removeListener(this);
    openProfileButton.removeListener(this);
    saveProfileButton.removeListener(this);
    deleteProfileButton.removeListener(this);
    profileBox.removeListener(this);
    perKeyTabButton.removeListener(this);
    globalTabButton.removeListener(this);
    resetKeyButton.removeListener(this);
    savePresetButton.removeListener(this);
    resetGlobalButton.removeListener(this);
    keyAdjustmentSlider.removeListener(this);
    curvatureSlider.removeListener(this);
    minimumVelocitySlider.removeListener(this);
    maximumVelocitySlider.removeListener(this);
    keyboardScrollBar.removeListener(this);
    smoothCurveToggle.removeListener(this);
}

void MainComponent::refreshMidiInputs()
{
    midiInputBox.clear();
    midiInputs = juce::MidiInput::getAvailableDevices();
    for (int index = 0; index < midiInputs.size(); ++index)
        midiInputBox.addItem(midiInputs[index].name, index + 1);
    if (!midiInputs.isEmpty())
        midiInputBox.setSelectedId(1, juce::dontSendNotification);
    else
        midiInputBox.setTextWhenNothingSelected("No MIDI inputs found");
}

void MainComponent::refreshMidiOutputs()
{
    midiOutputBox.clear();
#if ! JUCE_WINDOWS
    midiOutputBox.addItem("VelCal Output (virtual)", 1);
#endif
    midiOutputs = juce::MidiOutput::getAvailableDevices();
    for (int index = 0; index < midiOutputs.size(); ++index)
        midiOutputBox.addItem(midiOutputs[index].name, index + 2);
#if JUCE_WINDOWS
    midiOutputBox.setTextWhenNothingSelected(
        midiOutputs.isEmpty() ? "No MIDI outputs found" : "Select a MIDI output");
#else
    midiOutputBox.setSelectedId(1, juce::dontSendNotification);
#endif
}

void MainComponent::loadAppState()
{
    const auto file = appStateFile();
    if (!file.existsAsFile())
        return;

    try {
        std::ifstream stream(juceFilePath(file));
        const auto json = nlohmann::json::parse(stream);
        const auto inputIdentifier = juce::String(json.value("midiInputIdentifier", ""));
        const auto inputName = juce::String(json.value("midiInputName", ""));
        const auto outputIdentifier = juce::String(json.value("midiOutputIdentifier", ""));
        const auto outputName = juce::String(json.value("midiOutputName", ""));
        const auto useVirtualOutput = json.value("midiOutputVirtual", true);
        restoreMidiSelections(
            inputIdentifier, inputName, outputIdentifier, outputName, useVirtualOutput);

        const auto lastProfilePath = juce::String(json.value("lastProfilePath", ""));
        const auto lastProfile = lastProfilePath.isEmpty() ? juce::File{}
            : juce::File::isAbsolutePath(lastProfilePath) ? juce::File(lastProfilePath)
            : profileDirectory().getChildFile(lastProfilePath);
        if (lastProfile.existsAsFile())
            loadProfile(lastProfile);
    } catch (...) {
        statusLabel.setText("App preferences could not be read", juce::dontSendNotification);
    }
}

void MainComponent::saveAppState() const
{
    if (pluginState) {
        publishPluginState();
        return;
    }
    const auto inputIndex = midiInputBox.getSelectedId() - 1;
    const auto outputIndex = midiOutputBox.getSelectedId() - 2;
    nlohmann::json json;
    if (juce::isPositiveAndBelow(inputIndex, midiInputs.size())) {
        json["midiInputIdentifier"] = midiInputs[inputIndex].identifier.toStdString();
        json["midiInputName"] = midiInputs[inputIndex].name.toStdString();
    }
    json["midiOutputVirtual"] = midiOutputBox.getSelectedId() == 1;
    if (juce::isPositiveAndBelow(outputIndex, midiOutputs.size())) {
        json["midiOutputIdentifier"] = midiOutputs[outputIndex].identifier.toStdString();
        json["midiOutputName"] = midiOutputs[outputIndex].name.toStdString();
    }
    if (profileFile != juce::File{}) {
        json["lastProfilePath"] = (profileFile.isAChildOf(profileDirectory())
            ? profileFile.getRelativePathFrom(profileDirectory())
            : profileFile.getFullPathName()).toStdString();
    }

    try {
        profileDirectory().createDirectory();
        std::ofstream stream(juceFilePath(appStateFile()));
        stream << json.dump(2);
    } catch (...) {
    }
}

void MainComponent::restoreMidiSelections(
    const juce::String& inputIdentifier,
    const juce::String& inputName,
    const juce::String& outputIdentifier,
    const juce::String& outputName,
    const bool useVirtualOutput)
{
    updatingControls = true;
    auto inputId = 0;
    for (int index = 0; index < midiInputs.size(); ++index) {
        if ((!inputIdentifier.isEmpty() && midiInputs[index].identifier == inputIdentifier)
            || (!inputName.isEmpty() && midiInputs[index].name == inputName)) {
            inputId = index + 1;
            break;
        }
    }
    if (inputId != 0)
        midiInputBox.setSelectedId(inputId, juce::dontSendNotification);

    auto outputId = useVirtualOutput ? 1 : 0;
#if JUCE_WINDOWS
    if (useVirtualOutput)
        outputId = 0;
#endif
    if (!useVirtualOutput) {
        for (int index = 0; index < midiOutputs.size(); ++index) {
            if ((!outputIdentifier.isEmpty() && midiOutputs[index].identifier == outputIdentifier)
                || (!outputName.isEmpty() && midiOutputs[index].name == outputName)) {
                outputId = index + 2;
                break;
            }
        }
    }
    if (outputId != 0)
        midiOutputBox.setSelectedId(outputId, juce::dontSendNotification);
    updatingControls = false;
}

void MainComponent::refreshProfileList()
{
    updatingProfileList = true;
    profileFiles.clear();
    profileBox.clear(juce::dontSendNotification);

    juce::Array<juce::File> files;
    profileDirectory().findChildFiles(
        files, juce::File::findFiles, false, "*.velcal.json");
    files.sort();
    for (const auto& file : files) {
        profileFiles.add(file);
        profileBox.addItem(profileDisplayName(file), profileComboBaseId + profileFiles.size() - 1);
    }
    if (profileFile.existsAsFile() && !profileFiles.contains(profileFile)) {
        profileFiles.add(profileFile);
        profileBox.addItem(profileDisplayName(profileFile), profileComboBaseId + profileFiles.size() - 1);
    }

    selectProfileInList(profileFile);
    deleteProfileButton.setEnabled(profileFile != juce::File{} && profileFile.existsAsFile());
    updatingProfileList = false;
}

void MainComponent::selectProfileInList(const juce::File& file)
{
    auto selectedId = 0;
    for (int index = 0; index < profileFiles.size(); ++index) {
        if (profileFiles[index] == file) {
            selectedId = profileComboBaseId + index;
            break;
        }
    }
    if (selectedId != 0) {
        const auto name = profile && !profile->profileName.empty()
            ? juce::String(profile->profileName) : profileDisplayName(file);
        profileBox.changeItemText(selectedId,
            name + (profileDirty ? " *" : ""));
        profileBox.setSelectedId(selectedId, juce::dontSendNotification);
    }
    else if (profile)
        profileBox.setText(juce::String(profile->profileName) + (profileDirty ? " *" : ""), juce::dontSendNotification);
    else
        profileBox.setSelectedId(0, juce::dontSendNotification);
}

void MainComponent::comboBoxChanged(juce::ComboBox* comboBox)
{
    if (comboBox == &globalPresetBox) {
        applySelectedCurvePreset();
        return;
    }
    if (comboBox == &profileBox) {
        if (updatingProfileList)
            return;
        const auto index = profileBox.getSelectedId() - profileComboBaseId;
        if (juce::isPositiveAndBelow(index, profileFiles.size()))
            loadProfile(profileFiles[index]);
        return;
    }
    if (updatingControls)
        return;
    if (midiEngine.isCapturing()) {
        midiEngine.cancelCapture();
        updateCaptureControls();
    }
    if (midiEngine.isRouting()) {
        routingToggle.setToggleState(false, juce::dontSendNotification);
        midiEngine.stopRouting();
    }
    saveAppState();
    updateLabels();
}

void MainComponent::buttonClicked(juce::Button* button)
{
    if (button == &openProfileButton)
        chooseProfile();
    else if (button == &saveProfileButton)
        saveCurrentProfile();
    else if (button == &deleteProfileButton)
        deleteSelectedProfile();
    else if (button == &newProfileButton)
        createNewProfile();
    else if (button == &clearProfileButton)
        clearMeasurements();
    else if (button == &perKeyTabButton)
        setActiveTab(false);
    else if (button == &globalTabButton)
        setActiveTab(true);
    else if (button == &resetKeyButton) {
        if (profile) {
            profile->noteAdjustments[selectedNote] = 0;
            profile->noteCurveOverrides[selectedNote] = {};
            markProfileDirty();
            updateEditingControls();
            updateEffectiveMaps();
            repaint();
        }
    } else if (button == &savePresetButton)
        saveCurvePreset();
    else if (button == &resetGlobalButton) {
        if (profile) {
            profile->globalCurve = defaultCurvePresets()[0];
            markProfileDirty();
            refreshCurvePresets();
            updateEditingControls();
            updateEffectiveMaps();
            repaint();
        }
    }
    else if (button == &smoothCurveToggle) {
        if (profile) {
            ensureEditableCurve();
            editableCurveSmooth() = smoothCurveToggle.getToggleState();
            markProfileDirty();
            updateEffectiveMaps();
            repaint();
        }
    }
    else if (button == &captureButton)
        updateCapture();
    else if (button == &routingToggle)
        updateRouting();
}

void MainComponent::updateRouting()
{
    if (midiEngine.isCapturing()) {
        midiEngine.cancelCapture();
        updateCaptureControls();
    }
    if (!routingToggle.getToggleState()) {
        midiEngine.stopRouting();
        updateLabels();
        return;
    }
    const auto inputIndex = midiInputBox.getSelectedId() - 1;
    const auto outputIndex = midiOutputBox.getSelectedId() - 2;
    if (!juce::isPositiveAndBelow(inputIndex, midiInputs.size())) {
        routingToggle.setToggleState(false, juce::dontSendNotification);
        return;
    }

    juce::String error;
    const auto createVirtual = midiOutputBox.getSelectedId() == 1;
    if (!createVirtual && !juce::isPositiveAndBelow(outputIndex, midiOutputs.size())) {
        routingToggle.setToggleState(false, juce::dontSendNotification);
        juce::AlertWindow::showMessageBoxAsync(
            juce::MessageBoxIconType::WarningIcon,
            "Select a MIDI output",
            "Select a MIDI output before enabling routing.");
        return;
    }
    const auto outputIdentifier = juce::isPositiveAndBelow(outputIndex, midiOutputs.size())
        ? midiOutputs[outputIndex].identifier
        : juce::String{};
    if (!midiEngine.startRouting(
            midiInputs[inputIndex].identifier,
            outputIdentifier,
            createVirtual,
            error)) {
        routingToggle.setToggleState(false, juce::dontSendNotification);
        juce::AlertWindow::showMessageBoxAsync(
            juce::MessageBoxIconType::WarningIcon,
            "MIDI routing could not start",
            error);
    }
    updateLabels();
}

void MainComponent::updateCapture()
{
    if (midiEngine.isCapturing()) {
        const auto events = midiEngine.getCapturedEventsSnapshot();
        const auto group = keyGroupBox.getSelectedId() == 2
            ? velcal::KeyGroup::blackKeys
            : velcal::KeyGroup::whiteKeys;
        const auto analysis = velcal::analyzeSectionCapture(events, group, 0);
        if (analysis.rangeInferred) {
            const auto config = profile ? profile->settings : velcal::CalibrationConfig{};
            const auto guidance = makeCaptureGuidance(
                analysis, group, config, profile ? &profile->presses : nullptr);
            if (!guidance.ready) {
                const juce::Component::SafePointer<MainComponent> safeThis(this);
                juce::AlertWindow::showOkCancelBox(
                    juce::MessageBoxIconType::WarningIcon,
                    "Section still needs data",
                    guidance.warningText,
                    "Finish anyway",
                    "Keep capturing",
                    this,
                    juce::ModalCallbackFunction::create([safeThis](const int result) {
                        if (result != 0 && safeThis != nullptr)
                            safeThis->finishSectionCapture();
                    }));
                return;
            }
        }
        finishSectionCapture();
        return;
    }

    showCaptureGuide();
}

void MainComponent::showCaptureGuide()
{
    if (captureGuide)
        return;
    const auto groupName = keyGroupBox.getSelectedId() == 2 ? "black" : "white";
    captureGuide = std::make_unique<CaptureGuide>(groupName);
    captureGuide->setBounds(getLocalBounds());
    addAndMakeVisible(*captureGuide);
    captureGuide->toFront(false);
    const juce::Component::SafePointer<MainComponent> safeThis(this);
    captureGuide->enterModalState(
        true,
        juce::ModalCallbackFunction::create([safeThis](const int result) {
            if (safeThis == nullptr)
                return;
            safeThis->captureGuide.reset();
            if (result != 0)
                safeThis->beginSectionCapture();
        }),
        false);
}

void MainComponent::beginSectionCapture()
{
    const auto inputIndex = midiInputBox.getSelectedId() - 1;
    if (!pluginState && !juce::isPositiveAndBelow(inputIndex, midiInputs.size())) {
        juce::AlertWindow::showMessageBoxAsync(
            juce::MessageBoxIconType::WarningIcon,
            "No MIDI input",
            "Select the keyboard to calibrate first.");
        return;
    }

    if (!pluginState && midiEngine.isRouting()) {
        routingToggle.setToggleState(false, juce::dontSendNotification);
        midiEngine.stopRouting();
    }

    juce::String error;
    if (!midiEngine.startCapture(pluginState ? juce::String{} : midiInputs[inputIndex].identifier, error)) {
        juce::AlertWindow::showMessageBoxAsync(
            juce::MessageBoxIconType::WarningIcon,
            "Capture could not start",
            error);
        return;
    }

    captureStartMessageCount = midiEngine.getActivity().messagesReceived;
    updateCaptureControls();
    selectedNoteLabel.setText("Calibration capture", juce::dontSendNotification);
    statusLabel.setText("Start with soft bar presses", juce::dontSendNotification);
}

void MainComponent::finishSectionCapture()
{
    const auto captureError = midiEngine.getActivity().outputError;
    const auto events = midiEngine.finishCapture();
    updateCaptureControls();
    if (pluginState && captureError.isNotEmpty()) {
        statusLabel.setText(captureError, juce::dontSendNotification);
        return;
    }

    velcal::SegmentId segmentId = 1;
    if (profile) {
        for (const auto& press : profile->presses)
            segmentId = std::max(segmentId, press.segmentId + 1);
    }
    const auto group = keyGroupBox.getSelectedId() == 2
        ? velcal::KeyGroup::blackKeys
        : velcal::KeyGroup::whiteKeys;
    const auto analysis = velcal::analyzeSectionCapture(events, group, segmentId);
    if (!analysis.rangeInferred) {
        juce::AlertWindow::showMessageBoxAsync(
            juce::MessageBoxIconType::WarningIcon,
            "Section not captured",
            "VelCal needs at least three clear presses of the same keys to learn the bar position. No measurements were added.");
        updateLabels();
        return;
    }

    if (!profile) {
        profile.emplace();
        const auto inputIndex = midiInputBox.getSelectedId() - 1;
        const auto deviceName = pluginState ? juce::String("DAW MIDI")
            : juce::isPositiveAndBelow(inputIndex, midiInputs.size())
            ? midiInputs[inputIndex].name
            : juce::String("MIDI keyboard");
        profile->profileName = "New calibration";
        profile->createdUtc = juce::Time::getCurrentTime().toISO8601(true).toStdString();
        profile->inputDevice.name = deviceName.toStdString();
        if (!pluginState && juce::isPositiveAndBelow(inputIndex, midiInputs.size()))
            profile->inputDevice.endpointId = midiInputs[inputIndex].identifier.toStdString();
    }

    profile->presses.insert(
        profile->presses.end(), analysis.presses.begin(), analysis.presses.end());
    profile->generated = velcal::calibrate(profile->presses, profile->settings);
    markProfileDirty();
    updateEffectiveMaps();
    selectedNote = analysis.lowestNote;
    updateEditingControls();

    const auto accepted = static_cast<int>(std::count_if(
        analysis.presses.begin(), analysis.presses.end(),
        [](const auto& press) { return press.accepted; }));
    updateLabels();
    statusLabel.setText(
        "Added section " + midiNoteName(analysis.lowestNote) + "-"
            + midiNoteName(analysis.highestNote) + "  |  "
            + juce::String(accepted) + "/" + juce::String(analysis.presses.size())
            + " presses valid",
        juce::dontSendNotification);
    saveProfileButton.setEnabled(true);
    repaint();
}

void MainComponent::updateCaptureControls()
{
    const auto capturing = midiEngine.isCapturing();
    captureButton.setButtonText(capturing ? "Finish section" : "Start section");
    keyGroupBox.setEnabled(!capturing);
    midiInputBox.setEnabled(!capturing);
    routingToggle.setEnabled(!capturing);
}

void MainComponent::timerCallback()
{
    if (pluginState)
        syncPluginState();
    const auto activity = midiEngine.getActivity();
    if (activity.outputError.isNotEmpty()) {
        midiEngine.stopRouting();
        updateCaptureControls();
        routingToggle.setToggleState(false, juce::dontSendNotification);
        statusLabel.setText(activity.outputError, juce::dontSendNotification);
        return;
    }
    if (activity.outputOpening) {
        statusLabel.setText("Opening MIDI output", juce::dontSendNotification);
        return;
    }
    if (activity.safetyTripped) {
        midiEngine.stopRouting();
        routingToggle.setToggleState(false, juce::dontSendNotification);
        statusLabel.setText(
            "Routing stopped: abnormal MIDI traffic detected",
            juce::dontSendNotification);
        return;
    }
    if (midiEngine.isCapturing()) {
        const auto events = midiEngine.getCapturedEventsSnapshot();
        const auto group = keyGroupBox.getSelectedId() == 2
            ? velcal::KeyGroup::blackKeys
            : velcal::KeyGroup::whiteKeys;
        const auto analysis = velcal::analyzeSectionCapture(events, group, 0);
        if (!analysis.rangeInferred) {
            statusLabel.setText(
                "Press and release the same key section at least 3 times",
                juce::dontSendNotification);
            return;
        }
        const auto config = profile ? profile->settings : velcal::CalibrationConfig{};
        const auto guidance = makeCaptureGuidance(
            analysis, group, config, profile ? &profile->presses : nullptr);
        selectedNoteLabel.setText(guidance.countsText, juce::dontSendNotification);
        statusLabel.setText(guidance.statusText, juce::dontSendNotification);
        return;
    }
    if (!midiEngine.isRouting())
        return;
    statusLabel.setText(
        "Routing  " + midiNoteName(activity.lastNote) + "  "
            + juce::String(activity.lastRawVelocity) + " -> "
            + juce::String(activity.lastCorrectedVelocity),
        juce::dontSendNotification);
}

void MainComponent::chooseProfile()
{
    fileChooser = std::make_unique<juce::FileChooser>(
        "Open VelCal profile",
        profileDirectory(),
        "*.velcal.json");
    fileChooser->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& chooser) {
            const auto file = chooser.getResult();
            if (file.existsAsFile())
                loadProfile(file);
            fileChooser.reset();
        });
}

void MainComponent::loadProfile(const juce::File& file)
{
    selectProfileInList(profileFile);
    const juce::Component::SafePointer<MainComponent> safeThis(this);
    confirmDiscardUnsaved([safeThis, file] {
        if (safeThis != nullptr)
            safeThis->loadProfileConfirmed(file);
    });
}

void MainComponent::loadProfileConfirmed(const juce::File& file)
{
    try {
        profile = velcal::loadProfile(juceFilePath(file));
        profileFile = file;
        profileDirty = false;
        if (midiEngine.isCapturing())
            midiEngine.cancelCapture();
        updateCaptureControls();
        const auto firstMeasured = std::find_if(
            profile->generated.noteStats.begin(), profile->generated.noteStats.end(),
            [](const auto& stats) { return stats.samplesUsed != 0; });
        if (firstMeasured != profile->generated.noteStats.end()) {
            selectedNote = static_cast<std::uint8_t>(
                std::distance(profile->generated.noteStats.begin(), firstMeasured));
        }
        refreshCurvePresets();
        updateEditingControls();
        updateEffectiveMaps();
        refreshProfileList();
        saveAppState();
    } catch (const std::exception& error) {
        juce::AlertWindow::showMessageBoxAsync(
            juce::MessageBoxIconType::WarningIcon,
            "Could not open profile",
            error.what());
    }
    updateLabels();
    repaint();
}

void MainComponent::deleteSelectedProfile()
{
    juce::File file = profileFile;
    const auto index = profileBox.getSelectedId() - profileComboBaseId;
    if (file == juce::File{} && juce::isPositiveAndBelow(index, profileFiles.size()))
        file = profileFiles[index];
    if (!file.existsAsFile())
        return;

    const juce::Component::SafePointer<MainComponent> safeThis(this);
    juce::AlertWindow::showOkCancelBox(
        juce::MessageBoxIconType::WarningIcon,
        "Delete this profile?",
        "This will remove \"" + file.getFileName() + "\" from the profile list.",
        "Delete",
        "Cancel",
        this,
        juce::ModalCallbackFunction::create([safeThis, file](const int result) {
            if (result != 0 && safeThis != nullptr)
                safeThis->deleteProfileConfirmed(file);
        }));
}

void MainComponent::deleteProfileConfirmed(const juce::File& file)
{
    if (!file.existsAsFile())
        return;

    const auto deleted = file.moveToTrash() || file.deleteFile();
    if (!deleted) {
        juce::AlertWindow::showMessageBoxAsync(
            juce::MessageBoxIconType::WarningIcon,
            "Could not delete profile",
            "VelCal could not remove the selected profile file.");
        return;
    }

    if (file == profileFile) {
        profile.reset();
        profileDirty = false;
        profileFile = juce::File{};
        selectedNote = 60;
        midiEngine.stopRouting();
        updateCaptureControls();
        MidiEngine::MapBank identity;
        for (auto& map : identity)
            map = velcal::VelocityMap::identity();
        midiEngine.setMaps(identity);
        routingToggle.setToggleState(false, juce::dontSendNotification);
    }
    refreshProfileList();
    saveAppState();
    updateEditingControls();
    updateLabels();
    statusLabel.setText("Profile deleted", juce::dontSendNotification);
    repaint();
}

void MainComponent::createNewProfile()
{
    const juce::Component::SafePointer<MainComponent> safeThis(this);
    confirmDiscardUnsaved([safeThis] {
        if (safeThis != nullptr)
            safeThis->replaceWithNewProfile();
    });
}

void MainComponent::requestClose(std::function<void()> close)
{
    confirmDiscardUnsaved(std::move(close));
}

void MainComponent::markProfileDirty()
{
    profileDirty = true;
    selectProfileInList(profileFile);
    publishPluginState();
}

void MainComponent::confirmDiscardUnsaved(std::function<void()> action)
{
    if (discardPromptOpen)
        return;
    if (!profileDirty && !midiEngine.isCapturing()) {
        action();
        return;
    }
    discardPromptOpen = true;
    const juce::Component::SafePointer<MainComponent> safeThis(this);
    juce::AlertWindow::showOkCancelBox(
        juce::MessageBoxIconType::QuestionIcon,
        "Discard unsaved changes?",
        midiEngine.isCapturing()
            ? "The current capture will be discarded. Cancel to finish the section and save your profile."
            : "This profile has unsaved changes. Cancel to save your profile before continuing.",
        "Discard",
        "Cancel",
        this,
        juce::ModalCallbackFunction::create([safeThis, action = std::move(action)](const int result) {
            if (safeThis == nullptr)
                return;
            safeThis->discardPromptOpen = false;
            if (result != 0)
                action();
        }));
}

void MainComponent::replaceWithNewProfile()
{
    midiEngine.stopRouting();
    updateCaptureControls();
    routingToggle.setToggleState(false, juce::dontSendNotification);

    velcal::CalibrationProfile newProfile;
    const auto inputIndex = midiInputBox.getSelectedId() - 1;
    const auto deviceName = pluginState ? juce::String("DAW MIDI")
        : juce::isPositiveAndBelow(inputIndex, midiInputs.size())
        ? midiInputs[inputIndex].name
        : juce::String("MIDI keyboard");
    newProfile.profileName = "New calibration";
    newProfile.createdUtc = juce::Time::getCurrentTime().toISO8601(true).toStdString();
    newProfile.inputDevice.name = deviceName.toStdString();
    if (!pluginState && juce::isPositiveAndBelow(inputIndex, midiInputs.size()))
        newProfile.inputDevice.endpointId = midiInputs[inputIndex].identifier.toStdString();
    newProfile.generated = velcal::calibrate(newProfile.presses, newProfile.settings);

    profile = std::move(newProfile);
    profileDirty = false;
    profileFile = juce::File{};
    refreshProfileList();
    saveAppState();
    selectedNote = 60;
    refreshCurvePresets();
    updateEditingControls();
    updateEffectiveMaps();
    updateLabels();
    statusLabel.setText("New empty profile", juce::dontSendNotification);
    repaint();
}

void MainComponent::clearMeasurements()
{
    if (!profile || profile->presses.empty())
        return;

    const juce::Component::SafePointer<MainComponent> safeThis(this);
    juce::AlertWindow::showOkCancelBox(
        juce::MessageBoxIconType::QuestionIcon,
        "Clear all measurements?",
        "Every captured section will be removed and all velocity mappings reset. The saved file is unchanged until you select Save profile.",
        "Clear data",
        "Cancel",
        this,
        juce::ModalCallbackFunction::create([safeThis](const int result) {
            if (result != 0 && safeThis != nullptr)
                safeThis->clearMeasurementsConfirmed();
        }));
}

void MainComponent::clearMeasurementsConfirmed()
{
    midiEngine.stopRouting();
    updateCaptureControls();
    routingToggle.setToggleState(false, juce::dontSendNotification);
    profile->presses.clear();
    profile->generated = velcal::calibrate(profile->presses, profile->settings);
    profile->noteAdjustments.fill(0);
    profile->noteCurveOverrides.fill({});
    profile->globalCurve = defaultCurvePresets()[0];
    markProfileDirty();
    selectedNote = 60;
    refreshCurvePresets();
    updateEditingControls();
    updateEffectiveMaps();
    updateLabels();
    statusLabel.setText("Measurements cleared", juce::dontSendNotification);
    repaint();
}

void MainComponent::saveCurrentProfile()
{
    if (!profile)
        return;
    if (profileFile != juce::File{}) {
        writeProfile(profileFile);
        return;
    }

    const auto suggestedName = juce::File::createLegalFileName(profile->profileName)
        + ".velcal.json";
    profileDirectory().createDirectory();
    fileChooser = std::make_unique<juce::FileChooser>(
        "Save VelCal profile",
        profileDirectory().getChildFile(suggestedName),
        "*.velcal.json");
    fileChooser->launchAsync(
        juce::FileBrowserComponent::saveMode
            | juce::FileBrowserComponent::canSelectFiles
            | juce::FileBrowserComponent::warnAboutOverwriting,
        [this](const juce::FileChooser& chooser) {
            const auto file = chooser.getResult();
            if (file != juce::File{})
                writeProfile(file);
            fileChooser.reset();
        });
}

void MainComponent::writeProfile(const juce::File& file)
{
    try {
        velcal::saveProfile(
            *profile,
            juceFilePath(file));
        profileFile = file;
        profileDirty = false;
        refreshProfileList();
        saveAppState();
        statusLabel.setText("Profile saved", juce::dontSendNotification);
    } catch (const std::exception& error) {
        juce::AlertWindow::showMessageBoxAsync(
            juce::MessageBoxIconType::WarningIcon,
            "Could not save profile",
            error.what());
    }
}

void MainComponent::setActiveTab(const bool globalCurveTab)
{
    activeCurvePoint.reset();
    showingGlobalCurve = globalCurveTab;
    perKeyTabButton.setColour(
        juce::TextButton::buttonColourId,
        juce::Colour(globalCurveTab ? panelRaised : 0xff3c765d));
    globalTabButton.setColour(
        juce::TextButton::buttonColourId,
        juce::Colour(globalCurveTab ? 0xff3c765d : panelRaised));

    for (auto* component : std::array<juce::Component*, 8>{
             &keyGroupLabel, &keyGroupBox, &captureButton, &newProfileButton,
             &clearProfileButton, &keyAdjustmentLabel, &keyAdjustmentSlider, &resetKeyButton})
        component->setVisible(!globalCurveTab);
    keyboardScrollBar.setVisible(!globalCurveTab);
    for (auto* component : std::array<juce::Component*, 10>{
             &presetLabel, &globalPresetBox, &curvatureLabel, &curvatureSlider,
             &minimumVelocityLabel, &minimumVelocitySlider, &maximumVelocityLabel,
             &maximumVelocitySlider, &savePresetButton, &resetGlobalButton})
        component->setVisible(globalCurveTab);

    refreshCurvePresets();
    updateEditingControls();
    updateLabels();
    resized();
    repaint();
}

void MainComponent::refreshCurvePresets()
{
    updatingControls = true;
    globalPresetBox.clear(juce::dontSendNotification);
    const auto& defaults = defaultCurvePresets();
    for (std::size_t index = 0; index < defaults.size(); ++index)
        globalPresetBox.addItem(defaults[index].name, static_cast<int>(index + 1));
    globalPresetBox.addSeparator();
    if (profile) {
        for (std::size_t index = 0; index < profile->userGlobalPresets.size(); ++index) {
            globalPresetBox.addItem(
                profile->userGlobalPresets[index].name,
                static_cast<int>(100 + index));
        }

        int selectedId = 0;
        for (std::size_t index = 0; index < defaults.size(); ++index) {
            if (profile->globalCurve.name == defaults[index].name)
                selectedId = static_cast<int>(index + 1);
        }
        for (std::size_t index = 0; index < profile->userGlobalPresets.size(); ++index) {
            if (profile->globalCurve.name == profile->userGlobalPresets[index].name)
                selectedId = static_cast<int>(100 + index);
        }
        globalPresetBox.setSelectedId(selectedId, juce::dontSendNotification);
        if (selectedId == 0)
            globalPresetBox.setText("Custom", juce::dontSendNotification);
    }
    updatingControls = false;
}

void MainComponent::applySelectedCurvePreset()
{
    if (updatingControls || !profile)
        return;
    const auto selectedId = globalPresetBox.getSelectedId();
    activeCurvePoint.reset();
    if (selectedId >= 1 && selectedId <= static_cast<int>(defaultCurvePresets().size())) {
        profile->globalCurve = defaultCurvePresets()[static_cast<std::size_t>(selectedId - 1)];
    } else if (selectedId >= 100) {
        const auto index = static_cast<std::size_t>(selectedId - 100);
        if (index < profile->userGlobalPresets.size())
            profile->globalCurve = profile->userGlobalPresets[index];
    }
    markProfileDirty();
    updateEditingControls();
    updateEffectiveMaps();
    updateLabels();
    repaint();
}

void MainComponent::saveCurvePreset()
{
    if (!profile)
        return;

    auto* dialog = new juce::AlertWindow(
        "Save global curve preset",
        "Give this velocity curve a name.",
        juce::MessageBoxIconType::NoIcon);
    dialog->addTextEditor("name", "My curve", "Preset name");
    dialog->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
    dialog->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    const juce::Component::SafePointer<MainComponent> safeThis(this);
    dialog->enterModalState(
        true,
        juce::ModalCallbackFunction::create([safeThis, dialog](const int result) {
            if (result == 0 || safeThis == nullptr)
                return;
            const auto name = dialog->getTextEditorContents("name").trim();
            if (name.isEmpty())
                return;
            auto preset = safeThis->profile->globalCurve;
            preset.name = name.toStdString();
            safeThis->profile->globalCurve = preset;
            safeThis->profile->userGlobalPresets.push_back(preset);
            safeThis->markProfileDirty();
            safeThis->refreshCurvePresets();
            safeThis->updateEditingControls();
            safeThis->repaint();
        }),
        true);
}

void MainComponent::sliderValueChanged(juce::Slider* slider)
{
    if (updatingControls || !profile)
        return;

    if (slider == &keyAdjustmentSlider) {
        profile->noteAdjustments[selectedNote] = static_cast<int>(
            std::lround(keyAdjustmentSlider.getValue()));
    } else {
        activeCurvePoint.reset();
        auto& curve = profile->globalCurve;
        curve.name = "Custom";
        curve.points.clear();
        curve.smooth = true;
        curve.curvature = curvatureSlider.getValue();
        curve.minimumOutput = static_cast<std::uint8_t>(
            std::lround(minimumVelocitySlider.getValue()));
        curve.maximumOutput = static_cast<std::uint8_t>(
            std::lround(maximumVelocitySlider.getValue()));
        updatingControls = true;
        if (curve.minimumOutput > curve.maximumOutput) {
            if (slider == &minimumVelocitySlider) {
                curve.maximumOutput = curve.minimumOutput;
                maximumVelocitySlider.setValue(curve.maximumOutput, juce::dontSendNotification);
            } else {
                curve.minimumOutput = curve.maximumOutput;
                minimumVelocitySlider.setValue(curve.minimumOutput, juce::dontSendNotification);
            }
        }
        globalPresetBox.setSelectedId(0, juce::dontSendNotification);
        globalPresetBox.setText("Custom", juce::dontSendNotification);
        updatingControls = false;
    }
    markProfileDirty();
    updateEffectiveMaps();
    if (showingGlobalCurve) {
        selectedNoteLabel.setText(
            "Global velocity curve  |  " + juce::String(profile->globalCurve.name),
            juce::dontSendNotification);
    } else {
        const auto& stats = profile->generated.noteStats[selectedNote];
        const auto adjustment = profile->noteAdjustments[selectedNote];
        selectedNoteLabel.setText(
            midiNoteName(selectedNote) + "  |  " + juce::String(stats.samplesUsed)
                + " samples  |  " + (adjustment >= 0 ? "+" : "")
                + juce::String(adjustment),
            juce::dontSendNotification);
    }
    repaint();
}

std::vector<velcal::VelocityCurvePoint>& MainComponent::editableCurvePoints()
{
    return showingGlobalCurve
        ? profile->globalCurve.points
        : profile->noteCurveOverrides[selectedNote].points;
}

bool& MainComponent::editableCurveSmooth()
{
    return showingGlobalCurve
        ? profile->globalCurve.smooth
        : profile->noteCurveOverrides[selectedNote].smooth;
}

void MainComponent::ensureEditableCurve()
{
    auto& points = editableCurvePoints();
    if (!points.empty())
        return;
    points = sampledCurrentCurve();
    if (showingGlobalCurve) {
        profile->globalCurve.name = "Custom";
    } else {
        profile->noteAdjustments[selectedNote] = 0;
        updateEditingControls();
    }
    markProfileDirty();
    updateEffectiveMaps();
}

std::vector<velcal::VelocityCurvePoint> MainComponent::sampledCurrentCurve() const
{
    static constexpr std::array<int, 9> sampleInputs{
        1, 16, 32, 48, 64, 80, 96, 112, 127};
    std::vector<velcal::VelocityCurvePoint> points;
    points.reserve(sampleInputs.size());
    if (showingGlobalCurve) {
        for (const auto input : sampleInputs) {
            points.push_back({
                static_cast<double>(input),
                evaluateParametricCurve(profile->globalCurve, input)});
        }
    } else {
        const auto adjustment = profile->noteAdjustments[selectedNote];
        for (const auto input : sampleInputs) {
            points.push_back({
                static_cast<double>(input),
                static_cast<double>(std::clamp(
                    static_cast<int>(profile->generated.noteMaps[selectedNote].apply(
                        static_cast<std::uint8_t>(input))) + adjustment,
                    1, 127))});
        }
    }
    return points;
}

juce::Rectangle<float> MainComponent::curvePlotBounds() const
{
    return curveBounds
        .withTrimmedLeft(58.0f)
        .withTrimmedRight(28.0f)
        .withTrimmedTop(28.0f)
        .withTrimmedBottom(48.0f);
}

void MainComponent::paintCurveAxes(
    juce::Graphics& graphics,
    const juce::Rectangle<float> plot)
{
    static constexpr std::array<const char*, 8> dynamics{
        "ppp", "pp", "p", "mp", "mf", "f", "ff", "fff"};
    static constexpr std::array<int, 5> inputLabels{0, 32, 64, 96, 127};

    graphics.setColour(juce::Colour(0xff394147));
    for (std::size_t index = 0; index < dynamics.size(); ++index) {
        const auto amount = static_cast<float>(index)
            / static_cast<float>(dynamics.size() - 1);
        const auto y = plot.getBottom() - amount * plot.getHeight();
        graphics.drawLine(plot.getX(), y, plot.getRight(), y, 1.0f);
    }
    for (const auto velocity : inputLabels) {
        const auto amount = static_cast<float>(velocity) / 127.0f;
        const auto x = plot.getX() + amount * plot.getWidth();
        graphics.drawLine(x, plot.getY(), x, plot.getBottom(), 1.0f);
    }

    graphics.setColour(juce::Colour(textMuted));
    graphics.setFont(13.0f);
    for (std::size_t index = 0; index < dynamics.size(); ++index) {
        const auto amount = static_cast<float>(index)
            / static_cast<float>(dynamics.size() - 1);
        const auto y = plot.getBottom() - amount * plot.getHeight();
        graphics.drawText(
            dynamics[index],
            juce::Rectangle<float>(plot.getX() - 48.0f, y - 10.0f, 40.0f, 20.0f),
            juce::Justification::centredRight);
    }
    for (std::size_t index = 0; index < inputLabels.size(); ++index) {
        const auto amount = static_cast<float>(inputLabels[index]) / 127.0f;
        const auto x = plot.getX() + amount * plot.getWidth();
        auto label = juce::Rectangle<float>(x - 22.0f, plot.getBottom() + 5.0f, 44.0f, 18.0f);
        const auto justification = index == 0
            ? juce::Justification::centredLeft
            : index + 1 == inputLabels.size()
                ? juce::Justification::centredRight
                : juce::Justification::centred;
        graphics.drawText(juce::String(inputLabels[index]), label, justification);
    }
    graphics.setFont(10.0f);
    graphics.drawText(
        "INPUT VELOCITY",
        juce::Rectangle<float>(
            plot.getX(), plot.getBottom() + 25.0f, plot.getWidth(), 16.0f),
        juce::Justification::centred);
}

void MainComponent::paintCurveHandles(
    juce::Graphics& graphics,
    const juce::Rectangle<float> plot,
    const std::vector<velcal::VelocityCurvePoint>& points)
{
    for (std::size_t index = 0; index < points.size(); ++index) {
        const auto x = plot.getX()
            + static_cast<float>((points[index].input - 1.0) / 126.0) * plot.getWidth();
        const auto y = plot.getBottom()
            - static_cast<float>((points[index].output - 1.0) / 126.0) * plot.getHeight();
        graphics.setColour(
            activeCurvePoint && *activeCurvePoint == index
                ? juce::Colour(textPrimary)
                : juce::Colour(green));
        graphics.fillEllipse(x - 4.5f, y - 4.5f, 9.0f, 9.0f);
        graphics.setColour(juce::Colour(background));
        graphics.drawEllipse(x - 4.5f, y - 4.5f, 9.0f, 9.0f, 1.0f);
    }

    if (!activeCurvePoint || *activeCurvePoint >= points.size())
        return;
    const auto& selected = points[*activeCurvePoint];
    const auto pointX = plot.getX()
        + static_cast<float>((selected.input - 1.0) / 126.0) * plot.getWidth();
    const auto pointY = plot.getBottom()
        - static_cast<float>((selected.output - 1.0) / 126.0) * plot.getHeight();
    const auto text = "Input " + juce::String(static_cast<int>(std::lround(selected.input)))
        + "  |  Output " + juce::String(static_cast<int>(std::lround(selected.output)));
    constexpr float bubbleWidth = 142.0f;
    constexpr float bubbleHeight = 28.0f;
    auto bubbleX = pointX + 12.0f;
    if (bubbleX + bubbleWidth > plot.getRight())
        bubbleX = pointX - bubbleWidth - 12.0f;
    auto bubbleY = pointY - bubbleHeight - 12.0f;
    if (bubbleY < plot.getY())
        bubbleY = pointY + 12.0f;
    const auto bubble = juce::Rectangle<float>(bubbleX, bubbleY, bubbleWidth, bubbleHeight);
    graphics.setColour(juce::Colour(0xff111519).withAlpha(0.96f));
    graphics.fillRoundedRectangle(bubble, 4.0f);
    graphics.setColour(juce::Colour(green));
    graphics.drawRoundedRectangle(bubble, 4.0f, 1.0f);
    graphics.setColour(juce::Colour(textPrimary));
    graphics.setFont(12.0f);
    graphics.drawText(text, bubble.reduced(8.0f, 2.0f), juce::Justification::centred);
}

void MainComponent::updateEffectiveMaps()
{
    if (!profile)
        return;
    if (pluginState)
        publishPluginState();
    else
        midiEngine.setMaps(velcal::effectiveMaps(*profile));
}

juce::File MainComponent::profileDirectory() const
{
    return velcalProfileDirectory();
}

void MainComponent::publishPluginState() const
{
    if (!pluginState)
        return;
    PluginState::Snapshot next;
    next.profile = profile;
    next.profileFile = profileFile;
    next.dirty = profileDirty;
    next.keyGroup = keyGroupBox.getSelectedId();
    if (pluginState->publish(std::move(next), pluginRevision))
        ++pluginRevision;
}

void MainComponent::syncPluginState()
{
    if (pluginStateLoaded && pluginState->revision() == pluginRevision)
        return;
    const auto state = pluginState->snapshot();
    if (pluginStateLoaded && state.revision == pluginRevision)
        return;
    if (state.revision != pluginRevision) {
        midiEngine.cancelCapture();
        captureGuide.reset();
    }
    pluginRevision = state.revision;
    pluginStateLoaded = true;
    profile = state.profile;
    profileFile = state.profileFile;
    profileDirty = state.dirty;
    keyGroupBox.setSelectedId(state.keyGroup, juce::dontSendNotification);
    activeCurvePoint.reset();
    refreshProfileList();
    refreshCurvePresets();
    updateEditingControls();
    updateCaptureControls();
    updateLabels();
    repaint();
}

void MainComponent::updateEditingControls()
{
    updatingControls = true;
    const auto enabled = profile.has_value();
    keyAdjustmentSlider.setEnabled(enabled);
    resetKeyButton.setEnabled(enabled);
    globalPresetBox.setEnabled(enabled);
    curvatureSlider.setEnabled(enabled);
    minimumVelocitySlider.setEnabled(enabled);
    maximumVelocitySlider.setEnabled(enabled);
    savePresetButton.setEnabled(enabled);
    resetGlobalButton.setEnabled(enabled);
    smoothCurveToggle.setEnabled(enabled);
    if (profile) {
        keyAdjustmentSlider.setValue(
            profile->noteAdjustments[selectedNote], juce::dontSendNotification);
        curvatureSlider.setValue(
            profile->globalCurve.curvature, juce::dontSendNotification);
        minimumVelocitySlider.setValue(
            profile->globalCurve.minimumOutput, juce::dontSendNotification);
        maximumVelocitySlider.setValue(
            profile->globalCurve.maximumOutput, juce::dontSendNotification);
        const auto smooth = showingGlobalCurve
            ? profile->globalCurve.smooth
            : profile->noteCurveOverrides[selectedNote].smooth;
        smoothCurveToggle.setToggleState(smooth, juce::dontSendNotification);
    }
    updatingControls = false;
}

void MainComponent::updateLabels()
{
    if (!profile) {
        profileBox.setText("No profile loaded", juce::dontSendNotification);
        selectedNoteLabel.setText(
            showingGlobalCurve ? "Global velocity curve" : "No calibrated note",
            juce::dontSendNotification);
        statusLabel.setText("", juce::dontSendNotification);
        saveProfileButton.setEnabled(false);
        deleteProfileButton.setEnabled(false);
        return;
    }
    saveProfileButton.setEnabled(true);
    deleteProfileButton.setEnabled(profileFile != juce::File{} && profileFile.existsAsFile());
    selectProfileInList(profileFile);
    if (showingGlobalCurve) {
        selectedNoteLabel.setText(
            "Global velocity curve  |  " + juce::String(profile->globalCurve.name),
            juce::dontSendNotification);
    } else {
        const auto& stats = profile->generated.noteStats[selectedNote];
        const auto adjustment = profile->noteAdjustments[selectedNote];
        selectedNoteLabel.setText(
            midiNoteName(selectedNote) + "  |  " + juce::String(stats.samplesUsed)
                + " samples  |  " + (adjustment >= 0 ? "+" : "")
                + juce::String(adjustment),
            juce::dontSendNotification);
    }
    statusLabel.setText(
        profile->generated.segmentsConnected ? "Sections connected" : "Sections disconnected",
        juce::dontSendNotification);
}

void MainComponent::resized()
{
    if (captureGuide)
        captureGuide->setBounds(getLocalBounds());
    auto area = getLocalBounds().reduced(28);
    auto header = area.removeFromTop(48);
    titleLabel.setBounds(header.removeFromLeft(180));
    saveProfileButton.setBounds(header.removeFromRight(122).reduced(0, 5));
    header.removeFromRight(8);
    deleteProfileButton.setBounds(header.removeFromRight(84).reduced(0, 5));
    header.removeFromRight(8);
    openProfileButton.setBounds(header.removeFromRight(122).reduced(0, 5));
    header.removeFromRight(8);
    profileBox.setBounds(header.reduced(12, 5));

    area.removeFromTop(18);
    auto sidebar = area.removeFromLeft(245).withTrimmedRight(28);
    if (!pluginState) {
        deviceLabel.setBounds(sidebar.removeFromTop(24));
        midiInputBox.setBounds(sidebar.removeFromTop(38));
        sidebar.removeFromTop(18);
        outputLabel.setBounds(sidebar.removeFromTop(24));
        midiOutputBox.setBounds(sidebar.removeFromTop(38));
        sidebar.removeFromTop(12);
        routingToggle.setBounds(sidebar.removeFromTop(32));
        sidebar.removeFromTop(14);
    }

    auto globalSidebar = sidebar;
    keyGroupLabel.setBounds(sidebar.removeFromTop(24));
    keyGroupBox.setBounds(sidebar.removeFromTop(38));
    sidebar.removeFromTop(10);
    captureButton.setBounds(sidebar.removeFromTop(38));
    sidebar.removeFromTop(10);
    auto profileActions = sidebar.removeFromTop(34);
    newProfileButton.setBounds(profileActions.removeFromLeft(104));
    profileActions.removeFromLeft(8);
    clearProfileButton.setBounds(profileActions);

    presetLabel.setBounds(globalSidebar.removeFromTop(24));
    globalPresetBox.setBounds(globalSidebar.removeFromTop(38));
    globalSidebar.removeFromTop(12);
    curvatureLabel.setBounds(globalSidebar.removeFromTop(22));
    curvatureSlider.setBounds(globalSidebar.removeFromTop(36));
    globalSidebar.removeFromTop(8);
    minimumVelocityLabel.setBounds(globalSidebar.removeFromTop(22));
    minimumVelocitySlider.setBounds(globalSidebar.removeFromTop(36));
    globalSidebar.removeFromTop(8);
    maximumVelocityLabel.setBounds(globalSidebar.removeFromTop(22));
    maximumVelocitySlider.setBounds(globalSidebar.removeFromTop(36));
    globalSidebar.removeFromTop(12);
    auto globalActions = globalSidebar.removeFromTop(34);
    savePresetButton.setBounds(globalActions.removeFromLeft(104));
    globalActions.removeFromLeft(8);
    resetGlobalButton.setBounds(globalActions);

    area.removeFromLeft(28);
    auto tabs = area.removeFromTop(34);
    perKeyTabButton.setBounds(tabs.removeFromLeft(166));
    tabs.removeFromLeft(8);
    globalTabButton.setBounds(tabs.removeFromLeft(132));
    area.removeFromTop(10);
    auto selectedHeader = area.removeFromTop(36);
    selectedNoteLabel.setBounds(selectedHeader.removeFromLeft(300));
    smoothCurveToggle.setBounds(selectedHeader.removeFromLeft(90));
    statusLabel.setBounds(selectedHeader);
    area.removeFromTop(8);
    if (!showingGlobalCurve) {
        auto keyEditor = area.removeFromTop(38);
        keyAdjustmentLabel.setBounds(keyEditor.removeFromLeft(170));
        keyAdjustmentSlider.setBounds(keyEditor.removeFromLeft(250));
        keyEditor.removeFromLeft(8);
        resetKeyButton.setBounds(keyEditor.removeFromLeft(90));
        area.removeFromTop(10);
    }
    keyboardBounds = showingGlobalCurve
        ? juce::Rectangle<float>{}
        : area.removeFromTop(std::min(182, area.getHeight() / 3)).toFloat();
    if (!showingGlobalCurve) {
        auto keyArea = keyboardBounds.reduced(14.0f, 8.0f);
        keyArea.removeFromBottom(22.0f);
        const auto contentWidth = std::max(
            keyArea.getWidth(), minimumWhiteKeyWidth * pianoWhiteKeyCount);
        const auto needsScrolling = contentWidth > keyArea.getWidth() + 0.5f;
        keyboardKeyBounds = needsScrolling
            ? keyArea.withTrimmedBottom(13.0f)
            : keyArea;
        const auto visibleWidth = static_cast<double>(keyboardKeyBounds.getWidth());
        const auto maximumStart = std::max(0.0, static_cast<double>(contentWidth) - visibleWidth);
        const auto rangeStart = std::clamp(
            keyboardScrollBar.getCurrentRangeStart(), 0.0, maximumStart);
        keyboardScrollBar.setRangeLimits(0.0, contentWidth);
        keyboardScrollBar.setCurrentRange(rangeStart, visibleWidth, juce::dontSendNotification);
        keyboardScrollBar.setBounds(
            keyArea.withTop(keyArea.getBottom() - 10.0f).toNearestInt());
        keyboardScrollBar.setVisible(needsScrolling);
        area.removeFromTop(20);
    } else {
        keyboardKeyBounds = {};
        keyboardScrollBar.setVisible(false);
    }
    curveBounds = area.toFloat();
}

void MainComponent::paint(juce::Graphics& graphics)
{
    graphics.fillAll(juce::Colour(background));
    graphics.setColour(juce::Colour(panel));
    graphics.fillRect(juce::Rectangle<float>(0.0f, 86.0f, 273.0f, static_cast<float>(getHeight() - 86)));

    if (!profile) {
        graphics.setColour(juce::Colour(textMuted));
        graphics.setFont(16.0f);
        graphics.drawText("Open a calibration profile", getLocalBounds().reduced(300),
            juce::Justification::centred);
        return;
    }

    if (showingGlobalCurve) {
        paintGlobalCurve(graphics, curveBounds);
    } else {
        auto sidebar = juce::Rectangle<float>(28.0f, 465.0f, 217.0f, 270.0f);
        const auto& coverage = profile->generated.coverage;
        paintMetric(graphics, sidebar.removeFromTop(72.0f), "VALID PRESSES",
            juce::String(coverage.lowPresses + coverage.mediumPresses + coverage.highPresses),
            juce::Colour(green));
        sidebar.removeFromTop(12.0f);
        paintMetric(graphics, sidebar.removeFromTop(72.0f), "SAMPLED KEY COVERAGE",
            juce::String(static_cast<int>(std::lround(coverage.score * 100.0))) + "%",
            coverage.score >= 0.8 ? juce::Colour(green) : juce::Colour(amber));
        sidebar.removeFromTop(12.0f);
        paintMetric(graphics, sidebar.removeFromTop(72.0f), "SECTIONS",
            juce::String(profile->generated.segmentAlignments.size()),
            profile->generated.segmentsConnected ? juce::Colour(green) : juce::Colour(red));

        paintKeyboard(graphics, keyboardBounds);
        paintCurve(graphics, curveBounds);
    }
}

void MainComponent::paintMetric(
    juce::Graphics& graphics,
    const juce::Rectangle<float> bounds,
    const juce::String& label,
    const juce::String& value,
    const juce::Colour accent)
{
    graphics.setColour(juce::Colour(panelRaised));
    graphics.fillRoundedRectangle(bounds, 5.0f);
    graphics.setColour(accent);
    graphics.fillRect(bounds.withWidth(4.0f));
    graphics.setColour(juce::Colour(textMuted));
    graphics.setFont(11.0f);
    graphics.drawText(label, bounds.reduced(14.0f).removeFromTop(18.0f), juce::Justification::left);
    graphics.setColour(juce::Colour(textPrimary));
    graphics.setFont(22.0f);
    graphics.drawText(value, bounds.reduced(14.0f), juce::Justification::centredLeft);
}

void MainComponent::paintKeyboard(juce::Graphics& graphics, const juce::Rectangle<float> bounds)
{
    graphics.setColour(juce::Colour(panel));
    graphics.fillRoundedRectangle(bounds, 5.0f);
    if (keyboardKeyBounds.isEmpty())
        return;

    const auto contentWidth = std::max(
        keyboardKeyBounds.getWidth(), minimumWhiteKeyWidth * pianoWhiteKeyCount);
    const auto whiteWidth = contentWidth / pianoWhiteKeyCount;
    const auto originX = keyboardKeyBounds.getX()
        - static_cast<float>(keyboardScrollBar.getCurrentRangeStart());
    graphics.saveState();
    graphics.reduceClipRegion(keyboardKeyBounds.toNearestInt());
    int whiteIndex = 0;
    for (int note = firstPianoNote; note <= lastPianoNote; ++note) {
        if (velcal::isBlackKey(static_cast<std::uint8_t>(note)))
            continue;
        auto key = juce::Rectangle<float>(
            originX + whiteIndex * whiteWidth,
            keyboardKeyBounds.getY(), whiteWidth, keyboardKeyBounds.getHeight());
        const auto& stats = profile->generated.noteStats[static_cast<std::size_t>(note)];
        graphics.setColour(juce::Colour(0xffe2e5e4));
        graphics.fillRect(key);
        graphics.setColour(juce::Colour(0xffaab0b2));
        graphics.drawRect(key, 1.0f);
        if (stats.samplesSeen != 0) {
            const auto statusColour = !hasCompleteRegionalCoverage(
                                          stats, profile->settings.desiredSamplesPerRegion)
                ? juce::Colour(amber)
                : responseColour(stats.medianRawMinusReference, 1.0);
            graphics.setColour(statusColour);
            graphics.fillRect(key.removeFromBottom(10.0f).reduced(1.0f, 0.0f));
        }
        if (note == selectedNote) {
            graphics.setColour(juce::Colour(selection));
            graphics.drawRect(juce::Rectangle<float>(
                originX + whiteIndex * whiteWidth,
                keyboardKeyBounds.getY(), whiteWidth, keyboardKeyBounds.getHeight()), 2.0f);
        }
        ++whiteIndex;
    }
    whiteIndex = 0;
    for (int note = firstPianoNote; note <= lastPianoNote; ++note) {
        if (!velcal::isBlackKey(static_cast<std::uint8_t>(note))) {
            ++whiteIndex;
            continue;
        }
        const auto blackWidth = whiteWidth * 0.62f;
        const auto key = juce::Rectangle<float>(
            originX + whiteIndex * whiteWidth - blackWidth * 0.5f,
            keyboardKeyBounds.getY(), blackWidth, keyboardKeyBounds.getHeight() * 0.64f);
        const auto& stats = profile->generated.noteStats[static_cast<std::size_t>(note)];
        graphics.setColour(juce::Colour(0xff292e32));
        graphics.fillRoundedRectangle(key, 1.5f);
        if (stats.samplesSeen != 0) {
            const auto statusColour = !hasCompleteRegionalCoverage(
                                          stats, profile->settings.desiredSamplesPerRegion)
                ? juce::Colour(amber)
                : responseColour(stats.medianRawMinusReference, 1.0);
            graphics.setColour(statusColour);
            graphics.fillRect(key.withTop(key.getBottom() - 9.0f).reduced(1.0f, 0.0f));
        }
        if (note == selectedNote) {
            graphics.setColour(juce::Colour(selection));
            graphics.drawRoundedRectangle(key, 1.5f, 2.0f);
        }
    }
    graphics.restoreState();

    auto legend = bounds.reduced(16.0f, 7.0f).removeFromBottom(16.0f);
    const std::array<std::pair<juce::Colour, juce::String>, 5> items{{
        {juce::Colour(panel), "No data"},
        {juce::Colour(amber), "Needs data"},
        {juce::Colour(cyan), "Boosted"},
        {juce::Colour(0xff69737a), "Neutral"},
        {juce::Colour(red), "Reduced"},
    }};
    const auto itemWidth = legend.getWidth() / static_cast<float>(items.size());
    for (const auto& item : items) {
        auto itemBounds = legend.removeFromLeft(itemWidth);
        const auto swatch = itemBounds.removeFromLeft(10.0f).reduced(0.0f, 3.0f);
        graphics.setColour(item.first);
        graphics.fillRect(swatch);
        if (item.second == "No data") {
            graphics.setColour(juce::Colour(0xff69737a));
            graphics.drawRect(swatch);
        }
        itemBounds.removeFromLeft(5.0f);
        graphics.setColour(juce::Colour(textMuted));
        graphics.setFont(10.0f);
        graphics.drawText(item.second, itemBounds, juce::Justification::centredLeft);
    }
}

void MainComponent::paintCurve(juce::Graphics& graphics, const juce::Rectangle<float> bounds)
{
    graphics.setColour(juce::Colour(panel));
    graphics.fillRoundedRectangle(bounds, 5.0f);
    const auto plot = curvePlotBounds();
    paintCurveAxes(graphics, plot);

    juce::Path identity;
    identity.startNewSubPath(plot.getBottomLeft());
    identity.lineTo(plot.getTopRight());
    graphics.setColour(juce::Colour(textMuted).withAlpha(0.45f));
    graphics.strokePath(identity, juce::PathStrokeType(1.0f));

    const auto& map = profile->generated.noteMaps[selectedNote];
    const auto& overrideCurve = profile->noteCurveOverrides[selectedNote];
    const auto adjustment = profile->noteAdjustments[selectedNote];
    juce::Path curve;
    const auto samples = std::max(256, static_cast<int>(plot.getWidth()));
    for (int sample = 0; sample < samples; ++sample) {
        const auto input = 1.0 + 126.0 * sample / static_cast<double>(samples - 1);
        double output = 1.0;
        if (!overrideCurve.points.empty()) {
            output = velcal::evaluateVelocityCurve(
                overrideCurve.points, overrideCurve.smooth, input);
        } else {
            const auto lower = static_cast<int>(std::floor(input));
            const auto upper = std::min(127, lower + 1);
            const auto fraction = input - lower;
            output = map.apply(static_cast<std::uint8_t>(lower)) * (1.0 - fraction)
                + map.apply(static_cast<std::uint8_t>(upper)) * fraction;
        }
        output = std::clamp(output + adjustment, 1.0, 127.0);
        const auto x = plot.getX() + static_cast<float>((input - 1.0) / 126.0) * plot.getWidth();
        const auto y = plot.getBottom()
            - static_cast<float>((output - 1.0) / 126.0) * plot.getHeight();
        if (sample == 0)
            curve.startNewSubPath(x, y);
        else
            curve.lineTo(x, y);
    }
    graphics.setColour(juce::Colour(green));
    graphics.strokePath(curve, juce::PathStrokeType(2.5f));
    paintCurveHandles(
        graphics,
        plot,
        overrideCurve.points.empty() ? sampledCurrentCurve() : overrideCurve.points);
}

void MainComponent::paintGlobalCurve(
    juce::Graphics& graphics,
    const juce::Rectangle<float> bounds)
{
    graphics.setColour(juce::Colour(panel));
    graphics.fillRoundedRectangle(bounds, 5.0f);
    const auto plot = curvePlotBounds();
    paintCurveAxes(graphics, plot);

    juce::Path identity;
    identity.startNewSubPath(plot.getBottomLeft());
    identity.lineTo(plot.getTopRight());
    graphics.setColour(juce::Colour(textMuted).withAlpha(0.45f));
    graphics.strokePath(identity, juce::PathStrokeType(1.0f));

    juce::Path curve;
    const auto samples = std::max(256, static_cast<int>(plot.getWidth()));
    for (int sample = 0; sample < samples; ++sample) {
        const auto input = 1.0 + 126.0 * sample / static_cast<double>(samples - 1);
        const auto output = profile->globalCurve.points.empty()
            ? evaluateParametricCurve(profile->globalCurve, input)
            : velcal::evaluateVelocityCurve(
                profile->globalCurve.points, profile->globalCurve.smooth, input);
        const auto x = plot.getX() + static_cast<float>((input - 1.0) / 126.0) * plot.getWidth();
        const auto y = plot.getBottom()
            - static_cast<float>((output - 1.0) / 126.0) * plot.getHeight();
        if (sample == 0)
            curve.startNewSubPath(x, y);
        else
            curve.lineTo(x, y);
    }
    graphics.setColour(juce::Colour(green));
    graphics.strokePath(curve, juce::PathStrokeType(3.0f));
    paintCurveHandles(
        graphics,
        plot,
        profile->globalCurve.points.empty() ? sampledCurrentCurve() : profile->globalCurve.points);
}

void MainComponent::mouseDown(const juce::MouseEvent& event)
{
    if (!profile)
        return;
    const auto plot = curvePlotBounds();
    if (plot.contains(event.position)) {
        ensureEditableCurve();
        auto& points = editableCurvePoints();
        const auto input = std::round(std::clamp(
            1.0 + 126.0 * (event.position.x - plot.getX()) / plot.getWidth(),
            1.0, 127.0));
        const auto output = std::round(std::clamp(
            1.0 + 126.0 * (plot.getBottom() - event.position.y) / plot.getHeight(),
            1.0, 127.0));

        std::optional<std::size_t> nearest;
        auto nearestDistance = 12.0f;
        for (std::size_t index = 0; index < points.size(); ++index) {
            const auto x = plot.getX()
                + static_cast<float>((points[index].input - 1.0) / 126.0) * plot.getWidth();
            const auto y = plot.getBottom()
                - static_cast<float>((points[index].output - 1.0) / 126.0) * plot.getHeight();
            const auto distance = event.position.getDistanceFrom({x, y});
            if (distance < nearestDistance) {
                nearest = index;
                nearestDistance = distance;
            }
        }

        if (event.mods.isRightButtonDown()) {
            if (nearest && *nearest != 0 && *nearest + 1 < points.size()) {
                points.erase(points.begin() + static_cast<std::ptrdiff_t>(*nearest));
                markProfileDirty();
                updateEffectiveMaps();
                repaint();
            }
            return;
        }

        if (!nearest) {
            const auto previousSize = points.size();
            nearest = velcal::insertVelocityCurvePoint(points, input, output);
            if (points.size() != previousSize) {
                markProfileDirty();
                updateEffectiveMaps();
            }
        }
        activeCurvePoint = nearest;
        mouseDrag(event);
        return;
    }

    if (showingGlobalCurve)
        return;
    if (const auto note = noteAtPosition(event.position)) {
        selectedNote = *note;
        updateEditingControls();
        updateLabels();
        repaint();
    }
}

void MainComponent::mouseDrag(const juce::MouseEvent& event)
{
    if (!profile || !activeCurvePoint)
        return;
    auto& points = editableCurvePoints();
    const auto index = *activeCurvePoint;
    if (index >= points.size())
        return;
    const auto plot = curvePlotBounds();
    auto input = std::round(std::clamp(
        1.0 + 126.0 * (event.position.x - plot.getX()) / plot.getWidth(),
        1.0, 127.0));
    auto output = std::round(std::clamp(
        1.0 + 126.0 * (plot.getBottom() - event.position.y) / plot.getHeight(),
        1.0, 127.0));
    const auto previous = points[index];
    velcal::moveVelocityCurvePoint(points, index, input, output);
    if (points[index].input == previous.input && points[index].output == previous.output) {
        repaint();
        return;
    }
    markProfileDirty();
    if (showingGlobalCurve) {
        profile->globalCurve.name = "Custom";
        globalPresetBox.setSelectedId(0, juce::dontSendNotification);
        globalPresetBox.setText("Custom", juce::dontSendNotification);
        selectedNoteLabel.setText("Global velocity curve  |  Custom", juce::dontSendNotification);
    }
    updateEffectiveMaps();
    repaint();
}

void MainComponent::mouseUp(const juce::MouseEvent&)
{
    activeCurvePoint.reset();
    repaint();
}

void MainComponent::scrollBarMoved(juce::ScrollBar* scrollBar, const double)
{
    if (scrollBar == &keyboardScrollBar)
        repaint(keyboardBounds.toNearestInt());
}

void MainComponent::mouseWheelMove(
    const juce::MouseEvent& event,
    const juce::MouseWheelDetails& wheel)
{
    if (keyboardScrollBar.isVisible() && keyboardBounds.contains(event.position)) {
        const auto movement = std::abs(wheel.deltaX) > std::abs(wheel.deltaY)
            ? wheel.deltaX
            : wheel.deltaY;
        keyboardScrollBar.setCurrentRangeStart(
            keyboardScrollBar.getCurrentRangeStart() - movement * 180.0);
        return;
    }
    juce::Component::mouseWheelMove(event, wheel);
}

std::optional<std::uint8_t> MainComponent::noteAtPosition(const juce::Point<float> position) const
{
    if (!profile || !keyboardKeyBounds.contains(position))
        return std::nullopt;

    std::vector<std::uint8_t> whiteNotes;
    for (int note = firstPianoNote; note <= lastPianoNote; ++note) {
        if (!velcal::isBlackKey(static_cast<std::uint8_t>(note)))
            whiteNotes.push_back(static_cast<std::uint8_t>(note));
    }
    const auto contentWidth = std::max(
        keyboardKeyBounds.getWidth(), minimumWhiteKeyWidth * pianoWhiteKeyCount);
    const auto width = contentWidth / pianoWhiteKeyCount;
    const auto originX = keyboardKeyBounds.getX()
        - static_cast<float>(keyboardScrollBar.getCurrentRangeStart());
    int whiteIndex = 0;
    for (int note = firstPianoNote; note <= lastPianoNote; ++note) {
        if (!velcal::isBlackKey(static_cast<std::uint8_t>(note))) {
            ++whiteIndex;
            continue;
        }
        const auto blackWidth = width * 0.62f;
        const auto blackKey = juce::Rectangle<float>(
            originX + whiteIndex * width - blackWidth * 0.5f,
            keyboardKeyBounds.getY(), blackWidth, keyboardKeyBounds.getHeight() * 0.64f);
        if (blackKey.contains(position))
            return static_cast<std::uint8_t>(note);
    }
    const auto index = juce::jlimit(
        0,
        static_cast<int>(whiteNotes.size() - 1),
        static_cast<int>((position.x - originX) / width));
    return whiteNotes[static_cast<std::size_t>(index)];
}

juce::String MainComponent::midiNoteName(const std::uint8_t note)
{
    static constexpr const char* names[] = {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    return juce::String(names[note % 12]) + juce::String(static_cast<int>(note) / 12 - 1);
}
