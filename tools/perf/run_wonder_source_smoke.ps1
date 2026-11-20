param(
    [Parameter(Mandatory=$true)][string]$Runtime,
    [Parameter(Mandatory=$true)][string]$DataRoot,
    [Parameter(Mandatory=$true)][string]$Output,
    [string]$InputScript='',
    [string]$LiveInputFile='',
    [string]$PlayerProbe='vt=0',
    [ValidateRange(60,100000)][int]$QuitAfterFrames=1800,
    [ValidateRange(60,3600)][int]$TimeoutSeconds=600,
    [ValidateRange(1,10000)][int]$CaptureEvery=300,
    [string]$ValidationLayer='',
    [switch]$TraceTextureSources,
    [switch]$TracePipelineMisses,
    [switch]$RequireAudio,
    [ValidateRange(0,3600)][int]$ThreadDumpAtSeconds=0,
    [switch]$TraceFiles,
    [string]$ContentPython='',
    [string]$QuitFile='',
    [ValidateRange(0,100000)][int]$ThirtyFpsUntilFrame=0,
    [switch]$ScanPlayer,
    [string]$LeaseId=''
)
# A real native-game smoke test, distinct from the library GUI's unit tests.
# Use only a task-owned fixture: gameplay can update that fixture's saves.
# A successful smoke is not whole-game, visual or Switch 2 content certification.
$ErrorActionPreference='Stop'
$runtimePath=(Resolve-Path -LiteralPath $Runtime).Path
$dataPath=(Resolve-Path -LiteralPath $DataRoot).Path
if(Test-Path -LiteralPath $Output){throw 'Choose a new evidence folder.'}
$outputPath=[IO.Path]::GetFullPath($Output)
$inputPath=if($InputScript){(Resolve-Path -LiteralPath $InputScript).Path}else{''}
$livePath=if($LiveInputFile){(Resolve-Path -LiteralPath $LiveInputFile).Path}else{''}
$layerPath=if($ValidationLayer){(Resolve-Path -LiteralPath $ValidationLayer).Path}else{''}
$contentPythonPath=if($ContentPython){(Resolve-Path -LiteralPath $ContentPython).Path}else{''}
$quitFilePath=if($QuitFile){[IO.Path]::GetFullPath($QuitFile)}else{''}
if($quitFilePath -and (Test-Path -LiteralPath $quitFilePath)){throw 'Choose a new quit-request path; an existing marker would stop the test immediately.'}
$reservation=Join-Path $PSScriptRoot 'game_test_reservation.ps1'
$releaseLease=!$LeaseId
if($LeaseId) {
    $state=& $reservation -Action Status | ConvertFrom-Json
    $ownerStarted=(Get-Process -Id $PID).StartTime.ToUniversalTime().ToString('o')
    if(!$state.lease -or $state.lease.lease_id -ne $LeaseId -or $state.lease.owner_pid -ne $PID -or $state.lease.owner_started_utc -ne $ownerStarted) {
        throw 'A shared smoke lease must belong to this PowerShell process and match its start time.'
    }
    $claim=[pscustomobject]@{state='acquired';lease=$state.lease}
} else {
    $claim=& $reservation -Action Acquire -Task 'Wonder source-version native smoke' -Workspace $PSScriptRoot -OwnerPid $PID | ConvertFrom-Json
}
if($claim.state -ne 'acquired'){Write-Output ($claim|ConvertTo-Json -Compress -Depth 5);exit 75}
$game=$null;$started=$false
try {
    $null=New-Item -ItemType Directory -Path $outputPath
    if(Get-Process wonder,wonder-launcher,mkw,mkw-launcher,longshot -ErrorAction SilentlyContinue){throw 'A foreign game started; smoke deferred.'}
    $info=New-Object Diagnostics.ProcessStartInfo
    $info.FileName=$runtimePath
    $info.WorkingDirectory=Split-Path -Parent $runtimePath
    $info.UseShellExecute=$false
    $info.CreateNoWindow=$true
    $info.RedirectStandardOutput=$true
    $info.RedirectStandardError=$true
    foreach($key in @($info.EnvironmentVariables.Keys)) {
        if($key -like 'NX_*' -or $key -like 'VK_*'){$info.EnvironmentVariables.Remove($key)}
    }
    $options=@{
        NX_DATA_ROOT=$dataPath;NX_LOG=(Join-Path $outputPath 'native.log');
        NX_NO_LAUNCHER='1';NX_HEADLESS='1';NX_HEADLESS_PRESENT='1';NX_MUTE='1';
        NX_TEST_VIRTUAL_ONLY='1';
        NX_TEST_QUIT_FRAME=[string]$QuitAfterFrames;NX_DUMP_FRAMES=[string]$CaptureEvery;
        NX_DUMP_PRESENTED='1';NX_FRAMETRACE=(Join-Path $outputPath 'frametrace.csv');
        NX_AUDIO_DUMP=(Join-Path $outputPath 'audio.wav');
        NX_PROBE=(Join-Path $outputPath 'probe.csv');NX_PROBE_PLAYER=$PlayerProbe
    }
    if($inputPath){$options['NX_INPUT_FRAMES']='@'+$inputPath}
    if($livePath){$options['NX_INPUT_SCRIPT']='@'+$livePath}
    if($TraceTextureSources){$options['NX_TEXTURE_UPLOAD_TRACE']='1'}
    if($ThreadDumpAtSeconds){$options['NX_THREAD_DUMP_AT']=[string]$ThreadDumpAtSeconds}
    if($TraceFiles){$options['NX_TRACE_FS']='1'}
    if($quitFilePath){$options['NX_TEST_QUIT_FILE']=$quitFilePath}
    if($ThirtyFpsUntilFrame){$options['NX_FPS_CAP_UNTIL']=[string]$ThirtyFpsUntilFrame}
    if($ScanPlayer){$options['NX_PROBE_SCAN']=Join-Path $outputPath 'player-scan.txt'}
    if($layerPath) {
        $options['VK_LAYER_PATH']=$layerPath
        $options['VK_INSTANCE_LAYERS']='VK_LAYER_KHRONOS_validation'
        $options['VK_LAYER_VALIDATE_SYNC']='1'
    }
    if($TracePipelineMisses){$options['NX_PIPELINE_MISS_TRACE']=Join-Path $outputPath 'pipeline-miss-trace.csv'}
    foreach($key in $options.Keys){$info.EnvironmentVariables[$key]=$options[$key]}
    $game=New-Object Diagnostics.Process;$game.StartInfo=$info
    $began=[DateTime]::UtcNow
    $null=$game.Start();$started=$true
    $stdout=$game.StandardOutput.ReadToEndAsync();$stderr=$game.StandardError.ReadToEndAsync()
    $reason='exited';$deadline=$began.AddSeconds($TimeoutSeconds)
    while(!$game.HasExited) {
        $foreign=@(Get-Process wonder,wonder-launcher,mkw,mkw-launcher,longshot -ErrorAction SilentlyContinue | Where-Object {$_.Id -ne $game.Id})
        if($foreign.Count){$reason='foreign game started';$game.Kill();break}
        if([DateTime]::UtcNow -gt $deadline){$reason='timeout';$game.Kill();break}
        Start-Sleep -Milliseconds 500
    }
    $game.WaitForExit()
    [IO.File]::WriteAllText((Join-Path $outputPath 'stdout.log'),$stdout.GetAwaiter().GetResult())
    [IO.File]::WriteAllText((Join-Path $outputPath 'stderr.log'),$stderr.GetAwaiter().GetResult())
    $logPath=Join-Path $outputPath 'native.log'
    $log=if(Test-Path -LiteralPath $logPath){[IO.File]::ReadAllText($logPath)}else{''}
    $quit=[regex]::Match($log,'scripted render-thread shutdown at frame (\d+)')
    $requestQuit=[regex]::Match($log,'scripted render-thread shutdown by request at frame (\d+)')
    $intentional=if($quitFilePath){$requestQuit.Success}else{$quit.Success -and [int64]$quit.Groups[1].Value -ge $QuitAfterFrames}
    $audioPath=Join-Path $outputPath 'audio.wav'
    $audioBytes=if(Test-Path -LiteralPath $audioPath){(Get-Item -LiteralPath $audioPath).Length}else{0}
    $frames=@(Get-ChildItem -LiteralPath (Join-Path $dataPath 'frames') -File -ErrorAction SilentlyContinue | Where-Object {$_.LastWriteTimeUtc -ge $began})
    if($frames.Count) {
        $capture=Join-Path $outputPath 'frames';$null=New-Item -ItemType Directory -Path $capture
        foreach($frame in $frames){Copy-Item -LiteralPath $frame.FullName -Destination (Join-Path $capture $frame.Name)}
    }
    $contentPassed=$null;$audioPassed=$null
    if($contentPythonPath) {
        & $contentPythonPath (Join-Path $PSScriptRoot 'check_wonder_captures.py') (Join-Path $outputPath 'frames') --json (Join-Path $outputPath 'content-check.json')
        $contentPassed=$LASTEXITCODE -eq 0
        & $contentPythonPath (Join-Path $PSScriptRoot 'check_wonder_audio.py') $audioPath --json (Join-Path $outputPath 'audio-check.json')
        $audioPassed=$LASTEXITCODE -eq 0
    }
    $result=[pscustomobject]@{
        runtime=$runtimePath;data_root=$dataPath;reason=$reason;exit_code=$game.ExitCode;
        intentional_quit=$intentional;shutdown_mode=$(if($quitFilePath){'request-file'}else{'frame'});
        fresh_captures=$frames.Count;fatal_errors=[regex]::Matches($log,'FATAL:').Count;
        validation_enabled=[bool]$layerPath;
        audio_bytes=$audioBytes;audio_required=[bool]$RequireAudio;
        audio_checked=[bool]$contentPythonPath;non_silent_audio_observed=$audioPassed;
        physical_controllers_excluded=$true;
        content_checked=[bool]$contentPythonPath;nonflat_content_observed=$contentPassed;
        validation_errors=[regex]::Matches($log,'Validation Error:').Count;
        scope='native process/capture/audio/shutdown smoke; navigation, visual correctness and edition contents need separate inspection'
    }
    $result|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $outputPath 'result.json') -Encoding UTF8
    $result|ConvertTo-Json -Compress|Write-Output
    if($reason -ne 'exited' -or $result.exit_code -ne 0 -or !$result.intentional_quit -or !$result.fresh_captures -or $result.fatal_errors -or $result.validation_errors -or ($RequireAudio -and ($audioBytes -le 44 -or ($contentPythonPath -and !$audioPassed))) -or ($contentPythonPath -and !$contentPassed)){exit 1}
} finally {
    try {
        if($started -and !$game.HasExited){$game.Kill();$game.WaitForExit()}
        if($game){$game.Dispose()}
    } finally {
        if($releaseLease){& $reservation -Action Release -LeaseId $claim.lease.lease_id}
    }
}
