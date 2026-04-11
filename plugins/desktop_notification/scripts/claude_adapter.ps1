#Requires -Version 5.1
<#
.SYNOPSIS
    Claude Code hook adapter — forwards Claude Stop/Notification events to
    Windows desktop notifications.

.DESCRIPTION
    Primary path  : HTTP POST to the MCP server's tools/call endpoint.
                    The server's windows_notify tool handles the WinRT toast.

    Fallback path : If the MCP server is unreachable, fires a direct WinRT
                    toast using inline C# (Add-Type), with no external deps.

    This adapter is LLM-agnostic by design: swap the trigger (Claude hook,
    webhook, stdin pipe) and the same MCP HTTP call works for any LLM.
    See hooks/generic/http_adapter.sh for a curl-based example.

.PARAMETER HookType
    "Stop" or "Notification" (matches Claude Code hook event names).
    Determines the notification type and default message.

.PARAMETER Title
    Override the notification title.  Default derives from HookType.

.PARAMETER Message
    Override the notification body.  Default derives from HookType.

.PARAMETER Type
    One of: info, success, warning, error.  Default: derived from HookType.

.PARAMETER McpPort
    Port the MCP server is listening on.  Default: reads $env:MCP_PORT,
    falls back to 8080.

.EXAMPLE
    # Called automatically by Claude Code hooks:
    powershell -File claude_adapter.ps1 -HookType Stop

.EXAMPLE
    # Manual test (no Claude required):
    powershell -File claude_adapter.ps1 -HookType Stop -Title "Done" -Message "Build finished"
#>
param(
    [ValidateSet("Stop","Notification","PreToolUse","PostToolUse","SubagentStop")]
    [string]$HookType   = "Notification",

    [string]$Title      = "",
    [string]$Message    = "",
    [string]$Type       = "",

    [int]   $McpPort    = 0,
    [int]   $FlashCount = 0
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "SilentlyContinue"  # never crash Claude Code

# ---------------------------------------------------------------------------
# 1. Resolve MCP server port
# ---------------------------------------------------------------------------
if ($McpPort -eq 0) {
    $McpPort = if ($env:MCP_PORT) { [int]$env:MCP_PORT } else { 8080 }
}

# ---------------------------------------------------------------------------
# 2. Read Claude hook payload from stdin (optional — Claude sends JSON)
# ---------------------------------------------------------------------------
$hookPayload = $null
if (-not [Console]::IsInputRedirected) { } else {
    try {
        $raw = [Console]::In.ReadToEnd()
        if ($raw -and $raw.Trim().StartsWith("{")) {
            $hookPayload = $raw | ConvertFrom-Json
        }
    } catch { }
}

# ---------------------------------------------------------------------------
# 3. Derive defaults from HookType
# ---------------------------------------------------------------------------
switch ($HookType) {
    "Stop" {
        if (-not $Title)   { $Title   = "Claude finished" }
        if (-not $Message) {
            $Message = if ($hookPayload -and $hookPayload.PSObject.Properties["stop_reason"]) {
                "Task complete: $($hookPayload.stop_reason)"
            } else {
                "The task has completed."
            }
        }
        if (-not $Type)    { $Type = "success" }
        if ($FlashCount -eq 0) { $FlashCount = 3 }
    }
    "Notification" {
        if (-not $Title)   { $Title   = "Claude" }
        if (-not $Message) {
            $Message = if ($hookPayload -and $hookPayload.PSObject.Properties["message"]) {
                $hookPayload.message
            } else {
                "You have a new notification from Claude."
            }
        }
        if (-not $Type)    { $Type = "info" }
    }
    default {
        if (-not $Title)   { $Title   = "Claude Code" }
        if (-not $Message) { $Message = "Event: $HookType" }
        if (-not $Type)    { $Type    = "info" }
    }
}

# ---------------------------------------------------------------------------
# 4. Primary path: call the MCP server's desktop_notify tool
# ---------------------------------------------------------------------------
$mcpUri  = "http://localhost:$McpPort/mcp"
$toolName = if ($FlashCount -gt 0) { "desktop_notify_flash" } else { "desktop_notify" }

$args = @{
    title   = $Title
    message = $Message
    type    = $Type
}
if ($FlashCount -gt 0) { $args["flash_count"] = $FlashCount }

$mcpBody = @{
    jsonrpc = "2.0"
    id      = 1
    method  = "tools/call"
    params  = @{
        name      = $toolName
        arguments = $args
    }
} | ConvertTo-Json -Depth 5

$mcpOk = $false
try {
    $response = Invoke-RestMethod -Uri $mcpUri `
                                  -Method Post `
                                  -ContentType "application/json" `
                                  -Body $mcpBody `
                                  -TimeoutSec 3
    # Check MCP response for isError
    if ($response -and $response.result -and -not $response.result.isError) {
        $mcpOk = $true
    }
} catch { }

if ($mcpOk) { exit 0 }

# ---------------------------------------------------------------------------
# 5. Fallback path: direct WinRT toast via inline C# (no MCP server needed)
# ---------------------------------------------------------------------------
try {
    Add-Type -AssemblyName System.Runtime.WindowsRuntime
    $null = [Windows.UI.Notifications.ToastNotificationManager,
             Windows.UI.Notifications, ContentType = WindowsRuntime]

    $aumid   = "MCPServer.DesktopNotification"
    $appName = "MCP Server"

    # Choose scenario/prefix based on type
    $prefix = switch ($Type) {
        "success" { [char]0x2713 + " " }  # ✓
        "warning" { [char]0x26A0 + " " }  # ⚠
        "error"   { [char]0x2717 + " " }  # ✗
        default   { [char]0x2139 + " " }  # ℹ
    }

    $safeTitle   = [System.Security.SecurityElement]::Escape("$prefix$Title")
    $safeMessage = [System.Security.SecurityElement]::Escape($Message)

    $toastXml = @"
<toast>
  <visual>
    <binding template="ToastGeneric">
      <text>$safeTitle</text>
      <text>$safeMessage</text>
    </binding>
  </visual>
</toast>
"@

    $xmlDoc = [Windows.Data.Xml.Dom.XmlDocument,
               Windows.Data.Xml.Dom, ContentType = WindowsRuntime]::new()
    $xmlDoc.LoadXml($toastXml)

    $toast    = [Windows.UI.Notifications.ToastNotification,
                 Windows.UI.Notifications, ContentType = WindowsRuntime]::new($xmlDoc)
    $notifier = [Windows.UI.Notifications.ToastNotificationManager,
                 Windows.UI.Notifications, ContentType = WindowsRuntime]::CreateToastNotifierWithId($aumid)
    $notifier.Show($toast)

    # Flash taskbar if requested
    if ($FlashCount -gt 0) {
        Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public class WinFlash {
    [StructLayout(LayoutKind.Sequential)]
    public struct FLASHWINFO {
        public uint  cbSize;
        public IntPtr hwnd;
        public uint  dwFlags;
        public uint  uCount;
        public uint  dwTimeout;
    }
    [DllImport("user32.dll")] public static extern bool FlashWindowEx(ref FLASHWINFO fi);
    [DllImport("kernel32.dll")] public static extern IntPtr GetConsoleWindow();
    public const uint FLASHW_ALL = 3;
    public const uint FLASHW_TIMERNOFG = 12;
}
"@ -ErrorAction SilentlyContinue

        $hwnd = [WinFlash]::GetConsoleWindow()
        if ($hwnd -ne [IntPtr]::Zero) {
            $fi = New-Object WinFlash+FLASHWINFO
            $fi.cbSize    = [Runtime.InteropServices.Marshal]::SizeOf($fi)
            $fi.hwnd      = $hwnd
            $fi.dwFlags   = [WinFlash]::FLASHW_ALL -bor [WinFlash]::FLASHW_TIMERNOFG
            $fi.uCount    = [uint32]$FlashCount
            $fi.dwTimeout = 0
            [WinFlash]::FlashWindowEx([ref]$fi) | Out-Null
        }
    }
} catch { }

exit 0
