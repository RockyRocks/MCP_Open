---
name: jira_get_comments
description: Retrieve all comments for a Jira issue, optionally filtered by text. Trigger phrases: show comments, what did people say, read comments on PROJ-123, discussion on ticket, comment history.
type: command
command_template: python ${PLUGIN_DIR}/scripts/jira_tools.py get-comments "{{issue_key}}" "{{filter}}"
variables:
  - issue_key
  - filter
rules:
  - issue_key MUST be a valid Jira key in PROJECT-NUMBER format (e.g. PROJ-123).
  - filter is optional. When provided, only comments containing the filter text (case-insensitive) are returned. Leave empty to get all comments.
  - If the user asks about a specific topic within an issue's comments, pass that topic as the filter to narrow results.
  - Present comments chronologically with author and timestamp. For long comment threads, summarise the key points unless the user asks for everything.
  - If no comments exist, inform the user clearly.
  - If the command returns a "not found" error, verify the issue key and inform the user.
  - If the command returns an auth error, inform the user about the required environment variables.
---

# Jira Get Comments

Fetches comments from any Jira issue with optional text filtering.

The filter parameter performs case-insensitive substring matching on comment bodies,
useful for finding comments about a specific topic in long threads.

**Required environment variables:** `JIRA_BASE_URL`, `JIRA_USERNAME`, `JIRA_API_TOKEN`
