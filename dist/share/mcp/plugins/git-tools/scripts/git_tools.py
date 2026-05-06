#!/usr/bin/env python3
"""MCP script plugin — local git operations."""
import sys
import json
import argparse
import subprocess
import os
import re

TOOLS = [
    {
        "name": "git_status",
        "description": "Branch, tracking, ahead/behind, working tree state, stash count, merge/rebase state",
        "inputSchema": {
            "type": "object",
            "properties": {}
        }
    },
    {
        "name": "git_changes",
        "description": "Diff between current branch and base (main/master). Shows what a PR would contain.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "format": {
                    "type": "string",
                    "enum": ["stat", "diff", "both"],
                    "description": "Output format: stat summary, full diff, or both (default: both)"
                },
                "from_ref": {
                    "type": "string",
                    "description": "Base ref (default: auto-detect main/master)"
                },
                "to_ref": {
                    "type": "string",
                    "description": "Target ref (default: HEAD)"
                }
            }
        }
    },
    {
        "name": "git_search",
        "description": "Search git history by code changes (-S) or commit messages (--grep)",
        "inputSchema": {
            "type": "object",
            "properties": {
                "code": {
                    "type": "string",
                    "description": "Search for commits that add/remove this string (git log -S)"
                },
                "message": {
                    "type": "string",
                    "description": "Search commit messages matching this pattern (git log --grep)"
                },
                "file_filter": {
                    "type": "string",
                    "description": "Restrict search to files matching this path pattern"
                },
                "limit": {
                    "type": "integer",
                    "description": "Max results (default: 20)"
                }
            }
        }
    },
    {
        "name": "git_sync",
        "description": "Fetch origin and rebase current branch. Optionally push.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "no_push": {
                    "type": "boolean",
                    "description": "Only fetch and rebase, do not push (default: false)"
                },
                "force_push": {
                    "type": "boolean",
                    "description": "Force-push with lease after rebase (default: false)"
                },
                "branch": {
                    "type": "string",
                    "description": "Branch to sync (default: current branch)"
                }
            }
        }
    },
    {
        "name": "git_reset",
        "description": "Switch to default branch, pull latest. Optionally delete feature branch.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "delete_branch": {
                    "type": "boolean",
                    "description": "Delete the current feature branch after switching (default: false)"
                },
                "stash": {
                    "type": "boolean",
                    "description": "Stash uncommitted changes before switching (default: false)"
                }
            }
        }
    },
    {
        "name": "git_conflicts",
        "description": "List and parse merge/rebase conflict markers in working tree files",
        "inputSchema": {
            "type": "object",
            "properties": {}
        }
    }
]

MAX_OUTPUT = 100_000


def run_git(*cmd_args):
    """Run a git command and return (returncode, stdout, stderr)."""
    cmd = ["git"] + list(cmd_args)
    try:
        result = subprocess.run(
            cmd, capture_output=True, text=True, timeout=60
        )
        return result.returncode, result.stdout, result.stderr
    except FileNotFoundError:
        return -1, "", "git is not installed or not on PATH"
    except subprocess.TimeoutExpired:
        return -1, "", "git command timed out after 60s"


def detect_default_branch():
    """Auto-detect the default branch name."""
    for candidate in ["main", "master"]:
        rc, _, _ = run_git("rev-parse", "--verify", candidate)
        if rc == 0:
            return candidate
    for candidate in ["origin/main", "origin/master"]:
        rc, _, _ = run_git("rev-parse", "--verify", candidate)
        if rc == 0:
            return candidate
    return "main"


def ok(content):
    return {"status": "ok", "content": content}


def err(message):
    return {"status": "error", "error": message}


def git_status(_args):
    rc, out, stderr = run_git("status", "--porcelain=v2", "--branch")
    if rc != 0:
        return err(f"git status failed: {stderr.strip()}")

    info = {
        "branch": "",
        "upstream": "",
        "ahead": 0,
        "behind": 0,
        "modified": 0,
        "added": 0,
        "deleted": 0,
        "untracked": 0,
        "conflicts": 0,
        "stashes": 0,
        "state": "clean"
    }

    for line in out.splitlines():
        if line.startswith("# branch.head "):
            info["branch"] = line.split(" ", 2)[2]
        elif line.startswith("# branch.upstream "):
            info["upstream"] = line.split(" ", 2)[2]
        elif line.startswith("# branch.ab "):
            parts = line.split()
            info["ahead"] = int(parts[2].lstrip("+"))
            info["behind"] = abs(int(parts[3].lstrip("-")))
        elif line.startswith("1 ") or line.startswith("2 "):
            xy = line.split()[1]
            if xy[0] in ("M", "R", "C"):
                info["modified"] += 1
            elif xy[0] == "A":
                info["added"] += 1
            elif xy[0] == "D":
                info["deleted"] += 1
            if xy[1] in ("M", "R", "C"):
                info["modified"] += 1
            elif xy[1] == "A":
                info["added"] += 1
            elif xy[1] == "D":
                info["deleted"] += 1
        elif line.startswith("u "):
            info["conflicts"] += 1
        elif line.startswith("? "):
            info["untracked"] += 1

    rc2, stash_out, _ = run_git("stash", "list")
    if rc2 == 0 and stash_out.strip():
        info["stashes"] = len(stash_out.strip().splitlines())

    rc3, git_dir_out, _ = run_git("rev-parse", "--git-dir")
    if rc3 == 0:
        git_dir = git_dir_out.strip()
        if os.path.exists(os.path.join(git_dir, "MERGE_HEAD")):
            info["state"] = "merging"
        elif os.path.isdir(os.path.join(git_dir, "rebase-merge")) or \
             os.path.isdir(os.path.join(git_dir, "rebase-apply")):
            info["state"] = "rebasing"
        elif info["modified"] or info["added"] or info["deleted"] or info["untracked"]:
            info["state"] = "dirty"

    lines = []
    lines.append(f"Branch: {info['branch']}")
    if info["upstream"]:
        lines.append(f"Upstream: {info['upstream']} (+{info['ahead']} / -{info['behind']})")
    lines.append(f"Working tree: {info['modified']} modified, {info['added']} added, "
                 f"{info['deleted']} deleted, {info['untracked']} untracked")
    if info["conflicts"]:
        lines.append(f"Conflicts: {info['conflicts']} files")
    if info["stashes"]:
        lines.append(f"Stashes: {info['stashes']}")
    lines.append(f"State: {info['state']}")

    return ok("\n".join(lines))


def git_changes(args):
    fmt = args.get("format", "both")
    from_ref = args.get("from_ref", "")
    to_ref = args.get("to_ref", "HEAD")

    if not from_ref:
        from_ref = detect_default_branch()

    parts = []

    if fmt in ("stat", "both"):
        rc, out, stderr = run_git("diff", f"{from_ref}...{to_ref}", "--stat")
        if rc != 0:
            return err(f"git diff --stat failed: {stderr.strip()}")
        parts.append("=== Stat ===\n" + out[:MAX_OUTPUT])

    if fmt in ("diff", "both"):
        rc, out, stderr = run_git("diff", f"{from_ref}...{to_ref}")
        if rc != 0:
            return err(f"git diff failed: {stderr.strip()}")
        diff_text = out[:MAX_OUTPUT]
        if len(out) > MAX_OUTPUT:
            diff_text += "\n... (truncated at 100KB)"
        parts.append("=== Diff ===\n" + diff_text)

    if not parts:
        return err(f"Invalid format: {fmt}. Use stat, diff, or both.")

    return ok("\n\n".join(parts))


def git_search(args):
    code = args.get("code", "")
    message = args.get("message", "")
    file_filter = args.get("file_filter", "")
    limit = args.get("limit", 20)

    if not code and not message:
        return err("At least one of 'code' or 'message' is required")

    cmd = ["log", f"-n{limit}", "--format=%H %ai %s"]
    if code:
        cmd.append(f"-S{code}")
    if message:
        cmd.append(f"--grep={message}")
    if file_filter:
        cmd.append("--")
        cmd.append(file_filter)

    rc, out, stderr = run_git(*cmd)
    if rc != 0:
        return err(f"git log search failed: {stderr.strip()}")

    if not out.strip():
        return ok("No matching commits found.")

    return ok(out.strip()[:MAX_OUTPUT])


def git_sync(args):
    no_push = args.get("no_push", False)
    force_push = args.get("force_push", False)
    branch = args.get("branch", "")

    steps = []

    if branch:
        rc, _, stderr = run_git("checkout", branch)
        if rc != 0:
            return err(f"Failed to checkout {branch}: {stderr.strip()}")
        steps.append(f"Checked out {branch}")

    rc, _, stderr = run_git("fetch", "origin")
    if rc != 0:
        return err(f"Fetch failed: {stderr.strip()}")
    steps.append("Fetched origin")

    rc, current_out, _ = run_git("rev-parse", "--abbrev-ref", "HEAD")
    current = current_out.strip()

    rc, _, stderr = run_git("rebase", f"origin/{current}")
    if rc != 0:
        run_git("rebase", "--abort")
        return err(f"Rebase failed (aborted): {stderr.strip()}")
    steps.append(f"Rebased onto origin/{current}")

    if not no_push:
        push_args = ["push"]
        if force_push:
            push_args.append("--force-with-lease")
        rc, _, stderr = run_git(*push_args)
        if rc != 0:
            return err(f"Push failed: {stderr.strip()}")
        steps.append("Pushed" + (" (force-with-lease)" if force_push else ""))

    return ok("\n".join(steps))


def git_reset(args):
    delete_branch = args.get("delete_branch", False)
    stash = args.get("stash", False)

    rc, current_out, _ = run_git("rev-parse", "--abbrev-ref", "HEAD")
    current = current_out.strip()
    default = detect_default_branch()

    steps = []

    if stash:
        rc, _, stderr = run_git("stash", "push", "-m", "git_reset auto-stash")
        if rc != 0:
            return err(f"Stash failed: {stderr.strip()}")
        steps.append("Stashed uncommitted changes")

    if current != default:
        rc, _, stderr = run_git("checkout", default)
        if rc != 0:
            return err(f"Checkout {default} failed: {stderr.strip()}")
        steps.append(f"Switched to {default}")

    rc, _, stderr = run_git("pull", "--ff-only")
    if rc != 0:
        return err(f"Pull failed: {stderr.strip()}")
    steps.append(f"Pulled latest {default}")

    if delete_branch and current != default:
        rc, _, stderr = run_git("branch", "-D", current)
        if rc != 0:
            return err(f"Delete branch failed: {stderr.strip()}")
        steps.append(f"Deleted branch {current}")

    return ok("\n".join(steps))


def git_conflicts(_args):
    rc, out, stderr = run_git("diff", "--name-only", "--diff-filter=U")
    if rc != 0:
        return err(f"git diff failed: {stderr.strip()}")

    files = [f for f in out.strip().splitlines() if f]
    if not files:
        return ok("No conflicted files.")

    results = []
    for filepath in files[:20]:
        try:
            with open(filepath, encoding="utf-8", errors="replace") as f:
                content = f.read()
        except OSError as e:
            results.append(f"### {filepath}\nCould not read: {e}")
            continue

        conflicts = []
        in_conflict = False
        current_section = None
        ours_lines = []
        theirs_lines = []

        for line in content.splitlines():
            if line.startswith("<<<<<<<"):
                in_conflict = True
                current_section = "ours"
                ours_lines = []
                theirs_lines = []
            elif line.startswith("=======") and in_conflict:
                current_section = "theirs"
            elif line.startswith(">>>>>>>") and in_conflict:
                conflicts.append({
                    "ours": "\n".join(ours_lines),
                    "theirs": "\n".join(theirs_lines)
                })
                in_conflict = False
                current_section = None
            elif in_conflict:
                if current_section == "ours":
                    ours_lines.append(line)
                elif current_section == "theirs":
                    theirs_lines.append(line)

        if conflicts:
            parts = [f"### {filepath} ({len(conflicts)} conflict(s))"]
            for i, c in enumerate(conflicts, 1):
                parts.append(f"  Conflict {i}:")
                parts.append(f"    OURS:\n      " + c["ours"].replace("\n", "\n      "))
                parts.append(f"    THEIRS:\n      " + c["theirs"].replace("\n", "\n      "))
            results.append("\n".join(parts))
        else:
            results.append(f"### {filepath}\n  (conflict markers not found in file content)")

    return ok("\n\n".join(results))


DISPATCH = {
    "git_status": git_status,
    "git_changes": git_changes,
    "git_search": git_search,
    "git_sync": git_sync,
    "git_reset": git_reset,
    "git_conflicts": git_conflicts,
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

    sys.stderr.write("Usage: git_tools.py --mcp-list | --mcp-call <tool> --mcp-args-file <file>\n")
    sys.exit(1)


if __name__ == "__main__":
    main()
