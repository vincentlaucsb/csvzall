<#
.SYNOPSIS
Builds and installs csvzall on Windows, optionally adding it to PATH.

.EXAMPLE
.\scripts\Install-csvzall.ps1

Builds csvzall, installs to C:\Program Files\csvzall, and adds
C:\Program Files\csvzall\bin to the machine PATH. If needed, the script
relaunches itself with a UAC prompt.

.EXAMPLE
.\scripts\Install-csvzall.ps1 -InstallPrefix "$env:LOCALAPPDATA\csvzall" -PathScope User

Installs for the current user without requiring elevation.
#>

[CmdletBinding()]
param(
    [string]$SourceDir = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path,

    [string]$BuildDir = '',

    [string]$Config = 'Release',

    [string]$InstallPrefix = (Join-Path $env:ProgramFiles 'csvzall'),

    [ValidateSet('Auto', 'User', 'Machine', 'None')]
    [string]$PathScope = 'Auto',

    [string]$CMake = 'cmake',

    [string[]]$ExtraConfigureArgs = @(),

    [string]$LogPath = (Join-Path $env:TEMP 'csvzall-install.log'),

    [string]$CopyFromExe = '',

    [switch]$SkipConfigure,

    [switch]$SkipBuild,

    [switch]$NoPath,

    [switch]$NoElevate
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$transcriptStarted = $false
try {
    Start-Transcript -Path $LogPath -Append | Out-Null
    $transcriptStarted = $true
}
catch {
    Write-Warning "Could not start install log at '$LogPath': $($_.Exception.Message)"
}

function Test-IsAdministrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Resolve-FullPath {
    param([Parameter(Mandatory)][string]$Path)

    $executionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Path)
}

function Get-CMakeCacheValue {
    param(
        [Parameter(Mandatory)][string]$BuildDir,
        [Parameter(Mandatory)][string]$Name
    )

    $cachePath = Join-Path $BuildDir 'CMakeCache.txt'
    if (-not (Test-Path $cachePath)) {
        return $null
    }

    $escapedName = [regex]::Escape($Name)
    $line = Get-Content $cachePath |
        Where-Object { $_ -match "^${escapedName}:[^=]*=" } |
        Select-Object -First 1

    if ($null -eq $line) {
        return $null
    }

    return ($line -replace '^[^=]*=', '')
}

function Test-MsvcCMakeBuild {
    param([Parameter(Mandatory)][string]$BuildDir)

    $compiler = Get-CMakeCacheValue -BuildDir $BuildDir -Name 'CMAKE_CXX_COMPILER'
    if ([string]::IsNullOrWhiteSpace($compiler)) {
        $compiler = Get-CMakeCacheValue -BuildDir $BuildDir -Name 'CMAKE_C_COMPILER'
    }

    if ([string]::IsNullOrWhiteSpace($compiler)) {
        return $false
    }

    $compilerPath = ($compiler -replace '/', '\')
    return [IO.Path]::GetFileName($compilerPath).Equals('cl.exe', [StringComparison]::OrdinalIgnoreCase)
}

function Test-VisualStudioDevEnvironment {
    if ([string]::IsNullOrWhiteSpace($env:INCLUDE) -or [string]::IsNullOrWhiteSpace($env:LIB)) {
        return $false
    }

    return $env:INCLUDE -match [regex]::Escape('\VC\Tools\MSVC\') -or
        -not [string]::IsNullOrWhiteSpace($env:VCToolsInstallDir)
}

function Find-VcVars64 {
    param([Parameter(Mandatory)][string]$BuildDir)

    $compiler = Get-CMakeCacheValue -BuildDir $BuildDir -Name 'CMAKE_CXX_COMPILER'
    if ([string]::IsNullOrWhiteSpace($compiler)) {
        $compiler = Get-CMakeCacheValue -BuildDir $BuildDir -Name 'CMAKE_C_COMPILER'
    }

    if (-not [string]::IsNullOrWhiteSpace($compiler)) {
        $compilerPath = ($compiler -replace '/', '\')
        $vcToolsMarker = '\VC\Tools\'
        $markerIndex = $compilerPath.IndexOf($vcToolsMarker, [StringComparison]::OrdinalIgnoreCase)
        if ($markerIndex -ge 0) {
            $vsRoot = $compilerPath.Substring(0, $markerIndex)
            $candidate = Join-Path $vsRoot 'VC\Auxiliary\Build\vcvars64.bat'
            if (Test-Path $candidate) {
                return (Resolve-FullPath $candidate)
            }
        }
    }

    $vswhere = $null
    if (${env:ProgramFiles(x86)}) {
        $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    }

    if ($null -ne $vswhere -and (Test-Path $vswhere)) {
        $installations = & $vswhere -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        foreach ($installation in $installations) {
            if ([string]::IsNullOrWhiteSpace($installation)) {
                continue
            }

            $candidate = Join-Path $installation 'VC\Auxiliary\Build\vcvars64.bat'
            if (Test-Path $candidate) {
                return (Resolve-FullPath $candidate)
            }
        }
    }

    $programFilesRoots = @()
    if ($env:ProgramFiles) {
        $programFilesRoots += $env:ProgramFiles
    }
    if (${env:ProgramFiles(x86)}) {
        $programFilesRoots += ${env:ProgramFiles(x86)}
    }

    $versions = @('18', '2022', '2019', '2017')
    $editions = @('Community', 'Professional', 'Enterprise', 'BuildTools')
    foreach ($root in $programFilesRoots) {
        foreach ($version in $versions) {
            foreach ($edition in $editions) {
                $candidate = Join-Path $root "Microsoft Visual Studio\$version\$edition\VC\Auxiliary\Build\vcvars64.bat"
                if (Test-Path $candidate) {
                    return (Resolve-FullPath $candidate)
                }
            }
        }
    }

    return $null
}

function Import-VisualStudioDevEnvironment {
    param([Parameter(Mandatory)][string]$BuildDir)

    if (-not (Test-MsvcCMakeBuild -BuildDir $BuildDir)) {
        return
    }

    if (Test-VisualStudioDevEnvironment) {
        Write-Host "Visual Studio developer environment already active."
        return
    }

    $vcvars64 = Find-VcVars64 -BuildDir $BuildDir
    if ([string]::IsNullOrWhiteSpace($vcvars64)) {
        throw "This CMake build uses MSVC, but Visual Studio vcvars64.bat was not found. Run from a Visual Studio Developer PowerShell or install the Desktop development with C++ workload."
    }

    Write-Host "Importing Visual Studio developer environment: $vcvars64"
    $environmentLines = & $env:ComSpec /d /s /c "`"$vcvars64`" >nul && set"
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to import Visual Studio developer environment from '$vcvars64'."
    }

    foreach ($line in $environmentLines) {
        if ($line -match '^([^=]+)=(.*)$') {
            [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process')
        }
    }
}

function Find-MostRecentCMakeBuildDir {
    param([Parameter(Mandatory)][string]$SourceDir)

    $buildRoot = Join-Path $SourceDir 'out\build'
    if (-not (Test-Path $buildRoot)) {
        return $null
    }

    $exe = Get-ChildItem -Path $buildRoot -Recurse -Filter csvzall.exe -File |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1

    if ($null -ne $exe) {
        $dir = $exe.Directory
        while ($null -ne $dir -and $dir.FullName.StartsWith($buildRoot, [StringComparison]::OrdinalIgnoreCase)) {
            if (Test-Path (Join-Path $dir.FullName 'CMakeCache.txt')) {
                return $dir.FullName
            }
            $dir = $dir.Parent
        }
    }

    $cache = Get-ChildItem -Path $buildRoot -Recurse -Filter CMakeCache.txt -File |
        Where-Object {
            $_.DirectoryName -notmatch '\\_deps\\' -and
            $_.DirectoryName -notmatch '\\CMakeFiles\\CMakeScratch\\' -and
            $_.DirectoryName -notlike (Join-Path $buildRoot 'install')
        } |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1

    if ($null -eq $cache) {
        return $null
    }

    return $cache.DirectoryName
}

function Test-IsUnderPath {
    param(
        [Parameter(Mandatory)][string]$Path,
        [Parameter(Mandatory)][string]$Parent
    )

    $fullPath = [IO.Path]::GetFullPath($Path).TrimEnd('\')
    $fullParent = [IO.Path]::GetFullPath($Parent).TrimEnd('\')

    return $fullPath.Equals($fullParent, [StringComparison]::OrdinalIgnoreCase) -or
        $fullPath.StartsWith($fullParent + '\', [StringComparison]::OrdinalIgnoreCase)
}

function Quote-Argument {
    param([Parameter(Mandatory)][AllowEmptyString()][string]$Value)

    if ($Value -notmatch '[\s"]') {
        return $Value
    }

    return '"' + ($Value -replace '"', '\"') + '"'
}

function Add-PathEntry {
    param(
        [Parameter(Mandatory)][string]$BinDir,
        [Parameter(Mandatory)][ValidateSet('User', 'Machine')][string]$Scope
    )

    $current = [Environment]::GetEnvironmentVariable('Path', $Scope)
    $entries = @()
    if (-not [string]::IsNullOrWhiteSpace($current)) {
        $entries = $current -split ';' |
            Where-Object { -not [string]::IsNullOrWhiteSpace($_) }
    }

    $normalizedBin = [IO.Path]::GetFullPath($BinDir).TrimEnd('\')
    $alreadyPresent = $false
    foreach ($entry in $entries) {
        try {
            $normalizedEntry = [IO.Path]::GetFullPath($entry).TrimEnd('\')
        }
        catch {
            $normalizedEntry = $entry.TrimEnd('\')
        }

        if ($normalizedEntry.Equals($normalizedBin, [StringComparison]::OrdinalIgnoreCase)) {
            $alreadyPresent = $true
            break
        }
    }

    if ($alreadyPresent) {
        Write-Host "PATH already contains $BinDir ($Scope)."
        return
    }

    $newPath = if ([string]::IsNullOrWhiteSpace($current)) {
        $BinDir
    }
    else {
        $current.TrimEnd(';') + ';' + $BinDir
    }

    [Environment]::SetEnvironmentVariable('Path', $newPath, $Scope)
    Write-Host "Added $BinDir to $Scope PATH. Open a new terminal to pick it up."
}

function Find-BuiltCsvzallExe {
    param(
        [Parameter(Mandatory)][string]$BuildDir,
        [Parameter(Mandatory)][string]$Config
    )

    $candidates = @(
        (Join-Path $BuildDir 'csvzall.exe'),
        (Join-Path (Join-Path $BuildDir $Config) 'csvzall.exe'),
        (Join-Path (Join-Path $BuildDir 'RelWithDebInfo') 'csvzall.exe'),
        (Join-Path (Join-Path $BuildDir 'Release') 'csvzall.exe'),
        (Join-Path (Join-Path $BuildDir 'Debug') 'csvzall.exe')
    )

    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) {
            return (Resolve-FullPath $candidate)
        }
    }

    $found = Get-ChildItem -Path $BuildDir -Recurse -Filter csvzall.exe -File |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1

    if ($null -ne $found) {
        return $found.FullName
    }

    return $null
}

function Install-CsvzallExe {
    param(
        [Parameter(Mandatory)][string]$SourceExe,
        [Parameter(Mandatory)][string]$BinDir
    )

    if (-not (Test-Path $SourceExe)) {
        throw "Built executable not found: $SourceExe"
    }

    New-Item -ItemType Directory -Force -Path $BinDir | Out-Null
    Copy-Item -LiteralPath $SourceExe -Destination (Join-Path $BinDir 'csvzall.exe') -Force
    Write-Host "Copied $SourceExe to $(Join-Path $BinDir 'csvzall.exe')"
}

function Invoke-CommandChecked {
    param(
        [Parameter(Mandatory)][string]$FilePath,
        [Parameter(Mandatory)][string[]]$Arguments
    )

    Write-Host "> $FilePath $($Arguments -join ' ')"
    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code ${LASTEXITCODE}: $FilePath"
    }
}

function Stop-InstallTranscript {
    if ($script:transcriptStarted) {
        Stop-Transcript | Out-Null
        $script:transcriptStarted = $false
    }
}

$SourceDir = Resolve-FullPath $SourceDir
if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $recentBuildDir = Find-MostRecentCMakeBuildDir -SourceDir $SourceDir
    if ($null -ne $recentBuildDir) {
        $BuildDir = $recentBuildDir
        Write-Host "Using most recent CMake build directory: $BuildDir"
    }
    else {
        $BuildDir = Join-Path $SourceDir 'out\build\install'
    }
}

$BuildDir = Resolve-FullPath $BuildDir
$InstallPrefix = Resolve-FullPath $InstallPrefix
$binDir = Join-Path $InstallPrefix 'bin'
if (-not [string]::IsNullOrWhiteSpace($CopyFromExe)) {
    $CopyFromExe = Resolve-FullPath $CopyFromExe
}

$programFilesRoots = @()
if ($env:ProgramFiles) {
    $programFilesRoots += (Resolve-FullPath $env:ProgramFiles)
}
if (${env:ProgramFiles(x86)}) {
    $programFilesRoots += (Resolve-FullPath ${env:ProgramFiles(x86)})
}

$installsUnderProgramFiles = $false
foreach ($root in $programFilesRoots) {
    if (Test-IsUnderPath -Path $InstallPrefix -Parent $root) {
        $installsUnderProgramFiles = $true
        break
    }
}

$effectivePathScope = $PathScope
if ($NoPath) {
    $effectivePathScope = 'None'
}
elseif ($PathScope -eq 'Auto') {
    $effectivePathScope = if ($installsUnderProgramFiles) { 'Machine' } else { 'User' }
}

$needsAdmin = $installsUnderProgramFiles -or ($effectivePathScope -eq 'Machine')
$isAdmin = Test-IsAdministrator

Write-Host "Source:  $SourceDir"
Write-Host "Build:   $BuildDir"
Write-Host "Install: $InstallPrefix"
Write-Host "PATH:    $effectivePathScope"
Write-Host "Log:     $LogPath"

if ([string]::IsNullOrWhiteSpace($CopyFromExe)) {
    New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null

    if (-not $SkipConfigure) {
        $cachePath = Join-Path $BuildDir 'CMakeCache.txt'
        $hasExistingCache = Test-Path $cachePath
        $existingGenerator = Get-CMakeCacheValue -BuildDir $BuildDir -Name 'CMAKE_GENERATOR'

        if ($hasExistingCache -and $ExtraConfigureArgs.Count -eq 0) {
            Write-Host "Existing CMake cache found; skipping configure to preserve generator: $existingGenerator"
        }
        else {
            $configureArgs = @(
                '-S', $SourceDir,
                '-B', $BuildDir,
                "-DCMAKE_BUILD_TYPE=$Config",
                '-DCSVZALL_CXX_STANDARD=23'
            ) + $ExtraConfigureArgs

            if (-not [string]::IsNullOrWhiteSpace($existingGenerator)) {
                Write-Host "Reusing existing CMake generator: $existingGenerator"
                $configureArgs = @('-G', $existingGenerator) + $configureArgs
            }

            Invoke-CommandChecked -FilePath $CMake -Arguments $configureArgs
        }
    }

    if (-not $SkipBuild) {
        Import-VisualStudioDevEnvironment -BuildDir $BuildDir
        Invoke-CommandChecked -FilePath $CMake -Arguments @(
            '--build', $BuildDir,
            '--config', $Config
        )
    }

    $CopyFromExe = Find-BuiltCsvzallExe -BuildDir $BuildDir -Config $Config
    if ([string]::IsNullOrWhiteSpace($CopyFromExe)) {
        throw "Could not find csvzall.exe under build directory '$BuildDir'."
    }
}
else {
    Write-Host "Finalizing install from built executable: $CopyFromExe"
}

if ($needsAdmin -and -not $isAdmin) {
    if ($NoElevate) {
        throw "Installing to '$InstallPrefix' or setting Machine PATH requires Administrator rights. Rerun elevated or use -InstallPrefix `"$env:LOCALAPPDATA\csvzall`" -PathScope User."
    }

    Write-Host "Administrator rights are required for copy/PATH. Relaunching just that step with a Windows UAC prompt..."

    $relayArgs = @(
        '-NoProfile',
        '-ExecutionPolicy', 'Bypass',
        '-File', $PSCommandPath,
        '-SourceDir', $SourceDir,
        '-BuildDir', $BuildDir,
        '-Config', $Config,
        '-InstallPrefix', $InstallPrefix,
        '-PathScope', $PathScope,
        '-CMake', $CMake,
        '-LogPath', $LogPath,
        '-CopyFromExe', $CopyFromExe,
        '-SkipConfigure',
        '-SkipBuild',
        '-NoElevate'
    )

    foreach ($arg in $ExtraConfigureArgs) {
        $relayArgs += '-ExtraConfigureArgs'
        $relayArgs += $arg
    }
    if ($NoPath) { $relayArgs += '-NoPath' }

    $quotedRelayArgs = $relayArgs | ForEach-Object { Quote-Argument $_ }
    $powerShellExe = (Get-Process -Id $PID).Path
    $process = Start-Process -FilePath $powerShellExe `
        -ArgumentList ($quotedRelayArgs -join ' ') `
        -Verb RunAs `
        -Wait `
        -PassThru

    if ($process.ExitCode -ne 0) {
        Write-Error "Elevated install failed with exit code $($process.ExitCode). See log: $LogPath"
        if (Test-Path $LogPath) {
            Write-Host "Last install log lines:"
            Get-Content $LogPath -Tail 40
        }
        exit $process.ExitCode
    }
    Write-Host "csvzall install complete. Open a new terminal to pick up PATH changes."
    Stop-InstallTranscript
    exit 0
}

Install-CsvzallExe -SourceExe $CopyFromExe -BinDir $binDir

if ($effectivePathScope -ne 'None') {
    Add-PathEntry -BinDir $binDir -Scope $effectivePathScope
}

Write-Host "csvzall install complete: $(Join-Path $binDir 'csvzall.exe')"

Stop-InstallTranscript
