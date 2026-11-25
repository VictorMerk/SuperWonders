# Run one local-co-op simulation. A streaming app can supply separate guest
# controllers; this script does not start a LAN room or send invitations.
param([string]$Runtime,[string]$DataRoot)
$ErrorActionPreference='Stop'

function New-WonderLocalCoopStartInfo([string]$Executable,[string]$Data) {
    $info=New-Object Diagnostics.ProcessStartInfo
    $info.FileName=$Executable
    $info.WorkingDirectory=Split-Path -Parent $Executable
    $info.Arguments='--play'
    $info.UseShellExecute=$false
    # A local session must not inherit a room, replay, hidden test or virtual-only
    # controller configuration from the shell used for previous LAN diagnostics.
    foreach($key in @($info.EnvironmentVariables.Keys)) {
        if($key -like 'NX_LAN_*' -or $key -like 'NX_NATIVE_*' -or $key -like 'NX_STEAM_*' -or $key -like 'NX_TEST_*' -or $key -like 'NX_INPUT_*' -or
           $key -like 'NX_PROBE*' -or $key -like 'NX_*_PROBE' -or $key -like 'NX_*_TOUR' -or
           $key -like 'NX_PLAY_*' -or $key -like 'NX_TRACE_*' -or $key -like 'NX_DUMP_*' -or
           $key -in @('NX_HEADLESS','NX_HEADLESS_PRESENT','NX_MUTE','NX_FRAMETRACE','NX_AUDIO_DUMP','NX_LOG','NX_PIPELINE_MISS_TRACE','NX_PIPELINE_CACHE_TRACE','SteamAppId','SteamGameId')) {
            $info.EnvironmentVariables.Remove($key)
        }
    }
    $info.EnvironmentVariables['NX_DATA_ROOT']=$Data
    $info.EnvironmentVariables['NX_NO_LAUNCHER']='1'
    return $info
}

if($MyInvocation.InvocationName -eq '.') { return }
if(!$Runtime -or !$DataRoot){throw 'Specify -Runtime <wonder.exe> and -DataRoot <prepared data folder>.'}
$runtimePath=(Resolve-Path -LiteralPath $Runtime).Path
$dataPath=(Resolve-Path -LiteralPath $DataRoot).Path
if(!(Test-Path -LiteralPath $runtimePath -PathType Leaf)){throw 'Runtime must be a file.'}
if(!(Test-Path -LiteralPath $dataPath -PathType Container)){throw 'DataRoot must be a directory.'}
$reservation=Join-Path $PSScriptRoot '../perf/game_test_reservation.ps1'
$claim=& $reservation -Action Acquire -Task 'Wonder local co-op / shared-PC play' -Workspace $PSScriptRoot -OwnerPid $PID | ConvertFrom-Json
if($claim.state -ne 'acquired') { $claim | ConvertTo-Json -Depth 5; exit 75 }
$game=$null
try {
    if(Get-Process wonder,wonder-launcher,mkw,mkw-launcher -ErrorAction SilentlyContinue){throw 'Another game started. Try again when it has closed.'}
    $game=New-Object Diagnostics.Process
    $game.StartInfo=New-WonderLocalCoopStartInfo $runtimePath $dataPath
    if(!$game.Start()){throw 'Could not start Wonder.'}
    $retainedHandle=$game.Handle
    $game.WaitForExit()
    $result=$game.ExitCode
} finally {
    if($game){$game.Dispose()}
    & $reservation -Action Release -LeaseId $claim.lease.lease_id
}
exit $result
