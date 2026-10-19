#include "settings.h"
#include <cstddef>

namespace wireview {
namespace {
constexpr int status_id = 1001;
constexpr int pause_id = 1002;
constexpr int resume_id = 1003;

void update_status(HWND dialog, Monitor& monitor) {
    const auto status = monitor.status();
    const wchar_t* text = status.paused
        ? (status.usb_released ? L"Paused. USB released." : L"Pausing... waiting for USB release.")
        : (status.connected ? L"Monitoring active. USB connected."
                            : L"Monitoring enabled. Waiting for device / USB port.");
    SetDlgItemTextW(dialog, status_id, text);
    EnableWindow(GetDlgItem(dialog, pause_id), !status.paused);
    EnableWindow(GetDlgItem(dialog, resume_id), status.paused && status.usb_released);
}

HWND control(HWND parent, const wchar_t* type, const wchar_t* text, DWORD style,
             int x, int y, int width, int height, int id) {
    RECT rect{x, y, x + width, y + height};
    MapDialogRect(parent, &rect);
    HWND child = CreateWindowExW(0, type, text, WS_CHILD | WS_VISIBLE | style,
        rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
        parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
    HFONT font = reinterpret_cast<HFONT>(SendMessageW(parent, WM_GETFONT, 0, 0));
    if (!font) font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    if (child) SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    return child;
}

INT_PTR CALLBACK dialog_proc(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* monitor = reinterpret_cast<Monitor*>(GetWindowLongPtrW(dialog, DWLP_USER));
    try {
        if (message == WM_INITDIALOG) {
            monitor = reinterpret_cast<Monitor*>(lparam);
            SetWindowLongPtrW(dialog, DWLP_USER, lparam);
            const bool created =
                control(dialog, L"STATIC", L"", 0, 12, 12, 276, 25, status_id)
                && control(dialog, L"STATIC",
                    L"Pause releases USB for the official WireView application.\r\n"
                    L"Close that application before resuming monitoring.\r\n"
                    L"Closing this dialog keeps your current monitoring state.",
                    0, 12, 42, 276, 45, 0)
                && control(dialog, L"BUTTON", L"Pause / Release USB", WS_TABSTOP | BS_PUSHBUTTON,
                           12, 100, 132, 18, pause_id)
                && control(dialog, L"BUTTON", L"Resume monitoring", WS_TABSTOP | BS_PUSHBUTTON,
                           156, 100, 132, 18, resume_id)
                && control(dialog, L"BUTTON", L"Close", WS_TABSTOP | BS_DEFPUSHBUTTON,
                           220, 128, 68, 18, IDOK);
            if (!created || !SetTimer(dialog, 1, 200, nullptr)) {
                EndDialog(dialog, -1);
                return TRUE;
            }
            update_status(dialog, *monitor);
            return TRUE;
        }
        if (!monitor) return FALSE;
        if (message == WM_TIMER) {
            update_status(dialog, *monitor);
            return TRUE;
        }
        if (message == WM_CLOSE || (message == WM_COMMAND
                && (LOWORD(wparam) == IDOK || LOWORD(wparam) == IDCANCEL))) {
            KillTimer(dialog, 1);
            EndDialog(dialog, IDOK);
            return TRUE;
        }
        if (message == WM_COMMAND && (LOWORD(wparam) == pause_id || LOWORD(wparam) == resume_id)) {
            if (!monitor->set_paused(LOWORD(wparam) == pause_id))
                MessageBoxW(dialog, L"Unable to start monitoring. Try Resume monitoring again.",
                            L"WireView Pro II", MB_OK | MB_ICONERROR);
            update_status(dialog, *monitor);
            return TRUE;
        }
    } catch (...) {
        KillTimer(dialog, 1);
        EndDialog(dialog, -1);
        return TRUE;
    }
    return FALSE;
}
}

bool show_settings(Monitor& monitor, HWND parent) {
    // No resource file or custom window class is needed. System dialog and
    // control classes are owned by Windows, so nothing outlives the DLL.
    struct alignas(DWORD) SettingsTemplate {
        DLGTEMPLATE dialog;
        WORD menu;
        WORD window_class;
        WCHAR title[32];
    } settings{};
    static_assert(offsetof(SettingsTemplate, menu) == sizeof(DLGTEMPLATE));
    settings.dialog.style = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | DS_CENTER;
    settings.dialog.cx = 300;
    settings.dialog.cy = 158;
    const wchar_t title[] = L"WireView Pro II monitoring";
    for (std::size_t i = 0; i < sizeof(title) / sizeof(title[0]); ++i) settings.title[i] = title[i];
    return DialogBoxIndirectParamW(GetModuleHandleW(nullptr), &settings.dialog, parent,
        dialog_proc, reinterpret_cast<LPARAM>(&monitor)) == IDOK;
}
}
