#Requires -Version 5.1
<#
.SYNOPSIS
Validates the recorded Sunshine/Moonlight pair and shared dependency checkout.
.DESCRIPTION
Checks committed pins, actual checkouts, repository URLs, and foundation source
baselines. This read-only check does not build binaries or exercise streaming.
.PARAMETER HostRoot
Sunshine checkout. Defaults to the parent directory of this script.
.PARAMETER ClientRoot
Moonlight Qt checkout. Defaults to the sibling moonlightCustom directory.
#>
[CmdletBinding()]
param(
    [string]$HostRoot,
    [string]$ClientRoot
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
# Resolve script-relative defaults after parameter binding has initialized $PSScriptRoot.
if ([string]::IsNullOrWhiteSpace($HostRoot)) {
    $HostRoot = (Split-Path -Parent $PSScriptRoot)
}

function Invoke-PairGit {
    param([string]$Repository, [string[]]$GitArguments)
    $result = @(& git -C $Repository @GitArguments)
    if ($LASTEXITCODE -ne 0) {
        throw "Git check failed in ${Repository}: $($GitArguments -join ' ')"
    }
    return ($result -join "`n").Trim()
}

function Assert-PairEqual {
    param([string]$Actual, [string]$Expected, [string]$Label)
    if ($Actual -cne $Expected) {
        throw "${Label}: expected '$Expected', got '$Actual'"
    }
}

function Assert-PairSubmodule {
    param([string]$Repository, [string]$SubmodulePath, [string]$ExpectedCommit, [string]$ExpectedUrl)
    $entry = Invoke-PairGit $Repository @('ls-tree', 'HEAD', '--', $SubmodulePath)
    if ($entry -notmatch '^160000 commit ([0-9a-f]{40})\t') {
        throw "Missing committed submodule: $Repository/$SubmodulePath"
    }
    Assert-PairEqual $Matches[1] $ExpectedCommit "Committed pin for $SubmodulePath"
    $url = Invoke-PairGit $Repository @('config', '-f', '.gitmodules', '--get', "submodule.$SubmodulePath.url")
    Assert-PairEqual $url $ExpectedUrl "Recorded URL for $SubmodulePath"
    $checkout = Join-Path $Repository $SubmodulePath
    if (-not (Test-Path -LiteralPath (Join-Path $checkout '.git'))) {
        throw "Submodule is not initialized: $checkout"
    }
    Assert-PairEqual (Invoke-PairGit $checkout @('rev-parse', 'HEAD')) $ExpectedCommit "Checkout for $SubmodulePath"
    Assert-PairEqual (Invoke-PairGit $checkout @('remote', 'get-url', 'origin')) $ExpectedUrl "Fetch URL for $SubmodulePath"
    Assert-PairEqual (Invoke-PairGit $checkout @('status', '--porcelain', '--untracked-files=all')) '' "Clean dependency $SubmodulePath"
}

function Assert-PairBaseline {
    param([string]$Repository, [string]$BaseCommit, [string[]]$Paths)
    $null = Invoke-PairGit $Repository @('merge-base', '--is-ancestor', $BaseCommit, 'HEAD')
    $null = Invoke-PairGit $Repository (@('diff', '--exit-code', '--quiet', $BaseCommit, '--') + $Paths)
    $untracked = Invoke-PairGit $Repository (@('ls-files', '--others', '--exclude-standard', '--') + $Paths)
    Assert-PairEqual $untracked '' "Untracked source files in $Repository"
}

$HostRoot = (Resolve-Path -LiteralPath $HostRoot).Path
if ([string]::IsNullOrWhiteSpace($ClientRoot)) {
    $ClientRoot = Join-Path (Split-Path -Parent $HostRoot) 'moonlightCustom'
}
$ClientRoot = (Resolve-Path -LiteralPath $ClientRoot).Path
$hostRecord = Get-Content -LiteralPath (Join-Path $HostRoot 'docs/custom/compatibility.json') -Raw -Encoding UTF8 | ConvertFrom-Json
$clientRecord = Get-Content -LiteralPath (Join-Path $ClientRoot 'docs/custom/compatibility.json') -Raw -Encoding UTF8 | ConvertFrom-Json
Assert-PairEqual ([string]$hostRecord.schema_version) '1' 'Host record version'
Assert-PairEqual ([string]$clientRecord.schema_version) '1' 'Client record version'
Assert-PairEqual (Invoke-PairGit $HostRoot @('remote', 'get-url', 'origin')) $clientRecord.host_repository 'Host repository'
Assert-PairEqual (Invoke-PairGit $ClientRoot @('rev-parse', 'HEAD')) $hostRecord.client_commit 'Recorded client revision'
Assert-PairEqual (Invoke-PairGit $ClientRoot @('remote', 'get-url', 'origin')) $hostRecord.client_repository 'Client repository'
Assert-PairEqual $clientRecord.common_repository $hostRecord.common_repository 'Shared repository'
Assert-PairEqual $clientRecord.common_commit $hostRecord.common_commit 'Shared pin'
Assert-PairEqual $clientRecord.common_code_base $hostRecord.common_code_base 'Shared code baseline'
Assert-PairSubmodule $HostRoot 'third-party/moonlight-common-c' $hostRecord.common_commit $hostRecord.common_repository
Assert-PairSubmodule $ClientRoot 'moonlight-common-c/moonlight-common-c' $hostRecord.common_commit $hostRecord.common_repository

if ($hostRecord.phase -eq 'foundation') {
    Assert-PairBaseline $HostRoot $hostRecord.host_upstream_base @('src', 'src_assets', 'tests', ':(exclude)tests/custom/**', 'cmake', 'CMakeLists.txt')
    Assert-PairBaseline $ClientRoot $clientRecord.client_upstream_base @('app', 'moonlight-common-c/moonlight-common-c.pro', 'globaldefs.pri', 'moonlight-qt.pro', 'config.tests')
    Assert-PairBaseline (Join-Path $HostRoot 'third-party/moonlight-common-c') $hostRecord.common_code_base @('src', 'cmake', 'CMakeLists.txt')
}

Write-Output "Pair pins verified: client $($hostRecord.client_commit), common $($hostRecord.common_commit)"
Write-Output "Clipboard: $($hostRecord.clipboard_status); native build: $($hostRecord.native_build_status); end-to-end: $($hostRecord.end_to_end_status)"
