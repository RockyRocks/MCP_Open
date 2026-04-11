// DesktopNotification_test.cpp
// Unit tests for the desktop_notification plugin using MockBackend.
//
// All tests are platform-neutral: MockBackend replaces the real backend, so
// no WinRT, notify-send, or osascript is invoked during testing.
// CreateBackendTest exercises the real factory to verify it returns a non-null
// backend on the current platform.

#include <gtest/gtest.h>

#include "../plugins/desktop_notification/src/NotificationBackend.h"

#include <memory>
#include <string>

// ============================================================================
// MockBackend — test double for INotificationBackend
// ============================================================================

struct MockBackend : public INotificationBackend {
    // Captured state for assertions
    NotificationRequest lastRequest;
    int                 sendCallCount  = 0;
    int                 flashCallCount = 0;
    int                 lastFlashCount = 0;

    // Failure injection
    bool shouldFailSend  = false;
    bool shouldFailFlash = false;

    bool Send(const NotificationRequest& req) override {
        lastRequest = req;
        ++sendCallCount;
        return !shouldFailSend;
    }

    bool Flash(int count) override {
        ++flashCallCount;
        lastFlashCount = count;
        return !shouldFailFlash;
    }

    std::string GetBackendName() const override { return "Mock"; }
};

// ============================================================================
// Helpers
// ============================================================================

namespace {

NotificationRequest MakeReq(
    const std::string& title   = "Test title",
    const std::string& message = "Test message",
    NotificationType   type    = NotificationType::Info,
    int                dur     = 5000,
    int                flash   = 0)
{
    NotificationRequest req;
    req.title       = title;
    req.message     = message;
    req.type        = type;
    req.duration_ms = dur;
    req.flash_count = flash;
    return req;
}

}  // namespace

// ============================================================================
// NotificationRequest — struct defaults
// ============================================================================

TEST(NotificationRequestTest, DefaultTypeIsInfo) {
    NotificationRequest req;
    EXPECT_EQ(req.type, NotificationType::Info);
}

TEST(NotificationRequestTest, DefaultDurationIs5000ms) {
    NotificationRequest req;
    EXPECT_EQ(req.duration_ms, 5000);
}

TEST(NotificationRequestTest, DefaultFlashCountIsZero) {
    NotificationRequest req;
    EXPECT_EQ(req.flash_count, 0);
}

// ============================================================================
// MockBackend — Send behaviour
// ============================================================================

TEST(MockBackendTest, SendCapturesRequest) {
    MockBackend backend;
    auto req = MakeReq("Hello", "World", NotificationType::Success, 8000);
    backend.Send(req);

    EXPECT_EQ(backend.lastRequest.title,       "Hello");
    EXPECT_EQ(backend.lastRequest.message,     "World");
    EXPECT_EQ(backend.lastRequest.type,        NotificationType::Success);
    EXPECT_EQ(backend.lastRequest.duration_ms, 8000);
}

TEST(MockBackendTest, SendReturnsTrueByDefault) {
    MockBackend backend;
    EXPECT_TRUE(backend.Send(MakeReq()));
}

TEST(MockBackendTest, SendReturnsFalseWhenInjected) {
    MockBackend backend;
    backend.shouldFailSend = true;
    EXPECT_FALSE(backend.Send(MakeReq()));
}

TEST(MockBackendTest, SendIncrementsSendCallCount) {
    MockBackend backend;
    backend.Send(MakeReq());
    backend.Send(MakeReq());
    EXPECT_EQ(backend.sendCallCount, 2);
}

// ============================================================================
// MockBackend — Flash behaviour
// ============================================================================

TEST(MockBackendTest, FlashCapturesCount) {
    MockBackend backend;
    backend.Flash(5);
    EXPECT_EQ(backend.lastFlashCount, 5);
}

TEST(MockBackendTest, FlashReturnsTrueByDefault) {
    MockBackend backend;
    EXPECT_TRUE(backend.Flash(3));
}

TEST(MockBackendTest, FlashReturnsFalseWhenInjected) {
    MockBackend backend;
    backend.shouldFailFlash = true;
    EXPECT_FALSE(backend.Flash(3));
}

// ============================================================================
// NotificationType — all enum values exist
// ============================================================================

TEST(NotificationTypeTest, AllEnumValuesDistinct) {
    EXPECT_NE(NotificationType::Info,    NotificationType::Success);
    EXPECT_NE(NotificationType::Info,    NotificationType::Warning);
    EXPECT_NE(NotificationType::Info,    NotificationType::Error);
    EXPECT_NE(NotificationType::Success, NotificationType::Warning);
    EXPECT_NE(NotificationType::Success, NotificationType::Error);
    EXPECT_NE(NotificationType::Warning, NotificationType::Error);
}

// ============================================================================
// GetBackendName
// ============================================================================

TEST(MockBackendTest, GetBackendNameReturnsMock) {
    MockBackend backend;
    EXPECT_EQ(backend.GetBackendName(), "Mock");
}

// ============================================================================
// CreateBackend — factory smoke test (exercises the real platform backend)
// ============================================================================

TEST(CreateBackendTest, FactoryReturnsNonNull) {
    auto backend = CreateBackend();
    ASSERT_NE(backend, nullptr);
}

TEST(CreateBackendTest, FactoryBackendNameIsKnown) {
    auto backend = CreateBackend();
    std::string name = backend->GetBackendName();
    // Accept any backend that the current platform can provide
    bool known = (name == "WinRT"            // Windows 10/11
               || name == "Win32"            // older Windows
               || name == "LinuxNotifySend"  // Linux with notify-send
               || name == "MacOsascript"     // macOS
               || name == "Null");           // headless / unsupported platform
    EXPECT_TRUE(known) << "Unexpected backend name: " << name;
}
