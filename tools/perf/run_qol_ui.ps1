param(
    [Parameter(Mandatory=$true)][string]$Runtime,
    [Parameter(Mandatory=$true)][string]$Output,
    [string[]]$Cases=@('launcher-720','launcher-360','profiles-720','profiles-360','coop-540','resize','comfort-720','profile-controller')
)
$ErrorActionPreference='Stop'
$runtimePath=(Resolve-Path -LiteralPath $Runtime).Path
$outputPath=[IO.Path]::GetFullPath($Output)
if(Test-Path -LiteralPath $outputPath){throw 'Use a new output folder; existing evidence is preserved.'}
$reservation=Join-Path $PSScriptRoot 'game_test_reservation.ps1'
$claim=& $reservation -Action Acquire -Task 'Profiles and launcher QoL: native UI layouts and resize' -Workspace (Get-Location).Path -OwnerPid $PID | ConvertFrom-Json
if($claim.state -ne 'acquired'){Write-Output ($claim|ConvertTo-Json -Depth 5);return}
$owned=$null
$utf8=New-Object Text.UTF8Encoding($false)
try {
    New-Item -ItemType Directory -Path $outputPath | Out-Null
    $bin=Join-Path $outputPath 'runtime'
    New-Item -ItemType Directory -Path $bin | Out-Null
    foreach($name in @('wonder.exe','wonder-launcher.exe','libc++.dll','libunwind.dll')) {
        Copy-Item -LiteralPath (Join-Path $runtimePath $name) -Destination (Join-Path $bin $name)
    }
    $results=@()
    $definitions=@(
        @{name='launcher-720';width=1280;height=720;hook='NX_LAUNCHER_TOUR';value='30'},
        @{name='launcher-360';width=640;height=360;hook='NX_LAUNCHER_TOUR';value='30'},
        @{name='profiles-720';width=1280;height=720;hook='NX_PROFILE_MANAGER_TOUR';value='1'},
        @{name='profiles-360';width=640;height=360;hook='NX_PROFILE_MANAGER_TOUR';value='1'},
        @{name='coop-540';width=960;height=540;hook='NX_LAN_LOBBY_TOUR';value='1'},
        @{name='resize';width=1280;height=720;hook='NX_QOL_RESIZE_TOUR';value='1'},
        @{name='comfort-720';width=1280;height=720;hook='NX_LAUNCHER_TOUR';value='30'},
        @{name='profile-controller';width=1280;height=720;hook='NX_PROFILE_MANAGER_TOUR';value='1'}
    )
    foreach($caseName in $Cases) {
        $case=$definitions | Where-Object { $_.name -eq $caseName } | Select-Object -First 1
        if(!$case){throw ('Unknown UI case: '+$caseName)}
        $foreign=Get-Process wonder,wonder-launcher,mkw,mkw-launcher -ErrorAction SilentlyContinue | Where-Object { !$_.HasExited }
        if($foreign){throw 'A foreign game is running; native UI tests deferred.'}
        $data=Join-Path $outputPath $case.name
        New-Item -ItemType Directory -Path $data | Out-Null
        [IO.File]::WriteAllText((Join-Path $data 'pc-settings.ini'),"width=$($case.width)`nheight=$($case.height)`nvolume=0`ncontroller_preset=1",$utf8)
        [IO.File]::WriteAllText((Join-Path $data 'profile-name.txt'),'Player One',$utf8)
        $other=Join-Path $data 'profiles/p-123'
        New-Item -ItemType Directory -Path $other | Out-Null
        [IO.File]::WriteAllText((Join-Path $other 'profile-name.txt'),'Player Two',$utf8)
        if($case.name -eq 'comfort-720'){[IO.File]::WriteAllText((Join-Path $data 'usability.ini'),"menu_fullscreen=1`nreduced_motion=1`nmenu_opacity=100",$utf8)}
        $start=New-Object Diagnostics.ProcessStartInfo
        $start.FileName=Join-Path $bin 'wonder-launcher.exe'
        $start.WorkingDirectory=$bin
        $start.UseShellExecute=$false
        $start.CreateNoWindow=$true
        $start.WindowStyle=[Diagnostics.ProcessWindowStyle]::Hidden
        foreach($key in @($start.EnvironmentVariables.Keys)) {
            if($key -like 'NX_*' -or $key -like 'SUPERWONDERS_*'){$start.EnvironmentVariables.Remove($key)}
        }
        $start.EnvironmentVariables['NX_DATA_ROOT']=$data
        $start.EnvironmentVariables['NX_HEADLESS']='1'
        $start.EnvironmentVariables['NX_HEADLESS_PRESENT']='1'
        $start.EnvironmentVariables['NX_DUMP_PRESENTED']='1'
        $start.EnvironmentVariables['NX_DUMP_PREP']='20'
        $start.EnvironmentVariables['NX_MUTE']='1'
        $start.EnvironmentVariables['NX_TEST_VIRTUAL_ONLY']='1'
        $start.EnvironmentVariables[$case.hook]=$case.value
        if($case.name -eq 'profile-controller'){$start.EnvironmentVariables['NX_TEST_PAD']='30:east:3;60:down:3;80:east:3'}
        $owned=New-Object Diagnostics.Process
        $owned.StartInfo=$start
        if(!$owned.Start()){throw 'Could not start owned UI test.'}
        $deadline=[DateTime]::UtcNow.AddSeconds(90)
        while(!$owned.HasExited -and [DateTime]::UtcNow -lt $deadline) {
            $foreign=Get-Process wonder,wonder-launcher,mkw,mkw-launcher -ErrorAction SilentlyContinue | Where-Object { $_.Id -ne $owned.Id -and !$_.HasExited }
            if($foreign){$owned.Kill();$owned.WaitForExit();throw 'Foreign game started; stopped only this owned UI test.'}
            Start-Sleep -Milliseconds 200
        }
        if(!$owned.HasExited){$owned.Kill();$owned.WaitForExit();throw ('UI test timeout: '+$case.name)}
        $code=$owned.ExitCode
        if($code -ne 0){throw ('UI test exit '+$code+': '+$case.name)}
        $frames=@(Get-ChildItem -LiteralPath (Join-Path $data 'frames') -Filter '*.ppm')
        if($frames.Count -lt 3){throw ('Missing UI captures: '+$case.name)}
        if($case.name -eq 'resize') {
            $sizes=@()
            foreach($frame in $frames) {
                $reader=New-Object IO.StreamReader($frame.FullName)
                try {$magic=$reader.ReadLine();$sizes+=$reader.ReadLine()} finally {$reader.Dispose()}
            }
            foreach($expected in @('1280 720','960 540','640 360')) {
                if($sizes -notcontains $expected){throw ('Resize did not present '+$expected)}
            }
        }
        if($case.name -eq 'profile-controller') {
            if(!(Test-Path -LiteralPath (Join-Path $data 'active-profile.txt')) -or (Get-Content -LiteralPath (Join-Path $data 'active-profile.txt') -Raw).Trim() -ne 'p-123'){throw 'Virtual controller did not select the second profile.'}
        }
        $results+=[pscustomobject]@{case=$case.name;exit_code=$code;captures=$frames.Count;data=$data;scope='Native UI only; synthetic empty game data, no gameplay/performance claim'}
        $owned.Dispose();$owned=$null
        $results|ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $outputPath 'results.json') -Encoding UTF8
        Write-Output ('Passed '+$case.name+' ('+$frames.Count+' captures)')
    }
} finally {
    if($owned -and !$owned.HasExited){$owned.Kill();$owned.WaitForExit()}
    & $reservation -Action Release -LeaseId $claim.lease.lease_id
}
