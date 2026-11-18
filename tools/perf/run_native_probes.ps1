param(
    [Parameter(Mandatory=$true)][string]$Runtime,
    [Parameter(Mandatory=$true)][string]$DataRoot,
    [Parameter(Mandatory=$true)][string]$Output,
    [string]$ValidationLayer = '',
    [ValidateSet('All','Counter','Framegen','Overlay','Texture')][string]$Probe = 'All',
    [ValidateRange(10,600)][int]$TimeoutSeconds = 180
)
$ErrorActionPreference = 'Stop'
$runtimePath = (Resolve-Path -LiteralPath $Runtime).Path
$dataPath = (Resolve-Path -LiteralPath $DataRoot).Path
if (Get-Process wonder,wonder-launcher,mkw -ErrorAction SilentlyContinue) { throw 'A game is already running; no GPU probes started.' }
if (Test-Path -LiteralPath $Output) { throw 'Choose a new output directory to preserve previous evidence.' }
$null = New-Item -ItemType Directory -Path $Output
$outputPath = (Resolve-Path -LiteralPath $Output).Path
$layerPath = if ($ValidationLayer) { (Resolve-Path -LiteralPath $ValidationLayer).Path } else { '' }
if ($layerPath -and !(Test-Path -LiteralPath (Join-Path $layerPath 'VkLayer_khronos_validation.json'))) { throw 'Validation layer manifest missing.' }
$cases = @(
    @{Name='counter-sync'; Kind='Counter'; Flag='NX_COUNTER_CLEAR_PROBE'; Count=9; Pattern='counter/clear GPU probe case \d+: PASS'; Mode='sync'},
    @{Name='counter-pipelined'; Kind='Counter'; Flag='NX_COUNTER_CLEAR_PROBE'; Count=9; Pattern='counter/clear GPU probe case \d+: PASS'; Mode='pipelined'},
    @{Name='counter-async'; Kind='Counter'; Flag='NX_COUNTER_CLEAR_PROBE'; Count=9; Pattern='counter/clear GPU probe case \d+: PASS'; Mode='async'},
    @{Name='framegen'; Kind='Framegen'; Flag='NX_FRAMEGEN_PROBE'; Count=87; Pattern='framegen probe \d+x .*: PASS'; Mode='sync'},
    @{Name='overlay'; Kind='Overlay'; Flag='NX_OVERLAY_INPUT_PROBE'; Count=4; Pattern='(?:overlay input probe: .*|controller bridge probe preset \d): PASS'; Mode='sync'},
    @{Name='texture'; Kind='Texture'; Flag='NX_TEXTURE_CONVERSION_PROBE'; Count=8; Pattern='texture conversion probe .*: PASS'; Mode='sync'}
)
$results = @()
foreach ($case in $cases) {
    if ($Probe -ne 'All' -and $Probe -ne $case.Kind) { continue }
    if (Get-Process wonder,wonder-launcher,mkw -ErrorAction SilentlyContinue) { throw 'A game started between probes; remaining probes deferred.' }
    $start = New-Object Diagnostics.ProcessStartInfo
    $start.FileName = $runtimePath
    $start.WorkingDirectory = Split-Path -Parent $runtimePath
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    # Each child gets its own environment. Do not leak QA switches into the user's shell.
    foreach ($key in @($start.EnvironmentVariables.Keys)) {
        if ($key -like 'NX_*' -or $key -like 'VK_LAYER*' -or $key -eq 'VK_INSTANCE_LAYERS' -or $key -eq 'VK_LOADER_DEBUG') {
            $start.EnvironmentVariables.Remove($key)
        }
    }
    $start.EnvironmentVariables['NX_DATA_ROOT'] = $dataPath
    $start.EnvironmentVariables['NX_HEADLESS'] = '1'
    $start.EnvironmentVariables['NX_HEADLESS_PRESENT'] = '1'
    $start.EnvironmentVariables['NX_MUTE'] = '1'
    $start.EnvironmentVariables[$case.Flag] = '1'
    $start.EnvironmentVariables['NX_ASYNC_GPU'] = if ($case.Mode -eq 'async') { '1' } else { '0' }
    $start.EnvironmentVariables['NX_PIPELINED'] = if ($case.Mode -eq 'pipelined') { '2' } else { '0' }
    if ($layerPath) {
        $start.EnvironmentVariables['VK_LAYER_PATH'] = $layerPath
        $start.EnvironmentVariables['VK_INSTANCE_LAYERS'] = 'VK_LAYER_KHRONOS_validation'
        $start.EnvironmentVariables['VK_LAYER_VALIDATE_SYNC'] = '1'
        $start.EnvironmentVariables['VK_LOADER_DEBUG'] = 'layer'
    }
    $process = New-Object Diagnostics.Process
    $process.StartInfo = $start
    $null = $process.Start()
    # Drain both pipes concurrently: validation output can otherwise fill a pipe and deadlock.
    $stdout = $process.StandardOutput.ReadToEndAsync()
    $stderr = $process.StandardError.ReadToEndAsync()
    $finished = $process.WaitForExit($TimeoutSeconds * 1000)
    if (!$finished) { $process.Kill(); $process.WaitForExit() }
    $text = $stdout.GetAwaiter().GetResult() + "`n" + $stderr.GetAwaiter().GetResult()
    [IO.File]::WriteAllText((Join-Path $outputPath ($case.Name+'.log')), $text)
    $passed = if ($case.Pattern) { [regex]::Matches($text,$case.Pattern).Count } else { 0 }
    $errors = [regex]::Matches($text,'Validation Error:').Count
    $loaded = !$layerPath -or $case.Kind -eq 'Texture' -or ($text.Contains('Loading layer library') -and $text.Contains('VkLayer_khronos_validation'))
    $ok = $finished -and $process.ExitCode -eq 0 -and $passed -eq $case.Count -and $errors -eq 0 -and $loaded
    $confirmed = if ($layerPath -and $case.Kind -ne 'Texture') { $loaded } else { $null }
    $results += [pscustomobject]@{probe=$case.Name;passed=$passed;expected=$case.Count;validation_errors=$errors;layer_confirmed=$confirmed;completed=$finished;exit_code=$process.ExitCode;ok=$ok}
    $process.Dispose()
    ConvertTo-Json -InputObject @($results) -Depth 4 | Set-Content -LiteralPath (Join-Path $outputPath 'results.json') -Encoding UTF8
    if (!$ok) { throw "Probe $($case.Name) failed; inspect its preserved log." }
    Write-Output "PASS $($case.Name): $passed/$($case.Count); validation errors $errors"
}
