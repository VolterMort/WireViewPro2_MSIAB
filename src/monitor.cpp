#include "monitor.h"
#include <process.h>

namespace wireview {
void Monitor::clear_cache() {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    values_ = unavailable();
    last_sample_ = 0;
}

bool Monitor::start_locked() {
    if (worker_ && WaitForSingleObject(worker_, 0) == WAIT_OBJECT_0) stop_locked();
    if (worker_) return true;
    stop_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    wake_event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (stop_event_ && wake_event_)
        worker_ = reinterpret_cast<HANDLE>(_beginthreadex(nullptr, 0, entry, this, 0, nullptr));
    if (worker_) return true;
    if (stop_event_) CloseHandle(stop_event_);
    if (wake_event_) CloseHandle(wake_event_);
    stop_event_ = wake_event_ = nullptr;
    return false;
}

void Monitor::stop_locked() {
    if (!worker_) return;
    SetEvent(stop_event_);
    WaitForSingleObject(worker_, INFINITE);
    CloseHandle(worker_);
    CloseHandle(stop_event_);
    CloseHandle(wake_event_);
    worker_ = stop_event_ = wake_event_ = nullptr;
}

float Monitor::read(DWORD index) {
    if (index >= source_count) return FLT_MAX;
    std::lock_guard<std::mutex> lifecycle_lock(lifecycle_);
    if (!paused_.load() && !start_locked()) return index == connection_source ? 0 : FLT_MAX;
    std::lock_guard<std::mutex> lock(cache_mutex_);
    if (paused_.load() || !last_sample_ || GetTickCount64() - last_sample_ > 2500)
        return index == connection_source ? 0 : FLT_MAX;
    return values_[index];
}

bool Monitor::set_paused(bool paused) {
    std::lock_guard<std::mutex> lock(lifecycle_);
    const bool previous = paused_.load();
    if (paused) {
        // Mark release pending before waking the worker. Repeated pause calls
        // must not overwrite an acknowledgement already received from it.
        if (!previous) usb_released_.store(false);
        paused_.store(true);
        if (!worker_ || WaitForSingleObject(worker_, 0) == WAIT_OBJECT_0)
            usb_released_.store(true);
        clear_cache();
    } else {
        paused_.store(false);
        if (!start_locked()) return false;
    }
    if (wake_event_) SetEvent(wake_event_);
    return true;
}

MonitorStatus Monitor::status() {
    std::lock_guard<std::mutex> lifecycle_lock(lifecycle_);
    std::lock_guard<std::mutex> lock(cache_mutex_);
    const bool paused = paused_.load();
    const bool released = usb_released_.load() || !worker_
        || WaitForSingleObject(worker_, 0) == WAIT_OBJECT_0;
    return {paused, released, !paused && last_sample_ != 0
        && GetTickCount64() - last_sample_ <= 2500};
}

unsigned __stdcall Monitor::entry(void* self) noexcept {
    static_cast<Monitor*>(self)->run();
    return 0;
}

void Monitor::run() noexcept {
    const HANDLE events[] = {stop_event_, wake_event_};
    const Cancellation cancellation{stop_event_, paused_};
    try {
        while (WaitForSingleObject(stop_event_, 0) != WAIT_OBJECT_0) {
            if (paused_.load()) {
                transport_.close();
                clear_cache();
                usb_released_.store(true);
                if (WaitForMultipleObjects(2, events, FALSE, INFINITE) == WAIT_OBJECT_0) break;
                continue;
            }
            usb_released_.store(false);
            Values next = unavailable();
            const bool ok = transport_.sample(next, cancellation);
            {
                std::lock_guard<std::mutex> lock(cache_mutex_);
                // A pause requested during a read must never publish that read.
                const bool publish = ok && !cancellation.requested();
                values_ = publish ? next : unavailable();
                last_sample_ = publish ? GetTickCount64() : 0;
            }
            if (!ok) transport_.close();
            if (WaitForMultipleObjects(2, events, FALSE, ok ? 1000 : 3000) == WAIT_OBJECT_0) break;
        }
    } catch (...) {
        // Background transport failures must never escape into Afterburner.
    }
    transport_.close();
    clear_cache();
    usb_released_.store(true);
}

void Monitor::stop() {
    std::lock_guard<std::mutex> lock(lifecycle_);
    stop_locked();
    clear_cache();
}
}
