#include <windows.h>
#include <setupapi.h>
#include <initguid.h>
#include <devguid.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <MSIAfterburnerMonitoringSourceDesc.h>
#include "protocol.h"
#include "monitor.h"
#include "settings.h"

namespace {
class Handle {
public:
    explicit Handle(HANDLE value = INVALID_HANDLE_VALUE) : value_(value) {}
    ~Handle() { reset(); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    HANDLE get() const { return value_; }
    void reset(HANDLE value = INVALID_HANDLE_VALUE) {
        if (value_ != INVALID_HANDLE_VALUE && value_ != nullptr) CloseHandle(value_);
        value_ = value;
    }
    HANDLE release() { HANDLE value = value_; value_ = INVALID_HANDLE_VALUE; return value; }
private:
    HANDLE value_;
};

std::vector<std::string> candidates() {
    std::vector<std::string> result;
    HDEVINFO devices = SetupDiGetClassDevsA(&GUID_DEVCLASS_PORTS, nullptr, nullptr, DIGCF_PRESENT);
    if (devices == INVALID_HANDLE_VALUE) return result;
    struct DeviceListGuard {
        HDEVINFO value;
        ~DeviceListGuard() { SetupDiDestroyDeviceInfoList(value); }
    } device_list_guard{devices};
    SP_DEVINFO_DATA info{};
    info.cbSize = sizeof(info);
    for (DWORD i = 0; SetupDiEnumDeviceInfo(devices, i, &info); ++i) {
        // Reserve two zero bytes even if a driver returns an unterminated MULTI_SZ.
        char ids[4098]{};
        if (!SetupDiGetDeviceRegistryPropertyA(devices, &info, SPDRP_HARDWAREID, nullptr,
                reinterpret_cast<BYTE*>(ids), sizeof(ids) - 2, nullptr)) continue;
        // STM32 virtual COM VID/PID is shared by other products; verify vendor/product below.
        bool match = false;
        for (const char* id = ids; *id; id += std::strlen(id) + 1)
            for (const char* part = id; *part; ++part)
                if (_strnicmp(part, "VID_0483&PID_5740", 17) == 0) match = true;
        if (!match) continue;
        HKEY key = SetupDiOpenDevRegKey(devices, &info, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
        if (key == INVALID_HANDLE_VALUE) continue;
        char port[257]{};
        DWORD bytes = sizeof(port) - 1, type = 0;
        const auto status = RegQueryValueExA(key, "PortName", nullptr, &type,
                                            reinterpret_cast<BYTE*>(port), &bytes);
        RegCloseKey(key);
        if (status == ERROR_SUCCESS && type == REG_SZ && _strnicmp(port, "COM", 3) == 0)
            result.emplace_back(std::string("\\\\.\\") + port);
    }
    return result;
}

bool request(HANDLE port, BYTE command, BYTE* response, DWORD length,
             const wireview::Cancellation& cancellation) {
    if (cancellation.requested()) return false;
    if (!PurgeComm(port, PURGE_RXCLEAR | PURGE_TXCLEAR)) return false;
    DWORD written;
    if (!WriteFile(port, &command, 1, &written, nullptr) || written != 1) return false;
    const auto deadline = GetTickCount64() + 1000;
    DWORD offset = 0;
    while (offset < length && GetTickCount64() < deadline) {
        if (cancellation.requested()) return false;
        DWORD count = 0;
        if (!ReadFile(port, response + offset, length - offset, &count, nullptr)) return false;
        offset += count;
    }
    DWORD errors = 0;
    COMSTAT status{};
    // Reject an overlong response already queued by the driver, rather than
    // accepting its prefix as a valid sensor frame.
    return offset == length && ClearCommError(port, &errors, &status)
        && errors == 0 && status.cbInQue == 0;
}

HANDLE connect(const wireview::Cancellation& cancellation) {
    for (const auto& name : candidates()) {
        if (cancellation.requested()) break;
        Handle port(CreateFileA(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                               OPEN_EXISTING, 0, nullptr));
        if (port.get() == INVALID_HANDLE_VALUE) continue;
        DCB dcb{};
        dcb.DCBlength = sizeof(dcb);
        COMMTIMEOUTS timeouts{20, 0, 100, 0, 100};
        bool ok = GetCommState(port.get(), &dcb) != FALSE;
        dcb.BaudRate = CBR_115200;
        dcb.ByteSize = 8;
        dcb.Parity = NOPARITY;
        dcb.StopBits = ONESTOPBIT;
        dcb.fBinary = TRUE;
        dcb.fParity = dcb.fOutxCtsFlow = dcb.fOutxDsrFlow = FALSE;
        dcb.fOutX = dcb.fInX = dcb.fDsrSensitivity = FALSE;
        dcb.fDtrControl = DTR_CONTROL_ENABLE;
        dcb.fRtsControl = RTS_CONTROL_DISABLE;
        dcb.fErrorChar = dcb.fNull = dcb.fAbortOnError = FALSE;
        BYTE identity[3]{};
        ok = ok && SetCommState(port.get(), &dcb) && SetCommTimeouts(port.get(), &timeouts)
            && request(port.get(), 0x01, identity, sizeof(identity), cancellation)
            && identity[0] == 0xEF && (identity[1] == 5 || identity[1] == 6);
        if (ok) return port.release();
    }
    return INVALID_HANDLE_VALUE;
}

class SerialTransport : public wireview::Transport {
public:
    bool sample(wireview::Values& values, const wireview::Cancellation& cancellation) override {
        if (cancellation.requested()) return false;
        if (port_.get() == INVALID_HANDLE_VALUE) port_.reset(connect(cancellation));
        BYTE frame[wireview::frame_size]{};
        return port_.get() != INVALID_HANDLE_VALUE
            && request(port_.get(), 0x04, frame, sizeof(frame), cancellation)
            && wireview::decode(frame, sizeof(frame), values);
    }
    void close() noexcept override { port_.reset(); }
private:
    Handle port_;
};
SerialTransport transport;
wireview::Monitor monitor(transport);
}

extern "C" DWORD GetSourcesNum() { return DWORD(wireview::source_count); }

extern "C" BOOL SetupSource(DWORD index, HWND parent) {
    if (index != MAXDWORD && index >= wireview::source_count) return FALSE;
    if (!parent) return TRUE; // SDK capability query; never start USB I/O here.
    try { return wireview::show_settings(monitor, parent) ? TRUE : FALSE; }
    catch (...) { return FALSE; }
}

extern "C" BOOL GetSourceDesc(DWORD index, LPMONITORING_SOURCE_DESC desc) {
    if (!desc || index >= wireview::source_count || desc->dwVersion < 0x10000) return FALSE;
    const auto version = desc->dwVersion;
    std::memset(desc, 0, sizeof(*desc));
    desc->dwVersion = version;
    const char* units = "";
    const char* format = "%.1f";
    const char* fixed[] = {"Total power", "Total current", "Average voltage"};
    const char* temp[] = {"Temperature in", "Temperature out", "External temperature 1", "External temperature 2"};
    char label[100]{};
    desc->fltMaxLimit = 100;
    if (index < 3) {
        std::snprintf(label, sizeof(label), "%s", fixed[index]);
        units = index == 0 ? "W" : index == 1 ? "A" : "V";
        desc->fltMaxLimit = index == 0 ? 600.0f : index == 1 ? 60.0f : 15.0f;
    } else if (index < 21) {
        const auto kind = (index - 3) % 3;
        std::snprintf(label, sizeof(label), "Pin %lu %s", (index - 3) / 3 + 1,
                      kind == 0 ? "voltage" : kind == 1 ? "current" : "power");
        units = kind == 0 ? "V" : kind == 1 ? "A" : "W";
        desc->fltMaxLimit = kind == 0 ? 15.0f : kind == 1 ? 15.0f : 150.0f;
    } else if (index < 25) {
        std::snprintf(label, sizeof(label), "%s", temp[index - 21]);
        units = "\xB0" "C";
        desc->fltMinLimit = -40;
        desc->fltMaxLimit = 150;
    } else if (index == wireview::cable_rating_source) {
        std::snprintf(label, sizeof(label), "Cable rating");
        units = "W";
        format = "%.0f";
        desc->fltMaxLimit = 600;
    } else {
        std::snprintf(label, sizeof(label), "%s", index == 25 ? "Fan duty" : "Connected");
        units = index == 25 ? "%" : "";
        format = "%.0f";
        if (index == 26) desc->fltMaxLimit = 1;
    }
    std::snprintf(desc->szName, sizeof(desc->szName), "WireView %s", label);
    std::snprintf(desc->szGroup, sizeof(desc->szGroup), "WireView");
    std::snprintf(desc->szUnits, sizeof(desc->szUnits), "%s", units);
    std::snprintf(desc->szFormat, sizeof(desc->szFormat), "%s", format);
    desc->dwID = 0xFF; // MONITORING_SOURCE_ID_PLUGIN_MISC
    desc->dwInstance = index;
    return TRUE;
}

extern "C" FLOAT GetSourceData(DWORD index) {
    try { return monitor.read(index); }
    catch (...) { return FLT_MAX; }
}

extern "C" void Uninit() {
    monitor.stop(); // SDK calls Uninit before unloading, outside DllMain.
}
