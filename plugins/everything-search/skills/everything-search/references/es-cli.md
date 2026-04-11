# es.exe CLI Reference (ES 1.1.0.9)

Source: <https://github.com/voidtools/ES>

## Prerequisites

1. **Everything** by voidtools must be installed and running:
   <https://www.voidtools.com/downloads/>
2. The Everything daemon indexes all NTFS drives in real-time.
   It must be **running** before any `es.exe` search — exit code 8 means the
   daemon is not found (`"Everything IPC window not found"`).
3. `es.exe` is bundled at `scripts/es.exe` in this plugin.
   Source and updates: <https://github.com/voidtools/ES>

---

## Usage

```
es.exe [options] search text
```

---

## Search Options

| Flag | Description |
|------|-------------|
| `-r`, `-regex` | Search using regular expressions |
| `-i`, `-case` | Match case (case-sensitive) |
| `-w`, `-ww`, `-whole-word`, `-whole-words` | Match whole words |
| `-p`, `-match-path` | Match full path and file name |
| `-a`, `-diacritics` | Match diacritical marks |
| `-path <path>` | Search for subfolders and files in path |
| `-parent-path <path>` | Search for subfolders and files in the parent of path |
| `-parent <path>` | Search for files with the specified parent path |

---

## Result Options

| Flag | Description |
|------|-------------|
| `-o <offset>`, `-offset <offset>` | Show results starting from offset |
| `-n <num>`, `-max-results <num>` | Limit the number of results shown to `<num>` |
| `-s` | Sort by full path |

---

## Column Display

| Flag | Description |
|------|-------------|
| `-name` | Show name column |
| `-path-column` | Show path column |
| `-full-path-and-name`, `-filename-column` | Show full path and name column |
| `-extension`, `-ext` | Show extension column |
| `-size` | Show size column |
| `-date-created`, `-dc` | Show date created column |
| `-date-modified`, `-dm` | Show date modified column |
| `-date-accessed`, `-da` | Show date accessed column |
| `-attributes`, `-attribs`, `-attrib` | Show attributes column |

---

## Sort Options

| Flag | Description |
|------|-------------|
| `-sort <name[-ascending\|-descending]>` | Set sort by: name, path, size, extension, date-created, date-modified, date-accessed, attributes, run-count, date-recently-changed, date-run |
| `-sort-ascending` | Set sort order ascending |
| `-sort-descending` | Set sort order descending |

### DIR-style sorts

| Flag | Description |
|------|-------------|
| `/on` | Sort by name ascending |
| `/o-n` | Sort by name descending |
| `/os` | Sort by size ascending |
| `/o-s` | Sort by size descending |
| `/oe` | Sort by extension ascending |
| `/o-e` | Sort by extension descending |
| `/od` | Sort by date modified ascending |
| `/o-d` | Sort by date modified descending |

---

## Filter Options (DIR-style)

| Flag | Description |
|------|-------------|
| `/ad` | Folders only |
| `/a-d` | Files only |
| `/a[RHSDAVNTPLCOIE]` | Attribute filters (R=Read-only, H=Hidden, S=System, D=Directory, A=Archive, etc.) |

---

## Export Formats

| Flag | Description |
|------|-------------|
| `-csv` | Output as CSV |
| `-efu` | Output as EFU |
| `-txt` | Output as plain text |
| `-m3u` | Output as M3U |
| `-m3u8` | Output as M3U8 |
| `-export-csv <out.csv>` | Export results to CSV file |
| `-export-efu <out.efu>` | Export results to EFU file |
| `-export-txt <out.txt>` | Export results to TXT file |

---

## Everything Search Syntax (Search Terms)

These operators appear inside the search text, not as CLI flags:

| Operator | Description | Example |
|----------|-------------|---------|
| `ext:<ext>` | Filter by extension | `ext:py` |
| `ext:<ext1;ext2>` | Multiple extensions | `ext:cpp;h;hpp` |
| `path:<path>` | Restrict to a directory tree | `path:F:\Git\MyProject` |
| `folder:` | Folders only | `folder: src` |
| `file:` | Files only | `file: main` |
| `*` | Wildcard (any characters) | `test_*.py` |
| `?` | Single character wildcard | `config?.json` |
| `\|` | OR operator | `ext:jpg \| ext:png` |
| `!` | NOT operator | `ext:py !test` |
| `< >` | Grouping | `<ext:cpp \| ext:h> path:src` |
| `startwith:<text>` | Name starts with | `startwith:index` |
| `endwith:<text>` | Name ends with | `endwith:_test.py` |
| `size:<size>` | Filter by size | `size:>1mb` |
| `dm:<date>` | Date modified filter | `dm:today`, `dm:lastweek` |
| `dc:<date>` | Date created filter | `dc:2024` |

---

## Default Command Used by This Plugin

```
es.exe -n 50 -dm -sort-dm "<query>"
```

- `-n 50` — cap results at 50 to avoid flooding the output
- `-dm` — show date-modified column
- `-sort-dm` — sort by date modified (most recently changed files first)

**Important:** when including `path:` in the query it must be a separate
space-delimited token, not inside the quoted search string:

```
# Correct — path: as a token within the query value
query = "path:F:\MyProject ext:cpp controller"

# Wrong — path: does not scope correctly if it is the entire query
query = "path:F:\MyProject"  followed by separate shell args
```

---

## Miscellaneous

| Flag | Description |
|------|-------------|
| `-instance <name>` | Connect to a specific Everything instance |
| `-timeout <ms>` | Timeout for database load |
| `-save-settings` | Save current settings |
| `-clear-settings` | Clear saved settings |

## Notes

- `-` prefixes can be omitted (e.g., `-nodigitgrouping`)
- Switches can also start with `/`
- Switches can be disabled with `no-` prefix (e.g., `-no-size`)
- Use double quotes to escape spaces in search terms
- Use `^` to escape `\`, `&`, `|`, `>`, `<`, and `^` in Windows cmd
- **Exit code 8** means the Everything IPC window was not found — start Everything and retry
