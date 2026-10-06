#include "UpdateCheck.hpp"

#include <algorithm>
#include <chrono>
#include <optional>
#include <sstream>
#include <vector>

#include <nlohmann/json.hpp>

#ifndef VELCAL_VERSION
#define VELCAL_VERSION "0.0.0"
#endif
#ifndef VELCAL_ENABLE_UPDATE_CHECK
#define VELCAL_ENABLE_UPDATE_CHECK 0
#endif

namespace {
constexpr const char* releasesPage = "https://github.com/SH4DOWSIX/VelCal/releases";

std::vector<int> versionParts(juce::String version)
{
    version = version.trim();
    if (version.startsWithIgnoreCase("v"))
        version = version.substring(1);
    std::vector<int> parts;
    std::stringstream stream(version.toStdString());
    std::string segment;
    while (std::getline(stream, segment, '.')) {
        const auto suffix = segment.find_first_not_of("0123456789");
        if (suffix != std::string::npos)
            segment = segment.substr(0, suffix);
        parts.push_back(segment.empty() ? 0 : std::stoi(segment));
    }
    return parts;
}

int compareVersions(const juce::String& left, const juce::String& right)
{
    auto leftParts = versionParts(left);
    auto rightParts = versionParts(right);
    const auto count = std::max(leftParts.size(), rightParts.size());
    leftParts.resize(count);
    rightParts.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        if (leftParts[i] < rightParts[i]) return -1;
        if (leftParts[i] > rightParts[i]) return 1;
    }
    return 0;
}

class GithubRequest final : public UpdateCheck::Request {
public:
    GithubRequest()
        : stream(juce::URL("https://api.github.com/repos/SH4DOWSIX/VelCal/releases/latest"), false)
    {
        stream.withConnectionTimeout(5000).withNumRedirectsToFollow(0)
            .withExtraHeaders(juce::String("User-Agent: VelCal/") + VELCAL_VERSION
                + "\r\nAccept: application/vnd.github+json\r\n");
    }

    UpdateStatus perform() override
    {
        struct CloseStream {
            juce::WebInputStream& stream;
            ~CloseStream() { stream.cancel(); }
        } close{stream};
        UpdateStatus unavailable{"Update check unavailable", "Could not reach GitHub releases", false};
        if (cancelled.load() || !stream.connect(nullptr) || stream.getStatusCode() != 200)
            return unavailable;

        constexpr std::size_t maximumResponseBytes = 256 * 1024;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        std::string response;
        char buffer[4096];
        while (!cancelled.load() && !stream.isExhausted()) {
            if (std::chrono::steady_clock::now() >= deadline)
                return unavailable;
            const auto count = stream.read(buffer, static_cast<int>(sizeof(buffer)));
            if (count <= 0) break;
            if (response.size() + static_cast<std::size_t>(count) > maximumResponseBytes)
                return unavailable;
            response.append(buffer, static_cast<std::size_t>(count));
        }
        if (cancelled.load() || stream.isError())
            return unavailable;
        const auto release = nlohmann::json::parse(response);
        const auto tag = juce::String(release.value("tag_name", std::string{}));
        if (tag.isEmpty())
            return unavailable;
        const auto available = compareVersions(tag, VELCAL_VERSION) > 0;
        return {available ? "Update available: " + tag : "VelCal is up to date",
            available ? "VelCal " + tag + " is available at " + releasesPage
                      : "Installed version " + juce::String(VELCAL_VERSION), available};
    }

    void cancel() override
    {
        cancelled.store(true);
        stream.cancel();
    }

private:
    std::atomic<bool> cancelled{false};
    juce::WebInputStream stream;
};
} // namespace

UpdateCheck::UpdateCheck(UpdateStatus completed) : status(std::move(completed)) {}

UpdateCheck::UpdateCheck(std::unique_ptr<Request> next,
    std::function<void(const UpdateStatus&)> completed) : request(std::move(next))
{
    worker = std::thread([this, completed = std::move(completed)] {
        UpdateStatus result{"Update check unavailable", "Could not read GitHub releases", false};
        try { result = request->perform(); } catch (...) {}
        {
            const std::lock_guard<std::mutex> lock(mutex);
            status = result;
        }
        if (!stopping.load() && completed)
            completed(result);
    });
}

UpdateCheck::~UpdateCheck()
{
    stopping.store(true);
    if (request) request->cancel();
    if (worker.joinable()) worker.join();
}

UpdateStatus UpdateCheck::snapshot()
{
    const std::lock_guard<std::mutex> lock(mutex);
    return status;
}

std::shared_ptr<UpdateCheck> sharedUpdateCheck()
{
    struct Cache {
        std::mutex mutex;
        std::weak_ptr<UpdateCheck> active;
        std::optional<UpdateStatus> completed;
    };
    // Only instances own workers. DLL-static storage retains results and a weak reference.
    static const auto cache = std::make_shared<Cache>();
    const std::lock_guard<std::mutex> lock(cache->mutex);
    if (auto active = cache->active.lock())
        return active;
    std::shared_ptr<UpdateCheck> check;
    if (cache->completed) {
        check = std::make_shared<UpdateCheck>(*cache->completed);
    } else {
#if VELCAL_ENABLE_UPDATE_CHECK
        if (juce::SystemStats::getEnvironmentVariable("VELCAL_DISABLE_UPDATE_CHECK", {}) != "1") {
            check = std::make_shared<UpdateCheck>(std::make_unique<GithubRequest>(),
                [state = cache](const UpdateStatus& result) {
                    const std::lock_guard<std::mutex> lock(state->mutex);
                    state->completed = result;
                });
        }
#endif
        if (!check) {
            cache->completed = UpdateStatus{"Update check off", "GitHub update checks are disabled", false};
            check = std::make_shared<UpdateCheck>(*cache->completed);
        }
    }
    cache->active = check;
    return check;
}
