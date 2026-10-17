[CmdletBinding()]
param(
    [ValidateSet('FactoryEditor','FactoryGameSteam')][string]$Target = 'FactoryEditor',
    [string]$SmlRoot = 'F:/dev/satisfactory_mods/SatisfactoryModLoader',
    [string]$EngineRoot = 'C:/Program Files/Unreal Engine - CSS',
    [string]$CompilerRoot = 'C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.38.33130',
    [ValidateRange(1, 4)][int]$CompilerWorkers = 2
)
# Use the existing SDK binaries. Access-friend declarations do not change their
# ABI. UBT prepares reflection and response files without executing or deleting
# SDK build products; only this plugin's C++ and DLL are compiled below.
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$config = if ($Target -eq 'FactoryEditor') { 'Development' } else { 'Shipping' }
$binary = if ($Target -eq 'FactoryEditor') { 'UnrealEditor' } else { $Target }
$suffix = if ($Target -eq 'FactoryEditor') { '' } else { '-Win64-Shipping' }
$plugin = Join-Path $SmlRoot 'Mods/GameFeatures/BuildingSkills'
$intermediate = Join-Path $plugin "Intermediate/Build/Win64/x64/$binary/$config/BuildingSkills"
$saved = Join-Path $repo "Saved/NativeBuild/$Target"
New-Item -ItemType Directory -Path $saved -Force | Out-Null
if (Get-Process -Name UnrealEditor,UnrealEditor-Cmd -ErrorAction SilentlyContinue) { throw 'Close Unreal Editor before compiling.' }
$previousModules = $env:PSModulePath
try {
    $env:PSModulePath = Join-Path $env:SystemRoot 'System32/WindowsPowerShell/v1.0/Modules'
    & (Join-Path $EngineRoot 'Engine/Build/BatchFiles/Build.bat') $Target Win64 $config "-Project=$SmlRoot/FactoryGame.uproject" -WaitMutex -NoHotReloadFromIDE -NoUBA -SkipBuild
    if ($LASTEXITCODE -ne 0) { throw 'Could not prepare native build files.' }
} finally { $env:PSModulePath = $previousModules }
$compiler = Join-Path $CompilerRoot 'bin/Hostx64/x64/cl.exe'
$linker = Join-Path $CompilerRoot 'bin/Hostx64/x64/link.exe'
$dllRsp = Join-Path $intermediate "$binary-BuildingSkills$suffix.dll.rsp"
$linkText = Get-Content -LiteralPath $dllRsp -Raw
Push-Location (Join-Path $EngineRoot 'Engine/Source')
try {
# An earlier interrupted full SDK build can leave import libraries missing.
# Re-create imports from unchanged cached objects in Saved, never in the SDK.
foreach ($entry in @(@('FactoryGame','Intermediate'), @('SML','Mods/SML/Intermediate'))) {
    $name = $entry[0]
    $directory = Join-Path $SmlRoot "$($entry[1])/Build/Win64/x64/$binary/$config/$name"
    $source = Join-Path $directory "$binary-$name$suffix.lib"
    if (Test-Path -LiteralPath $source) { continue }
    $localLib = Join-Path $saved "$binary-$name$suffix.lib"
    if (-not (Test-Path -LiteralPath $localLib)) {
        $text = Get-Content -LiteralPath "$source.rsp" -Raw
        $text = [regex]::Replace($text, '(?m)^/OUT:.*$', ('/OUT:"' + $localLib + '"'))
        $localRsp = Join-Path $saved "$name-import.rsp"
        [IO.File]::WriteAllText($localRsp, $text)
        & $linker /LIB "@$localRsp"
        if ($LASTEXITCODE -ne 0) { throw "Could not generate local $name import library." }
    }
    $linkText = $linkText.Replace($source, $localLib).Replace($source.Replace('\','/'),$localLib)
}
$pluginPrefix = [IO.Path]::GetFullPath($plugin).Replace('\','/').TrimEnd('/') + '/'
# Each prepared response file owns distinct .obj/.dep outputs. Compile the
# whole batch before reporting failures; never link a mixture of old/new objects.
$objects = @([regex]::Matches($linkText, '(?m)^"([^"]+\.cpp\.obj)"') | ForEach-Object {
    $path = $_.Groups[1].Value
    if ($path.Replace('\','/').StartsWith($pluginPrefix,[StringComparison]::OrdinalIgnoreCase)) { $path }
})
if ($objects.Count -eq 0) { throw 'No plugin translation units found; refusing to link stale objects.' }
& (Join-Path $PSScriptRoot 'CompileUnits.ps1') -Compiler $compiler -ResponseFiles @($objects | ForEach-Object { $_ + '.rsp' }) -WorkingDirectory (Get-Location).Path -OutputDirectory $saved -Workers $CompilerWorkers
& $linker /LIB "@$intermediate/$binary-BuildingSkills$suffix.lib.rsp"
if ($LASTEXITCODE -ne 0) { throw 'Plugin import library failed.' }
$localLink = Join-Path $saved 'PluginLink.rsp'
[IO.File]::WriteAllText($localLink, $linkText)
& $linker "@$localLink"
if ($LASTEXITCODE -ne 0) { throw 'Plugin DLL link failed.' }
Write-Host "Building Skills $Target compiled against the existing SDK binaries."

} finally { Pop-Location }
