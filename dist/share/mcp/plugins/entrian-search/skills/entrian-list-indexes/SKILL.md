---
name: entrian_list_indexes
description: Lists all Entrian Source Search index files on this machine. Call this BEFORE entrian_search to discover available indexes and their full paths. Trigger phrases: list indexes, find index, which index, available indexes.
type: command
command_template: dir "%USERPROFILE%\AppData\Local\Entrian Source Search\Indexes\*.index" /B /S
rules:
  - If output contains "File Not Found", no Entrian indexes exist yet. Guide the user to install Entrian Source Search (https://www.entriantools.com) and create an index using entrian_search references.
  - Pick the index whose name best matches the project being searched, then pass its full path as index_path to entrian_search.
---

Lists Entrian Source Search `.index` files installed at the default location.

Run this first to obtain the `index_path` required by `entrian_search`.
The standard index location is `%USERPROFILE%\AppData\Local\Entrian Source Search\Indexes\`.
