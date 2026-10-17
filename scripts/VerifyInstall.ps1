[CmdletBinding()]
param(
    [string]$SmlRoot = 'F:\dev\satisfactory_mods\SatisfactoryModLoader',
    [string]$GameRoot = 'F:\Programme et jeux\Steam\steamapps\common\Satisfactory',
    [string]$ArchivePath
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not $ArchivePath) { $ArchivePath = Join-Path $SmlRoot 'Saved\ArchivedPlugins\BuildingSkills\BuildingSkills-Windows.zip' }
$installedRoot = [IO.Path]::GetFullPath((Join-Path $GameRoot 'FactoryGame\Mods\GameFeatures\BuildingSkills'))
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [IO.Compression.ZipFile]::OpenRead($archivePath)
$verified = @()
try {
    $toc = @($archive.Entries | Where-Object { $_.FullName.EndsWith('.utoc') })
    if ($toc.Count -ne 1) { throw 'Expected one cooked asset container in the package.' }
    $tocStream = $toc[0].Open()
    $tocBytes = New-Object IO.MemoryStream
    try { $tocStream.CopyTo($tocBytes); $tocText = [Text.Encoding]::UTF8.GetString($tocBytes.ToArray()) }
    finally { $tocStream.Dispose(); $tocBytes.Dispose() }
    # The unencrypted IoStore directory index contains each cooked filename.
    # Hash equality alone also accepts a faithfully installed empty package.
    foreach ($name in @('BuildingSkills.uasset', 'RootGameWorld_BuildingSkills.uasset', 'RootGameInstance_BuildingSkills.uasset',
        'MC_BuildingSkills.uasset', 'MC_EditOperations.uasset', 'IA_ToggleEditMode.uasset')) {
        if (-not $tocText.Contains($name)) { throw "The package omitted required cooked content: $name" }
    }
    foreach ($entry in $archive.Entries) {
        if (-not $entry.Name) { continue }
        $target = [IO.Path]::GetFullPath((Join-Path $installedRoot $entry.FullName))
        if (-not $target.StartsWith($installedRoot.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) {
            throw "Archive entry is outside the plugin directory: $($entry.FullName)"
        }
        $stream = $entry.Open()
        $sha = [Security.Cryptography.SHA256]::Create()
        try { $expected = [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-', '') }
        finally { $stream.Dispose(); $sha.Dispose() }
        $actual = (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash
        if ($actual -ne $expected) { throw "Installed file differs from the package: $($entry.FullName)" }
        $verified += [ordered]@{ Path = $entry.FullName; SHA256 = $actual }
    }
} finally { $archive.Dispose() }
if ($verified.Count -eq 0) { throw 'The package contains no files.' }
$receipt = [ordered]@{
    Name = 'Building Skills'
    VerifiedAtUtc = [DateTime]::UtcNow.ToString('o')
    FilesVerified = $verified.Count
    Files = $verified
    PackageSHA256 = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash
}
$saved = Join-Path $repoRoot 'Saved'
New-Item -ItemType Directory -Path $saved -Force | Out-Null
$receipt | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $saved 'InstallationVerification.json') -Encoding utf8
Write-Host "Verified $($verified.Count) installed files against the package by SHA-256."
