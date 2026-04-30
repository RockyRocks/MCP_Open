---
name: jira_get_issue
description: Fetch full Jira issue details — summary, status, assignee, description, custom fields, and URL. Trigger phrases: show issue, get ticket, what is PROJ-123, issue details, describe ticket. Supports any Jira instance (Cloud or Server/Data Center) via environment variables.
type: command
command_template: python ${PLUGIN_DIR}/scripts/jira_tools.py get-issue "{{issue_key}}"
variables:
  - issue_key
rules:
  - issue_key MUST be a valid Jira key in PROJECT-NUMBER format (e.g. PROJ-123, QF-4567). Extract it from user input before calling.
  - If the user mentions a ticket by name or number without the project prefix, ask which project or check recent context for the project key.
  - If the result contains a description, present a concise summary unless the user asks for the full text.
  - If custom fields are present in the output, include them — they carry domain-specific context the user likely needs.
  - If the command returns an error about missing environment variables, inform the user that JIRA_BASE_URL, JIRA_USERNAME, and JIRA_API_TOKEN must be set.
  - If the command returns a "not found" error, inform the user and verify the issue key is correct.
  - Present the URL so the user can open the issue in their browser.
  - When the issue has comments, the server may automatically chain to jira_get_comments and append them. If comments appear in the output, include a summary — do not re-fetch.
---

# Jira Get Issue

Fetches complete issue details from any Jira instance (Cloud or Server/Data Center).

**Required environment variables:**
- `JIRA_BASE_URL` — Jira instance URL
- `JIRA_USERNAME` — email (Cloud) or username (Server/DC)
- `JIRA_API_TOKEN` — API token (Cloud) or personal access token (Server/DC)

**Optional:**
- `JIRA_API_VERSION` — `"2"` (default, Server/DC) or `"3"` (Cloud)
- `JIRA_CUSTOM_FIELDS` — JSON string mapping custom field display names to field IDs
