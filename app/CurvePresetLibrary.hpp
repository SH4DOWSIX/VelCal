#pragma once

#include "DataPaths.hpp"
#include "velcal/profile.hpp"

#include <algorithm>
#include <functional>
#include <stdexcept>

class CurvePresetLibrary final {
public:
    using Presets = std::vector<velcal::VelocityCurveSettings>;

    static juce::File file()
    {
        return velcalProfileDirectory().getChildFile(".velcal-curve-presets.json");
    }

    static juce::File directory()
    {
        return velcalProfileDirectory().getChildFile("curves");
    }

    static juce::File curveFile(const std::string& name)
    {
        const auto text = juce::String(name);
        const auto stem = juce::File::createLegalFileName(text).substring(0, 64);
        return directory().getChildFile(stem + "-"
            + juce::String::toHexString(text.toLowerCase().hashCode64()) + ".velcal-curve.json");
    }

    static velcal::VelocityCurveSettings readCurveFile(const juce::File& source)
    {
        juce::FileInputStream stream(source);
        if (!stream.openedOk())
            throw std::runtime_error("could not read the global curve file");
        const auto presets = velcal::deserializeCurvePresets(stream.readEntireStreamAsString().toStdString());
        validate(presets);
        if (presets.size() != 1)
            throw std::runtime_error("a global curve file must contain exactly one curve");
        return presets.front();
    }

    static void writeCurveFile(const juce::File& target, const velcal::VelocityCurveSettings& curve)
    {
        validate({curve});
        const auto bytes = velcal::serializeCurvePresets({curve});
        static_cast<void>(velcal::deserializeCurvePresets(bytes));
        writeFile(target, bytes);
    }

    static Presets loadWithIndividualFiles()
    {
        Presets presets;
        withLock([&] {
            presets = load();
            synchronizeFiles(presets);
        });
        return presets;
    }

    static Presets load()
    {
        const auto source = file();
        if (!source.exists())
            return {};
        juce::FileInputStream stream(source);
        if (!stream.openedOk())
            throw std::runtime_error("could not read the curve preset library");
        auto presets = velcal::deserializeCurvePresets(stream.readEntireStreamAsString().toStdString());
        validate(presets);
        return presets;
    }

    static bool sameName(const std::string& left, const std::string& right)
    {
        return juce::String(left).equalsIgnoreCase(juce::String(right));
    }

    static void save(velcal::VelocityCurveSettings curve, bool replace)
    {
        edit([&](Presets& presets) {
            auto found = find(presets, curve.name);
            if (found != presets.end()) {
                if (!replace)
                    throw std::runtime_error("a preset with that name already exists");
                *found = std::move(curve);
            } else {
                presets.push_back(std::move(curve));
            }
        });
    }

    static void rename(const std::string& previous, const std::string& name)
    {
        edit([&](Presets& presets) {
            const auto found = find(presets, previous);
            if (found == presets.end())
                throw std::runtime_error("the preset no longer exists");
            const auto duplicate = find(presets, name);
            if (duplicate != presets.end() && duplicate != found)
                throw std::runtime_error("a preset with that name already exists");
            found->name = name;
        });
    }

    static void remove(const std::string& name)
    {
        edit([&](Presets& presets) {
            const auto found = find(presets, name);
            if (found == presets.end())
                throw std::runtime_error("the preset no longer exists");
            presets.erase(found);
        });
    }

private:
    static Presets::iterator find(Presets& presets, const std::string& name)
    {
        return std::find_if(presets.begin(), presets.end(), [&](const auto& curve) {
            return sameName(curve.name, name);
        });
    }

    static void validate(const Presets& presets)
    {
        for (std::size_t index = 0; index < presets.size(); ++index) {
            const auto name = juce::String(presets[index].name);
            if (name.trim().isEmpty() || name != name.trim())
                throw std::runtime_error("preset names must not be empty or padded with spaces");
            for (std::size_t other = 0; other < index; ++other)
                if (sameName(presets[index].name, presets[other].name))
                    throw std::runtime_error("the curve preset library contains duplicate names");
        }
    }

    static void withLock(const std::function<void()>& action)
    {
        const auto target = file();
        // Serialize read-modify-replace across standalone and separate host processes.
        juce::InterProcessLock lock("VelCalCurvePresets-" + juce::String::toHexString(
            target.getFullPathName().hashCode64()));
        if (!lock.enter(250))
            throw std::runtime_error("the curve preset library is busy; try again");
        struct Unlock {
            juce::InterProcessLock& lock;
            ~Unlock() { lock.exit(); }
        } unlock{lock};
        action();
    }

    static void writeFile(const juce::File& target, const std::string& bytes)
    {
        if (target.getParentDirectory().createDirectory().failed())
            throw std::runtime_error("could not create the curve preset folder");
        juce::TemporaryFile temporary(target);
        {
            juce::FileOutputStream stream(temporary.getFile());
            if (!stream.openedOk() || !stream.write(bytes.data(), bytes.size()))
                throw std::runtime_error("could not write the curve preset file");
            stream.flush();
            if (stream.getStatus().failed())
                throw std::runtime_error("could not flush the curve preset file");
        }
        if (!temporary.overwriteTargetFileWithTemporary())
            throw std::runtime_error("could not replace the curve preset file");
    }

    static void synchronizeFiles(const Presets& presets)
    {
        for (const auto& curve : presets) {
            const auto target = curveFile(curve.name);
            const auto bytes = velcal::serializeCurvePresets({curve});
            if (!target.existsAsFile() || target.loadFileAsString().toStdString() != bytes)
                writeCurveFile(target, curve);
        }
    }

    static void edit(const std::function<void(Presets&)>& change)
    {
        withLock([&] {
            const auto previous = load();
            auto presets = previous;
            change(presets);
            validate(presets);
            const auto bytes = velcal::serializeCurvePresets(presets);
            static_cast<void>(velcal::deserializeCurvePresets(bytes));
            // Keep the original library as the index, preserving older installations.
            synchronizeFiles(presets);
            writeFile(file(), bytes);
            for (const auto& curve : previous) {
                const auto oldFile = curveFile(curve.name);
                const auto retained = std::any_of(presets.begin(), presets.end(), [&](const auto& saved) {
                    return curveFile(saved.name) == oldFile;
                });
                if (!retained && oldFile.exists() && !oldFile.deleteFile())
                    throw std::runtime_error("the library was updated but an old curve file could not be removed");
            }
        });
    }
};
