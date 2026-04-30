---
name: jira_search
description: Search Jira issues using JQL (Jira Query Language). Returns matching issues with key, summary, status, assignee, priority, type, and URL. Trigger phrases: search jira, find issues, my open tickets, search for bugs, list issues, find tickets assigned to me. Query examples: project = PROJ AND status != Done, assignee = currentUser(), priority = High AND type = Bug, text ~ "login error".
type: command
command_template: python ${PLUGIN_DIR}/scripts/jira_tools.py search "{{jql}}" "{{max_results}}"
variables:
  - jql
  - max_results
rules:
  - Build the JQL query from the user's intent. Common translations: "my issues" → assignee = currentUser(), "open bugs" → type = Bug AND status != Done, "recent issues" → ORDER BY created DESC.
  - max_results defaults to 20 if not provided. Maximum is 50. Do not pass max_results unless the user specifies a limit.
  - JQL strings must use Jira field names exactly: summary, description, assignee, reporter, status, priority, issuetype (not "type" in JQL), project, labels, created, updated.
  - The text search operator is ~ (tilde): summary ~ "search term" or text ~ "search term" for full-text.
  - currentUser() works natively in JQL when authenticated — no need to know the username.
  - If no results are found, suggest broadening the search: fewer filters, wider date range, or checking the project key.
  - If the user asks to search without providing enough context, ask for the project key or search terms before calling.
  - Present results as a concise list. If many results, group or summarise by status or priority rather than listing every issue.
  - If the command returns an auth error, inform the user about the required environment variables.
  - When search returns exactly one issue, the server automatically chains to jira_get_issue and returns full details. Present the complete result — do not re-fetch the issue.
---

# Jira Search

Searches any Jira instance using JQL queries.

**Common JQL patterns:**
- `project = PROJ AND status != Done` — open issues in a project
- `assignee = currentUser() ORDER BY updated DESC` — your issues, recently updated
- `priority = High AND type = Bug AND status != Closed` — high-priority open bugs
- `text ~ "error message" AND project = PROJ` — full-text search within a project
- `labels = "release-blocker"` — issues with a specific label
- `created >= -7d AND project = PROJ` — issues created in the last 7 days

**Required environment variables:** `JIRA_BASE_URL`, `JIRA_USERNAME`, `JIRA_API_TOKEN`
