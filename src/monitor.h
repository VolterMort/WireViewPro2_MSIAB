#pragma once
#include <windows.h>
#include <atomic>
#include <mutex>
#include "protocol.h"

namespace wireview {
struct Cancellation {
    HANDLE stop;
    const std::atomic<bool>& paused;
    bool requested() const {
        return paused.load() || WaitForSingleObject(stop, 0) == WAIT_OBJECT_0;
    }
};

class Transport {
public:
    virtual ~Transport() = default;
    virtual bool sample(Values& values, const Cancellation& cancellation) = 0;
    virtual void close() noexcept = 0;
};

struct MonitorStatus {
    bool paused;
    bool usb_released;
    bool connected;
};

// The worker is the sole owner of transport I/O. Pause requests never join it
// on the UI thread; usb_released acknowledges that close() has completed.
class Monitor {
public:
    explicit Monitor(Transport& transport) : transport_(transport) {}
    Monitor(const Monitor&) = delete;
    Monitor& operator=(const Monitor&) = delete;
    float read(DWORD index);
    bool set_paused(bool paused);
    MonitorStatus status();
    void stop(); // Host must call before unloading the DLL, outside DllMain.

private:
    static unsigned __stdcall entry(void* self) noexcept;
    void run() noexcept;
    bool start_locked();
    void stop_locked();
    void clear_cache();
    Transport& transport_;
    std::mutex lifecycle_, cache_mutex_;
    HANDLE worker_ = nullptr;
    HANDLE stop_event_ = nullptr;
    HANDLE wake_event_ = nullptr;
    std::atomic<bool> paused_{false};
    std::atomic<bool> usb_released_{true};
    Values values_ = unavailable();
    ULONGLONG last_sample_ = 0;
};
}
