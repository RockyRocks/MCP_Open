#!/usr/bin/env python3
"""MCP script plugin — file system operations: read, write, edit, list, search."""
import sys
import json
import argparse
import os
import re
import fnmatch

TOOLS = [
    {
        "name": "fs_read",
        "description": "Read file contents. Supports offset and limit for large files.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "path": {"type": "string", "description": "File path (absolute or relative to cwd)"},
                "offset": {"type": "integer", "description": "Line number to start reading from (0-based, default: 0)"},
                "limit": {"type": "integer", "description": "Maximum number of lines to read (default: 2000)"}
            },
            "required": ["path"]
        }
    },
    {
        "name": "fs_write",
        "description": "Write content to a file. Creates parent directories if needed. Overwrites existing files.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "path": {"type": "string", "description": "File path"},
                "content": {"type": "string", "description": "Content to write"}
            },
            "required": ["path", "content"]
        }
    },
    {
        "name": "fs_edit",
        "description": "Replace exact text in a file. old_string must match exactly (including whitespace).",
        "inputSchema": {
            "type": "object",
            "properties": {
                "path": {"type": "string", "description": "File path"},
                "old_string": {"type": "string", "description": "Exact text to find"},
                "new_string": {"type": "string", "description": "Replacement text"},
                "replace_all": {"type": "boolean", "description": "Replace all occurrences (default: false)"}
            },
            "required": ["path", "old_string", "new_string"]
        }
    },
    {
        "name": "fs_mkdir",
        "description": "Create a directory and any missing parent directories.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "path": {"type": "string", "description": "Directory path to create"}
            },
            "required": ["path"]
        }
    },
    {
        "name": "fs_delete",
        "description": "Delete a file or empty directory.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "path": {"type": "string", "description": "Path to delete"},
                "recursive": {"type": "boolean", "description": "Delete directory and all contents (default: false)"}
            },
            "required": ["path"]
        }
    },
    {
        "name": "fs_list",
        "description": "List directory contents. Supports glob pattern filtering.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "path": {"type": "string", "description": "Directory path (default: cwd)"},
                "pattern": {"type": "string", "description": "Glob pattern to filter (e.g. '*.py', '**/*.ts')"},
                "recursive": {"type": "boolean", "description": "List recursively (default: false)"}
            }
        }
    },
    {
        "name": "fs_search",
        "description": "Search file contents using regex. Returns matching lines with file paths and line numbers.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "pattern": {"type": "string", "description": "Regex pattern to search for"},
                "path": {"type": "string", "description": "Directory to search in (default: cwd)"},
                "glob": {"type": "string", "description": "File glob filter (e.g. '*.py')"},
                "max_results": {"type": "integer", "description": "Maximum matches to return (default: 100)"}
            },
            "required": ["pattern"]
        }
    }
]

MAX_READ_LINES = 2000
MAX_SEARCH_RESULTS = 100
MAX_LIST_ENTRIES = 1000
MAX_FILE_SIZE = 10_000_000  # 10 MB


def ok(content):
    return {"status": "ok", "content": content}


def err(message):
    return {"status": "error", "error": message}


def resolve_path(path):
    """Resolve to absolute path."""
    return os.path.realpath(os.path.expanduser(path))


def fs_read(args):
    path = args.get("path")
    if not path:
        return err("Missing required argument: path")

    abspath = resolve_path(path)
    if not os.path.isfile(abspath):
        return err(f"File not found: {abspath}")

    offset = args.get("offset", 0)
    limit = args.get("limit", MAX_READ_LINES)

    try:
        with open(abspath, encoding="utf-8", errors="replace") as f:
            lines = f.readlines()
    except OSError as e:
        return err(f"Cannot read file: {e}")

    total = len(lines)
    selected = lines[offset:offset + limit]

    numbered = []
    for i, line in enumerate(selected, start=offset + 1):
        numbered.append(f"{i}\t{line.rstrip()}")

    header = f"File: {abspath} ({total} lines)"
    if offset > 0 or limit < total:
        header += f" [showing lines {offset + 1}-{min(offset + limit, total)}]"

    return ok(header + "\n" + "\n".join(numbered))


def fs_write(args):
    path = args.get("path")
    content = args.get("content")
    if not path:
        return err("Missing required argument: path")
    if content is None:
        return err("Missing required argument: content")

    abspath = resolve_path(path)

    parent = os.path.dirname(abspath)
    if parent and not os.path.isdir(parent):
        try:
            os.makedirs(parent, exist_ok=True)
        except OSError as e:
            return err(f"Cannot create parent directory: {e}")

    try:
        with open(abspath, "w", encoding="utf-8") as f:
            f.write(content)
    except OSError as e:
        return err(f"Cannot write file: {e}")

    size = os.path.getsize(abspath)
    return ok(f"Written {size} bytes to {abspath}")


def fs_edit(args):
    path = args.get("path")
    old_string = args.get("old_string")
    new_string = args.get("new_string")
    if not path:
        return err("Missing required argument: path")
    if old_string is None:
        return err("Missing required argument: old_string")
    if new_string is None:
        return err("Missing required argument: new_string")

    abspath = resolve_path(path)
    if not os.path.isfile(abspath):
        return err(f"File not found: {abspath}")

    try:
        with open(abspath, encoding="utf-8") as f:
            content = f.read()
    except OSError as e:
        return err(f"Cannot read file: {e}")

    replace_all = args.get("replace_all", False)
    count = content.count(old_string)

    if count == 0:
        return err("old_string not found in file")
    if count > 1 and not replace_all:
        return err(f"old_string found {count} times. Set replace_all=true to replace all, or provide more context to make it unique.")

    if replace_all:
        new_content = content.replace(old_string, new_string)
    else:
        new_content = content.replace(old_string, new_string, 1)

    try:
        with open(abspath, "w", encoding="utf-8") as f:
            f.write(new_content)
    except OSError as e:
        return err(f"Cannot write file: {e}")

    replacements = count if replace_all else 1
    return ok(f"Replaced {replacements} occurrence(s) in {abspath}")


def fs_mkdir(args):
    path = args.get("path")
    if not path:
        return err("Missing required argument: path")

    abspath = resolve_path(path)
    try:
        os.makedirs(abspath, exist_ok=True)
    except OSError as e:
        return err(f"Cannot create directory: {e}")

    return ok(f"Created directory: {abspath}")


def fs_delete(args):
    path = args.get("path")
    if not path:
        return err("Missing required argument: path")

    abspath = resolve_path(path)
    if not os.path.exists(abspath):
        return err(f"Path not found: {abspath}")

    recursive = args.get("recursive", False)

    try:
        if os.path.isfile(abspath):
            os.remove(abspath)
            return ok(f"Deleted file: {abspath}")
        elif os.path.isdir(abspath):
            if recursive:
                import shutil
                shutil.rmtree(abspath)
                return ok(f"Deleted directory (recursive): {abspath}")
            else:
                os.rmdir(abspath)
                return ok(f"Deleted empty directory: {abspath}")
    except OSError as e:
        return err(f"Cannot delete: {e}")


def fs_list(args):
    path = args.get("path", ".")
    pattern = args.get("pattern", "")
    recursive = args.get("recursive", False)

    abspath = resolve_path(path)
    if not os.path.isdir(abspath):
        return err(f"Directory not found: {abspath}")

    entries = []
    try:
        if recursive:
            for root, dirs, files in os.walk(abspath):
                for name in dirs + files:
                    full = os.path.join(root, name)
                    rel = os.path.relpath(full, abspath)
                    if pattern and not fnmatch.fnmatch(rel, pattern):
                        continue
                    kind = "d" if os.path.isdir(full) else "f"
                    entries.append(f"[{kind}] {rel}")
                    if len(entries) >= MAX_LIST_ENTRIES:
                        break
                if len(entries) >= MAX_LIST_ENTRIES:
                    break
        else:
            for name in sorted(os.listdir(abspath)):
                if pattern and not fnmatch.fnmatch(name, pattern):
                    continue
                full = os.path.join(abspath, name)
                kind = "d" if os.path.isdir(full) else "f"
                entries.append(f"[{kind}] {name}")
                if len(entries) >= MAX_LIST_ENTRIES:
                    break
    except OSError as e:
        return err(f"Cannot list directory: {e}")

    if not entries:
        return ok(f"No entries found in {abspath}" + (f" matching '{pattern}'" if pattern else ""))

    header = f"Directory: {abspath} ({len(entries)} entries)"
    if len(entries) >= MAX_LIST_ENTRIES:
        header += f" (truncated at {MAX_LIST_ENTRIES})"

    return ok(header + "\n" + "\n".join(entries))


def fs_search(args):
    pattern = args.get("pattern")
    if not pattern:
        return err("Missing required argument: pattern")

    path = args.get("path", ".")
    glob_filter = args.get("glob", "")
    max_results = args.get("max_results", MAX_SEARCH_RESULTS)

    abspath = resolve_path(path)
    if not os.path.isdir(abspath):
        return err(f"Directory not found: {abspath}")

    try:
        regex = re.compile(pattern)
    except re.error as e:
        return err(f"Invalid regex pattern: {e}")

    matches = []
    for root, _dirs, files in os.walk(abspath):
        for name in files:
            if glob_filter and not fnmatch.fnmatch(name, glob_filter):
                continue

            filepath = os.path.join(root, name)
            rel = os.path.relpath(filepath, abspath)

            try:
                size = os.path.getsize(filepath)
                if size > MAX_FILE_SIZE:
                    continue
                with open(filepath, encoding="utf-8", errors="replace") as f:
                    for lineno, line in enumerate(f, 1):
                        if regex.search(line):
                            matches.append(f"{rel}:{lineno}: {line.rstrip()}")
                            if len(matches) >= max_results:
                                break
            except (OSError, UnicodeDecodeError):
                continue

            if len(matches) >= max_results:
                break
        if len(matches) >= max_results:
            break

    if not matches:
        return ok(f"No matches for '{pattern}' in {abspath}")

    header = f"Search results for '{pattern}' ({len(matches)} matches)"
    if len(matches) >= max_results:
        header += f" (limit reached: {max_results})"

    return ok(header + "\n" + "\n".join(matches))


DISPATCH = {
    "fs_read": fs_read,
    "fs_write": fs_write,
    "fs_edit": fs_edit,
    "fs_mkdir": fs_mkdir,
    "fs_delete": fs_delete,
    "fs_list": fs_list,
    "fs_search": fs_search,
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

    sys.stderr.write("Usage: filesystem_tools.py --mcp-list | --mcp-call <tool> --mcp-args-file <file>\n")
    sys.exit(1)


if __name__ == "__main__":
    main()
