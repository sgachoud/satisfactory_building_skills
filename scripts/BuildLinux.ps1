[CmdletBinding()]
param(
    [string]$SmlRoot = 'F:/dev/satisfactory_mods/SatisfactoryModLoader',
    [string]$EngineRoot = 'C:/Program Files/Unreal Engine - CSS'
)
$ErrorActionPreference = 'Stop'
$bsPlugin = Join-Path $SmlRoot 'Mods/GameFeatures/BuildingSkills'
$bsIntermediate = Join-Path $bsPlugin 'Intermediate/Build/Linux/x64/FactoryServer/Shipping/BuildingSkills'
$bsPreviousModules = $env:PSModulePath
try {
    $env:PSModulePath = Join-Path $env:SystemRoot 'System32/WindowsPowerShell/v1.0/Modules'
    & (Join-Path $EngineRoot 'Engine/Build/BatchFiles/Build.bat') FactoryServer Linux Shipping "-Project=$SmlRoot/FactoryGame.uproject" -WaitMutex -NoHotReloadFromIDE -NoUBA -SkipBuild
    if ($LASTEXITCODE -ne 0) { throw 'Could not prepare Linux native build files.' }
} finally { $env:PSModulePath = $bsPreviousModules }
$bsLinkScript = Join-Path $bsIntermediate 'Link-libFactoryServer-BuildingSkills-Linux-Shipping.so.bat'
$bsLinkText = Get-Content -LiteralPath $bsLinkScript -Raw
$bsCompilerMatch = [regex]::Match($bsLinkText, '(?m)^"([^"]+/clang\+\+\.exe)"')
if (-not $bsCompilerMatch.Success) { throw 'Could not identify the configured Linux compiler.' }
$bsCompiler = $bsCompilerMatch.Groups[1].Value
Push-Location (Join-Path $EngineRoot 'Engine/Source')
try {
    $bsResponses = @(Get-ChildItem -LiteralPath $bsIntermediate -Filter '*.cpp.o.rsp')
    if ($bsResponses.Count -eq 0) { throw 'No plugin C++ translation units found.' }
    foreach ($bsResponse in $bsResponses) {
        & $bsCompiler "@$($bsResponse.FullName)"
        if ($LASTEXITCODE -ne 0) { throw "Linux compilation failed: $($bsResponse.Name)" }
    }
    # UBT's generated linker script touches only this plugin's binary/debug files.
    & $bsLinkScript
    if ($LASTEXITCODE -ne 0) { throw 'Linux plugin linking failed.' }
} finally { Pop-Location }
Write-Host 'Building Skills Linux Server compiled against the existing SDK binaries.'
