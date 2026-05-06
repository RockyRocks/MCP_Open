#include "NotificationBackend.h"

// ============================================================================
// NullBackend — shared across all platforms as a last-resort fallback.
// Defined first so each platform's CreateBackend() can return it.
// ============================================================================

class NullBackend : public INotificationBackend {
public:
    bool        Send(const NotificationRequest&) override { return false; }
    bool        Flash(int)                       override { return false; }
    std::string GetBackendName()           const override { return "Null"; }
};

// ============================================================================
// Windows — WinRT Toast (primary) + Win32 MessageBeep fallback
// ============================================================================
#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>

// ---------------------------------------------------------------------------
// Shared helper — flash the current process's terminal window
// ---------------------------------------------------------------------------

static bool FlashTerminalWindow(int count) {
    if (count <= 0) return true;
    HWND hwnd = GetConsoleWindow();
    if (!hwnd) return false;
    FLASHWINFO fi  = {};
    fi.cbSize      = sizeof(fi);
    fi.hwnd        = hwnd;
    fi.dwFlags     = FLASHW_ALL | FLASHW_TIMERNOFG;
    fi.uCount      = static_cast<UINT>(count);
    fi.dwTimeout   = 0;
    return FlashWindowEx(&fi) != FALSE;
}

// ---------------------------------------------------------------------------
// Win32Backend — fallback: audio cue only (no UI dep, works everywhere)
// ---------------------------------------------------------------------------

class Win32Backend : public INotificationBackend {
public:
    bool Send(const NotificationRequest& req) override {
        UINT sound = MB_ICONINFORMATION;
        if (req.type == NotificationType::Warning) sound = MB_ICONEXCLAMATION;
        if (req.type == NotificationType::Error)   sound = MB_ICONHAND;
        MessageBeep(sound);
        return true;
    }

    bool Flash(int count) override {
        return FlashTerminalWindow(count);
    }

    std::string GetBackendName() const override { return "Win32"; }
};

// ---------------------------------------------------------------------------
// WinRtBackend — Windows 10/11 Toast via WRL (no cppwinrt required)
// ---------------------------------------------------------------------------

#include <wrl.h>
#include <wrl/wrappers/corewrappers.h>
#include <windows.ui.notifications.h>
#include <windows.data.xml.dom.h>
#include <roapi.h>
#pragma comment(lib, "runtimeobject.lib")

using namespace Microsoft::WRL;
using namespace Microsoft::WRL::Wrappers;
using namespace ABI::Windows::UI::Notifications;
using namespace ABI::Windows::Data::Xml::Dom;

static std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (n <= 0) return {};
    std::wstring w(n - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
    return w;
}

static std::wstring XmlEscape(const std::wstring& s) {
    std::wstring out;
    out.reserve(s.size() + 16);
    for (wchar_t c : s) {
        switch (c) {
            case L'&':  out += L"&amp;";  break;
            case L'<':  out += L"&lt;";   break;
            case L'>':  out += L"&gt;";   break;
            case L'"':  out += L"&quot;"; break;
            case L'\'': out += L"&apos;"; break;
            default:    out += c;         break;
        }
    }
    return out;
}

static std::wstring BuildToastXml(const NotificationRequest& req) {
    // Toast duration: long (25s) for duration_ms >= 10s, else short (~7s)
    const wchar_t* dur = (req.duration_ms >= 10000) ? L"long" : L"short";

    // Prefix the title with a UTF-8 status indicator so the type is visible
    // even in text-only notification lists (Action Center, accessibility tools).
    std::wstring prefix;
    switch (req.type) {
        case NotificationType::Success: prefix = L"\u2713 "; break;  // ✓
        case NotificationType::Warning: prefix = L"\u26a0 "; break;  // ⚠
        case NotificationType::Error:   prefix = L"\u2717 "; break;  // ✗
        default:                        prefix = L"\u2139 "; break;  // ℹ
    }

    std::wstring title   = XmlEscape(prefix + Utf8ToWide(req.title));
    std::wstring message = XmlEscape(Utf8ToWide(req.message));

    return L"<toast duration=\"" + std::wstring(dur) + L"\">"
           L"<visual><binding template=\"ToastGeneric\">"
             L"<text>" + title   + L"</text>"
             L"<text>" + message + L"</text>"
           L"</binding></visual>"
         L"</toast>";
}

class WinRtBackend : public INotificationBackend {
public:
    bool Send(const NotificationRequest& req) override {
        HRESULT hrInit = RoInitialize(RO_INIT_MULTITHREADED);
        bool didInit   = (SUCCEEDED(hrInit) && hrInit != S_FALSE);
        if (FAILED(hrInit) && hrInit != S_FALSE
                           && hrInit != CO_E_ALREADYINITIALIZED) {
            return false;
        }

        bool ok = ShowToast(req);

        if (didInit) RoUninitialize();
        return ok;
    }

    bool Flash(int count) override {
        return FlashTerminalWindow(count);
    }

    std::string GetBackendName() const override { return "WinRT"; }

private:
    bool ShowToast(const NotificationRequest& req) {
        std::wstring xml = BuildToastXml(req);

        ComPtr<IXmlDocumentIO> docIO;
        HRESULT hr = RoGetActivationFactory(
            HStringReference(RuntimeClass_Windows_Data_Xml_Dom_XmlDocument).Get(),
            IID_PPV_ARGS(&docIO));
        if (FAILED(hr)) return false;

        hr = docIO->LoadXml(HStringReference(xml.c_str()).Get());
        if (FAILED(hr)) return false;

        ComPtr<IXmlDocument> xmlDoc;
        hr = docIO.As(&xmlDoc);
        if (FAILED(hr)) return false;

        ComPtr<IToastNotificationManagerStatics> mgr;
        hr = RoGetActivationFactory(
            HStringReference(RuntimeClass_Windows_UI_Notifications_ToastNotificationManager).Get(),
            IID_PPV_ARGS(&mgr));
        if (FAILED(hr)) return false;

        ComPtr<IToastNotifier> notifier;
        hr = mgr->CreateToastNotifier(&notifier);
        if (FAILED(hr)) return false;

        ComPtr<IToastNotificationFactory> factory;
        hr = RoGetActivationFactory(
            HStringReference(RuntimeClass_Windows_UI_Notifications_ToastNotification).Get(),
            IID_PPV_ARGS(&factory));
        if (FAILED(hr)) return false;

        ComPtr<IToastNotification> toast;
        hr = factory->CreateToastNotification(xmlDoc.Get(), &toast);
        if (FAILED(hr)) return false;

        return SUCCEEDED(notifier->Show(toast.Get()));
    }
};

// ---------------------------------------------------------------------------
// CreateBackend — probe WinRT availability, fall back gracefully
// ---------------------------------------------------------------------------

std::unique_ptr<INotificationBackend> CreateBackend() {
    HRESULT hr = RoInitialize(RO_INIT_MULTITHREADED);
    bool winRtOk = SUCCEEDED(hr) || hr == S_FALSE || hr == CO_E_ALREADYINITIALIZED;
    if (SUCCEEDED(hr) && hr != S_FALSE) RoUninitialize();
    return winRtOk ? std::unique_ptr<INotificationBackend>(new WinRtBackend())
                   : std::unique_ptr<INotificationBackend>(new Win32Backend());
}

// ============================================================================
// Linux — notify-send subprocess (primary) + NullBackend fallback
// ============================================================================
#elif defined(__linux__)

#include <unistd.h>    // fork, execvp, access, _exit
#include <sys/types.h> // pid_t
#include <cstdio>      // fputc
#include <string>

// Fire-and-forget: fork a child that execs `prog argv[]`, parent returns
// immediately without waitpid — avoids blocking the MCP server thread.
static bool ExecDetached(const char* prog, const char* const argv[]) {
    pid_t pid = fork();
    if (pid < 0) return false;
    if (pid == 0) {
        // Child: replace ourselves with the notification command.
        // If exec fails, _exit immediately — do NOT run any C++ destructors.
        execvp(prog, const_cast<char* const*>(argv));
        _exit(1);
    }
    return true;  // parent: child is detached, we don't waitpid
}

// Check common binary locations to decide whether notify-send is available.
static bool CommandExists(const char* name) {
    const char* dirs[] = { "/usr/bin/", "/usr/local/bin/", "/bin/", nullptr };
    for (int i = 0; dirs[i]; ++i) {
        std::string path = std::string(dirs[i]) + name;
        if (access(path.c_str(), X_OK) == 0) return true;
    }
    return false;
}

class LinuxNotifySendBackend : public INotificationBackend {
public:
    bool Send(const NotificationRequest& req) override {
        // Map type to notify-send urgency level
        const char* urgency = (req.type == NotificationType::Error)
                              ? "critical" : "normal";
        std::string expire  = std::to_string(req.duration_ms);

        // Pass args as an array — no shell involved, no quoting needed.
        const char* argv[] = {
            "notify-send",
            "--expire-time", expire.c_str(),
            "--urgency",     urgency,
            "--",            // ensures title/message aren't parsed as flags
                             req.title.c_str(),
                             req.message.c_str(),
            nullptr
        };
        return ExecDetached("notify-send", argv);
    }

    bool Flash(int /*count*/) override {
        fputc('\a', stderr);  // terminal bell — universally supported
        return true;
    }

    std::string GetBackendName() const override { return "LinuxNotifySend"; }
};

std::unique_ptr<INotificationBackend> CreateBackend() {
    if (CommandExists("notify-send"))
        return std::unique_ptr<INotificationBackend>(new LinuxNotifySendBackend());
    return std::unique_ptr<INotificationBackend>(new NullBackend());
}

// ============================================================================
// macOS — osascript subprocess
// ============================================================================
#elif defined(__APPLE__)

#include <unistd.h>    // fork, execvp, _exit
#include <sys/types.h> // pid_t
#include <cstdio>      // fputc
#include <string>

// Escape for AppleScript double-quoted string literals.
// The script is passed as a -e argument directly to execvp — no shell quoting.
static std::string OsaEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        if      (c == '"')  out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else                out += c;
    }
    return out;
}

class MacOsascriptBackend : public INotificationBackend {
public:
    bool Send(const NotificationRequest& req) override {
        // Build:  display notification "MESSAGE" with title "TITLE"
        // Passed directly to osascript via execvp — shell not involved.
        std::string script =
            "display notification \""  + OsaEscape(req.message)
          + "\" with title \""         + OsaEscape(req.title) + "\"";

        const char* argv[] = { "osascript", "-e", script.c_str(), nullptr };

        pid_t pid = fork();
        if (pid < 0) return false;
        if (pid == 0) {
            execvp("osascript", const_cast<char* const*>(argv));
            _exit(1);
        }
        return true;
    }

    bool Flash(int /*count*/) override {
        fputc('\a', stderr);
        return true;
    }

    std::string GetBackendName() const override { return "MacOsascript"; }
};

std::unique_ptr<INotificationBackend> CreateBackend() {
    return std::unique_ptr<INotificationBackend>(new MacOsascriptBackend());
}

// ============================================================================
// Other platforms — NullBackend only
// ============================================================================
#else

std::unique_ptr<INotificationBackend> CreateBackend() {
    return std::unique_ptr<INotificationBackend>(new NullBackend());
}

#endif
