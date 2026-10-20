[CmdletBinding()]
param(
    [string]$GameRoot = 'F:\Programme et jeux\Steam\steamapps\common\Satisfactory',
    [switch]$Render,
    [switch]$NetworksOnly,
    [string]$BlueprintFixture,
    [switch]$ConnectedOnly,
    [switch]$VolumeOnly,
    [switch]$StickOnly,
    [switch]$PlanOnly,
    [switch]$PolishOnly,
    [switch]$TransformersOnly,
    [switch]$ARSDoubleTest,
    [string]$ARSPresetDirectory,
    [switch]$BlueprintsOnly,
    [switch]$PooledVisualsOnly,
    [ValidateRange(1, 30)]
    [int]$TimeoutMinutes = 6
)

$ErrorActionPreference = 'Stop'
if ($PooledVisualsOnly -and -not $Render) { throw 'The pooled visual regression requires -Render.' }
if ($StickOnly -and -not $Render) { throw 'The Stick regression requires -Render for its temporary numeric settings and endpoint-tilt checks.' }
if (Get-Process -Name 'FactoryGameSteam-Win64-Shipping' -ErrorAction SilentlyContinue) {
    throw 'Close Satisfactory before running the isolated runtime test.'
}
if (-not (Get-Process -Name steam -ErrorAction SilentlyContinue)) {
    throw 'Start Steam before running the isolated runtime test; native menu initialization requires the Steam client.'
}
$repoRoot = Split-Path -Parent $PSScriptRoot
$saved = Join-Path $repoRoot 'Saved'
New-Item -ItemType Directory -Path $saved -Force | Out-Null
$log = Join-Path $saved 'RuntimeTest.log'
$exe = Join-Path $GameRoot 'Engine\Binaries\Win64\FactoryGameSteam-Win64-Shipping.exe'
$arguments = @('FactoryGame', '-BuildingSkillsRuntimeTest', '-unattended', '-nosound', '-nosplash', '-NO_EOS_OVERLAY', ('-abslog="' + $log + '"'))
# Keep the isolated test independent of Steam/EOS overlay injection. These are
# process-local engine config overrides; no installed or user INI is edited.
$arguments += '-ini:Engine:[EOSSDK.Platform.Live]:bDisableOverlay=True,[EOSSDK.Platform.Staging]:bDisableOverlay=True,[EOSSDK.Platform.Development]:bDisableOverlay=True,[/Script/OnlineSubsystemEOS.EOSSettings]:bEnableOverlay=False,[/Script/OnlineSubsystemEOS.EOSSettings]:bEnableSocialOverlay=False'
if ($ConnectedOnly) { $arguments += '-BuildingSkillsConnectedTest' }
if ($VolumeOnly) { $arguments += '-BuildingSkillsVolumeTest' }
if ($StickOnly) { $arguments += '-BuildingSkillsStickTest' }
if ($PlanOnly) { $arguments += '-BuildingSkillsPlanTest' }
if ($TransformersOnly) { $arguments += '-BuildingSkillsTransformersTest' }
if ($PolishOnly) { $arguments += '-BuildingSkillsPolishTest' }
if ($ARSDoubleTest) { $arguments += '-BuildingSkillsARSDoubleTest' }
if ($ARSPresetDirectory) { $arguments += ('-BuildingSkillsARSPreset="' + $ARSPresetDirectory + '"') }
if ($NetworksOnly) { $arguments += '-BuildingSkillsNetworkTest' }
if ($BlueprintFixture) {
    $fixtureFile = (Resolve-Path -LiteralPath $BlueprintFixture).Path
    $arguments += @('-BuildingSkillsBlueprintNetworkTest', ('-BuildingSkillsBlueprintFixture="' + $fixtureFile + '"'))
}
if ($BlueprintsOnly) { $arguments += '-BuildingSkillsBlueprintTest' }
if ($PooledVisualsOnly) { $arguments += '-BuildingSkillsPooledVisualTest' }
if ($Render) { $arguments += @('-RenderOffscreen', '-windowed', '-ResX=1600', '-ResY=900', '-ForceRes') }
else { $arguments += '-nullrhi' }
$previousSteamAppId = $env:SteamAppId
$started = Get-Date
try {
    $env:SteamAppId = '526870'
    $testProcess = Start-Process -FilePath $exe -ArgumentList $arguments -WindowStyle Hidden -PassThru
} finally { $env:SteamAppId = $previousSteamAppId }
while (-not $testProcess.WaitForExit(30000)) {
    if ((Get-Date) - $started -gt [TimeSpan]::FromMinutes($TimeoutMinutes)) {
        Stop-Process -Id $testProcess.Id
        throw "Runtime test timed out. See $log"
    }
}
if ($Render -and -not $PlanOnly -and -not $BlueprintFixture -and -not $TransformersOnly -and -not $PolishOnly -and -not $StickOnly -and -not $VolumeOnly -and -not $ConnectedOnly -and -not $NetworksOnly -and -not $BlueprintsOnly -and -not $PooledVisualsOnly) {
    $screenshot = Join-Path $env:LOCALAPPDATA 'FactoryGame\Saved\BuildingSkillsRuntimeTest.png'
    if ((Test-Path -LiteralPath $screenshot) -and (Get-Item -LiteralPath $screenshot).LastWriteTime -ge $started) {
        Copy-Item -LiteralPath $screenshot -Destination (Join-Path $saved 'RuntimeTest.png') -Force
    } else { throw 'The runtime test did not produce a fresh screenshot.' }
    $transformScreenshot = Join-Path $env:LOCALAPPDATA 'FactoryGame\Saved\BuildingSkillsTransformTest.png'
    if ((Test-Path -LiteralPath $transformScreenshot) -and (Get-Item -LiteralPath $transformScreenshot).LastWriteTime -ge $started) {
        Copy-Item -LiteralPath $transformScreenshot -Destination (Join-Path $saved 'TransformTest.png') -Force
    } else { throw 'The runtime test did not produce a fresh transform screenshot.' }
}
if ($Render -and -not $PlanOnly -and -not $BlueprintFixture -and -not $TransformersOnly -and -not $PolishOnly -and -not $StickOnly -and -not $VolumeOnly -and -not $ConnectedOnly -and -not $NetworksOnly -and -not $PooledVisualsOnly) {
    $blueprintScreenshot = Join-Path $env:LOCALAPPDATA 'FactoryGame\Saved\BuildingSkillsBlueprintTest.png'
    if ((Test-Path -LiteralPath $blueprintScreenshot) -and (Get-Item -LiteralPath $blueprintScreenshot).LastWriteTime -ge $started) {
        Copy-Item -LiteralPath $blueprintScreenshot -Destination (Join-Path $saved 'BlueprintTest.png') -Force
    } else { throw 'The runtime test did not produce a fresh blueprint screenshot.' }
}
if ($Render -and -not $BlueprintFixture -and $TransformersOnly) {
    foreach ($name in @('Settings','Preview','BlueprintPreview')) {
        $image = Join-Path $env:LOCALAPPDATA ('FactoryGame/Saved/BuildingSkillsTransformers' + $name + '.png')
        if (-not (Test-Path -LiteralPath $image) -or (Get-Item -LiteralPath $image).LastWriteTime -lt $started) { throw "Missing fresh Transformers $name screenshot." }
        Copy-Item -LiteralPath $image -Destination (Join-Path $saved ('Transformers' + $name + '.png')) -Force
    }
}
if ($Render -and -not $BlueprintFixture -and $PolishOnly) {
    $image = Join-Path $env:LOCALAPPDATA 'FactoryGame\Saved\BuildingSkillsPolishPose.png'
    if (-not (Test-Path -LiteralPath $image) -or (Get-Item -LiteralPath $image).LastWriteTime -lt $started) { throw 'Missing fresh transform pose screenshot.' }
    Copy-Item -LiteralPath $image -Destination (Join-Path $saved 'PolishPose.png') -Force
}
if ($Render -and -not $BlueprintFixture -and $VolumeOnly) {
    foreach ($name in @('Settings','Preview','Sphere','Cube')) {
        $image = Join-Path $env:LOCALAPPDATA ('FactoryGame\Saved\BuildingSkillsVolume' + $name + '.png')
        if (-not (Test-Path -LiteralPath $image) -or (Get-Item -LiteralPath $image).LastWriteTime -lt $started) { throw "Missing fresh Volume $name screenshot." }
        Copy-Item -LiteralPath $image -Destination (Join-Path $saved ('Volume' + $name + '.png')) -Force
    }
}
if ($Render -and -not $BlueprintFixture -and $StickOnly) {
    foreach ($name in @('Hints','Settings')) {
        $image = Join-Path $env:LOCALAPPDATA ('FactoryGame\Saved\BuildingSkillsStick' + $name + '.png')
        if (-not (Test-Path -LiteralPath $image) -or (Get-Item -LiteralPath $image).LastWriteTime -lt $started) { throw "Missing fresh Stick $name screenshot." }
        Copy-Item -LiteralPath $image -Destination (Join-Path $saved ('Stick' + $name + '.png')) -Force
    }
}
if ($Render) {
    if ($PlanOnly) {
        foreach ($name in @('Settings', 'Place', 'Radial', 'BuildSpecification')) {
            $image = Join-Path $env:LOCALAPPDATA ("FactoryGame/Saved/BuildingSkillsPlan$name.png")
            if (-not (Test-Path -LiteralPath $image) -or (Get-Item -LiteralPath $image).LastWriteTime -lt $started) { throw "Missing fresh Plan $name screenshot." }
            Copy-Item -LiteralPath $image -Destination (Join-Path $saved ("Plan$name.png")) -Force
        }
    }
    if ($BlueprintsOnly) {
        foreach ($name in @('CopyHints', 'BlueprintHints')) {
            $image = Join-Path $env:LOCALAPPDATA ('FactoryGame\Saved\BuildingSkills' + $name + '.png')
            if (-not (Test-Path -LiteralPath $image) -or (Get-Item -LiteralPath $image).LastWriteTime -lt $started) { throw "Missing fresh $name screenshot." }
            Copy-Item -LiteralPath $image -Destination (Join-Path $saved ($name + '.png')) -Force
        }
    }
    foreach ($name in @('AnchorBlue', 'Anchor', 'StepSettings')) {
        $image = Join-Path $env:LOCALAPPDATA ('FactoryGame\Saved\BuildingSkills' + $name + 'Test.png')
        if ((Test-Path -LiteralPath $image) -and (Get-Item -LiteralPath $image).LastWriteTime -ge $started) {
            Copy-Item -LiteralPath $image -Destination (Join-Path $saved ($name + 'Test.png')) -Force
        }
    }
    $networkScreenshot = Join-Path $env:LOCALAPPDATA 'FactoryGame\Saved\BuildingSkillsNetworkTest.png'
    if ((Test-Path -LiteralPath $networkScreenshot) -and (Get-Item -LiteralPath $networkScreenshot).LastWriteTime -ge $started) {
        Copy-Item -LiteralPath $networkScreenshot -Destination (Join-Path $saved 'NetworkTest.png') -Force
    }
}
if ((Get-Item -LiteralPath $log).LastWriteTime -lt $started) { throw 'Runtime test log is stale.' }
Select-String -LiteralPath $log -Pattern 'RUNTIME (TEST|EDIT|ASSERT|TARGET|UI|MAPPING)' | ForEach-Object { $_.Line }
if ($testProcess.ExitCode -ne 0 -or -not (Select-String -LiteralPath $log -SimpleMatch 'RUNTIME EDIT CHECK: PASS' -Quiet)) {
    throw "Runtime test failed. See $log"
}
