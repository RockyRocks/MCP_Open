#!/usr/bin/env python3
"""MCP script plugin — build, test, and lint runners with structured output parsing."""
import sys
import json
import argparse
import subprocess
import os
import re

TOOLS = [
    {
        "name": "build_run",
        "description": "Run a build command. Auto-detects build system from project files if no command given.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "command": {"type": "string", "description": "Explicit build command (overrides auto-detect)"},
                "cwd": {"type": "string", "description": "Working directory (default: cwd)"},
                "target": {"type": "string", "description": "Build target (for cmake/make)"},
                "config": {"type": "string", "description": "Build configuration (e.g. Release, Debug)"}
            }
        }
    },
    {
        "name": "test_run",
        "description": "Run tests. Auto-detects test framework from project files if no command given.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "command": {"type": "string", "description": "Explicit test command (overrides auto-detect)"},
                "cwd": {"type": "string", "description": "Working directory (default: cwd)"},
                "filter": {"type": "string", "description": "Test filter pattern"},
                "timeout": {"type": "integer", "description": "Timeout in seconds (default: 300)"}
            }
        }
    },
    {
        "name": "lint_run",
        "description": "Run a linter. Specify the linter command or let it auto-detect from project files.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "command": {"type": "string", "description": "Explicit lint command"},
                "cwd": {"type": "string", "description": "Working directory (default: cwd)"},
                "fix": {"type": "boolean", "description": "Apply auto-fixes if supported (default: false)"}
            }
        }
    }
]

MAX_OUTPUT = 500_000
DEFAULT_TIMEOUT = 300


def ok(content):
    return {"status": "ok", "content": content}


def err(message):
    return {"status": "error", "error": message}


def run_cmd(command, cwd=None, timeout=DEFAULT_TIMEOUT):
    """Run a command and return (stdout, stderr, exit_code)."""
    try:
        result = subprocess.run(
            command,
            shell=True,
            capture_output=True,
            text=True,
            timeout=timeout,
            cwd=cwd
        )
        return result.stdout[:MAX_OUTPUT], result.stderr[:MAX_OUTPUT], result.returncode
    except subprocess.TimeoutExpired:
        return "", f"Command timed out after {timeout}s", -1
    except FileNotFoundError:
        return "", "Command not found", -1
    except OSError as e:
        return "", str(e), -1


def detect_build_system(cwd):
    """Detect the build system from project files."""
    cwd = cwd or os.getcwd()
    if os.path.exists(os.path.join(cwd, "build", "Makefile")) or \
       os.path.exists(os.path.join(cwd, "build")):
        if os.path.exists(os.path.join(cwd, "CMakeLists.txt")):
            return "cmake"
    if os.path.exists(os.path.join(cwd, "CMakeLists.txt")):
        return "cmake"
    if os.path.exists(os.path.join(cwd, "Makefile")):
        return "make"
    if os.path.exists(os.path.join(cwd, "package.json")):
        return "npm"
    if os.path.exists(os.path.join(cwd, "Cargo.toml")):
        return "cargo"
    if os.path.exists(os.path.join(cwd, "go.mod")):
        return "go"
    return None


def detect_test_framework(cwd):
    """Detect the test framework from project files."""
    cwd = cwd or os.getcwd()
    if os.path.exists(os.path.join(cwd, "package.json")):
        return "npm"
    if os.path.exists(os.path.join(cwd, "Cargo.toml")):
        return "cargo"
    if os.path.exists(os.path.join(cwd, "go.mod")):
        return "go"
    if os.path.exists(os.path.join(cwd, "CMakeLists.txt")):
        return "ctest"
    for root, dirs, files in os.walk(cwd):
        for f in files:
            if f.endswith("_test.py") or f.startswith("test_"):
                return "pytest"
        break
    return None


def build_run(args):
    cwd = args.get("cwd", None)
    command = args.get("command", "")
    target = args.get("target", "")
    config = args.get("config", "")

    if not command:
        system = detect_build_system(cwd)
        if system == "cmake":
            command = "cmake --build build"
            if config:
                command += f" --config {config}"
            if target:
                command += f" --target {target}"
        elif system == "make":
            command = "make"
            if target:
                command += f" {target}"
        elif system == "npm":
            command = "npm run build"
        elif system == "cargo":
            command = "cargo build"
            if config == "Release":
                command += " --release"
        elif system == "go":
            command = "go build ./..."
        else:
            return err("No build system detected. Specify a command explicitly.")

    stdout, stderr, rc = run_cmd(command, cwd=cwd, timeout=DEFAULT_TIMEOUT)

    lines = [f"Command: {command}", f"Exit code: {rc}"]
    if rc == 0:
        lines.append("Build: SUCCESS")
    else:
        lines.append("Build: FAILED")

    if stdout:
        lines.append(f"--- stdout ---\n{stdout}")
    if stderr:
        lines.append(f"--- stderr ---\n{stderr}")

    return ok("\n".join(lines))


def test_run(args):
    cwd = args.get("cwd", None)
    command = args.get("command", "")
    test_filter = args.get("filter", "")
    timeout = args.get("timeout", DEFAULT_TIMEOUT)

    if not command:
        framework = detect_test_framework(cwd)
        if framework == "npm":
            command = "npm test"
        elif framework == "cargo":
            command = "cargo test"
            if test_filter:
                command += f" {test_filter}"
                test_filter = ""
        elif framework == "go":
            command = "go test ./..."
        elif framework == "ctest":
            command = "ctest --test-dir build --output-on-failure"
            if test_filter:
                command += f" -R {test_filter}"
                test_filter = ""
        elif framework == "pytest":
            command = "python -m pytest -v"
            if test_filter:
                command += f" -k {test_filter}"
                test_filter = ""
        else:
            return err("No test framework detected. Specify a command explicitly.")

    if test_filter and test_filter not in command:
        command += f" {test_filter}"

    stdout, stderr, rc = run_cmd(command, cwd=cwd, timeout=timeout)

    lines = [f"Command: {command}", f"Exit code: {rc}"]
    if rc == 0:
        lines.append("Tests: PASSED")
    else:
        lines.append("Tests: FAILED")

    if stdout:
        lines.append(f"--- stdout ---\n{stdout}")
    if stderr:
        lines.append(f"--- stderr ---\n{stderr}")

    return ok("\n".join(lines))


def lint_run(args):
    cwd = args.get("cwd", None)
    command = args.get("command", "")
    fix = args.get("fix", False)

    if not command:
        abs_cwd = os.path.realpath(cwd) if cwd else os.getcwd()
        if os.path.exists(os.path.join(abs_cwd, ".eslintrc.json")) or \
           os.path.exists(os.path.join(abs_cwd, ".eslintrc.js")) or \
           os.path.exists(os.path.join(abs_cwd, "eslint.config.js")):
            command = "npx eslint ."
            if fix:
                command += " --fix"
        elif os.path.exists(os.path.join(abs_cwd, "pyproject.toml")) or \
             os.path.exists(os.path.join(abs_cwd, "setup.py")):
            command = "python -m pylint ."
        elif os.path.exists(os.path.join(abs_cwd, "Cargo.toml")):
            command = "cargo clippy"
            if fix:
                command += " --fix --allow-dirty"
        elif os.path.exists(os.path.join(abs_cwd, ".clang-tidy")):
            command = "clang-tidy"
        else:
            return err("No linter detected. Specify a command explicitly.")

    stdout, stderr, rc = run_cmd(command, cwd=cwd, timeout=DEFAULT_TIMEOUT)

    lines = [f"Command: {command}", f"Exit code: {rc}"]
    if rc == 0:
        lines.append("Lint: CLEAN")
    else:
        lines.append("Lint: ISSUES FOUND")

    if stdout:
        lines.append(f"--- stdout ---\n{stdout}")
    if stderr:
        lines.append(f"--- stderr ---\n{stderr}")

    return ok("\n".join(lines))


DISPATCH = {
    "build_run": build_run,
    "test_run": test_run,
    "lint_run": lint_run,
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

    sys.stderr.write("Usage: build_tools.py --mcp-list | --mcp-call <tool> --mcp-args-file <file>\n")
    sys.exit(1)


if __name__ == "__main__":
    main()
