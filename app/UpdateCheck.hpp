#pragma once

#include <JuceHeader.h>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

struct UpdateStatus {
    juce::String text{"Checking for updates"};
    juce::String tooltip{"Checks the latest VelCal release on GitHub"};
    bool available{};
    juce::String latestVersion{};
};

class UpdateCheck final {
public:
    class Request {
    public:
        virtual ~Request() = default;
        virtual UpdateStatus perform() = 0;
        virtual void cancel() = 0;
    };

    explicit UpdateCheck(UpdateStatus completed);
    explicit UpdateCheck(std::unique_ptr<Request> request,
        std::function<void(const UpdateStatus&)> completed = {});
    ~UpdateCheck();
    UpdateStatus snapshot();

private:
    std::mutex mutex;
    UpdateStatus status;
    std::atomic<bool> stopping{false};
    std::unique_ptr<Request> request;
    std::thread worker;
};

std::shared_ptr<UpdateCheck> sharedUpdateCheck();
