[CmdletBinding()]
param(
    [string]$GameRoot = 'F:\Programme et jeux\Steam\steamapps\common\Satisfactory',
    [switch]$Render
)

$ErrorActionPreference = 'Stop'
if (Get-Process -Name 'FactoryGameSteam-Win64-Shipping' -ErrorAction SilentlyContinue) {
    throw 'Close Satisfactory before running the isolated runtime test.'
}
$repoRoot = Split-Path -Parent $PSScriptRoot
$saved = Join-Path $repoRoot 'Saved'
New-Item -ItemType Directory -Path $saved -Force | Out-Null
$log = Join-Path $saved 'RuntimeTest.log'
$exe = Join-Path $GameRoot 'Engine\Binaries\Win64\FactoryGameSteam-Win64-Shipping.exe'
$arguments = @('FactoryGame', '-BuildingSkillsRuntimeTest', '-unattended', '-nosound', '-nosplash', '-NO_EOS_OVERLAY', ('-abslog="' + $log + '"'))
if ($Render) { $arguments += @('-RenderOffscreen', '-windowed', '-ResX=1600', '-ResY=900', '-ForceRes') }
else { $arguments += '-nullrhi' }
$previousSteamAppId = $env:SteamAppId
$started = Get-Date
try {
    $env:SteamAppId = '526870'
    $testProcess = Start-Process -FilePath $exe -ArgumentList $arguments -WindowStyle Hidden -PassThru
} finally { $env:SteamAppId = $previousSteamAppId }
while (-not $testProcess.WaitForExit(30000)) {
    if ((Get-Date) - $started -gt [TimeSpan]::FromMinutes(6)) {
        Stop-Process -Id $testProcess.Id
        throw "Runtime test timed out. See $log"
    }
}
if ($Render) {
    $screenshot = Join-Path $env:LOCALAPPDATA 'FactoryGame\Saved\BuildingSkillsRuntimeTest.png'
    if ((Test-Path -LiteralPath $screenshot) -and (Get-Item -LiteralPath $screenshot).LastWriteTime -ge $started) {
        Copy-Item -LiteralPath $screenshot -Destination (Join-Path $saved 'RuntimeTest.png') -Force
    } else { throw 'The runtime test did not produce a fresh screenshot.' }
    $transformScreenshot = Join-Path $env:LOCALAPPDATA 'FactoryGame\Saved\BuildingSkillsTransformTest.png'
    if ((Test-Path -LiteralPath $transformScreenshot) -and (Get-Item -LiteralPath $transformScreenshot).LastWriteTime -ge $started) {
        Copy-Item -LiteralPath $transformScreenshot -Destination (Join-Path $saved 'TransformTest.png') -Force
    } else { throw 'The runtime test did not produce a fresh transform screenshot.' }
}
if ((Get-Item -LiteralPath $log).LastWriteTime -lt $started) { throw 'Runtime test log is stale.' }
Select-String -LiteralPath $log -Pattern 'RUNTIME (TEST|EDIT|ASSERT|TARGET|UI|MAPPING)' | ForEach-Object { $_.Line }
if ($testProcess.ExitCode -ne 0 -or -not (Select-String -LiteralPath $log -SimpleMatch 'RUNTIME EDIT CHECK: PASS' -Quiet)) {
    throw "Runtime test failed. See $log"
}
