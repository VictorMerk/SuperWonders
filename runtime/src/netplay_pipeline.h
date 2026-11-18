#pragma once
#include <condition_variable>
#include <exception>
#include <functional>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <utility>

namespace nx::lan {
// One producer, one persistent worker, one outstanding exchange. Submit returns
// the previous result, so simulation can overlap transport without prediction.
// The first submit returns no result: callers must supply the neutral lead-in.
template<class Job, class Result> class OneAhead {
    std::function<Result(Job)> work;
    std::function<void()> cancel;
    std::mutex lock;
    std::condition_variable changed;
    std::optional<Job> queued;
    std::optional<Result> completed;
    std::exception_ptr error;
    bool outstanding=false, ready=false, closed=false;
    std::thread worker;
    void loop() {
        for (;;) {
            std::unique_lock guard(lock);
            changed.wait(guard,[&] { return closed || queued.has_value(); });
            if (closed) return;
            Job job=std::move(*queued); queued.reset();
            guard.unlock();
            std::optional<Result> result;
            std::exception_ptr failure;
            try { result.emplace(work(std::move(job))); }
            catch (...) { failure=std::current_exception(); }
            guard.lock();
            completed=std::move(result); error=failure; ready=true;
            changed.notify_all();
        }
    }
public:
    OneAhead(std::function<Result(Job)> exchange, std::function<void()> interrupt)
        :work(std::move(exchange)),cancel(std::move(interrupt)),worker([this] { loop(); }) {}
    OneAhead(const OneAhead&)=delete;
    OneAhead& operator=(const OneAhead&)=delete;
    ~OneAhead() { stop(); }
    std::optional<Result> receive() {
        std::unique_lock guard(lock);
        if (!outstanding) return {};
        changed.wait(guard,[&] { return closed || ready; });
        if (error) std::rethrow_exception(error);
        if (closed) throw std::runtime_error("LAN pipeline cancelled");
        auto result=std::move(completed); completed.reset();
        outstanding=false; ready=false;
        return result;
    }
    std::optional<Result> submit(Job job) {
        auto previous=receive();
        std::lock_guard guard(lock);
        if (closed) throw std::runtime_error("LAN pipeline cancelled");
        queued.emplace(std::move(job)); outstanding=true;
        changed.notify_all();
        return previous;
    }
    void stop() noexcept {
        {
            std::lock_guard guard(lock);
            if (closed) return;
            closed=true; changed.notify_all();
        }
        try { cancel(); } catch (...) {}
        if (worker.joinable()) worker.join();
    }
};
}
