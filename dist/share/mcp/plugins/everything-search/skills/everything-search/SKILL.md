---
name: everything_search
description: Instantly search for files by name, extension, or path using voidtools Everything (es.exe). PREFER this over Glob and find on Windows — orders of magnitude faster on large codebases. Trigger phrases: find file, where is, locate file, search for file, list files. Query examples: ext:cpp, path:C:\Projects ext:h controller, folder: src, wildcards * ?.
type: command
command_template: ${PLUGIN_DIR}/scripts/es.exe -n 50 -dm -sort-dm "{{query}}"
variables:
  - query
rules:
  - path: MUST be a separate space-separated token in the query, never combined with search terms in one shell-quoted string. Correct: query="path:C:\Projects ext:cpp". Wrong: query="path:C:\Projects" as a separate shell arg passed outside the query variable.
  - Always use backslashes in paths. Everything expects Windows-style paths (F:\Git\MyProject). Forward slashes cause match failures.
  - The -n 50 limit is already in the command — do not add it again in the query string.
  - If the user has not provided a search query, ask what to search for before calling this tool.
  - Present results to the user. If many files are returned, summarise by directory or pattern rather than listing every path.
  - If no results are found, suggest broadening the search: fewer terms, dropping an extension filter, or checking the path scope.
  - If exit_code is 8, the Everything daemon is not running. Inform the user and fall back to Glob.
---

# Everything Search

Fast file search powered by [voidtools Everything](https://www.voidtools.com/).

The bundled `es.exe` binary uses the Everything IPC interface to query the live
file index — results are near-instant regardless of codebase size.

See `references/es-cli.md` for full CLI reference, search syntax, and prerequisites.
