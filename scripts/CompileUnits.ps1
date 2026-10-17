[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Compiler,
    [Parameter(Mandatory)][string[]]$ResponseFiles,
    [Parameter(Mandatory)][string]$WorkingDirectory,
    [Parameter(Mandatory)][string]$OutputDirectory,
    [ValidateRange(1, 4)][int]$Workers = 2
)
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$pending = [Collections.Generic.Queue[string]]::new()
foreach ($object in $ResponseFiles) { $pending.Enqueue($object) }
$running = [Collections.Generic.List[object]]::new()
$failures = [Collections.Generic.List[string]]::new()
try {
    while ($pending.Count -gt 0 -or $running.Count -gt 0) {
        while ($pending.Count -gt 0 -and $running.Count -lt $Workers) {
            $object = $pending.Dequeue()
            $name = [IO.Path]::GetFileName($object)
            $stdout = Join-Path $OutputDirectory ($name + '.compile.log')
            $stderr = Join-Path $OutputDirectory ($name + '.compile.err.log')
            Write-Host "Compiling $name"
            $process = Start-Process -FilePath $Compiler -ArgumentList ('@"' + $object + '"') -WorkingDirectory $WorkingDirectory -WindowStyle Hidden -RedirectStandardOutput $stdout -RedirectStandardError $stderr -PassThru
            $running.Add([pscustomobject]@{ Process=$process; Object=$object; Stdout=$stdout; Stderr=$stderr })
        }
        foreach ($job in @($running.ToArray())) {
            if (-not $job.Process.WaitForExit(1000)) { continue }
            if (Test-Path -LiteralPath $job.Stdout) { Get-Content -LiteralPath $job.Stdout }
            if (Test-Path -LiteralPath $job.Stderr) { Get-Content -LiteralPath $job.Stderr }
            if ($job.Process.ExitCode -ne 0) { $failures.Add($job.Object) }
            $job.Process.Dispose()
            [void]$running.Remove($job)
        }
    }
} finally {
    # Only processes created by this invocation may be stopped on interruption.
    foreach ($job in $running) {
        if (-not $job.Process.HasExited) { $job.Process.Kill(); $job.Process.WaitForExit() }
        $job.Process.Dispose()
    }
}
if ($failures.Count -gt 0) { throw ("Compilation failed: " + ($failures -join ', ')) }
