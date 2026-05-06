#!/usr/bin/env python3
"""MCP script plugin — shell command execution with timeout and background processes."""
import sys
import json
import argparse
import subprocess
import os
import signal

TOOLS = [
    {
        "name": "shell_exec",
        "description": "Execute a shell command and return stdout, stderr, and exit code. Has a configurable timeout.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "command": {"type": "string", "description": "Command to execute"},
                "cwd": {"type": "string", "description": "Working directory (default: cwd)"},
                "timeout": {"type": "integer", "description": "Timeout in seconds (default: 300, max: 600)"},
                "env": {"type": "object", "description": "Additional environment variables"}
            },
            "required": ["command"]
        }
    },
    {
        "name": "shell_exec_background",
        "description": "Start a command in the background. Returns a process ID for later checking or killing.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "command": {"type": "string", "description": "Command to execute"},
                "cwd": {"type": "string", "description": "Working directory (default: cwd)"}
            },
            "required": ["command"]
        }
    },
    {
        "name": "shell_check",
        "description": "Check status of a background process started with shell_exec_background.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "pid": {"type": "integer", "description": "Process ID to check"}
            },
            "required": ["pid"]
        }
    },
    {
        "name": "shell_kill",
        "description": "Kill a background process started with shell_exec_background.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "pid": {"type": "integer", "description": "Process ID to kill"}
            },
            "required": ["pid"]
        }
    }
]

MAX_TIMEOUT = 600
DEFAULT_TIMEOUT = 300
MAX_OUTPUT = 500_000

_background_procs = {}


def ok(content):
    return {"status": "ok", "content": content}


def err(message):
    return {"status": "error", "error": message}


def shell_exec(args):
    command = args.get("command")
    if not command:
        return err("Missing required argument: command")

    cwd = args.get("cwd", None)
    timeout = min(args.get("timeout", DEFAULT_TIMEOUT), MAX_TIMEOUT)
    extra_env = args.get("env", {})

    env = os.environ.copy()
    if extra_env and isinstance(extra_env, dict):
        env.update({str(k): str(v) for k, v in extra_env.items()})

    try:
        result = subprocess.run(
            command,
            shell=True,
            capture_output=True,
            text=True,
            timeout=timeout,
            cwd=cwd,
            env=env
        )
    except subprocess.TimeoutExpired:
        return err(f"Command timed out after {timeout}s: {command}")
    except FileNotFoundError:
        return err(f"Command not found: {command}")
    except OSError as e:
        return err(f"Failed to execute: {e}")

    stdout = result.stdout[:MAX_OUTPUT]
    stderr = result.stderr[:MAX_OUTPUT]

    lines = []
    lines.append(f"Exit code: {result.returncode}")
    if stdout:
        lines.append(f"--- stdout ---\n{stdout}")
    if stderr:
        lines.append(f"--- stderr ---\n{stderr}")
    if not stdout and not stderr:
        lines.append("(no output)")

    return ok("\n".join(lines))


def shell_exec_background(args):
    command = args.get("command")
    if not command:
        return err("Missing required argument: command")

    cwd = args.get("cwd", None)

    try:
        proc = subprocess.Popen(
            command,
            shell=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            cwd=cwd
        )
    except OSError as e:
        return err(f"Failed to start background process: {e}")

    _background_procs[proc.pid] = proc
    return ok(f"Started background process with PID {proc.pid}")


def shell_check(args):
    pid = args.get("pid")
    if pid is None:
        return err("Missing required argument: pid")

    proc = _background_procs.get(pid)
    if proc is None:
        return err(f"No tracked background process with PID {pid}")

    rc = proc.poll()
    if rc is None:
        return ok(f"Process {pid} is still running")

    stdout = ""
    stderr = ""
    try:
        stdout = proc.stdout.read().decode("utf-8", errors="replace")[:MAX_OUTPUT]
        stderr = proc.stderr.read().decode("utf-8", errors="replace")[:MAX_OUTPUT]
    except Exception:
        pass

    lines = [f"Process {pid} exited with code {rc}"]
    if stdout:
        lines.append(f"--- stdout ---\n{stdout}")
    if stderr:
        lines.append(f"--- stderr ---\n{stderr}")

    del _background_procs[pid]
    return ok("\n".join(lines))


def shell_kill(args):
    pid = args.get("pid")
    if pid is None:
        return err("Missing required argument: pid")

    proc = _background_procs.get(pid)
    if proc is None:
        return err(f"No tracked background process with PID {pid}")

    try:
        proc.terminate()
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait(timeout=5)
    except OSError as e:
        return err(f"Failed to kill process {pid}: {e}")

    del _background_procs[pid]
    return ok(f"Killed process {pid}")


DISPATCH = {
    "shell_exec": shell_exec,
    "shell_exec_background": shell_exec_background,
    "shell_check": shell_check,
    "shell_kill": shell_kill,
}


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
        payload = {}
        if args.mcp_args_file:
            with open(args.mcp_args_file, encoding="utf-8") as f:
                payload = json.load(f)
        handler = DISPATCH.get(args.mcp_call)
        if handler:
            result = handler(payload)
        else:
            result = {"status": "error", "error": f"Unknown tool: {args.mcp_call}"}
        sys.stdout.write(json.dumps(result) + "\n")
        sys.stdout.flush()
        return

    sys.stderr.write("Usage: shell_tools.py --mcp-list | --mcp-call <tool> --mcp-args-file <file>\n")
    sys.exit(1)


if __name__ == "__main__":
    main()
