#!/usr/bin/env bash
# http_adapter.sh — generic curl-based adapter for the desktop_notify MCP tool.
#
# Usage
# -----
#   http_adapter.sh [OPTIONS]
#
# Options:
#   --title   TEXT    Notification heading          (required)
#   --message TEXT    Body text                     (required)
#   --type    TYPE    info|success|warning|error     (default: info)
#   --flash   N       Taskbar flash cycles 0-20      (default: 0)
#   --port    PORT    MCP server HTTP port           (default: $MCP_PORT or 8080)
#   --host    HOST    MCP server hostname            (default: localhost)
#
# This adapter is intentionally LLM-agnostic — hook it into any system that
# can exec a shell command:
#
#   GitHub Actions   :  - run: ./http_adapter.sh --title "CI passed" --type success
#   Zapier / Make    :  Shell script step
#   Claude hooks     :  "command": "bash ${HOOK_ROOT}/generic/http_adapter.sh ..."
#   GPT function call:  system tool calling this script
#   Cursor / Copilot :  terminal task post-hook
#
# Exit codes:
#   0 — notification delivered (or silently swallowed to avoid crashing caller)
#   Non-zero — only returned if --strict flag is set

set -euo pipefail

TITLE=""
MESSAGE=""
TYPE="info"
FLASH=0
PORT="${MCP_PORT:-8080}"
HOST="localhost"
STRICT=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --title)   TITLE="$2";   shift 2 ;;
        --message) MESSAGE="$2"; shift 2 ;;
        --type)    TYPE="$2";    shift 2 ;;
        --flash)   FLASH="$2";   shift 2 ;;
        --port)    PORT="$2";    shift 2 ;;
        --host)    HOST="$2";    shift 2 ;;
        --strict)  STRICT=1;     shift   ;;
        *)         shift ;;
    esac
done

if [[ -z "$TITLE" || -z "$MESSAGE" ]]; then
    echo "http_adapter.sh: --title and --message are required" >&2
    [[ $STRICT -eq 1 ]] && exit 1 || exit 0
fi

# Choose tool: flash variant only when flash_count > 0
TOOL="desktop_notify"
FLASH_JSON=""
if [[ $FLASH -gt 0 ]]; then
    TOOL="desktop_notify_flash"
    FLASH_JSON=", \"flash_count\": $FLASH"
fi

# Build JSON payload (minimal, avoids jq dependency)
json_escape() {
    local s="$1"
    s="${s//\\/\\\\}"
    s="${s//\"/\\\"}"
    s="${s//$'\n'/\\n}"
    s="${s//$'\r'/\\r}"
    s="${s//$'\t'/\\t}"
    printf '%s' "$s"
}

TITLE_ESC=$(json_escape "$TITLE")
MSG_ESC=$(json_escape "$MESSAGE")

BODY=$(cat <<EOF
{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "tools/call",
  "params": {
    "name": "$TOOL",
    "arguments": {
      "title": "$TITLE_ESC",
      "message": "$MSG_ESC",
      "type": "$TYPE"$FLASH_JSON
    }
  }
}
EOF
)

MCP_URL="http://${HOST}:${PORT}/mcp"

if ! curl -sf \
         -X POST "$MCP_URL" \
         -H "Content-Type: application/json" \
         -d "$BODY" \
         --connect-timeout 3 \
         --max-time 5 \
         > /dev/null 2>&1; then
    echo "http_adapter.sh: MCP server not reachable at $MCP_URL (notification skipped)" >&2
    [[ $STRICT -eq 1 ]] && exit 1 || exit 0
fi

exit 0
