# desktop_notification — Hook Adapter Pattern

The notification engine lives once, in `desktop_notification.dll|.so|.dylib`.
Every integration path is just a thin adapter that calls the same two MCP tools:

| Tool                    | When to use                                       |
|-------------------------|---------------------------------------------------|
| `desktop_notify`        | Show a desktop notification                       |
| `desktop_notify_flash`  | Notification + taskbar flash / terminal bell      |

Platform routing is transparent — the same tool call fires a WinRT Toast on
Windows, `notify-send` on Linux, and macOS Notification Center via `osascript`.

---

## How adapters work

```
Any trigger (Claude hook, GitHub Action, curl, CI bot...)
           │
           ▼
    adapter script
     (thin shim)
           │
           │ HTTP POST /mcp  (primary path)
           ▼
    MCP Server
    NativePluginLoader → desktop_notification.dll|.so|.dylib
           │
           ├── Windows  → WinRT Toast (Win10/11) / MessageBeep fallback
           ├── Linux    → notify-send subprocess
           └── macOS    → osascript subprocess
           ▼
    Desktop notification appears
```

If the MCP server is not running, adapters fall back to a direct platform call:
PowerShell adapter → inline WinRT C# (Windows) or native shell (Linux/macOS).

---

## Included adapters

### `generic/http_adapter.sh` — bash + curl (all platforms)

Works anywhere bash and curl are available: CI pipelines, Linux/macOS dev
machines, WSL, Docker containers.

```bash
# Notify on CI success
./hooks/generic/http_adapter.sh \
  --title "Build passed" \
  --message "All tests green on main" \
  --type success \
  --flash 3
```

### `../scripts/claude_adapter.ps1` — PowerShell (Claude Code, Windows primary)

Reads Claude Code hook stdin JSON, derives title/message/type from the hook
event, posts to the MCP server, and falls back to inline WinRT C# if the
server is not running.

```powershell
# Manual test (no Claude required)
powershell -File scripts/claude_adapter.ps1 -HookType Stop
```

---

## Platform-specific notes

### Linux

Requires `notify-send` (usually in `libnotify-bin` or `libnotify` package):

```bash
# Ubuntu/Debian
sudo apt install libnotify-bin

# Arch
sudo pacman -S libnotify

# Fedora
sudo dnf install libnotify
```

The MCP server process must have access to `DBUS_SESSION_BUS_ADDRESS` (set
automatically for graphical sessions; may need forwarding for background
services).

### macOS

Uses `osascript`, which ships with every macOS installation — no extra setup.
On macOS 10.14+, the Terminal (or whichever app runs the MCP server) must have
Notifications permission in System Settings → Notifications.

### Windows

Uses WinRT Toast on Windows 10/11. Falls back to `MessageBeep` (audio only)
on older Windows. No extra setup required.

---

## Wiring other LLMs / systems

Because the notification tools are standard MCP `tools/call` endpoints, any
system that speaks JSON over HTTP can trigger them:

### GPT-4o / OpenAI function calling

Define a function that issues the HTTP POST shown below. The model calls it;
the adapter fires the platform-appropriate notification.

### GitHub Actions

```yaml
- name: Notify desktop
  run: |
    curl -sf -X POST http://$MCP_HOST:8080/mcp \
         -H "Content-Type: application/json" \
         -d '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"desktop_notify","arguments":{"title":"CI passed","message":"${{ github.ref }}","type":"success"}}}'
```

### Zapier / Make

HTTP action → POST `http://localhost:8080/mcp` → body as above.

### Cursor / Copilot / Zed

Any editor that supports custom tasks or shell hooks can run
`http_adapter.sh` or `claude_adapter.ps1` as a post-task command.

---

## Extending

To add a new trigger source, copy `http_adapter.sh`, adjust the argument
parsing, and keep the same `curl` call at the bottom. The server side never
changes.
