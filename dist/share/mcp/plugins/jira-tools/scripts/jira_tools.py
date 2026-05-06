#!/usr/bin/env python3
"""MCP skill plugin — Read-only Jira operations via REST API.

Supports any Jira instance (Cloud or Server/Data Center).
Zero third-party dependencies — uses only Python stdlib.

CLI usage (invoked by SKILL.md command_template):
    python jira_tools.py get-issue <issue_key>
    python jira_tools.py search <jql> [max_results]
    python jira_tools.py get-comments <issue_key> [filter]

MCP script plugin protocol:
    python jira_tools.py --mcp-list
    python jira_tools.py --mcp-call <tool> --mcp-args-file <file>

Required environment variables:
    JIRA_BASE_URL   — Jira instance URL (e.g. https://jira.example.com)
    JIRA_USERNAME   — Email (Cloud) or username (Server/DC)
    JIRA_API_TOKEN  — API token (Cloud) or personal access token (Server/DC)

Optional:
    JIRA_API_VERSION   — "2" (default, Server/DC) or "3" (Cloud)
    JIRA_CUSTOM_FIELDS — JSON string mapping display names to field IDs
"""
import sys
import os
import json
import base64
import argparse
import re
import ssl
import urllib.request
import urllib.error
import urllib.parse

MAX_OUTPUT = 100_000

# ---------------------------------------------------------------------------
# MCP tool definitions (for --mcp-list / --mcp-call protocol)
# ---------------------------------------------------------------------------

TOOLS = [
    {
        "name": "jira_get_issue",
        "description": "Fetch full Jira issue details — summary, status, assignee, description, custom fields, and URL",
        "inputSchema": {
            "type": "object",
            "properties": {
                "issue_key": {"type": "string", "description": "Jira issue key (e.g. PROJ-123)"}
            },
            "required": ["issue_key"]
        }
    },
    {
        "name": "jira_search",
        "description": "Search Jira issues using JQL. Returns key, summary, status, assignee, priority, type, URL",
        "inputSchema": {
            "type": "object",
            "properties": {
                "jql": {"type": "string", "description": "JQL query string"},
                "max_results": {"type": "integer", "description": "Max results (default 20, max 50)"}
            },
            "required": ["jql"]
        }
    },
    {
        "name": "jira_get_comments",
        "description": "Retrieve all comments for a Jira issue, optionally filtered by text",
        "inputSchema": {
            "type": "object",
            "properties": {
                "issue_key": {"type": "string", "description": "Jira issue key (e.g. PROJ-123)"},
                "filter": {"type": "string", "description": "Optional text filter (case-insensitive substring match)"}
            },
            "required": ["issue_key"]
        }
    }
]

# ---------------------------------------------------------------------------
# Environment & auth
# ---------------------------------------------------------------------------


def check_jira():
    """Validate required environment variables. Returns (ok, error_msg)."""
    base_url = os.environ.get("JIRA_BASE_URL", "").strip()
    if not base_url:
        return False, "JIRA_BASE_URL environment variable not set"
    username = os.environ.get("JIRA_USERNAME", "").strip()
    if not username:
        return False, "JIRA_USERNAME environment variable not set"
    token = os.environ.get("JIRA_API_TOKEN", "").strip()
    if not token:
        return False, "JIRA_API_TOKEN environment variable not set"
    return True, ""


def _get_base_url():
    return os.environ["JIRA_BASE_URL"].strip().rstrip("/")


def _get_api_version():
    return os.environ.get("JIRA_API_VERSION", "2").strip()


def _get_auth_header():
    username = os.environ["JIRA_USERNAME"].strip()
    token = os.environ["JIRA_API_TOKEN"].strip()
    creds = base64.b64encode(f"{username}:{token}".encode()).decode()
    return f"Basic {creds}"


# ---------------------------------------------------------------------------
# HTTP helpers
# ---------------------------------------------------------------------------


def jira_request(path, params=None):
    """HTTP GET to Jira REST API. Returns parsed JSON or raises."""
    base = _get_base_url()
    version = _get_api_version()
    url = f"{base}/rest/api/{version}/{path}"
    if params:
        url += "?" + urllib.parse.urlencode(params)

    req = urllib.request.Request(url)
    req.add_header("Authorization", _get_auth_header())
    req.add_header("Accept", "application/json")

    ctx = ssl.create_default_context()
    try:
        with urllib.request.urlopen(req, timeout=30, context=ctx) as resp:
            return json.loads(resp.read().decode("utf-8"))
    except urllib.error.HTTPError as e:
        body = ""
        try:
            body = e.read().decode("utf-8", errors="replace")[:500]
        except Exception:
            pass
        if e.code == 401:
            raise RuntimeError(
                f"Authentication failed (HTTP 401). Check JIRA_USERNAME and JIRA_API_TOKEN. {body}"
            )
        if e.code == 404:
            raise RuntimeError(f"Not found (HTTP 404). {body}")
        raise RuntimeError(f"HTTP {e.code}: {body}")
    except urllib.error.URLError as e:
        raise RuntimeError(f"Connection error: {e.reason}")


# ---------------------------------------------------------------------------
# ADF (Atlassian Document Format) → plain text
# ---------------------------------------------------------------------------


def adf_to_text(node):
    """Convert an ADF JSON node (Jira Cloud API v3) to plain text."""
    if node is None:
        return ""
    if isinstance(node, str):
        return node

    node_type = node.get("type", "")
    text = node.get("text", "")

    if node_type == "text":
        return text

    parts = []
    for child in node.get("content", []):
        parts.append(adf_to_text(child))

    joined = "".join(parts)

    if node_type == "paragraph":
        return joined + "\n"
    if node_type == "heading":
        level = node.get("attrs", {}).get("level", 1)
        return "#" * level + " " + joined + "\n"
    if node_type == "bulletList":
        return joined
    if node_type == "orderedList":
        return joined
    if node_type == "listItem":
        return "  - " + joined
    if node_type == "codeBlock":
        return "```\n" + joined + "```\n"
    if node_type == "blockquote":
        return "> " + joined.replace("\n", "\n> ") + "\n"
    if node_type == "hardBreak":
        return "\n"
    if node_type == "mention":
        return "@" + node.get("attrs", {}).get("text", "unknown")
    if node_type == "inlineCard":
        return node.get("attrs", {}).get("url", "")
    if node_type == "mediaGroup" or node_type == "mediaSingle":
        return "[media]\n"

    return joined


# ---------------------------------------------------------------------------
# Callstack parser (ported from CallstackParser.cs)
# ---------------------------------------------------------------------------


def parse_callstack(text):
    """Parse Jira's fixed-width callstack format to CSV."""
    if not text or not text.strip():
        return text or ""

    lines = [l for l in text.split("\n") if l.strip()]
    if len(lines) < 2:
        return text

    first_line = lines[0]
    version = None

    if first_line.startswith("{noformat:"):
        title_match = re.search(r"title=([^|]+)\|", first_line)
        if title_match:
            version = title_match.group(1).strip()
        brace_pos = first_line.find("}")
        header_line = first_line[brace_pos + 1:] if brace_pos >= 0 else first_line
    else:
        header_line = first_line

    boundaries = []
    current_start = 0
    for i, ch in enumerate(header_line):
        if ch == "|":
            name = header_line[current_start:i].strip(". ")
            boundaries.append((current_start, i, name))
            current_start = i + 1

    if current_start < len(header_line):
        name = header_line[current_start:].strip(". \r\n")
        if name:
            boundaries.append((current_start, len(header_line), name))

    col_names = [b[2] for b in boundaries]

    def escape_csv(field):
        if not field:
            return ""
        if any(c in field for c in (",", '"', "\n", "\r")):
            return '"' + field.replace('"', '""') + '"'
        return field

    csv_lines = [",".join(escape_csv(c) for c in col_names)]

    for line in lines[1:]:
        if not line.strip() or line.strip() == "{noformat}":
            continue
        values = []
        for start, end, _ in boundaries:
            if start < len(line):
                actual_end = min(end, len(line))
                values.append(line[start:actual_end].strip())
            else:
                values.append("")
        csv_lines.append(",".join(escape_csv(v) for v in values))

    result = "\n".join(csv_lines)
    if version:
        result = f"Version: {version}\n{result}"
    return result


# ---------------------------------------------------------------------------
# Custom fields configuration
# ---------------------------------------------------------------------------


def get_custom_fields_config():
    """Parse JIRA_CUSTOM_FIELDS env var. Returns (issue_types, fields_map) or ([], {})."""
    raw = os.environ.get("JIRA_CUSTOM_FIELDS", "").strip()
    if not raw:
        return [], {}
    try:
        cfg = json.loads(raw)
        issue_types = cfg.get("issue_types", [])
        fields = cfg.get("fields", {})
        return issue_types, fields
    except (json.JSONDecodeError, AttributeError):
        return [], {}


# ---------------------------------------------------------------------------
# Core operations
# ---------------------------------------------------------------------------


def get_issue(issue_key):
    """Fetch and format a single Jira issue."""
    data = jira_request(f"issue/{issue_key}")

    fields = data.get("fields", {})
    key = data.get("key", issue_key)
    base_url = _get_base_url()

    summary = fields.get("summary", "")
    status = (fields.get("status") or {}).get("name", "Unknown")
    priority = (fields.get("priority") or {}).get("name", "None")
    issue_type = (fields.get("issuetype") or {}).get("name", "Unknown")
    assignee = (fields.get("assignee") or {}).get("displayName", "Unassigned")
    reporter = (fields.get("reporter") or {}).get("displayName", "Unknown")
    project = (fields.get("project") or {}).get("key", "")
    created = fields.get("created", "")
    updated = fields.get("updated", "")
    labels = fields.get("labels", [])
    components = [c.get("name", "") for c in (fields.get("components") or [])]

    desc_raw = fields.get("description", "")
    if isinstance(desc_raw, dict):
        description = adf_to_text(desc_raw)
    else:
        description = desc_raw or ""

    comment_data = fields.get("comment", {})
    comment_count = comment_data.get("total", 0) if isinstance(comment_data, dict) else 0

    lines = [
        f"Issue: {key}",
        f"Summary: {summary}",
        f"Status: {status} | Priority: {priority} | Type: {issue_type}",
        f"Assignee: {assignee} | Reporter: {reporter}",
        f"Project: {project}",
    ]
    if labels:
        lines.append(f"Labels: {', '.join(labels)}")
    if components:
        lines.append(f"Components: {', '.join(components)}")
    lines.append(f"Created: {created} | Updated: {updated}")
    lines.append(f"URL: {base_url}/browse/{key}")

    if description.strip():
        lines.append(f"\n--- Description ---\n{description.strip()}")

    issue_types_cfg, fields_cfg = get_custom_fields_config()
    if fields_cfg and (not issue_types_cfg or issue_type in issue_types_cfg):
        custom_parts = []
        for display_name, field_def in fields_cfg.items():
            field_id = field_def.get("id", "")
            if not field_id:
                continue
            raw_value = fields.get(field_id)
            if raw_value is None:
                continue
            if isinstance(raw_value, dict):
                value = raw_value.get("value", raw_value.get("name", str(raw_value)))
            elif isinstance(raw_value, list):
                value = ", ".join(str(v) for v in raw_value)
            else:
                value = str(raw_value)
            if field_def.get("parser") == "callstack" and value:
                value = parse_callstack(value)
            custom_parts.append(f"{display_name}: {value}")
        if custom_parts:
            lines.append("\n--- Custom Fields ---")
            lines.extend(custom_parts)

    lines.append(f"\nComments: {comment_count}")

    output = "\n".join(lines)

    if comment_count > 0:
        output += "\n" + json.dumps({
            "chain": {
                "tool": "jira_get_comments",
                "args": {"issue_key": key}
            }
        })

    return output


def search_issues(jql, max_results=20):
    """Search Jira using JQL and format results."""
    max_results = min(int(max_results), 50) if max_results else 20
    base_url = _get_base_url()

    data = jira_request("search", {
        "jql": jql,
        "maxResults": max_results,
        "fields": "summary,status,assignee,priority,issuetype"
    })

    issues = data.get("issues", [])
    total = data.get("total", len(issues))

    if not issues:
        return f"Results: 0 issues (total matching: {total})\n\nNo issues found."

    lines = [f"Results: {len(issues)} issue{'s' if len(issues) != 1 else ''} (total matching: {total})"]
    lines.append("")

    for issue in issues:
        key = issue.get("key", "")
        f = issue.get("fields", {})
        summary = f.get("summary", "")
        status = (f.get("status") or {}).get("name", "?")
        assignee = (f.get("assignee") or {}).get("displayName", "Unassigned")
        priority = (f.get("priority") or {}).get("name", "None")
        issue_type = (f.get("issuetype") or {}).get("name", "?")
        lines.append(f"{key} [{status}] {summary} (@{assignee}) [{priority}] [{issue_type}]")

    output = "\n".join(lines)

    if len(issues) == 1:
        single_key = issues[0].get("key", "")
        output += "\n" + json.dumps({
            "chain": {
                "tool": "jira_get_issue",
                "args": {"issue_key": single_key}
            }
        })

    return output


def get_comments(issue_key, filter_text=""):
    """Fetch and format comments for an issue."""
    data = jira_request(f"issue/{issue_key}/comment")

    comments = data.get("comments", [])

    if filter_text:
        filter_lower = filter_text.lower()
        comments = [c for c in comments if filter_lower in (c.get("body", "") if isinstance(c.get("body"), str) else json.dumps(c.get("body", ""))).lower()]

    if not comments:
        if filter_text:
            return f"Issue: {issue_key} — 0 comments matching \"{filter_text}\""
        return f"Issue: {issue_key} — 0 comments"

    lines = [f"Issue: {issue_key} — {len(comments)} comment{'s' if len(comments) != 1 else ''}"]
    if filter_text:
        lines[0] += f" matching \"{filter_text}\""
    lines.append("")

    for comment in comments:
        author_data = comment.get("author", {})
        author = author_data.get("displayName", author_data.get("name", "Unknown"))
        created = comment.get("created", "")

        body_raw = comment.get("body", "")
        if isinstance(body_raw, dict):
            body = adf_to_text(body_raw)
        else:
            body = body_raw or ""

        lines.append(f"{author} ({created}):")
        lines.append(body.strip())
        lines.append("")

    return "\n".join(lines).rstrip()


# ---------------------------------------------------------------------------
# CLI interface (invoked by SKILL.md command_template)
# ---------------------------------------------------------------------------


def cli_main():
    if len(sys.argv) < 2:
        print_usage()
        sys.exit(1)

    command = sys.argv[1].lower()

    if command in ("--help", "-h", "help"):
        print_usage()
        sys.exit(0)

    ok, msg = check_jira()
    if not ok:
        sys.stderr.write(f"Error: {msg}\n")
        sys.exit(1)

    try:
        if command == "get-issue":
            if len(sys.argv) < 3:
                sys.stderr.write("Usage: jira_tools.py get-issue <issue_key>\n")
                sys.exit(1)
            output = get_issue(sys.argv[2])

        elif command == "search":
            if len(sys.argv) < 3:
                sys.stderr.write("Usage: jira_tools.py search <jql> [max_results]\n")
                sys.exit(1)
            jql = sys.argv[2]
            max_r = sys.argv[3] if len(sys.argv) > 3 else 20
            output = search_issues(jql, max_r)

        elif command == "get-comments":
            if len(sys.argv) < 3:
                sys.stderr.write("Usage: jira_tools.py get-comments <issue_key> [filter]\n")
                sys.exit(1)
            issue_key = sys.argv[2]
            filter_text = sys.argv[3] if len(sys.argv) > 3 else ""
            output = get_comments(issue_key, filter_text)

        else:
            print_usage()
            sys.exit(1)

        output = output[:MAX_OUTPUT]
        if len(output) == MAX_OUTPUT:
            output += "\n... (truncated at 100KB)"

        sys.stdout.write(output + "\n")
        sys.stdout.flush()

    except RuntimeError as e:
        sys.stderr.write(f"Error: {e}\n")
        sys.exit(1)
    except Exception as e:
        sys.stderr.write(f"Unexpected error: {e}\n")
        sys.exit(1)


def print_usage():
    sys.stdout.write(
        "jira_tools.py — Read-only Jira operations\n"
        "\n"
        "Usage:\n"
        "  jira_tools.py get-issue <issue_key>\n"
        "  jira_tools.py search <jql> [max_results]\n"
        "  jira_tools.py get-comments <issue_key> [filter]\n"
        "\n"
        "Environment variables:\n"
        "  JIRA_BASE_URL     Jira instance URL (required)\n"
        "  JIRA_USERNAME      Email or username (required)\n"
        "  JIRA_API_TOKEN     API token (required)\n"
        "  JIRA_API_VERSION   \"2\" (Server, default) or \"3\" (Cloud)\n"
        "  JIRA_CUSTOM_FIELDS JSON string for custom field mapping\n"
        "\n"
        "MCP protocol:\n"
        "  jira_tools.py --mcp-list\n"
        "  jira_tools.py --mcp-call <tool> --mcp-args-file <file>\n"
    )


# ---------------------------------------------------------------------------
# MCP script plugin protocol (--mcp-list / --mcp-call)
# ---------------------------------------------------------------------------


def mcp_dispatch(tool_name, args):
    """Dispatch an MCP tool call. Returns result dict."""
    ok_flag, msg = check_jira()
    if not ok_flag:
        return {"status": "error", "error": msg}

    try:
        if tool_name == "jira_get_issue":
            issue_key = args.get("issue_key", "")
            if not issue_key:
                return {"status": "error", "error": "Missing required argument: issue_key"}
            return {"status": "ok", "content": get_issue(issue_key)}

        elif tool_name == "jira_search":
            jql = args.get("jql", "")
            if not jql:
                return {"status": "error", "error": "Missing required argument: jql"}
            max_r = args.get("max_results", 20)
            return {"status": "ok", "content": search_issues(jql, max_r)}

        elif tool_name == "jira_get_comments":
            issue_key = args.get("issue_key", "")
            if not issue_key:
                return {"status": "error", "error": "Missing required argument: issue_key"}
            filter_text = args.get("filter", "")
            return {"status": "ok", "content": get_comments(issue_key, filter_text)}

        else:
            return {"status": "error", "error": f"Unknown tool: {tool_name}"}

    except RuntimeError as e:
        return {"status": "error", "error": str(e)}
    except Exception as e:
        return {"status": "error", "error": f"Unexpected error: {e}"}


def mcp_main():
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--mcp-list", action="store_true")
    parser.add_argument("--mcp-call", metavar="TOOL")
    parser.add_argument("--mcp-args-file", metavar="FILE")
    args, _ = parser.parse_known_args()

    if args.mcp_list:
        sys.stdout.write(json.dumps(TOOLS) + "\n")
        sys.stdout.flush()
        return True

    if args.mcp_call:
        payload = {}
        if args.mcp_args_file:
            with open(args.mcp_args_file, encoding="utf-8") as f:
                payload = json.load(f)
        result = mcp_dispatch(args.mcp_call, payload)
        sys.stdout.write(json.dumps(result) + "\n")
        sys.stdout.flush()
        return True

    return False


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------


def main():
    if not mcp_main():
        cli_main()


if __name__ == "__main__":
    main()
