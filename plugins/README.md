# Plugins

The MCP server supports three plugin types, all loaded from subdirectories of `plugins/`. Every plugin tool is registered as a first-class MCP tool — LLM clients discover and call them via `tools/list` without needing to know the underlying plugin mechanism.

| Plugin Type | How it works | Examples |
| ----------- | ------------ | -------- |
| [Script Plugins](#script-plugins) | Per-call subprocess (Python / Node.js / C# / any executable) | `git-tools/`, `github-tools/`, `github-actions/` |
| [Native Plugins](#native-plugins) | Compiled C/C++ shared library (`.dll` / `.so`) loaded via `LoadLibrary` / `dlopen` | `example-plugin/`, `desktop-notification/` |
| [Skill Plugins](#skill-plugins-skillmd) | Prompt-template files (`SKILL.md`) with `{{variable}}` interpolation | `entrian-search/`, `everything-search/` |

---

## Script Plugins

Script plugins extend the server with tools written in any language. Each call spawns a fresh subprocess, reads stdout, and the process exits. No persistent IPC or language runtime embedding is required.

### Directory Layout

```text
plugins/
└── my-python-plugin/
    ├── plugin.json          # MUST contain "runtime" and "entrypoint"
    └── scripts/
        └── plugin.py        # (or .js / .dll / .exe — relative to plugin dir)
```

### `plugin.json` — Script Plugin Fields

```json
{
  "name":       "my-python-plugin",
  "version":    "1.0.0",
  "runtime":    "python",
  "entrypoint": "scripts/plugin.py"
}
```

| `runtime` value | Windows executable | Linux/macOS executable |
| --------------- | ------------------ | ---------------------- |
| `"python"` | `python` | `python3` |
| `"node"` | `node` | `node` |
| `"dotnet"` | `dotnet` | `dotnet` |
| `"executable"` | entrypoint IS the exe — no prefix | same |
| anything else | used as-is (e.g. `"python3"`, `"node20"`) | same |

### Plugin Protocol

**Discovery** — called once at startup:

```text
<runtime> "<entrypoint>" --mcp-list
stdout → [{"name":"tool_name","description":"...","inputSchema":{...}}, ...]
```

**Execution** — called once per `tools/call`. Arguments are passed via a temp JSON file (eliminates shell-escaping):

```text
<runtime> "<entrypoint>" --mcp-call <tool_name> --mcp-args-file "<tmp.json>"
stdout → {"status":"ok","content":"result text"}
       | {"status":"error","error":"message"}
```

### Python Plugin Example

```python
# plugin.py
import sys, json, argparse

TOOLS = [
    {
        "name": "echo_tool",
        "description": "Echoes a message back",
        "inputSchema": {
            "type": "object",
            "properties": {"message": {"type": "string"}},
            "required": ["message"]
        }
    }
]

parser = argparse.ArgumentParser(add_help=False)
parser.add_argument("--mcp-list",      action="store_true")
parser.add_argument("--mcp-call",      metavar="TOOL")
parser.add_argument("--mcp-args-file", metavar="FILE")
args, _ = parser.parse_known_args()

if args.mcp_list:
    sys.stdout.write(json.dumps(TOOLS) + "\n")

elif args.mcp_call == "echo_tool":
    with open(args.mcp_args_file) as f:
        payload = json.load(f)
    sys.stdout.write(json.dumps({"status": "ok", "content": payload["message"]}) + "\n")
```

### TypeScript / Node.js Plugin with Zod (v4+)

Zod v4's built-in `z.toJSONSchema()` generates the `inputSchema` at runtime — single source of truth for schema AND validation. Requires `npm install zod@^4`.

```javascript
// plugin.js  (or compiled from TypeScript)
const { z } = require('zod');
const fs    = require('fs');

const EchoSchema = z.object({
    message: z.string().describe("Text to echo back")
});

const TOOLS = [
    {
        name:        "echo_tool",
        description: "Echoes a message back",
        inputSchema: z.toJSONSchema(EchoSchema, { target: "draft-07" })
    }
];

const argv = process.argv.slice(2);

if (argv.includes("--mcp-list")) {
    process.stdout.write(JSON.stringify(TOOLS) + "\n");

} else if (argv.includes("--mcp-call")) {
    const toolName  = argv[argv.indexOf("--mcp-call") + 1];
    const argsFile  = argv[argv.indexOf("--mcp-args-file") + 1];
    try {
        const raw  = JSON.parse(fs.readFileSync(argsFile, "utf-8"));
        if (toolName === "echo_tool") {
            const args = EchoSchema.parse(raw);   // runtime validation via Zod
            process.stdout.write(JSON.stringify({ status: "ok", content: args.message }) + "\n");
        } else {
            process.stdout.write(JSON.stringify({ status: "error", error: `Unknown tool: ${toolName}` }) + "\n");
        }
    } catch (err) {
        process.stdout.write(JSON.stringify({ status: "error", error: String(err) }) + "\n");
        process.exit(1);
    }
}
```

> **Zod v3 users**: Use the `zod-to-json-schema` package instead of `z.toJSONSchema()`.

### `plugin.json` for Node.js

```json
{
  "name":       "my-node-plugin",
  "version":    "1.0.0",
  "runtime":    "node",
  "entrypoint": "scripts/plugin.js"
}
```

### C# Plugin Checklist

- `"runtime": "dotnet"` — entry point is a `.dll` built with `dotnet build`
- `"runtime": "executable"` — entry point is a self-contained `.exe`
- Use `Environment.GetCommandLineArgs()` and `System.Text.Json` for I/O
- Stdout must be a **single line** per command invocation

### Tool Name Validation

Tool names must match `[a-zA-Z0-9_-]+`. Names that fail validation are logged and skipped at discovery time.

---

## Native Plugins

Native plugins are compiled C/C++ shared libraries loaded at runtime via `LoadLibrary` (Windows) or `dlopen` (Linux/macOS). They communicate through a stable `extern "C"` ABI defined in [`include/plugins/PluginABI.h`](../include/plugins/PluginABI.h).

### Directory Layout

```text
plugins/
└── my_native_plugin/
    ├── CMakeLists.txt       # Standalone or in-tree build
    ├── plugin.json          # Plugin metadata (no "runtime" key)
    └── src/
        └── my_plugin.cpp    # Implements the C ABI functions
```

### C ABI Contract

Every native plugin must export these functions:

```c
extern "C" {
    const char* mcp_plugin_name();
    const char* mcp_plugin_version();
    const char* mcp_plugin_list_tools();    // JSON array of tool descriptors
    const char* mcp_plugin_execute(const char* tool_name, const char* args_json);
}
```

### Fault Isolation

`NativePluginAdapter` wraps each native plugin with three protection layers:

1. **Exception catch** — any thrown exception is converted to `isError: true`
2. **Timeout** — 30-second `wait_for` deadline per call
3. **Circuit breaker** — after 3 consecutive faults, the plugin is disabled

### Hot Reload

The `NativePluginLoader` runs a background watcher thread that polls `plugins/*/bin/` every 2 seconds. When a new `.dll` or `.so` appears:

1. The library is loaded and tools are registered
2. A `notifications/tools/list_changed` message is pushed to the stdio client
3. Connected LLMs refresh their tool list without reconnecting

### Building a Native Plugin

```bash
# In-tree (built alongside the server)
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release

# Standalone
cd plugins/my_native_plugin
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
# Copy the output to bin/
cp build/Release/my_native_plugin.dll bin/   # Windows
cp build/libmy_native_plugin.so bin/          # Linux
```

See [`example-plugin/`](example-plugin/) for a complete reference implementation with `ping` and `base64_encode` tools.

---

## Skill Plugins (SKILL.md)

Skill plugins define tools as prompt templates using Markdown files. At startup the server promotes every skill into its own first-class MCP tool, so any LLM can discover and call them directly via `tools/list`.

### Directory Layout

```text
plugins/
└── my_plugin/
    ├── plugin.json          # Plugin metadata
    └── skills/
        └── code_review/
            ├── SKILL.md     # Required — defines the tool
            ├── scripts/     # Optional — executable helpers
            ├── references/  # Optional — docs loaded into context
            └── assets/      # Optional — templates / static files
```

### `SKILL.md` Format

```markdown
---
name: code_review
description: Review code for bugs, style issues, and improvements
variables:
  - code
  - language
---

Review the following {{language}} code for bugs, style issues, and improvements:

\`\`\`{{language}}
{{code}}
\`\`\`

Provide structured feedback: 1) Bugs, 2) Style, 3) Improvements.
```

**Frontmatter fields:**

| Field | Required | Description |
| --- | --- | --- |
| `name` | No (falls back to dir name) | Tool name registered in MCP |
| `description` | Yes | Shown to LLMs in `tools/list` — make it specific |
| `variables` | No | List of `{{placeholder}}` names that callers must supply |

The Markdown body after the closing `---` becomes the `prompt_template`. Use `{{variable_name}}` placeholders — they are interpolated at call time and sanitized against injection.

### `plugin.json` Format

```json
{
  "name": "my-plugin",
  "description": "Short description of what this plugin provides",
  "author": { "name": "Your Name" },
  "keywords": ["tag1", "tag2"]
}
```

### Adding a Plugin

1. Create the plugin directory structure under `plugins/`.

2. Write your `SKILL.md` files following the format above.

3. Restart the server — plugins are loaded at startup. The new tools appear immediately in `tools/list`:

```bash
# Verify via REST
curl http://localhost:8080/skills

# Verify via MCP stdio
echo '{"jsonrpc":"2.0","id":1,"method":"tools/list","params":{}}' | ./build/mcp_server --stdio
```

4. Optionally configure the plugins directory in `config/mcp_config.json`:

```json
{
  "plugins": {
    "directory": "/absolute/path/to/plugins"
  }
}
```

### Calling a Plugin Tool

Once loaded, each skill is a first-class MCP tool:

```bash
echo '{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "tools/call",
  "params": {
    "name": "code_review",
    "arguments": {
      "code": "int x = foo();",
      "language": "cpp"
    }
  }
}' | ./build/mcp_server --stdio
```

### Parallel Skill Execution

Multiple skills can execute in parallel using the `tools/call_batch` extension:

```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "tools/call_batch",
  "params": {
    "calls": [
      {
        "name": "code_review",
        "arguments": { "code": "int x = foo();", "language": "cpp" }
      },
      {
        "name": "summarize",
        "arguments": { "input": "Long article text..." }
      }
    ]
  }
}
```

All calls are launched concurrently (`std::async`) before any result is collected, so total latency ≈ max(individual latencies) rather than their sum. Individual failures are isolated — a failed call sets `"isError": true` for that entry without cancelling the others.

---

## Tool Chaining

A tool can include a `"chain"` field in its result to immediately invoke another tool with derived arguments — no LLM round-trip required. The MCP client receives only the final result; all intermediate hops are invisible.

### Chain Response Format

```json
{
  "status":  "ok",
  "content": "step 1 done",
  "chain":   { "tool": "next_tool", "args": { "input": "derived value" } }
}
```

### Cross-Source Chaining

`CommandRegistry::ExecuteWithChaining` detects the `"chain"` field after every `ExecuteAsync` call, making chaining source-agnostic:

| Tool source | Can initiate chain | Can be chained to |
| ----------- | ------------------ | ----------------- |
| `BuiltIn` | Yes | Yes |
| `JsonSkill` | Yes | Yes |
| `Plugin` (SKILL.md) | Yes | Yes |
| `NativePlugin` (.dll/.so) | Yes | Yes |
| `ScriptPlugin` (Python/Node/C#) | Yes | Yes |

Maximum chain depth is **5** (`kMaxChainDepth`). Exceeding it returns the last result without further chaining and logs a warning.
