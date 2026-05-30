# Todoist Script Guidance

This folder is for small source-adapter scripts that fetch Todoist data into
CSV/Markdown/SVG-friendly local artifacts. Keep csvzall focused on local
CSV/SQLite/query/chart work; keep Todoist auth and network fetching here.

## Auth

- Use `TODOIST_API_TOKEN` from the environment.
- Never print the token.
- Prefer checking presence/length only:
  `GetEnvironmentVariable("TODOIST_API_TOKEN")`.
- A personal Todoist API token is enough for one-user/local workflows.
- OAuth/developer-app flow is only needed if this becomes a distributed app for
  other users.

## Current API

- Use Todoist API v1 under `https://api.todoist.com/api/v1/...`.
- Old `/rest/v2/...` endpoints may return a deprecation error.
- All requests use:
  `Authorization: Bearer $env:TODOIST_API_TOKEN`.
- Most list endpoints are paginated. Continue until `next_cursor` is null.
- Use `limit=200` where supported to reduce request count.

## Active Tasks

Use:

```text
GET /api/v1/tasks
```

Notes:

- The endpoint returns `results` and `next_cursor`.
- The first page may not include the task you need.
- Always paginate when searching active tasks.
- For the current habit task, broad search found:
  - content: `Gym workout 🏋‍♂️`
  - id: `6XMFh56Fm3xX7xg5`
  - due: `every day`
  - recurring: true

## Completed Recurring Habit History

For recurring habit tasks, the completed-tasks endpoint may not find the task by
content or current active task id. Use the activity log instead.

Preferred endpoint:

```text
GET /api/v1/activities?object_type=item&object_id=<task_id>&event_type=completed&limit=100
```

Notes:

- This returned the actual recurrence completion history for `Gym workout 🏋‍♂️`.
- The response returns `results` and `next_cursor`.
- Completion time is in `event_date`.
- Task metadata is in `extra_data`.
- Useful fields:
  - `event_date`
  - `event_type`
  - `object_id`
  - `parent_project_id`
  - `extra_data.content`
  - `extra_data.due_date`
  - `extra_data.completed_due_date_local`
  - `extra_data.was_overdue`
  - `extra_data.client`

Example recurring completion event shape:

```json
{
  "id": 2148846797991822068047640471540859234,
  "event_type": "completed",
  "event_date": "2026-04-29T17:40:10.391658Z",
  "object_type": "item",
  "object_id": "6XMFh56Fm3xX7xg5",
  "parent_project_id": "6Frc4Jvrw5Q4CQ85",
  "extra_data": {
    "client": "Mozilla/5.0; Todoist/10416",
    "completed_date_source": "due_date",
    "completed_due_date": "2026-04-21T06:59:59Z",
    "completed_due_date_local": "2026-04-20T23:59:59",
    "content": "Gym workout 🏋‍♂️",
    "due_date": "2026-05-01T06:59:59Z",
    "has_time": false,
    "is_recurring": true,
    "note_count": 1,
    "priority": 1,
    "was_overdue": true
  }
}
```

JSON-to-table flattening should make fields like
`extra_data.content`, `extra_data.due_date`, and
`extra_data.was_overdue` directly addressable as columns.

## Completed Tasks By Date

Endpoint:

```text
GET /api/v1/tasks/completed/by_completion_date
```

Use this for broad completed-task exports, not current recurring task identity.

Constraints discovered from docs:

- `since` is required.
- `until` is required.
- Range is limited to up to 3 months.
- Response is paginated with `next_cursor`.
- Response object uses `items`, not `results`.

## Date Handling

- Todoist timestamps are UTC.
- Convert to local dates before habit/calendar analysis.
- For this workspace, use `America/Phoenix` semantics. In Windows PowerShell,
  `US Mountain Standard Time` is the practical timezone id.
- For habit heatmaps, prefer local completion date.
- Keep due date separately; it may differ from completion date, especially when
  a recurring task was overdue.

## Recommended Habit Export Shape

Export recurring habit activity to CSV with stable columns:

```text
task_id
content
event_id
completed_at_utc
completed_date_local
completed_time_local
due_date_local
was_overdue
client
project_id
```

Then let csvzall do summaries/charts:

```powershell
csvzall sql query --csv todoist_gym_workout.csv --sql "SELECT completed_date_local, COUNT(*) AS completions FROM data GROUP BY completed_date_local ORDER BY completed_date_local"
```

## Product Boundary

Do not rush Todoist fetching into the core csvzall binary.

Good split:

- PowerShell: Todoist API -> local CSV.
- csvzall: CSV -> SQLite SQL -> Markdown/SVG/calendar heatmap.
- Obsidian: display generated Markdown/SVG.

This keeps network/auth complexity out of csvzall while preserving the useful
single-binary CSV workflow.
