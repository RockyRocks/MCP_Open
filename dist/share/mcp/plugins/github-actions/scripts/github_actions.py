#!/usr/bin/env python3
"""MCP script plugin — GitHub Actions CI: workflow status, logs, re-runs."""
import sys
import json
import argparse
import subprocess
import re

TOOLS = [
    {
        "name": "gh_actions_status",
        "description": "Show latest workflow run status for the current branch or a specific PR",
        "inputSchema": {
            "type": "object",
            "properties": {
                "branch": {
                    "type": "string",
                    "description": "Branch name (default: current branch)"
                },
                "pr_number": {
                    "type": "integer",
                    "description": "PR number to check (overrides branch)"
                }
            }
        }
    },
    {
        "name": "gh_actions_list",
        "description": "List recent workflow runs with optional filters",
        "inputSchema": {
            "type": "object",
            "properties": {
                "workflow": {
                    "type": "string",
                    "description": "Filter by workflow name or filename"
                },
                "branch": {
                    "type": "string",
                    "description": "Filter by branch"
                },
                "status": {
                    "type": "string",
                    "enum": ["queued", "in_progress", "completed", "success", "failure", "cancelled"],
                    "description": "Filter by status"
                },
                "limit": {
                    "type": "integer",
                    "description": "Max results (default: 10)"
                }
            }
        }
    },
    {
        "name": "gh_actions_logs",
        "description": "Get logs for a workflow run with error extraction. Returns failed job logs by default.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "run_id": {
                    "type": "integer",
                    "description": "Workflow run ID"
                },
                "job_name": {
                    "type": "string",
                    "description": "Specific job name to get logs for (default: all failed jobs)"
                }
            },
            "required": ["run_id"]
        }
    },
    {
        "name": "gh_actions_rerun",
        "description": "Re-run a workflow. By default only re-runs failed jobs.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "run_id": {
                    "type": "integer",
                    "description": "Workflow run ID to re-run"
                },
                "failed_only": {
                    "type": "boolean",
                    "description": "Only re-run failed jobs (default: true)"
                }
            },
            "required": ["run_id"]
        }
    }
]

ANSI_ESCAPE = re.compile(r'\x1b\[[0-9;]*[A-Za-z]')
ERROR_PATTERNS = re.compile(
    r'(?:^|\s)(?:error|Error|ERROR|FAILED|FAILURE|fatal|Fatal|FATAL|'
    r'exception|Exception|EXCEPTION|panic|PANIC|assert|Assert|FAIL:|ERR!)',
)
NOISE_PATTERNS = re.compile(
    r'^\s*$|^##\[debug\]|^##\[group\]|^##\[endgroup\]|'
    r'^Post job cleanup|^Cleaning up orphan|^Complete job name:'
)
MAX_LOG_BYTES = 50_000


def extract_errors(log_text, context_lines=3):
    """Extract error-relevant lines from CI logs with surrounding context."""
    lines = ANSI_ESCAPE.sub('', log_text).splitlines()
    error_indices = set()

    for i, line in enumerate(lines):
        if NOISE_PATTERNS.match(line):
            continue
        if ERROR_PATTERNS.search(line):
            start = max(0, i - context_lines)
            end = min(len(lines), i + context_lines + 1)
            for j in range(start, end):
                error_indices.add(j)

    if not error_indices:
        tail = [l for l in lines[-30:] if not NOISE_PATTERNS.match(l)]
        return "\n".join(tail[-20:]) if tail else "(no errors found; log was empty or all noise)"

    result_lines = [lines[i] for i in sorted(error_indices) if i < len(lines)]
    result = "\n".join(result_lines)
    if len(result) > MAX_LOG_BYTES:
        result = result[:MAX_LOG_BYTES] + "\n... (truncated)"
    return result


def run_gh(*cmd_args):
    """Run gh CLI and return (returncode, stdout, stderr)."""
    cmd = ["gh"] + list(cmd_args)
    try:
        result = subprocess.run(
            cmd, capture_output=True, text=True, timeout=60
        )
        return result.returncode, result.stdout, result.stderr
    except FileNotFoundError:
        return -1, "", "gh CLI not found. Install from https://cli.github.com"
    except subprocess.TimeoutExpired:
        return -1, "", "gh command timed out"


def run_git(*cmd_args):
    """Run git and return (returncode, stdout, stderr)."""
    cmd = ["git"] + list(cmd_args)
    try:
        result = subprocess.run(
            cmd, capture_output=True, text=True, timeout=30
        )
        return result.returncode, result.stdout, result.stderr
    except (FileNotFoundError, subprocess.TimeoutExpired):
        return -1, "", "git not available"


def check_gh():
    rc, _, stderr = run_gh("auth", "status")
    if rc == -1:
        return False, stderr
    if rc != 0:
        return False, f"gh CLI not authenticated. Run 'gh auth login'. Detail: {stderr.strip()}"
    return True, ""


def ok(content):
    return {"status": "ok", "content": content}


def err(message):
    return {"status": "error", "error": message}


def gh_actions_status(args):
    available, msg = check_gh()
    if not available:
        return err(msg)

    branch = args.get("branch", "")
    pr_number = args.get("pr_number")

    if pr_number:
        rc, out, stderr = run_gh("pr", "view", str(pr_number), "--json", "headRefName")
        if rc != 0:
            return err(f"Failed to get PR branch: {stderr.strip()}")
        branch = json.loads(out).get("headRefName", "")

    if not branch:
        rc, out, _ = run_git("rev-parse", "--abbrev-ref", "HEAD")
        if rc != 0:
            return err("Could not detect current branch")
        branch = out.strip()

    fields = "databaseId,status,conclusion,name,event,createdAt,url,headBranch"
    rc, out, stderr = run_gh("run", "list", "--branch", branch, "--limit", "1", "--json", fields)
    if rc != 0:
        return err(f"gh run list failed: {stderr.strip()}")

    runs = json.loads(out)
    if not runs:
        return ok(f"No workflow runs found for branch '{branch}'.")

    run = runs[0]
    conclusion = run.get("conclusion") or run.get("status", "unknown")
    lines = [
        f"Workflow: {run.get('name', 'unknown')}",
        f"Run ID: {run['databaseId']}",
        f"Branch: {run.get('headBranch', branch)}",
        f"Status: {run.get('status', 'unknown')}",
        f"Conclusion: {conclusion}",
        f"Event: {run.get('event', 'unknown')}",
        f"Created: {run.get('createdAt', '')}",
        f"URL: {run.get('url', '')}",
    ]

    return ok("\n".join(lines))


def gh_actions_list(args):
    available, msg = check_gh()
    if not available:
        return err(msg)

    limit = args.get("limit", 10)
    fields = "databaseId,status,conclusion,name,headBranch,event,createdAt,url"

    cmd = ["run", "list", "--json", fields, "--limit", str(limit)]
    workflow = args.get("workflow", "")
    if workflow:
        cmd.extend(["--workflow", workflow])
    branch = args.get("branch", "")
    if branch:
        cmd.extend(["--branch", branch])
    status = args.get("status", "")
    if status:
        cmd.extend(["--status", status])

    rc, out, stderr = run_gh(*cmd)
    if rc != 0:
        return err(f"gh run list failed: {stderr.strip()}")

    runs = json.loads(out)
    if not runs:
        return ok("No workflow runs found.")

    lines = []
    for run in runs:
        conclusion = run.get("conclusion") or run.get("status", "?")
        lines.append(
            f"#{run['databaseId']} [{conclusion}] {run.get('name', '')} "
            f"({run.get('headBranch', '')}) {run.get('createdAt', '')}"
        )

    return ok("\n".join(lines))


def gh_actions_logs(args):
    run_id = args.get("run_id")
    if run_id is None:
        return err("Missing required argument: run_id")

    available, msg = check_gh()
    if not available:
        return err(msg)

    job_name = args.get("job_name", "")

    rc, out, stderr = run_gh("run", "view", str(run_id), "--json", "jobs")
    if rc != 0:
        return err(f"Failed to get run details: {stderr.strip()}")

    data = json.loads(out)
    jobs = data.get("jobs") or []

    if job_name:
        target_jobs = [j for j in jobs if j.get("name", "") == job_name]
        if not target_jobs:
            available_names = [j.get("name", "") for j in jobs]
            return err(f"Job '{job_name}' not found. Available: {', '.join(available_names)}")
    else:
        target_jobs = [j for j in jobs if j.get("conclusion") == "failure"]
        if not target_jobs:
            return ok("No failed jobs in this run.")

    results = []
    for job in target_jobs:
        job_id = job.get("databaseId", "")
        name = job.get("name", "unknown")

        log_cmd = ["run", "view", "--job", str(job_id), "--log-failed"]
        if job_name:
            log_cmd = ["run", "view", "--job", str(job_id), "--log"]

        rc, log_out, stderr = run_gh(*log_cmd)
        if rc != 0:
            results.append(f"### {name} (job {job_id})\nFailed to fetch logs: {stderr.strip()}")
            continue

        errors = extract_errors(log_out)
        results.append(f"### {name} (job {job_id})\n{errors}")

    return ok("\n\n".join(results))


def gh_actions_rerun(args):
    run_id = args.get("run_id")
    if run_id is None:
        return err("Missing required argument: run_id")

    available, msg = check_gh()
    if not available:
        return err(msg)

    failed_only = args.get("failed_only", True)

    cmd = ["run", "rerun", str(run_id)]
    if failed_only:
        cmd.append("--failed")

    rc, out, stderr = run_gh(*cmd)
    if rc != 0:
        return err(f"gh run rerun failed: {stderr.strip()}")

    mode = "failed jobs" if failed_only else "all jobs"
    return ok(f"Re-run triggered for run {run_id} ({mode})." + (f" {out.strip()}" if out.strip() else ""))


DISPATCH = {
    "gh_actions_status": gh_actions_status,
    "gh_actions_list": gh_actions_list,
    "gh_actions_logs": gh_actions_logs,
    "gh_actions_rerun": gh_actions_rerun,
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

    sys.stderr.write("Usage: github_actions.py --mcp-list | --mcp-call <tool> --mcp-args-file <file>\n")
    sys.exit(1)


if __name__ == "__main__":
    main()
