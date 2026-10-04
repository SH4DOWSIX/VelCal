#include "MainComponent.hpp"

#include <JuceHeader.h>
#include <VelCalIcon.h>

namespace {

class VelCalApplication final : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override { return "VelCal"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise(const juce::String&) override
    {
        mainWindow = std::make_unique<MainWindow>(getApplicationName());
    }

    void shutdown() override { mainWindow.reset(); }

    void systemRequestedQuit() override { quit(); }

private:
    class MainWindow final : public juce::DocumentWindow {
    public:
        explicit MainWindow(const juce::String& name)
            : DocumentWindow(
                name,
                juce::Colour(0xff16191d),
                DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar(true);
            const auto appIcon = juce::ImageFileFormat::loadFrom(
                VelCalIcon::appicon_png,
                VelCalIcon::appicon_pngSize).rescaled(128, 128);
            setIcon(appIcon);
            setResizable(true, true);
            setResizeLimits(860, 720, 1800, 1200);
            setContentOwned(new MainComponent(), true);
            centreWithSize(1180, 760);
            setVisible(true);
            if (auto* peer = getPeer())
                peer->setIcon(appIcon);
        }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }
    };

    std::unique_ptr<MainWindow> mainWindow;
};

} // namespace

START_JUCE_APPLICATION(VelCalApplication)
