param(
    [Parameter(Mandatory = $true)]
    [string]$CsvPath,

    [string]$OutputPath,

    [string]$CsvzallExe,

    [int]$MaxReps = 12
)

$ErrorActionPreference = "Stop"

function Resolve-CsvzallExe {
    param([string]$RequestedPath)

    if ($RequestedPath) {
        $resolved = Resolve-Path -LiteralPath $RequestedPath -ErrorAction Stop
        return $resolved.Path
    }

    $command = Get-Command csvzall.exe -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    $repoRoot = Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")
    $candidates = @(
        "out\build\codex-vs-cmake-ninja\csvzall.exe",
        "out\build\x64-Release\csvzall.exe",
        "out\build\codex-test-plan\csvzall.exe",
        "build\csvzall.exe"
    )

    foreach ($candidate in $candidates) {
        $path = Join-Path $repoRoot $candidate
        if (Test-Path -LiteralPath $path) {
            return (Resolve-Path -LiteralPath $path).Path
        }
    }

    throw "Unable to find csvzall.exe. Pass -CsvzallExe <path>."
}

function Escape-MarkdownCell {
    param([object]$Value)

    if ($null -eq $Value) {
        return ""
    }

    return ([string]$Value).Replace("\", "\\").Replace("|", "\|").Replace("`r", " ").Replace("`n", " ")
}

function ConvertTo-MarkdownTable {
    param(
        [object[]]$Rows,
        [string[]]$Columns
    )

    $lines = New-Object System.Collections.Generic.List[string]
    $lines.Add("| " + (($Columns | ForEach-Object { Escape-MarkdownCell $_ }) -join " | ") + " |")
    $lines.Add("|" + (($Columns | ForEach-Object { "---" }) -join "|") + "|")

    foreach ($row in $Rows) {
        $cells = foreach ($column in $Columns) {
            Escape-MarkdownCell $row.$column
        }
        $lines.Add("| " + ($cells -join " | ") + " |")
    }

    return $lines -join [Environment]::NewLine
}

$csvPathResolved = (Resolve-Path -LiteralPath $CsvPath).Path
if (-not $OutputPath) {
    $OutputPath = Join-Path (Split-Path -Parent $csvPathResolved) "FitNotes_All_Exercises_Estimated_1RM.md"
}
$outputPathResolved = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputPath)
$csvzall = Resolve-CsvzallExe $CsvzallExe

$tempCsv = Join-Path ([System.IO.Path]::GetTempPath()) ("csvzall_fitnotes_1rm_{0}.csv" -f ([System.Guid]::NewGuid()))

$sql = @"
WITH weighted_sets AS (
  SELECT
    [Date] AS date,
    [Exercise] AS exercise,
    CASE
      WHEN lower([Weight Unit]) IN ('kg', 'kgs', 'kilogram', 'kilograms')
        THEN CAST([Weight] AS REAL) * 2.2046226218
      ELSE CAST([Weight] AS REAL)
    END AS weight_lbs,
    CAST([Reps] AS INTEGER) AS reps
  FROM data
  WHERE
    [Date] IS NOT NULL AND [Date] <> ''
    AND [Exercise] IS NOT NULL AND [Exercise] <> ''
    AND [Weight] IS NOT NULL AND [Weight] <> ''
    AND [Reps] IS NOT NULL AND [Reps] <> ''
    AND CAST([Reps] AS INTEGER) > 0
    AND ($MaxReps <= 0 OR CAST([Reps] AS INTEGER) <= $MaxReps)
),
estimated AS (
  SELECT
    date,
    exercise,
    weight_lbs,
    reps,
    weight_lbs * (1.0 + reps / 30.0) AS estimated_1rm_lbs
  FROM weighted_sets
),
daily_best AS (
  SELECT
    date,
    exercise,
    MAX(estimated_1rm_lbs) AS estimated_1rm_lbs
  FROM estimated
  GROUP BY date, exercise
)
SELECT
  exercise AS [Exercise],
  date AS [Date],
  printf('%.1f', estimated_1rm_lbs) AS [Estimated 1RM (lb)]
FROM daily_best
ORDER BY exercise COLLATE NOCASE, date;
"@

try {
    $csvzallArgs = @("sql", "query", "--csv", $csvPathResolved, "--sql", $sql, "--quiet")
    & $csvzall @csvzallArgs > $tempCsv
    if ($LASTEXITCODE -ne 0) {
        throw "csvzall sql query failed with exit code $LASTEXITCODE"
    }

    $rows = @(Import-Csv -LiteralPath $tempCsv)
    $columns = @("Exercise", "Date", "Estimated 1RM (lb)")

    $markdown = New-Object System.Collections.Generic.List[string]
    $markdown.Add("---")
    $markdown.Add("tags:")
    $markdown.Add("  - exercise")
    $markdown.Add("  - fitnotes")
    $markdown.Add("  - strength")
    $markdown.Add("---")
    $markdown.Add("")
    $markdown.Add("# All Exercises Estimated 1RM")
    $markdown.Add("")
    $markdown.Add("Source: ``$(Split-Path -Leaf $csvPathResolved)``")
    $markdown.Add("")
    $markdown.Add("Generated with ``csvzall.exe sql query``.")
    $markdown.Add("Formula: Epley estimated 1RM = ``weight * (1 + reps / 30)``.")
    $markdown.Add("Rep cap: " + ($(if ($MaxReps -le 0) { "none" } else { $MaxReps })))
    $markdown.Add("")
    $markdown.Add("Rows: $($rows.Count)")
    $markdown.Add("")

    if ($rows.Count -gt 0) {
        $markdown.Add((ConvertTo-MarkdownTable -Rows $rows -Columns $columns))
    } else {
        $markdown.Add("No weighted rep sets matched the filters.")
    }

    $outputDir = Split-Path -Parent $outputPathResolved
    if ($outputDir) {
        New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
    }
    Set-Content -LiteralPath $outputPathResolved -Value ($markdown -join [Environment]::NewLine) -Encoding UTF8

    Write-Output $outputPathResolved
    Write-Output "Rows: $($rows.Count)"
} finally {
    Remove-Item -LiteralPath $tempCsv -Force -ErrorAction SilentlyContinue
}
