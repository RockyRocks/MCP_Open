#!/usr/bin/env python3
"""MCP script plugin — Unity3D development tools.

Reference implementation for game engine script plugins.
Tools: project info, build, test, asset search, log parsing, C# checks.
"""
import sys
import json
import argparse
import subprocess
import os
import re
import fnmatch
import platform
import xml.etree.ElementTree as ET
from datetime import datetime

TOOLS = [
    {
        "name": "unity_project_info",
        "description": (
            "Read Unity project metadata: editor version, company name, product name, "
            "scripting backend, and target platform from ProjectSettings."
        ),
        "inputSchema": {
            "type": "object",
            "properties": {
                "project_path": {
                    "type": "string",
                    "description": "Path to the Unity project root (contains Assets/ and ProjectSettings/)"
                }
            },
            "required": ["project_path"]
        }
    },
    {
        "name": "unity_build",
        "description": (
            "Trigger a Unity build in batch mode. Requires the Unity Editor executable path. "
            "Supports all major build targets and custom build methods."
        ),
        "inputSchema": {
            "type": "object",
            "properties": {
                "project_path": {
                    "type": "string",
                    "description": "Path to the Unity project root"
                },
                "unity_path": {
                    "type": "string",
                    "description": "Path to Unity Editor executable (e.g. C:/Program Files/Unity/Hub/Editor/2022.3.0f1/Editor/Unity.exe)"
                },
                "target": {
                    "type": "string",
                    "enum": ["Win64", "Android", "iOS", "WebGL", "Linux64", "OSXUniversal"],
                    "description": "Build target platform"
                },
                "build_method": {
                    "type": "string",
                    "description": "Custom C# static method to execute (e.g. BuildPipeline.BuildPlayer)"
                },
                "output_path": {
                    "type": "string",
                    "description": "Output directory for the build (default: Builds/<target>)"
                },
                "extra_args": {
                    "type": "string",
                    "description": "Additional command-line arguments to pass to Unity"
                }
            },
            "required": ["project_path", "unity_path", "target"]
        }
    },
    {
        "name": "unity_run_tests",
        "description": (
            "Run Unity EditMode or PlayMode tests in batch mode and parse the NUnit XML results. "
            "Returns structured test results with pass/fail counts."
        ),
        "inputSchema": {
            "type": "object",
            "properties": {
                "project_path": {
                    "type": "string",
                    "description": "Path to the Unity project root"
                },
                "unity_path": {
                    "type": "string",
                    "description": "Path to Unity Editor executable"
                },
                "test_mode": {
                    "type": "string",
                    "enum": ["EditMode", "PlayMode", "All"],
                    "description": "Which test mode to run (default: All)"
                },
                "filter": {
                    "type": "string",
                    "description": "Test name filter pattern"
                },
                "timeout": {
                    "type": "integer",
                    "description": "Timeout in seconds (default: 600)"
                }
            },
            "required": ["project_path", "unity_path"]
        }
    },
    {
        "name": "unity_asset_search",
        "description": (
            "Search for assets in a Unity project by name pattern, file extension, and directory. "
            "Returns matching file paths with size and modification time."
        ),
        "inputSchema": {
            "type": "object",
            "properties": {
                "project_path": {
                    "type": "string",
                    "description": "Path to the Unity project root"
                },
                "query": {
                    "type": "string",
                    "description": "File name pattern (glob, e.g. '*Player*', '*.shader')"
                },
                "extension": {
                    "type": "string",
                    "description": "Filter by extension (e.g. .cs, .prefab, .unity, .asset, .mat, .shader, .anim, .controller)"
                },
                "directory": {
                    "type": "string",
                    "description": "Subdirectory under Assets/ to search (e.g. Scripts, Prefabs)"
                },
                "limit": {
                    "type": "integer",
                    "description": "Maximum number of results (default: 100)"
                }
            },
            "required": ["project_path"]
        }
    },
    {
        "name": "unity_log_parser",
        "description": (
            "Parse Unity Editor.log for errors, warnings, and exceptions. "
            "Auto-detects the default log path on Windows and macOS."
        ),
        "inputSchema": {
            "type": "object",
            "properties": {
                "log_path": {
                    "type": "string",
                    "description": "Path to Editor.log (auto-detected if omitted)"
                },
                "severity": {
                    "type": "string",
                    "enum": ["error", "warning", "all"],
                    "description": "Filter by severity level (default: error)"
                },
                "limit": {
                    "type": "integer",
                    "description": "Maximum number of log entries to return (default: 50)"
                }
            }
        }
    },
    {
        "name": "unity_csharp_check",
        "description": (
            "Scan C# source files in a Unity project for common issues: "
            "missing namespaces, empty catch blocks, Update() in non-MonoBehaviour classes, "
            "[SerializeField] on non-serializable types, and public fields that should be properties."
        ),
        "inputSchema": {
            "type": "object",
            "properties": {
                "project_path": {
                    "type": "string",
                    "description": "Path to the Unity project root"
                },
                "path": {
                    "type": "string",
                    "description": "Specific file or subdirectory to check (relative to Assets/)"
                },
                "limit": {
                    "type": "integer",
                    "description": "Maximum number of issues to report (default: 100)"
                }
            },
            "required": ["project_path"]
        }
    }
]

MAX_OUTPUT = 500_000
DEFAULT_TIMEOUT = 600


def ok(content):
    return {"status": "ok", "content": content}


def err(message):
    return {"status": "error", "error": message}


def validate_project_path(project_path):
    """Validate that the path looks like a Unity project."""
    if not project_path:
        return None, "project_path is required"
    project_path = os.path.realpath(project_path)
    if not os.path.isdir(project_path):
        return None, f"Project path does not exist: {project_path}"
    assets_dir = os.path.join(project_path, "Assets")
    settings_dir = os.path.join(project_path, "ProjectSettings")
    if not os.path.isdir(assets_dir) and not os.path.isdir(settings_dir):
        return None, (
            f"Not a Unity project (no Assets/ or ProjectSettings/ found): {project_path}"
        )
    return project_path, None


# ---------------------------------------------------------------------------
# Tool: unity_project_info
# ---------------------------------------------------------------------------

def unity_project_info(args):
    project_path, error = validate_project_path(args.get("project_path"))
    if error:
        return err(error)

    info = {"project_path": project_path}

    version_file = os.path.join(project_path, "ProjectSettings", "ProjectVersion.txt")
    if os.path.isfile(version_file):
        with open(version_file, encoding="utf-8", errors="replace") as f:
            for line in f:
                if line.startswith("m_EditorVersion:"):
                    info["editor_version"] = line.split(":", 1)[1].strip()
                elif line.startswith("m_EditorVersionWithRevision:"):
                    info["editor_version_full"] = line.split(":", 1)[1].strip()

    settings_file = os.path.join(project_path, "ProjectSettings", "ProjectSettings.asset")
    if os.path.isfile(settings_file):
        with open(settings_file, encoding="utf-8", errors="replace") as f:
            content = f.read(200_000)
        patterns = {
            "company_name": r"companyName:\s*(.+)",
            "product_name": r"productName:\s*(.+)",
            "default_screen_width": r"defaultScreenWidth:\s*(\d+)",
            "default_screen_height": r"defaultScreenHeight:\s*(\d+)",
            "scripting_backend": r"scriptingBackend:\s*\{[^}]*Standalone:\s*(\d+)",
            "api_compatibility": r"apiCompatibilityLevelPerPlatform:\s*\{[^}]*Standalone:\s*(\d+)",
        }
        for key, pattern in patterns.items():
            m = re.search(pattern, content)
            if m:
                val = m.group(1).strip()
                if key == "scripting_backend":
                    val = "IL2CPP" if val == "1" else "Mono"
                info[key] = val

    packages_file = os.path.join(project_path, "Packages", "manifest.json")
    if os.path.isfile(packages_file):
        try:
            with open(packages_file, encoding="utf-8") as f:
                manifest = json.load(f)
            deps = manifest.get("dependencies", {})
            info["package_count"] = len(deps)
            info["packages"] = list(deps.keys())[:20]
        except (json.JSONDecodeError, OSError):
            pass

    lines = [f"Unity Project: {info.get('product_name', os.path.basename(project_path))}"]
    for k, v in info.items():
        if k == "packages":
            lines.append(f"  packages (first 20): {', '.join(v)}")
        else:
            lines.append(f"  {k}: {v}")

    return ok("\n".join(lines))


# ---------------------------------------------------------------------------
# Tool: unity_build
# ---------------------------------------------------------------------------

def unity_build(args):
    project_path, error = validate_project_path(args.get("project_path"))
    if error:
        return err(error)

    unity_path = args.get("unity_path", "")
    if not unity_path:
        return err("unity_path is required (path to Unity Editor executable)")

    target = args.get("target", "Win64")
    build_method = args.get("build_method", "")
    output_path = args.get("output_path", "")
    extra_args = args.get("extra_args", "")

    target_map = {
        "Win64": "Win64",
        "Android": "Android",
        "iOS": "iOS",
        "WebGL": "WebGL",
        "Linux64": "Linux64",
        "OSXUniversal": "OSXUniversal",
    }
    build_target = target_map.get(target, target)

    if not output_path:
        output_path = os.path.join(project_path, "Builds", build_target)

    cmd = [
        unity_path,
        "-batchmode",
        "-nographics",
        "-projectPath", project_path,
        "-buildTarget", build_target,
        "-logFile", "-",
        "-quit",
    ]

    if build_method:
        cmd.extend(["-executeMethod", build_method])

    if extra_args:
        cmd.extend(extra_args.split())

    try:
        result = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            timeout=DEFAULT_TIMEOUT
        )
        lines = [
            f"Build target: {build_target}",
            f"Output: {output_path}",
            f"Exit code: {result.returncode}",
            "Build: SUCCESS" if result.returncode == 0 else "Build: FAILED",
        ]
        if result.stdout:
            lines.append(f"--- stdout (last 5000 chars) ---\n{result.stdout[-5000:]}")
        if result.stderr:
            lines.append(f"--- stderr ---\n{result.stderr[:5000]}")
        return ok("\n".join(lines))
    except FileNotFoundError:
        return err(f"Unity Editor not found at: {unity_path}")
    except subprocess.TimeoutExpired:
        return err(f"Build timed out after {DEFAULT_TIMEOUT}s")
    except OSError as e:
        return err(f"Build failed: {e}")


# ---------------------------------------------------------------------------
# Tool: unity_run_tests
# ---------------------------------------------------------------------------

def unity_run_tests(args):
    project_path, error = validate_project_path(args.get("project_path"))
    if error:
        return err(error)

    unity_path = args.get("unity_path", "")
    if not unity_path:
        return err("unity_path is required")

    test_mode = args.get("test_mode", "All")
    test_filter = args.get("filter", "")
    timeout = args.get("timeout", DEFAULT_TIMEOUT)

    results_file = os.path.join(project_path, "TestResults.xml")

    modes = ["EditMode", "PlayMode"] if test_mode == "All" else [test_mode]
    all_results = []

    for mode in modes:
        cmd = [
            unity_path,
            "-batchmode",
            "-nographics",
            "-projectPath", project_path,
            "-runTests",
            "-testPlatform", mode,
            "-testResults", results_file,
            "-logFile", "-",
        ]
        if test_filter:
            cmd.extend(["-testFilter", test_filter])

        try:
            result = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
        except FileNotFoundError:
            return err(f"Unity Editor not found at: {unity_path}")
        except subprocess.TimeoutExpired:
            return err(f"Tests timed out after {timeout}s")

        mode_result = {"mode": mode, "exit_code": result.returncode, "tests": []}

        if os.path.isfile(results_file):
            try:
                tree = ET.parse(results_file)
                root = tree.getroot()
                mode_result["total"] = int(root.get("total", 0))
                mode_result["passed"] = int(root.get("passed", 0))
                mode_result["failed"] = int(root.get("failed", 0))
                mode_result["skipped"] = int(root.get("skipped", 0))

                for tc in root.iter("test-case"):
                    entry = {
                        "name": tc.get("name", ""),
                        "result": tc.get("result", ""),
                        "duration": tc.get("duration", ""),
                    }
                    failure = tc.find("failure")
                    if failure is not None:
                        msg = failure.find("message")
                        if msg is not None and msg.text:
                            entry["failure_message"] = msg.text[:500]
                    mode_result["tests"].append(entry)
            except ET.ParseError:
                mode_result["parse_error"] = "Failed to parse TestResults.xml"
            finally:
                try:
                    os.remove(results_file)
                except OSError:
                    pass

        all_results.append(mode_result)

    lines = []
    for r in all_results:
        total = r.get("total", "?")
        passed = r.get("passed", "?")
        failed = r.get("failed", "?")
        lines.append(f"[{r['mode']}] Total: {total}  Passed: {passed}  Failed: {failed}")
        for t in r.get("tests", []):
            status = "PASS" if t["result"] == "Passed" else "FAIL"
            lines.append(f"  [{status}] {t['name']} ({t.get('duration', '?')}s)")
            if "failure_message" in t:
                lines.append(f"         {t['failure_message']}")

    return ok("\n".join(lines) if lines else "No test results produced.")


# ---------------------------------------------------------------------------
# Tool: unity_asset_search
# ---------------------------------------------------------------------------

def unity_asset_search(args):
    project_path, error = validate_project_path(args.get("project_path"))
    if error:
        return err(error)

    query = args.get("query", "*")
    extension = args.get("extension", "")
    directory = args.get("directory", "")
    limit = args.get("limit", 100)

    search_root = os.path.join(project_path, "Assets")
    if directory:
        safe_dir = directory.replace("..", "").lstrip("/\\")
        search_root = os.path.join(search_root, safe_dir)

    if not os.path.isdir(search_root):
        return err(f"Directory not found: {search_root}")

    if extension and not extension.startswith("."):
        extension = "." + extension

    matches = []
    for root, dirs, files in os.walk(search_root):
        for fname in files:
            if extension and not fname.lower().endswith(extension.lower()):
                continue
            if query != "*" and not fnmatch.fnmatch(fname.lower(), query.lower()):
                continue

            full_path = os.path.join(root, fname)
            rel_path = os.path.relpath(full_path, project_path)
            try:
                stat = os.stat(full_path)
                matches.append({
                    "path": rel_path.replace("\\", "/"),
                    "size": stat.st_size,
                    "modified": datetime.fromtimestamp(stat.st_mtime).isoformat(),
                })
            except OSError:
                matches.append({"path": rel_path.replace("\\", "/")})

            if len(matches) >= limit:
                break
        if len(matches) >= limit:
            break

    lines = [f"Found {len(matches)} assets matching query='{query}' ext='{extension}'"]
    for m in matches:
        size = m.get("size", "?")
        if isinstance(size, int):
            if size > 1_048_576:
                size_str = f"{size / 1_048_576:.1f} MB"
            elif size > 1024:
                size_str = f"{size / 1024:.1f} KB"
            else:
                size_str = f"{size} B"
        else:
            size_str = "?"
        lines.append(f"  {m['path']}  ({size_str})")

    return ok("\n".join(lines))


# ---------------------------------------------------------------------------
# Tool: unity_log_parser
# ---------------------------------------------------------------------------

def _default_log_path():
    system = platform.system()
    if system == "Windows":
        local_app = os.environ.get("LOCALAPPDATA", "")
        if local_app:
            return os.path.join(local_app, "Unity", "Editor", "Editor.log")
    elif system == "Darwin":
        home = os.path.expanduser("~")
        return os.path.join(home, "Library", "Logs", "Unity", "Editor.log")
    elif system == "Linux":
        home = os.path.expanduser("~")
        return os.path.join(home, ".config", "unity3d", "Editor.log")
    return ""


def unity_log_parser(args):
    log_path = args.get("log_path", "") or _default_log_path()
    severity = args.get("severity", "error").lower()
    limit = args.get("limit", 50)

    if not log_path:
        return err("Could not determine log path. Provide log_path explicitly.")
    if not os.path.isfile(log_path):
        return err(f"Log file not found: {log_path}")

    error_patterns = [
        re.compile(r"^(.*Error.*)$", re.IGNORECASE),
        re.compile(r"^(.*Exception.*)$", re.IGNORECASE),
        re.compile(r"^(.*Fatal.*)$", re.IGNORECASE),
        re.compile(r"^(.*NullReferenceException.*)$"),
        re.compile(r"^(.*CompilerError.*)$"),
    ]
    warning_patterns = [
        re.compile(r"^(.*Warning.*)$", re.IGNORECASE),
        re.compile(r"^(.*Deprecated.*)$", re.IGNORECASE),
    ]

    entries = []
    try:
        with open(log_path, encoding="utf-8", errors="replace") as f:
            for line in f:
                line = line.rstrip()
                if not line:
                    continue

                is_error = any(p.match(line) for p in error_patterns)
                is_warning = any(p.match(line) for p in warning_patterns)

                if severity == "error" and is_error:
                    entries.append(("ERROR", line))
                elif severity == "warning" and is_warning:
                    entries.append(("WARNING", line))
                elif severity == "all" and (is_error or is_warning):
                    entries.append(("ERROR" if is_error else "WARNING", line))

                if len(entries) >= limit:
                    break
    except OSError as e:
        return err(f"Failed to read log: {e}")

    if not entries:
        return ok(f"No {severity} entries found in {log_path}")

    lines = [f"Log: {log_path}  ({len(entries)} entries, severity={severity})"]
    for sev, text in entries:
        lines.append(f"  [{sev}] {text[:300]}")

    return ok("\n".join(lines))


# ---------------------------------------------------------------------------
# Tool: unity_csharp_check
# ---------------------------------------------------------------------------

_RE_NAMESPACE = re.compile(r"^\s*namespace\s+", re.MULTILINE)
_RE_CLASS = re.compile(
    r"class\s+(\w+)\s*(?::\s*([\w.,\s]+))?\s*\{", re.MULTILINE
)
_RE_EMPTY_CATCH = re.compile(r"catch\s*\([^)]*\)\s*\{\s*\}", re.MULTILINE)
_RE_UPDATE = re.compile(r"void\s+Update\s*\(\s*\)", re.MULTILINE)
_RE_PUBLIC_FIELD = re.compile(
    r"^\s*public\s+(?!(?:static|const|readonly|override|virtual|abstract|event|delegate)\s)"
    r"[\w<>\[\],\s]+\s+(\w+)\s*[;=]",
    re.MULTILINE,
)
_MONO_BEHAVIOUR_BASES = {
    "MonoBehaviour", "NetworkBehaviour", "StateMachineBehaviour",
    "ScriptableObject", "Editor", "EditorWindow",
}


def unity_csharp_check(args):
    project_path, error = validate_project_path(args.get("project_path"))
    if error:
        return err(error)

    sub_path = args.get("path", "")
    limit = args.get("limit", 100)

    search_root = os.path.join(project_path, "Assets")
    if sub_path:
        safe = sub_path.replace("..", "").lstrip("/\\")
        search_root = os.path.join(search_root, safe)

    if not os.path.isdir(search_root):
        if os.path.isfile(search_root) and search_root.endswith(".cs"):
            files = [search_root]
        else:
            return err(f"Path not found: {search_root}")
    else:
        files = []
        for root, _, fnames in os.walk(search_root):
            for f in fnames:
                if f.endswith(".cs"):
                    files.append(os.path.join(root, f))

    issues = []

    for filepath in files:
        if len(issues) >= limit:
            break
        try:
            with open(filepath, encoding="utf-8", errors="replace") as f:
                content = f.read(200_000)
        except OSError:
            continue

        rel = os.path.relpath(filepath, project_path).replace("\\", "/")

        if not _RE_NAMESPACE.search(content):
            issues.append(f"{rel}: Missing namespace declaration")

        for m in _RE_EMPTY_CATCH.finditer(content):
            line_no = content[:m.start()].count("\n") + 1
            issues.append(f"{rel}:{line_no}: Empty catch block (swallows exceptions)")

        classes = _RE_CLASS.findall(content)
        has_monobehaviour_base = False
        for name, bases in classes:
            base_list = [b.strip() for b in bases.split(",")] if bases else []
            if any(b in _MONO_BEHAVIOUR_BASES for b in base_list):
                has_monobehaviour_base = True

        if _RE_UPDATE.search(content) and not has_monobehaviour_base and classes:
            issues.append(
                f"{rel}: Update() defined but class does not inherit from MonoBehaviour"
            )

        public_fields = _RE_PUBLIC_FIELD.findall(content)
        if len(public_fields) > 10:
            issues.append(
                f"{rel}: {len(public_fields)} public fields — consider using "
                f"[SerializeField] private fields or properties"
            )

    if not issues:
        return ok(f"No issues found in {len(files)} C# files.")

    lines = [f"Found {len(issues)} issues in {len(files)} C# files:"]
    for issue in issues[:limit]:
        lines.append(f"  - {issue}")

    return ok("\n".join(lines))


# ---------------------------------------------------------------------------
# Dispatch
# ---------------------------------------------------------------------------

DISPATCH = {
    "unity_project_info": unity_project_info,
    "unity_build": unity_build,
    "unity_run_tests": unity_run_tests,
    "unity_asset_search": unity_asset_search,
    "unity_log_parser": unity_log_parser,
    "unity_csharp_check": unity_csharp_check,
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

    sys.stderr.write(
        "Usage: unity_tools.py --mcp-list | --mcp-call <tool> --mcp-args-file <file>\n"
    )
    sys.exit(1)


if __name__ == "__main__":
    main()
