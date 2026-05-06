#!/usr/bin/env python3
"""MCP script plugin — GitHub operations via gh CLI."""
import sys
import json
import argparse
import subprocess

TOOLS = [
    {
        "name": "gh_pr_status",
        "description": "Show current branch's PR: title, state, checks, reviews, comments",
        "inputSchema": {
            "type": "object",
            "properties": {}
        }
    },
    {
        "name": "gh_pr_create",
        "description": "Create a pull request from the current branch",
        "inputSchema": {
            "type": "object",
            "properties": {
                "title": {"type": "string", "description": "PR title"},
                "body": {"type": "string", "description": "PR body/description"},
                "base": {"type": "string", "description": "Base branch (default: repo default)"},
                "draft": {"type": "boolean", "description": "Create as draft PR (default: false)"},
                "labels": {
                    "type": "array", "items": {"type": "string"},
                    "description": "Labels to apply"
                },
                "assignees": {
                    "type": "array", "items": {"type": "string"},
                    "description": "Assignees"
                },
                "reviewers": {
                    "type": "array", "items": {"type": "string"},
                    "description": "Reviewers to request"
                }
            },
            "required": ["title"]
        }
    },
    {
        "name": "gh_pr_list",
        "description": "List pull requests with optional filters",
        "inputSchema": {
            "type": "object",
            "properties": {
                "state": {
                    "type": "string",
                    "enum": ["open", "closed", "merged", "all"],
                    "description": "Filter by state (default: open)"
                },
                "author": {"type": "string", "description": "Filter by author username"},
                "label": {"type": "string", "description": "Filter by label"},
                "limit": {"type": "integer", "description": "Max results (default: 20)"}
            }
        }
    },
    {
        "name": "gh_pr_diff",
        "description": "Show the diff for a pull request",
        "inputSchema": {
            "type": "object",
            "properties": {
                "pr_number": {"type": "integer", "description": "PR number"}
            },
            "required": ["pr_number"]
        }
    },
    {
        "name": "gh_pr_review",
        "description": "Add a review to a pull request: approve, request-changes, or comment",
        "inputSchema": {
            "type": "object",
            "properties": {
                "pr_number": {"type": "integer", "description": "PR number"},
                "event": {
                    "type": "string",
                    "enum": ["approve", "request-changes", "comment"],
                    "description": "Review action"
                },
                "body": {"type": "string", "description": "Review comment body"}
            },
            "required": ["pr_number", "event"]
        }
    },
    {
        "name": "gh_issue_list",
        "description": "List issues with optional filters",
        "inputSchema": {
            "type": "object",
            "properties": {
                "state": {
                    "type": "string",
                    "enum": ["open", "closed", "all"],
                    "description": "Filter by state (default: open)"
                },
                "label": {"type": "string", "description": "Filter by label"},
                "assignee": {"type": "string", "description": "Filter by assignee"},
                "limit": {"type": "integer", "description": "Max results (default: 20)"}
            }
        }
    },
    {
        "name": "gh_issue_create",
        "description": "Create a new GitHub issue",
        "inputSchema": {
            "type": "object",
            "properties": {
                "title": {"type": "string", "description": "Issue title"},
                "body": {"type": "string", "description": "Issue body"},
                "labels": {
                    "type": "array", "items": {"type": "string"},
                    "description": "Labels to apply"
                },
                "assignees": {
                    "type": "array", "items": {"type": "string"},
                    "description": "Assignees"
                }
            },
            "required": ["title"]
        }
    },
    {
        "name": "gh_issue_view",
        "description": "View a GitHub issue with details and comments",
        "inputSchema": {
            "type": "object",
            "properties": {
                "issue_number": {"type": "integer", "description": "Issue number"}
            },
            "required": ["issue_number"]
        }
    },
    {
        "name": "gh_repo_info",
        "description": "Repository metadata: default branch, visibility, topics, description",
        "inputSchema": {
            "type": "object",
            "properties": {}
        }
    }
]

MAX_OUTPUT = 100_000


def run_gh(*cmd_args):
    """Run gh CLI and return (returncode, stdout, stderr)."""
    cmd = ["gh"] + list(cmd_args)
    try:
        result = subprocess.run(
            cmd, capture_output=True, text=True, timeout=30
        )
        return result.returncode, result.stdout, result.stderr
    except FileNotFoundError:
        return -1, "", "gh CLI not found. Install from https://cli.github.com"
    except subprocess.TimeoutExpired:
        return -1, "", "gh command timed out after 30s"


def check_gh():
    """Check if gh is available and authenticated. Returns (ok, error_msg)."""
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


def gh_pr_status(_args):
    available, msg = check_gh()
    if not available:
        return err(msg)

    fields = "number,title,state,headRefName,baseRefName,isDraft,url,statusCheckRollup,reviews,comments"
    rc, out, stderr = run_gh("pr", "view", "--json", fields)
    if rc != 0:
        if "no pull requests found" in stderr.lower() or "no open pull requests" in stderr.lower():
            return ok("No open PR for the current branch.")
        return err(f"gh pr view failed: {stderr.strip()}")

    pr = json.loads(out)
    lines = [
        f"PR #{pr['number']}: {pr['title']}",
        f"State: {pr['state']}" + (" (draft)" if pr.get("isDraft") else ""),
        f"Branch: {pr['headRefName']} → {pr['baseRefName']}",
        f"URL: {pr['url']}",
    ]

    checks = pr.get("statusCheckRollup") or []
    if checks:
        summary = {}
        for c in checks:
            state = c.get("conclusion") or c.get("status", "UNKNOWN")
            summary[state] = summary.get(state, 0) + 1
        lines.append(f"Checks: {', '.join(f'{v} {k}' for k, v in summary.items())}")

    reviews = pr.get("reviews") or []
    if reviews:
        review_states = {}
        for r in reviews:
            s = r.get("state", "UNKNOWN")
            review_states[s] = review_states.get(s, 0) + 1
        lines.append(f"Reviews: {', '.join(f'{v} {k}' for k, v in review_states.items())}")

    comment_count = len(pr.get("comments") or [])
    lines.append(f"Comments: {comment_count}")

    return ok("\n".join(lines))


def gh_pr_create(args):
    title = args.get("title", "")
    if not title:
        return err("Missing required argument: title")

    available, msg = check_gh()
    if not available:
        return err(msg)

    cmd = ["pr", "create", "--title", title]
    body = args.get("body", "")
    if body:
        cmd.extend(["--body", body])
    base = args.get("base", "")
    if base:
        cmd.extend(["--base", base])
    if args.get("draft", False):
        cmd.append("--draft")
    for label in args.get("labels", []):
        cmd.extend(["--label", label])
    for assignee in args.get("assignees", []):
        cmd.extend(["--assignee", assignee])
    for reviewer in args.get("reviewers", []):
        cmd.extend(["--reviewer", reviewer])

    rc, out, stderr = run_gh(*cmd)
    if rc != 0:
        return err(f"gh pr create failed: {stderr.strip()}")

    return ok(out.strip())


def gh_pr_list(args):
    available, msg = check_gh()
    if not available:
        return err(msg)

    state = args.get("state", "open")
    limit = args.get("limit", 20)
    fields = "number,title,state,author,labels,createdAt,url"

    cmd = ["pr", "list", "--json", fields, "--state", state, "--limit", str(limit)]
    author = args.get("author", "")
    if author:
        cmd.extend(["--author", author])
    label = args.get("label", "")
    if label:
        cmd.extend(["--label", label])

    rc, out, stderr = run_gh(*cmd)
    if rc != 0:
        return err(f"gh pr list failed: {stderr.strip()}")

    prs = json.loads(out)
    if not prs:
        return ok("No pull requests found.")

    lines = []
    for pr in prs:
        author_name = pr.get("author", {}).get("login", "unknown")
        labels = ", ".join(l.get("name", "") for l in (pr.get("labels") or []))
        line = f"#{pr['number']} [{pr['state']}] {pr['title']} (@{author_name})"
        if labels:
            line += f" [{labels}]"
        lines.append(line)

    return ok("\n".join(lines))


def gh_pr_diff(args):
    pr_number = args.get("pr_number")
    if pr_number is None:
        return err("Missing required argument: pr_number")

    available, msg = check_gh()
    if not available:
        return err(msg)

    rc, out, stderr = run_gh("pr", "diff", str(pr_number))
    if rc != 0:
        return err(f"gh pr diff failed: {stderr.strip()}")

    text = out[:MAX_OUTPUT]
    if len(out) > MAX_OUTPUT:
        text += "\n... (truncated at 100KB)"
    return ok(text)


def gh_pr_review(args):
    pr_number = args.get("pr_number")
    event = args.get("event", "")
    body = args.get("body", "")

    if pr_number is None:
        return err("Missing required argument: pr_number")
    if not event:
        return err("Missing required argument: event")
    if event in ("request-changes", "comment") and not body:
        return err(f"'body' is required when event is '{event}'")

    available, msg = check_gh()
    if not available:
        return err(msg)

    flag_map = {
        "approve": "--approve",
        "request-changes": "--request-changes",
        "comment": "--comment",
    }
    flag = flag_map.get(event)
    if not flag:
        return err(f"Invalid event: {event}. Use approve, request-changes, or comment.")

    cmd = ["pr", "review", str(pr_number), flag]
    if body:
        cmd.extend(["--body", body])

    rc, out, stderr = run_gh(*cmd)
    if rc != 0:
        return err(f"gh pr review failed: {stderr.strip()}")

    return ok(out.strip() or f"Review ({event}) submitted for PR #{pr_number}.")


def gh_issue_list(args):
    available, msg = check_gh()
    if not available:
        return err(msg)

    state = args.get("state", "open")
    limit = args.get("limit", 20)
    fields = "number,title,state,labels,assignees,createdAt,url"

    cmd = ["issue", "list", "--json", fields, "--state", state, "--limit", str(limit)]
    label = args.get("label", "")
    if label:
        cmd.extend(["--label", label])
    assignee = args.get("assignee", "")
    if assignee:
        cmd.extend(["--assignee", assignee])

    rc, out, stderr = run_gh(*cmd)
    if rc != 0:
        return err(f"gh issue list failed: {stderr.strip()}")

    issues = json.loads(out)
    if not issues:
        return ok("No issues found.")

    lines = []
    for issue in issues:
        labels = ", ".join(l.get("name", "") for l in (issue.get("labels") or []))
        line = f"#{issue['number']} [{issue['state']}] {issue['title']}"
        if labels:
            line += f" [{labels}]"
        lines.append(line)

    return ok("\n".join(lines))


def gh_issue_create(args):
    title = args.get("title", "")
    if not title:
        return err("Missing required argument: title")

    available, msg = check_gh()
    if not available:
        return err(msg)

    cmd = ["issue", "create", "--title", title]
    body = args.get("body", "")
    if body:
        cmd.extend(["--body", body])
    for label in args.get("labels", []):
        cmd.extend(["--label", label])
    for assignee in args.get("assignees", []):
        cmd.extend(["--assignee", assignee])

    rc, out, stderr = run_gh(*cmd)
    if rc != 0:
        return err(f"gh issue create failed: {stderr.strip()}")

    return ok(out.strip())


def gh_issue_view(args):
    issue_number = args.get("issue_number")
    if issue_number is None:
        return err("Missing required argument: issue_number")

    available, msg = check_gh()
    if not available:
        return err(msg)

    fields = "number,title,state,body,comments,labels,assignees,author,createdAt,url"
    rc, out, stderr = run_gh("issue", "view", str(issue_number), "--json", fields)
    if rc != 0:
        return err(f"gh issue view failed: {stderr.strip()}")

    issue = json.loads(out)
    lines = [
        f"Issue #{issue['number']}: {issue['title']}",
        f"State: {issue['state']}",
        f"Author: {issue.get('author', {}).get('login', 'unknown')}",
        f"Created: {issue.get('createdAt', '')}",
        f"URL: {issue.get('url', '')}",
    ]

    labels = [l.get("name", "") for l in (issue.get("labels") or [])]
    if labels:
        lines.append(f"Labels: {', '.join(labels)}")

    assignees = [a.get("login", "") for a in (issue.get("assignees") or [])]
    if assignees:
        lines.append(f"Assignees: {', '.join(assignees)}")

    body = issue.get("body", "")
    if body:
        lines.append(f"\n--- Body ---\n{body[:MAX_OUTPUT]}")

    comments = issue.get("comments") or []
    if comments:
        lines.append(f"\n--- Comments ({len(comments)}) ---")
        for c in comments[:50]:
            author = c.get("author", {}).get("login", "unknown")
            lines.append(f"\n@{author} ({c.get('createdAt', '')}):")
            lines.append(c.get("body", "")[:2000])

    return ok("\n".join(lines))


def gh_repo_info(_args):
    available, msg = check_gh()
    if not available:
        return err(msg)

    fields = "name,description,defaultBranchRef,visibility,repositoryTopics,url,owner"
    rc, out, stderr = run_gh("repo", "view", "--json", fields)
    if rc != 0:
        return err(f"gh repo view failed: {stderr.strip()}")

    repo = json.loads(out)
    default_branch = repo.get("defaultBranchRef", {}).get("name", "unknown")
    topics = [t.get("name", "") for t in (repo.get("repositoryTopics") or [])]
    owner = repo.get("owner", {}).get("login", "unknown")

    lines = [
        f"Repository: {owner}/{repo.get('name', '')}",
        f"Description: {repo.get('description', '(none)')}",
        f"Default branch: {default_branch}",
        f"Visibility: {repo.get('visibility', 'unknown')}",
        f"URL: {repo.get('url', '')}",
    ]
    if topics:
        lines.append(f"Topics: {', '.join(topics)}")

    return ok("\n".join(lines))


DISPATCH = {
    "gh_pr_status": gh_pr_status,
    "gh_pr_create": gh_pr_create,
    "gh_pr_list": gh_pr_list,
    "gh_pr_diff": gh_pr_diff,
    "gh_pr_review": gh_pr_review,
    "gh_issue_list": gh_issue_list,
    "gh_issue_create": gh_issue_create,
    "gh_issue_view": gh_issue_view,
    "gh_repo_info": gh_repo_info,
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

    sys.stderr.write("Usage: github_tools.py --mcp-list | --mcp-call <tool> --mcp-args-file <file>\n")
    sys.exit(1)


if __name__ == "__main__":
    main()
