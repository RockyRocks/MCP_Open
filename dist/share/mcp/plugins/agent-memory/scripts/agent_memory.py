#!/usr/bin/env python3
"""MCP script plugin — persistent key-value memory store for agent context."""
import sys
import json
import argparse
import os

TOOLS = [
    {
        "name": "memory_get",
        "description": "Retrieve a value from agent memory by key. Returns null if not found.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "key": {"type": "string", "description": "Memory key to retrieve"}
            },
            "required": ["key"]
        }
    },
    {
        "name": "memory_set",
        "description": "Store a key-value pair in persistent agent memory.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "key": {"type": "string", "description": "Memory key"},
                "value": {"type": "string", "description": "Value to store"}
            },
            "required": ["key", "value"]
        }
    },
    {
        "name": "memory_list",
        "description": "List all keys in agent memory, optionally filtered by prefix.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "prefix": {"type": "string", "description": "Optional key prefix filter"}
            }
        }
    },
    {
        "name": "memory_delete",
        "description": "Delete a key from agent memory. Returns whether the key existed.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "key": {"type": "string", "description": "Memory key to delete"}
            },
            "required": ["key"]
        }
    }
]

STORE_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "data")
STORE_FILE = os.path.join(STORE_DIR, "memory.json")
MAX_KEY_LENGTH = 256
MAX_VALUE_SIZE = 100_000
MAX_ENTRIES = 10_000


def ok(content):
    return {"status": "ok", "content": content}


def err(message):
    return {"status": "error", "error": message}


def load_store():
    if not os.path.isfile(STORE_FILE):
        return {}
    try:
        with open(STORE_FILE, encoding="utf-8") as f:
            return json.load(f)
    except (json.JSONDecodeError, OSError):
        return {}


def save_store(store):
    os.makedirs(STORE_DIR, exist_ok=True)
    tmp = STORE_FILE + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(store, f, indent=2)
    os.replace(tmp, STORE_FILE)


def validate_key(key):
    if not key or not isinstance(key, str):
        return "Key must be a non-empty string"
    if len(key) > MAX_KEY_LENGTH:
        return f"Key exceeds max length ({MAX_KEY_LENGTH})"
    return None


def memory_get(args):
    key = args.get("key", "")
    error = validate_key(key)
    if error:
        return err(error)

    store = load_store()
    value = store.get(key)
    if value is None:
        return ok(f"Key '{key}' not found")
    return ok(json.dumps({"key": key, "value": value}))


def memory_set(args):
    key = args.get("key", "")
    value = args.get("value", "")

    error = validate_key(key)
    if error:
        return err(error)

    if len(value) > MAX_VALUE_SIZE:
        return err(f"Value exceeds max size ({MAX_VALUE_SIZE} bytes)")

    store = load_store()
    if key not in store and len(store) >= MAX_ENTRIES:
        return err(f"Memory store is full ({MAX_ENTRIES} entries)")

    store[key] = value
    save_store(store)
    return ok(f"Stored key '{key}' ({len(value)} bytes)")


def memory_list(args):
    prefix = args.get("prefix", "")
    store = load_store()

    keys = sorted(store.keys())
    if prefix:
        keys = [k for k in keys if k.startswith(prefix)]

    return ok(json.dumps({
        "keys": keys,
        "count": len(keys),
        "total": len(store)
    }))


def memory_delete(args):
    key = args.get("key", "")
    error = validate_key(key)
    if error:
        return err(error)

    store = load_store()
    existed = key in store
    if existed:
        del store[key]
        save_store(store)

    return ok(json.dumps({"key": key, "deleted": existed}))


DISPATCH = {
    "memory_get": memory_get,
    "memory_set": memory_set,
    "memory_list": memory_list,
    "memory_delete": memory_delete,
}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--mcp-list", action="store_true")
    parser.add_argument("--mcp-call", type=str)
    parser.add_argument("--mcp-args-file", type=str)
    args = parser.parse_args()

    if args.mcp_list:
        print(json.dumps(TOOLS))
        return

    if args.mcp_call:
        tool_args = {}
        if args.mcp_args_file:
            with open(args.mcp_args_file, encoding="utf-8") as f:
                tool_args = json.load(f)

        handler = DISPATCH.get(args.mcp_call)
        if not handler:
            print(json.dumps(err(f"Unknown tool: {args.mcp_call}")))
            return

        result = handler(tool_args)
        print(json.dumps(result))
        return

    print(json.dumps(err("Usage: --mcp-list or --mcp-call <tool> --mcp-args-file <path>")))


if __name__ == "__main__":
    main()
