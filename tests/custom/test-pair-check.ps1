#Requires -Version 5.1
<#
.SYNOPSIS
Runs integration checks against isolated local Git fixtures for the pair checker.
.DESCRIPTION
Uses existing local repositories as read-only Git object sources. No network,
build tools, or Pester installation is needed. Developer checkouts are unchanged.
.PARAMETER HostRoot
Sunshine checkout used as a fixture source.
.PARAMETER ClientRoot
Moonlight Qt checkout used as a fixture source.
#>
[CmdletBinding()]
param(
    [string]$HostRoot = (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)),
    [string]$ClientRoot
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$HostRoot = (Resolve-Path -LiteralPath $HostRoot).Path
if ([string]::IsNullOrWhiteSpace($ClientRoot)) {
    $ClientRoot = Join-Path (Split-Path -Parent $HostRoot) 'moonlightCustom'
}
$ClientRoot = (Resolve-Path -LiteralPath $ClientRoot).Path
$checker = Join-Path $HostRoot 'scripts/check-custom-pair.ps1'
$record = Get-Content -LiteralPath (Join-Path $HostRoot 'docs/custom/compatibility.json') -Raw -Encoding UTF8 | ConvertFrom-Json
$clientRecord = Get-Content -LiteralPath (Join-Path $ClientRoot 'docs/custom/compatibility.json') -Raw -Encoding UTF8 | ConvertFrom-Json
$tempParent = [System.IO.Path]::GetFullPath([System.IO.Path]::GetTempPath())
$fixtureRoot = Join-Path $tempParent ('sunshine-pair-test-' + [guid]::NewGuid().ToString('N'))
$fixtureHost = Join-Path $fixtureRoot 'host'
$fixtureClient = Join-Path $fixtureRoot 'client'
$passed = 0

function Invoke-FixtureGit {
    param([string[]]$GitArguments)
    $null = & git @GitArguments 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "Fixture Git command failed: $($GitArguments -join ' ')"
    }
}

function Assert-CheckerRejects {
    param([string]$ExpectedMessage, [string]$Name)
    $failure = $null
    try {
        $null = & $checker -HostRoot $fixtureHost -ClientRoot $fixtureClient
    } catch {
        $failure = $_.Exception.Message
    }
    if (-not $failure -or -not $failure.Contains($ExpectedMessage)) {
        throw "${Name}: expected rejection '$ExpectedMessage', got '$failure'"
    }
    Write-Output "PASS: $Name"
}

try {
    $null = New-Item -ItemType Directory -Path $fixtureRoot
    Invoke-FixtureGit @('clone', '--quiet', '--shared', $HostRoot, $fixtureHost)
    Invoke-FixtureGit @('clone', '--quiet', '--shared', $ClientRoot, $fixtureClient)
    Invoke-FixtureGit @('-C', $fixtureHost, 'remote', 'set-url', 'origin', $clientRecord.host_repository)
    Invoke-FixtureGit @('-C', $fixtureClient, 'remote', 'set-url', 'origin', $record.client_repository)
    $fixtureHostCommon = Join-Path $fixtureHost 'third-party/moonlight-common-c'
    $fixtureClientCommon = Join-Path $fixtureClient 'moonlight-common-c/moonlight-common-c'
    Invoke-FixtureGit @('clone', '--quiet', '--shared', (Join-Path $HostRoot 'third-party/moonlight-common-c'), $fixtureHostCommon)
    Invoke-FixtureGit @('clone', '--quiet', '--shared', (Join-Path $ClientRoot 'moonlight-common-c/moonlight-common-c'), $fixtureClientCommon)
    foreach ($checkout in @($fixtureHostCommon, $fixtureClientCommon)) {
        Invoke-FixtureGit @('-C', $checkout, 'checkout', '--quiet', '--detach', $record.common_commit)
        Invoke-FixtureGit @('-C', $checkout, 'remote', 'set-url', 'origin', $record.common_repository)
    }

    $null = & $checker -HostRoot $fixtureHost -ClientRoot $fixtureClient
    Write-Output 'PASS: matching pair'
    $passed++

    Invoke-FixtureGit @('-C', $fixtureClientCommon, 'checkout', '--quiet', '--detach', $record.common_code_base)
    Assert-CheckerRejects 'Checkout for moonlight-common-c/moonlight-common-c' 'mismatched shared checkout'
    $passed++
    Invoke-FixtureGit @('-C', $fixtureClientCommon, 'checkout', '--quiet', '--detach', $record.common_commit)

    $mismatchedRecord = Get-Content -LiteralPath (Join-Path $fixtureClient 'docs/custom/compatibility.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    $mismatchedRecord.common_commit = $record.common_code_base
    $mismatchedRecord | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $fixtureClient 'docs/custom/compatibility.json') -Encoding UTF8
    Assert-CheckerRejects 'Shared pin' 'mismatched dependency records'
    $passed++
    Invoke-FixtureGit @('-C', $fixtureClient, 'restore', '--', 'docs/custom/compatibility.json')

    Invoke-FixtureGit @('-C', $fixtureClientCommon, 'remote', 'set-url', 'origin', 'https://github.com/moonlight-stream/moonlight-common-c.git')
    Assert-CheckerRejects 'Fetch URL for moonlight-common-c/moonlight-common-c' 'wrong shared fetch URL'
    $passed++
    Invoke-FixtureGit @('-C', $fixtureClientCommon, 'remote', 'set-url', 'origin', $record.common_repository)

    Add-Content -LiteralPath (Join-Path $fixtureHostCommon 'src/Input.h') -Value '// Fixture-only dirty change'
    Assert-CheckerRejects 'Clean dependency third-party/moonlight-common-c' 'dirty shared dependency'
    $passed++
    Invoke-FixtureGit @('-C', $fixtureHostCommon, 'restore', '--', 'src/Input.h')

    # Exercise the foundation guard against this fixture's own baseline even after implementation starts.
    $fixtureHostRecordPath = Join-Path $fixtureHost 'docs/custom/compatibility.json'
    $fixtureClientRecordPath = Join-Path $fixtureClient 'docs/custom/compatibility.json'
    $fixtureHostRecord = Get-Content -LiteralPath $fixtureHostRecordPath -Raw | ConvertFrom-Json
    $fixtureClientRecord = Get-Content -LiteralPath $fixtureClientRecordPath -Raw | ConvertFrom-Json
    $fixtureHostRecord.phase = 'foundation'
    $fixtureHostRecord.host_upstream_base = (& git -C $fixtureHost rev-parse HEAD).Trim()
    $fixtureClientRecord.client_upstream_base = (& git -C $fixtureClient rev-parse HEAD).Trim()
    $fixtureHostRecord.common_code_base = $record.common_commit
    $fixtureClientRecord.common_code_base = $record.common_commit
    $fixtureHostRecord | ConvertTo-Json | Set-Content -LiteralPath $fixtureHostRecordPath
    $fixtureClientRecord | ConvertTo-Json | Set-Content -LiteralPath $fixtureClientRecordPath
    Add-Content -LiteralPath (Join-Path $fixtureClient 'app/streaming/session.cpp') -Value '// Fixture-only source change'
    Assert-CheckerRejects 'Git check failed' 'unexpected foundation source change'
    $passed++
    Invoke-FixtureGit @('-C', $fixtureClient, 'restore', '--', 'app/streaming/session.cpp')

    Set-Content -LiteralPath (Join-Path $fixtureClient 'app/custom-fixture-only.cpp') -Value '// Fixture-only untracked source'
    Assert-CheckerRejects 'Untracked source files' 'untracked foundation source'
    $passed++

    Write-Output "$passed pair checks passed"
} finally {
    $resolvedFixture = [System.IO.Path]::GetFullPath($fixtureRoot)
    $expectedPrefix = $tempParent.TrimEnd('\', '/') + [System.IO.Path]::DirectorySeparatorChar
    if (-not $resolvedFixture.StartsWith($expectedPrefix, [System.StringComparison]::OrdinalIgnoreCase) -or
        -not [System.IO.Path]::GetFileName($resolvedFixture).StartsWith('sunshine-pair-test-')) {
        throw "Refusing to remove unexpected fixture path: $resolvedFixture"
    }
    if (Test-Path -LiteralPath $resolvedFixture) {
        Remove-Item -LiteralPath $resolvedFixture -Recurse -Force
    }
}
