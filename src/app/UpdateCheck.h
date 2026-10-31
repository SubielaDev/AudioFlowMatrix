#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>

namespace afm::app {

// Asks GitHub which release is the latest, once, on a background thread.
//
// One HTTPS GET to api.github.com (no data about the user is sent beyond what any request
// carries). Nothing is downloaded or run: the app only offers to open the release page in
// the browser, where the user downloads and can check the published SHA-256.
class UpdateCheck {
public:
    enum class State { Idle, Running, Done, Failed };

    // Starts the check; a no-op while one is running.
    void start();

    [[nodiscard]] State state() const noexcept { return shared_->state.load(std::memory_order_acquire); }
    // The latest release tag ("v0.3.2") once state() is Done; the error text once Failed.
    [[nodiscard]] std::string result() const;

    // Release page for a tag; the tag is validated before it gets here.
    [[nodiscard]] static std::wstring releasePage(const std::string& tag);

private:
    // Shared with the (detached) worker, so closing the app never waits for the network.
    struct Shared {
        std::atomic<State> state{State::Idle};
        mutable std::mutex mutex;
        std::string result;
    };
    std::shared_ptr<Shared> shared_ = std::make_shared<Shared>();
};

} // namespace afm::app
