#pragma once

#include <JuceHeader.h>
#include <VelCalIcon.h>

inline juce::Image velcalAppIcon()
{
    const auto icon = [] {
        const auto source = juce::SoftwareImageType{}.convert(juce::ImageFileFormat::loadFrom(
            VelCalIcon::appicon_png, VelCalIcon::appicon_pngSize));
        if (source.isNull())
            return source;
        int left = source.getWidth();
        int top = source.getHeight();
        int right = -1;
        int bottom = -1;
        const juce::Image::BitmapData pixels(source, juce::Image::BitmapData::readOnly);
        for (int y = 0; y < source.getHeight(); ++y) {
            for (int x = 0; x < source.getWidth(); ++x) {
                if (pixels.getPixelColour(x, y).getAlpha() == 0)
                    continue;
                left = juce::jmin(left, x);
                top = juce::jmin(top, y);
                right = juce::jmax(right, x);
                bottom = juce::jmax(bottom, y);
            }
        }
        if (right < left || bottom < top)
            return source;
        return source.getClippedImage(juce::Rectangle<int>(
            left, top, right - left + 1, bottom - top + 1).expanded(2)
                .getIntersection(source.getBounds()));
    }();
    return icon;
}

inline juce::Image velcalWindowIcon()
{
    juce::Image icon(juce::Image::ARGB, 128, 128, true, juce::SoftwareImageType{});
    juce::Graphics graphics(icon);
    graphics.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
    graphics.drawImageWithin(velcalAppIcon(), 0, 0, 128, 128, juce::RectanglePlacement::centred);
    return icon;
}
