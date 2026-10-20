[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('Link', 'ProjectFiles', 'BuildEditor', 'InitContent', 'EditAssets', 'Test', 'Package', 'PackageLinux', 'Release', 'Install')]
    [string]$Action,
    [string]$SmlRoot = 'F:\dev\satisfactory_mods\SatisfactoryModLoader',
    [string]$EngineRoot = 'C:\Program Files\Unreal Engine - CSS',
    [string]$GameRoot = 'F:\Programme et jeux\Steam\steamapps\common\Satisfactory',
    [switch]$SkipNativeBuild,
    [ValidateRange(1, 64)]
    [int]$MaxParallelActions = 2
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$pluginRoot = Join-Path $repoRoot 'mod\BuildingSkills'
$project = Join-Path $SmlRoot 'FactoryGame.uproject'
$linkPath = Join-Path $SmlRoot 'Mods\GameFeatures\BuildingSkills'
$batchRoot = Join-Path $EngineRoot 'Engine\Build\BatchFiles'

function Require-Path([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) { throw "Required path not found: $Path" }
}

function Invoke-Checked([string]$Program, [string[]]$Arguments) {
    Require-Path $Program
    # UBT launches Wwise's Windows PowerShell hook through cmd.exe. That child
    # must not resolve PowerShell 7 modules inherited from the calling shell.
    $previousModulePath = $env:PSModulePath
    try {
        $env:PSModulePath = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\Modules'
        & $Program @Arguments
        if ($LASTEXITCODE -ne 0) { throw "$Program failed with exit code $LASTEXITCODE" }
    } finally {
        $env:PSModulePath = $previousModulePath
    }
}

function Assert-Link {
    Require-Path $linkPath
    $item = Get-Item -LiteralPath $linkPath -Force
    if ($item.LinkType -ne 'Junction' -or
        [IO.Path]::GetFullPath([string]@($item.Target)[0]).TrimEnd('\') -ne
        [IO.Path]::GetFullPath($pluginRoot).TrimEnd('\')) {
        throw "Expected a junction to $pluginRoot at $linkPath. Existing paths will not be replaced."
    }
}

Require-Path $project
Require-Path (Join-Path $pluginRoot 'BuildingSkills.uplugin')

if ($Action -eq 'Link') {
    if (Test-Path -LiteralPath $linkPath) {
        Assert-Link
        Write-Host "Junction already points to $pluginRoot"
    } else {
        Require-Path (Split-Path -Parent $linkPath)
        New-Item -ItemType Junction -Path $linkPath -Target $pluginRoot | Out-Null
        Assert-Link
        Write-Host "Linked $linkPath -> $pluginRoot"
    }
    return
}

Assert-Link
if (Get-Process -Name UnrealEditor, UnrealEditor-Cmd -ErrorAction SilentlyContinue) {
    throw 'Close Unreal Editor and wait for other editor commandlets before running this action.'
}

switch ($Action) {
    'EditAssets' {
        New-Item -ItemType Directory -Path (Join-Path $repoRoot 'Saved') -Force | Out-Null
        Invoke-Checked (Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe') @(
            $project, '-run=pythonscript', "-script=$(Join-Path $PSScriptRoot 'setup_edit_assets.py')",
            '-unattended', '-nullrhi', '-nosound', '-nop4',
            "-abslog=$(Join-Path $repoRoot 'Saved\EditAssets.log')"
        )
    }
    'Test' {
        New-Item -ItemType Directory -Path (Join-Path $repoRoot 'Saved') -Force | Out-Null
        $testStarted = Get-Date
        Invoke-Checked (Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe') @(
            $project, '-unattended', '-nullrhi', '-nosound', '-nop4',
            '-ExecCmds=Automation RunTests BuildingSkills', '-TestExit=Automation Test Queue Empty',
            "-ReportExportPath=$(Join-Path $repoRoot 'Saved\Automation')",
            "-abslog=$(Join-Path $repoRoot 'Saved\Automation.log')"
        )
        $report = Join-Path $repoRoot 'Saved\Automation\index.json'
        Require-Path $report
        if ((Get-Item -LiteralPath $report).LastWriteTime -lt $testStarted) { throw 'Automation report is stale.' }
        $results = Get-Content -LiteralPath $report -Raw | ConvertFrom-Json
        if ($results.failed -gt 0 -or ($results.succeeded + $results.succeededWithWarnings) -lt 1) {
            throw "Automation did not pass: $report"
        }
    }
    'ProjectFiles' {
        Invoke-Checked (Join-Path $batchRoot 'Build.bat') @(
            '-projectfiles', "-Project=$project", '-game', '-engine'
        )
    }
    'BuildEditor' {
        & (Join-Path $PSScriptRoot 'BuildNative.ps1') -Target FactoryEditor -SmlRoot $SmlRoot -EngineRoot $EngineRoot
    }
    'InitContent' {
        $bootstrap = Join-Path $PSScriptRoot 'init_content.py'
        $log = Join-Path $repoRoot 'Saved\InitContent.log'
        $initialBootstrap = -not (Test-Path -LiteralPath (Join-Path $pluginRoot 'Content\BuildingSkills.uasset'))
        New-Item -ItemType Directory -Path (Split-Path -Parent $log) -Force | Out-Null
        $editor = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
        $editorArgs = @(
            $project, '-run=pythonscript', "-script=$bootstrap", '-unattended',
            '-nullrhi', '-nosound', '-nop4', '-UTF8Output', "-abslog=$log"
        )
        try {
            Invoke-Checked $editor $editorArgs
        } catch {
            # On a brand-new plugin, feature activation precedes Python and logs
            # a missing-asset error. Require successful creation, then a clean
            # second process; never accept that first exit code as validation.
            if (-not $initialBootstrap -or -not (Test-Path -LiteralPath $log) -or
                -not (Select-String -LiteralPath $log -SimpleMatch 'BuildingSkills content verified.' -Quiet)) {
                throw
            }
            Copy-Item -LiteralPath $log -Destination (Join-Path $repoRoot 'Saved\InitContent-bootstrap.log')
            Write-Host 'Starter assets created; restarting Unreal to verify they load cleanly.'
            Invoke-Checked $editor $editorArgs
        }
        if (-not (Select-String -LiteralPath $log -SimpleMatch 'BuildingSkills content verified.' -Quiet)) {
            throw "Content initialization did not report success. See $log"
        }
    }
    { $_ -in 'Package', 'PackageLinux', 'Release', 'Install' } {
        foreach ($asset in @('BuildingSkills', 'RootGameInstance_BuildingSkills', 'RootGameWorld_BuildingSkills')) {
            Require-Path (Join-Path $pluginRoot "Content\$asset.uasset")
        }
        Require-Path (Join-Path $pluginRoot 'Content\Inputs\IA_ToggleEditMode.uasset')
        Require-Path (Join-Path $pluginRoot 'Content\Inputs\MC_BuildingSkills.uasset')
        foreach ($asset in @('IA_Group', 'IA_Merge', 'IA_Ungroup', 'IA_Transform', 'MC_EditRoot', 'MC_EditOperations', 'MC_Transform', 'IA_MoveForward', 'IA_MoveBackward', 'IA_MoveLeft', 'IA_MoveRight', 'IA_MoveUp', 'IA_MoveDown', 'IA_Tiny', 'IA_Small', 'IA_Large', 'IA_ScrollUp', 'IA_ScrollDown')) {
            Require-Path (Join-Path $pluginRoot "Content\Inputs\$asset.uasset")
        }
        foreach ($asset in @('IA_Save', 'MC_Save', 'IA_Pivot', 'IA_AxisSpace', 'IA_Anchor', 'IA_Undo', 'IA_Redo', 'IA_ConnectedSettings', 'MC_Connected')) { Require-Path (Join-Path $pluginRoot "Content\Inputs\$asset.uasset") }
        foreach ($asset in @('IA_TogglePlan', 'IA_PlanSettings', 'IA_PlanScrollUp', 'IA_PlanScrollDown', 'IA_PlanDelete', 'IA_PlanUndo', 'IA_PlanRedo', 'MC_Plan')) { Require-Path (Join-Path $pluginRoot "Content\Inputs\$asset.uasset") }
        # Alpakit can report success with an empty asset container if another
        # project's editor build receipt disabled this game-feature plugin.
        # Check before cooking/deployment; never silently ship only the DLL.
        $editorReceiptPath = Join-Path $SmlRoot 'Binaries/Win64/FactoryEditor.target'
        Require-Path $editorReceiptPath
        $editorReceipt = Get-Content -LiteralPath $editorReceiptPath -Raw | ConvertFrom-Json
        if ($editorReceipt.BuildPlugins -notcontains 'BuildingSkills') {
            throw 'The editor build receipt disables Building Skills. Run Mod.ps1 BuildEditor before packaging so its content is mounted by the cooker.'
        }
        if ($Action -eq 'Release' -and -not $SkipNativeBuild) {
            foreach ($releaseTarget in @('FactoryGameSteam', 'FactoryGameEGS', 'FactoryServer')) {
                & (Join-Path $PSScriptRoot 'BuildNative.ps1') -Target $releaseTarget -SmlRoot $SmlRoot -EngineRoot $EngineRoot
            }
            & (Join-Path $PSScriptRoot 'BuildLinux.ps1') -SmlRoot $SmlRoot -EngineRoot $EngineRoot
        } elseif ($Action -eq 'PackageLinux' -and -not $SkipNativeBuild) {
            & (Join-Path $PSScriptRoot 'BuildLinux.ps1') -SmlRoot $SmlRoot -EngineRoot $EngineRoot
        } elseif ($Action -ne 'Release' -and -not $SkipNativeBuild) {
            & (Join-Path $PSScriptRoot 'BuildNative.ps1') -Target FactoryGameSteam -SmlRoot $SmlRoot -EngineRoot $EngineRoot
        }
        # Shared SDK receipts can be replaced by another mod's isolated build.
        # Existing DLLs alone are insufficient: Alpakit stages only receipt entries.
        $nativeReceipts = if ($Action -eq 'Release') {
            @('Win64/FactoryGameSteam-Win64-Shipping.target',
              'Win64/FactoryGameEGS-Win64-Shipping.target',
              'Win64/FactoryServer-Win64-Shipping.target',
              'Linux/FactoryServer-Linux-Shipping.target')
        } elseif ($Action -eq 'PackageLinux') {
            @('Linux/FactoryServer-Linux-Shipping.target')
        } else {
            @('Win64/FactoryGameSteam-Win64-Shipping.target')
        }
        foreach ($nativeReceiptName in $nativeReceipts) {
            $nativeReceiptPath = Join-Path $SmlRoot "Binaries/$nativeReceiptName"
            Require-Path $nativeReceiptPath
            $nativeReceipt = Get-Content -LiteralPath $nativeReceiptPath -Raw | ConvertFrom-Json
            if ($nativeReceipt.BuildPlugins -notcontains 'BuildingSkills') {
                throw "The shared receipt $nativeReceiptName excludes Building Skills. Refresh that target's build metadata before packaging; existing binaries will otherwise be omitted."
            }
        }
        $uatArgs = @(
            "-ScriptsForProject=$project", 'PackagePlugin', "-project=$project",
            '-clientconfig=Shipping', '-serverconfig=Shipping', '-utf8output',
            '-DLCName=BuildingSkills', '-platform=Win64',
            '-nocompileeditor', '-installed', "-UbtArgs=-NoUBA -MaxParallelActions=$MaxParallelActions"
        )
        if ($Action -eq 'Release') { $uatArgs += @('-server', '-serverplatform=Win64+Linux', '-merge') }
        if ($Action -eq 'PackageLinux') { $uatArgs += @('-noclient', '-server', '-serverplatform=Linux') }
        # The native development build above compiles Steam only. Explicitly
        # select it so packaging cannot include an older cached Epic binary.
        if ($Action -in 'Package', 'Install') { $uatArgs += '-target=FactoryGameSteam' }
        if ($Action -eq 'Install') {
            Require-Path (Join-Path $GameRoot 'FactoryGame')
            if (Get-Process -Name FactoryGame, FactoryGameSteam, FactoryGameEGS, FactoryGameSteam-Win64-Shipping, FactoryGameEGS-Win64-Shipping -ErrorAction SilentlyContinue) {
                throw 'Close Satisfactory before installing the mod.'
            }
            $resolvedGameRoot = (Resolve-Path -LiteralPath $GameRoot).Path
            $deployTarget = [IO.Path]::GetFullPath((Join-Path $resolvedGameRoot 'FactoryGame\Mods\GameFeatures\BuildingSkills'))
            if (-not $deployTarget.StartsWith($resolvedGameRoot.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) {
                throw 'Deployment target escaped the selected game installation.'
            }
            Write-Host "Alpakit deployment target: $deployTarget"
            $uatArgs += "-CopyToGameDirectory_Windows=$resolvedGameRoot"
        }
        Invoke-Checked (Join-Path $batchRoot 'RunUAT.bat') $uatArgs
        if ($Action -eq 'Install') {
            & (Join-Path $PSScriptRoot 'VerifyInstall.ps1') -SmlRoot $SmlRoot -GameRoot $GameRoot
        }
    }
}
