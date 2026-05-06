#pragma once
// NotificationBackend.h — Internal abstraction for the notification engine.
// This header is NOT part of the MCP plugin ABI; it is private to the DLL.
// The C ABI in windows_notification.cpp uses INotificationBackend via the
// opaque plugin handle, keeping WinRT/Win32 details out of the ABI layer.
//
// INotificationBackend is also the seam used in unit tests: MockBackend
// implements this interface and is injected via CreateBackend() in test builds.

#include <memory>
#include <string>

// ---------------------------------------------------------------------------
// NotificationType — maps to visual/audio cues and to the MCP tool's
// "type" parameter ("info" | "success" | "warning" | "error").
// ---------------------------------------------------------------------------
enum class NotificationType { Info, Success, Warning, Error };

// ---------------------------------------------------------------------------
// NotificationRequest — all parameters for one notification event.
// ---------------------------------------------------------------------------
struct NotificationRequest {
    std::string      title;
    std::string      message;
    NotificationType type        = NotificationType::Info;
    int              duration_ms = 5000;   // 1000–30000 ms clamped before use
    int              flash_count = 0;      // 0 = no taskbar flash; max 20
};

// ---------------------------------------------------------------------------
// INotificationBackend — strategy interface for the notification engine.
//
//   WinRtBackend  — Windows 10/11 Toast via WRL/COM (primary)
//   Win32Backend  — MessageBeep + FlashWindowEx (older Windows fallback)
//   NullBackend   — no-op (non-Windows / test isolation)
// ---------------------------------------------------------------------------
class INotificationBackend {
public:
    virtual ~INotificationBackend() = default;

    /// Show a desktop notification. Returns true if the system accepted it.
    /// Must return quickly (<10 ms); the OS handles display asynchronously.
    virtual bool Send(const NotificationRequest& req) = 0;

    /// Flash the terminal window in the taskbar `count` times.
    virtual bool Flash(int count) = 0;

    /// Human-readable name for logging ("WinRT", "Win32", "Null", "Mock").
    virtual std::string GetBackendName() const = 0;
};

// ---------------------------------------------------------------------------
// Factory — called once from mcp_plugin_create().
// Tries WinRtBackend first; falls back to Win32Backend; falls back to Null.
// ---------------------------------------------------------------------------
std::unique_ptr<INotificationBackend> CreateBackend();
