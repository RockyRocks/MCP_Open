---
name: entrian_search
description: Full-text indexed search across a codebase using Entrian Source Search (ess.exe). PREFER over Grep for large codebases — pre-built indexes make this orders of magnitude faster. Trigger phrases: search code, find symbol, find usage, search for, where is defined, find all references. Query examples: MyClass, ext:cpp Initialize, dir:component MyFunc, loose:"Init Update Render".
type: command
command_template: "C:\Program Files (x86)\Entrian Source Search\ess.exe" search -index="{{index_path}}" -showcount -verbosity=1 {{query}}
variables:
  - index_path
  - query
rules:
  - Always call entrian_list_indexes first to find the correct index_path for the target project.
  - index_path must be the full absolute path to the .index file. Do not add extra quotes — the command wraps it in double quotes automatically.
  - Multi-word queries are ANDed automatically. query="MyClass Initialize" finds files containing both terms.
  - For exact phrase searches, embed double quotes in the query variable: query="\"exact phrase\"".
  - Prefix filters — ext:cpp, dir:component, file:main, age:1d. Combine freely: ext:h,cpp MySymbol.
  - Proximity and OR searches using angle brackets and pipe are NOT supported via this tool — those characters are stripped for security. Use multiple separate searches instead.
  - Exit code 1 with "Matches: 0" in output means no results — this is normal, not an error. Only report error if output has no "Matches:" line at all.
  - If ess.exe is not found (output contains "not recognized" or "cannot find"), inform the user that Entrian Source Search must be installed at C:\Program Files (x86)\Entrian Source Search\.
  - Present results to the user. Output format is path(line): context. If many results, summarise by file or directory rather than listing every hit.
---

# Entrian Search

Full-text indexed code search using [Entrian Source Search](https://entrian.com/source-search/doc-command-line.html/).

The bundled `ess.exe` CLI queries pre-built indexes for near-instant results
regardless of codebase size — unlike grep which scans every file on each call.

See `references/ess-cli.md` for full CLI reference, query syntax, index
management commands, and common recipe examples.
