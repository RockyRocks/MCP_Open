# Entrian Source Search CLI Reference (ess.exe)

Source: <https://www.entriantools.com/>

## Prerequisites

1. **Entrian Source Search** must be installed:
   <https://www.entriantools.com/>
2. The default installation path is:
   `C:\Program Files (x86)\Entrian Source Search\ess.exe`
3. An index must be created for the target codebase before searching.
   Indexes are stored at:
   `%USERPROFILE%\AppData\Local\Entrian Source Search\Indexes\`
4. Use `entrian_list_indexes` MCP tool to discover available indexes.

---

## Index Management

### Creating an Index

Creating an index scans the entire source tree and can take several minutes.
**Always ask the user for permission before creating one.**

```bash
"C:\Program Files (x86)\Entrian Source Search\ess.exe" create
  -index="C:\Users\<username>\AppData\Local\Entrian Source Search\Indexes\<name>.index"
  -root="<root>"
  -include="*.h;*.cpp;*.c;*.cs;*.cxx;*.inl;*.hlsl;*.glsl;*.py;*.lua;*.xml;*.json;*.txt;*.md"
```

| Switch | Description |
| --- | --- |
| `-index=` / `-i=` | Path for the new index file [required] |
| `-root=` / `-r=` | Root folder to index [required, repeatable] |
| `-include=` / `-inc=` | File patterns to include, e.g. `*.h;*.cpp` [repeatable] |
| `-exclude=` / `-exc=` | File patterns to exclude, e.g. `*.xml;*.yaml` [repeatable] |
| `-watch` / `-w` | Keep watching for file changes and re-index automatically |
| `-ignoreglobalinclusions` / `-igi` | Ignore the global inclusion list from Entrian settings |
| `-ignoreglobalexclusions` / `-ige` | Ignore the global exclusion list from Entrian settings |
| `-log=` / `-l=` | Write log output to a file |

### Updating an Index

Refresh the index after files have changed:

```bash
"C:\Program Files (x86)\Entrian Source Search\ess.exe" update
  -index="C:\Users\<username>\AppData\Local\Entrian Source Search\Indexes\<name>.index"
```

Entrian also updates automatically when Visual Studio is open with the project.

### Unlocking a Stuck Index

If `ess.exe` crashed and left the index locked:

```bash
"C:\Program Files (x86)\Entrian Source Search\ess.exe" unlock
  -index="C:\Users\<username>\AppData\Local\Entrian Source Search\Indexes\<name>.index"
```

### Checking and Repairing an Index

```bash
"C:\Program Files (x86)\Entrian Source Search\ess.exe" check
  -index="C:\Users\<username>\AppData\Local\Entrian Source Search\Indexes\<name>.index"
```

Add `-fix` to attempt automatic repair:

```bash
"C:\Program Files (x86)\Entrian Source Search\ess.exe" check -fix
  -index="C:\Users\<username>\AppData\Local\Entrian Source Search\Indexes\<name>.index"
```

---

## Search Command Syntax

```bash
"C:\Program Files (x86)\Entrian Source Search\ess.exe" search
  -index="<path-to-index>"
  -showcount
  [options]
  <query>
```

**Always include `-showcount`** — the `Matches:` line it produces is required
to distinguish no results (exit code 1, `Matches: 0`) from a real error
(exit code 1, no `Matches:` line).

### Options

| Switch | Description |
| --- | --- |
| `-verbosity=N` / `-v=N` | Context chars shown per result line (1–100). Default is low; use `-v=50` for more context. |
| `-case` / `-c` | Case-sensitive search. |
| `-fuzzy` / `-f` | Fuzzy/approximate matching. |
| `-showcount` / `-s` | Print total match count at end. Always include. |

### Multiple Indexes

Pass `-index=` multiple times to search across several indexes simultaneously:

```bash
ess search -index="path\to\a.index" -index="path\to\b.index" -showcount MySymbol
```

---

## Query Syntax

### Text and Symbol Search

| Query | Meaning |
| --- | --- |
| `mySymbol` | Single word/identifier |
| `myVar1 myVar2` | Both words present (AND) |
| `"void MyFunction"` | Exact phrase |
| `myFunc*` | Wildcard — matches `myFunction`, `myFunctionHelper`, etc. |
| `loose:"Init Update Render"` | Words near each other, any order |

### File and Path Filtering

| Query | Meaning |
| --- | --- |
| `mySymbol ext:cpp` | Only `.cpp` files |
| `mySymbol ext:h,cpp` | Only `.h` or `.cpp` files |
| `mySymbol dir:component` | Only files under paths matching `component` |
| `mySymbol dir:system/component` | Narrow to a sub-path |
| `mySymbol -dir:tmp` | Exclude `tmp` directories |
| `file:entity` | Find files whose name contains `entity` |
| `file:entity ext:h` | Find `.h` files named `entity*` |
| `mySymbol -file:ChangeLog` | Exclude files named ChangeLog |

### Age Filtering

| Query | Meaning |
| --- | --- |
| `mySymbol age:1d` | Only files modified in the past day |

---

## Output Format

Each result line:

```bash
F:\path\to\file.ext(lineNumber): ...matching context...
```

The trailing `Matches: N` line (from `-showcount`) shows total result count.

---

## Exit Code Interpretation

| Condition | Exit Code | Output |
| --- | --- | --- |
| Results found | 0 | File hits + `Matches: N` |
| No results | 1 | `Matches: 0` |
| Real error | 1 | No `Matches:` line |

**Important:** Exit code 1 alone is not an error — always check for the
presence of `Matches:` in the output.

---

## MCP Tool Workflow

When using the `entrian_search` MCP tool:

1. Call `entrian_list_indexes` to discover available `.index` files.
2. Pick the index whose name best matches the project.
3. Formulate the query using the syntax above.
4. Call `entrian_search` with `index_path` (full path) and `query`.

**Note:** Proximity searches using `<word1 word2>` and OR searches using `|`
are not supported via the MCP tool — those characters are stripped for security.
Run `ess.exe` directly via the shell for these advanced queries.

---

## Common Recipes

```bash
# Find where a class is defined (header files only)
ess search -index=... -showcount -v=60 class MyClass ext:h -dir:tmp

# Find all usages of a function
ess search -index=... -showcount -v=60 MyFunction ext:h,cpp,cxx,hxx

# Find a file by name
ess search -index=... -showcount file:entity ext:h

# Find all .md files in a specific package
ess search -index=... -showcount file:* dir:system/component ext:md

# Case-sensitive search for a macro
ess search -index=... -showcount -case DECLARE_MANAGED_OBJECT ext:h

# Find class inheriting from a base class
ess search -index=... -showcount -v=80 public IEntity ext:h -dir:tmp

# Phrase search (wrap phrase in double quotes within query variable)
ess search -index=... -showcount -v=60 "void MyFunction(" ext:h,cpp

# Loose/proximity search
ess search -index=... -showcount loose:"Init Update Render" ext:h,cpp
```
