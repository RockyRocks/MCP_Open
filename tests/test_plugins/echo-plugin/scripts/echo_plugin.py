#!/usr/bin/env python3
"""MCP script plugin test fixture — echo_tool and fail_tool."""
import sys
import json
import argparse

TOOLS = [
    {
        "name": "echo_tool",
        "description": "Echoes the input message back",
        "inputSchema": {
            "type": "object",
            "properties": {
                "message": {"type": "string", "description": "Message to echo"}
            },
            "required": ["message"]
        }
    },
    {
        "name": "fail_tool",
        "description": "Always returns an error response",
        "inputSchema": {
            "type": "object",
            "properties": {}
        }
    }
]


def main():
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--mcp-list", action="store_true")
    parser.add_argument("--mcp-call", metavar="TOOL")
    parser.add_argument("--mcp-args-file", metavar="FILE")
    args, _ = parser.parse_known_args()

    if args.mcp_list:
        sys.stdout.write(json.dumps(TOOLS) + "\n")
        sys.stdout.flush()
        return

    if args.mcp_call:
        tool_name = args.mcp_call
        call_args = {}
        if args.mcp_args_file:
            with open(args.mcp_args_file, encoding="utf-8") as f:
                call_args = json.load(f)

        if tool_name == "echo_tool":
            msg = call_args.get("message", "(no message)")
            sys.stdout.write(json.dumps({"status": "ok", "content": msg}) + "\n")
        elif tool_name == "fail_tool":
            sys.stdout.write(json.dumps(
                {"status": "error", "error": "fail_tool always fails"}) + "\n")
        else:
            sys.stdout.write(json.dumps(
                {"status": "error", "error": f"Unknown tool: {tool_name}"}) + "\n")
        sys.stdout.flush()
        return

    sys.stderr.write("Usage: echo_plugin.py --mcp-list | --mcp-call <tool> --mcp-args-file <file>\n")
    sys.exit(1)


if __name__ == "__main__":
    main()
