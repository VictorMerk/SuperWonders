param([string]$Runtime,[string]$DataRoot,[string]$LocalAddress='',
      [string]$OutputRoot='', [string]$Preflight='', [string]$SessionId='', [string]$PeerId='', [switch]$DryRun)
$ErrorActionPreference='Stop'

function Assert-WonderNativeCapability([string]$Executable) {
    $manifest=Join-Path (Split-Path -Parent $Executable) 'wonder-features.json'
    if(!(Test-Path -LiteralPath $manifest -PathType Leaf)){throw 'This build has no verified native-LAN capability manifest.'}
    if((Get-Item -LiteralPath $manifest).Length -gt 65536){throw 'Capability manifest is too large.'}
    $feature=Get-Content -LiteralPath $manifest -Raw | ConvertFrom-Json
    if($feature.schema -ne 1 -or $feature.game -ne 'wonder' -or $feature.features.native_lan_provider -isnot [bool] -or !$feature.features.native_lan_provider){throw 'This runtime does not provide the experimental native-LAN backend.'}
    if((Get-FileHash -LiteralPath $Executable -Algorithm SHA256).Hash -ne $feature.runtime_sha256){throw 'Runtime and capability manifest do not match. Update both together.'}
    return $feature
}
function Test-WonderNativeIpv4([string]$Text) {
    $parsed=$null
    return ([Net.IPAddress]::TryParse($Text,[ref]$parsed) -and $parsed.AddressFamily -eq [Net.Sockets.AddressFamily]::InterNetwork -and $parsed.ToString() -ceq $Text)
}
function Test-WonderNativeCandidate($Candidate) {
    if(!$Candidate -or $Candidate.up -isnot [bool] -or !$Candidate.up -or
       $Candidate.loopback -isnot [bool] -or $Candidate.loopback -or
       $Candidate.point_to_point -isnot [bool] -or $Candidate.point_to_point -or
       $Candidate.gateway_known -isnot [bool] -or $Candidate.dns_known -isnot [bool] -or
       ($Candidate.gateway_known -and !(Test-WonderNativeIpv4 ([string]$Candidate.gateway))) -or
       (!$Candidate.gateway_known -and $Candidate.gateway) -or
       !(Test-WonderNativeIpv4 ([string]$Candidate.ipv4)) -or !(Test-WonderNativeIpv4 ([string]$Candidate.mask))){return $false}
    $octets=([Net.IPAddress]::Parse($Candidate.ipv4)).GetAddressBytes()
    $private=($octets[0] -eq 10 -or ($octets[0] -eq 172 -and $octets[1] -ge 16 -and $octets[1] -le 31) -or
              ($octets[0] -eq 192 -and $octets[1] -eq 168) -or ($octets[0] -eq 169 -and $octets[1] -eq 254))
    if(!$private){return $false}
    $maskBytes=([Net.IPAddress]::Parse($Candidate.mask)).GetAddressBytes()
    $maskBits=($maskBytes | ForEach-Object {[Convert]::ToString($_,2).PadLeft(8,'0')}) -join ''
    if($maskBits -notmatch '^1+0*$'){return $false}
    [uint64]$addressValue=0;[uint64]$maskValue=0
    foreach($octet in $octets){$addressValue=$addressValue*256+$octet}
    foreach($octet in $maskBytes){$maskValue=$maskValue*256+$octet}
    $hostMask=4294967295 -bxor $maskValue
    $hostPart=$addressValue -band $hostMask
    return !($hostMask -ge 3 -and ($hostPart -eq 0 -or $hostPart -eq $hostMask))
}
function Assert-WonderNativeDiagnosticIds([string]$SharedSession,[string]$Peer) {
    if([bool]$SharedSession -ne [bool]$Peer){throw 'Provide both SessionId and PeerId, or omit both. Use the same SessionId and a distinct PeerId on each PC.'}
    foreach($identifier in @($SharedSession,$Peer)) {
        if($identifier -and $identifier -cnotmatch '^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$'){throw 'Diagnostic identifiers must be 1..64 letters, digits, dots, underscores or hyphens. Use opaque IDs rather than personal names.'}
    }
}
function Assert-WonderNativePreflightReport($Report,[string]$Address,[int]$ExitCode=0) {
    if(!$Report -or !($Report.schema -is [int] -or $Report.schema -is [long]) -or $Report.schema -ne 1 -or
       $Report.source -isnot [string] -or $Report.source -cne 'host_os_metadata'){throw 'Unrecognized interface preflight report. Update the preflight helper with this build.'}
    if($ExitCode -ne 0 -or $Report.status -isnot [string] -or $Report.config_status -isnot [string] -or
       $Report.status -cne 'ok' -or $Report.config_status -cne 'ok') {
        $choices=@($Report.candidates | Where-Object {Test-WonderNativeCandidate $_} | ForEach-Object {"$($_.ipv4) (mask $($_.mask))"})
        $hint=if($choices.Count){' Eligible addresses on this PC: '+($choices -join ', ')+'. Specify -LocalAddress explicitly.'}else{' Check that an active private IPv4 interface is connected on this PC.'}
        throw ("Interface preflight failed: $($Report.status)."+$hint)
    }
    [long]$captured=0
    if(![long]::TryParse([string]$Report.captured_utc_unix_ms,[ref]$captured)){throw 'Interface preflight report has no valid capture time. Update the helper.'}
    $age=[DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()-$captured
    if($age -lt -5000 -or $age -gt 120000){throw 'Interface preflight report is stale. Run the metadata preflight again.'}
    $candidates=@($Report.candidates)
    if($Report.candidates -isnot [Array] -or
       !($Report.matching_candidates -is [int] -or $Report.matching_candidates -is [long]) -or
       !($Report.eligible_candidates -is [int] -or $Report.eligible_candidates -is [long]) -or
       $Report.matching_candidates -ne 1 -or $Report.eligible_candidates -gt 256 -or
       $Report.eligible_candidates -ne $candidates.Count -or
       (!$Address -and $candidates.Count -ne 1) -or
       @($candidates | Where-Object {!(Test-WonderNativeCandidate $_)}).Count -ne 0 -or
       !(Test-WonderNativeCandidate $Report.selected)){throw 'Interface preflight returned malformed or ineligible selection metadata.'}
    $matches=@($candidates | Where-Object {$_.ipv4 -ceq $Report.selected.ipv4 -and $_.mask -ceq $Report.selected.mask -and (Test-WonderNativeCandidate $_)})
    if($matches.Count -ne 1){throw 'Interface preflight selection is not one unique eligible candidate.'}
    if(($Address -and ($Report.selected.ipv4 -cne $Address -or $Report.requested_address -cne $Address)) -or
       (!$Address -and $Report.requested_address)){throw 'Interface preflight selected a different address than requested.'}
    # Persist only the documented address/operational metadata, even if an
    # independently supplied helper includes identifying extra properties.
    $clean=@($candidates | Select-Object ipv4,mask,up,loopback,point_to_point,gateway_known,gateway,dns_known)
    $selected=@($clean | Where-Object {$_.ipv4 -ceq $Report.selected.ipv4 -and $_.mask -ceq $Report.selected.mask})[0]
    return [pscustomobject]@{schema=1;source='host_os_metadata';status='ok';captured_utc_unix_ms=$captured;
        requested_address=$Report.requested_address;candidates=$clean;eligible_candidates=$clean.Count;matching_candidates=1;
        selected=$selected;config_status='ok';scope='Fresh eligible local interface metadata; no packet, room, latency or FPS certification.'}
}
function Get-WonderNativeLocalCandidates {
    # Read-only fallback for an explicit address only. No NIC IDs/MACs are
    # returned; internal identity is used solely to deduplicate one adapter.
    $seen=New-Object 'Collections.Generic.HashSet[string]'
    foreach($nic in [Net.NetworkInformation.NetworkInterface]::GetAllNetworkInterfaces()) {
        if($nic.OperationalStatus -ne [Net.NetworkInformation.OperationalStatus]::Up -or
           $nic.NetworkInterfaceType -in @([Net.NetworkInformation.NetworkInterfaceType]::Loopback,[Net.NetworkInformation.NetworkInterfaceType]::Tunnel,[Net.NetworkInformation.NetworkInterfaceType]::Ppp)){continue}
        foreach($entry in $nic.GetIPProperties().UnicastAddresses) {
            if($entry.Address.AddressFamily -ne [Net.Sockets.AddressFamily]::InterNetwork){continue}
            $candidate=[pscustomobject]@{ipv4=$entry.Address.ToString();mask=$entry.IPv4Mask.ToString();up=$true;loopback=$false;point_to_point=$false;gateway_known=$false;gateway=$null;dns_known=$false}
            $identity=$nic.Id+'|'+$candidate.ipv4+'|'+$candidate.mask
            if((Test-WonderNativeCandidate $candidate) -and $seen.Add($identity)){$candidate}
        }
    }
}
function Assert-WonderNativeAssignedAddress([string]$Address,$Candidates) {
    if(!(Test-WonderNativeIpv4 $Address)){throw 'LocalAddress must be a canonical IPv4 address assigned to this PC.'}
    $eligible=@($Candidates | Where-Object {Test-WonderNativeCandidate $_} | Select-Object ipv4,mask,up,loopback,point_to_point,gateway_known,gateway,dns_known)
    $matches=@($eligible | Where-Object {$_.ipv4 -ceq $Address})
    if($matches.Count -ne 1){throw 'LocalAddress is not one unique active eligible IPv4 address assigned to this PC. Run the native LAN preflight and choose an eligible address explicitly.'}
    return [pscustomobject]@{schema=1;source='dotnet_explicit_address';status='ok';selected=$matches[0];candidates=$eligible;eligible_candidates=$eligible.Count;
        scope='Assigned/up/private IPv4 validation only; runtime Nifm remains authoritative. No packet or gameplay test.'}
}
function Resolve-WonderNativePreflight([string]$Executable,[string]$Explicit) {
    if($Explicit) {
        if((Split-Path -Leaf $Explicit) -notin @('native_lan_preflight.exe','native-lan-preflight.exe')){throw 'Preflight must point to native_lan_preflight.exe (the metadata helper), not a game runtime or script.'}
        if(!(Test-Path -LiteralPath $Explicit -PathType Leaf)){throw 'The explicitly requested preflight helper does not exist.'}
        return (Resolve-Path -LiteralPath $Explicit).Path
    }
    foreach($name in @('native_lan_preflight.exe','native-lan-preflight.exe')) {
        $candidate=Join-Path (Split-Path -Parent $Executable) $name
        if(Test-Path -LiteralPath $candidate -PathType Leaf){return $candidate}
    }
    return ''
}
function Get-WonderNativePrelaunchEvidence([string]$Executable,[string]$Address,[string]$Helper='') {
    $tool=Resolve-WonderNativePreflight $Executable $Helper
    if($tool) {
        $arguments=@();if($Address){$arguments=@('--local-address',$Address)}
        $raw=@(& $tool @arguments);$code=$LASTEXITCODE
        $text=$raw -join "`n"
        if($text.Length -gt 262144){throw 'Interface preflight output is too large.'}
        try{$report=$text | ConvertFrom-Json}catch{throw 'Interface preflight did not return valid JSON; no game was started.'}
        return Assert-WonderNativePreflightReport $report $Address $code
    }
    if($Address){return Assert-WonderNativeAssignedAddress $Address @(Get-WonderNativeLocalCandidates)}
    Write-Warning 'Optional native LAN preflight helper is missing. Interface selection will be checked by the runtime; no interface has been guessed. Place native_lan_preflight.exe beside the runtime to validate before launch.'
    return [pscustomobject]@{schema=1;source='not_run';status='helper_missing';selected=$null;scope='No prelaunch interface selection check; runtime validation remains active.'}
}
function New-WonderNativeSessionMetadata([string]$RuntimeHash,[string]$Address,[string]$SharedSession,[string]$Peer,[string]$RunId,$Prelaunch,[DateTime]$Started) {
    Assert-WonderNativeDiagnosticIds $SharedSession $Peer
    return [ordered]@{schema=1;runtime_sha256=$RuntimeHash;mode='native_lan';run_id=$RunId;session_id=$SharedSession;peer_id=$Peer;
        native_lan=$true;local_address=$Address;preflight=$Prelaunch;started=$Started.ToUniversalTime().ToString('o');
        scope='Host-provider diagnostics; no whole-world synchronization certification.'}
}
function New-WonderNativeStartInfo([string]$Executable,[string]$Data,[string]$Address,[string]$Run) {
    $info=New-Object Diagnostics.ProcessStartInfo
    $info.FileName=$Executable;$info.WorkingDirectory=Split-Path -Parent $Executable
    $info.Arguments='--play';$info.UseShellExecute=$false
    foreach($key in @($info.EnvironmentVariables.Keys)) {
        if($key -like 'NX_LAN_*' -or $key -like 'NX_NATIVE_*' -or $key -like 'NX_STEAM_*' -or
           $key -like 'NX_TEST_*' -or $key -like 'NX_INPUT_*' -or $key -like 'NX_PROBE*' -or
           $key -in @('NX_HEADLESS','NX_HEADLESS_PRESENT','NX_MUTE','NX_FRAMETRACE','NX_AUDIO_DUMP','NX_LOG','SteamAppId','SteamGameId')) {
            $info.EnvironmentVariables.Remove($key)
        }
    }
    $info.EnvironmentVariables['NX_DATA_ROOT']=$Data
    $info.EnvironmentVariables['NX_NO_LAUNCHER']='1'
    $info.EnvironmentVariables['NX_NATIVE_LAN']='1'
    if($Address){$info.EnvironmentVariables['NX_NATIVE_LAN_ADDRESS']=$Address}
    if($Run) {
        $info.EnvironmentVariables['NX_LOG']=Join-Path $Run 'native.log'
        $info.EnvironmentVariables['NX_FRAMETRACE']=Join-Path $Run 'frametrace.csv'
    }
    return $info
}
if($MyInvocation.InvocationName -eq '.'){return}
if(!$Runtime -or !$DataRoot){throw 'Specify -Runtime <new wonder.exe> and -DataRoot <prepared data folder>.'}
$executable=(Resolve-Path -LiteralPath $Runtime).Path
$data=(Resolve-Path -LiteralPath $DataRoot).Path
if(!(Test-Path -LiteralPath $executable -PathType Leaf) -or !(Test-Path -LiteralPath $data -PathType Container)){throw 'Runtime/data paths are invalid.'}
$feature=Assert-WonderNativeCapability $executable
if($LocalAddress -and !(Test-WonderNativeIpv4 $LocalAddress)){throw 'LocalAddress must be a canonical IPv4 address assigned to this PC.'}
Assert-WonderNativeDiagnosticIds $SessionId $PeerId
$prelaunch=Get-WonderNativePrelaunchEvidence $executable $LocalAddress $Preflight
$runId=[Guid]::NewGuid().ToString('N') # local run only; never invent a shared session ID
if($DryRun) {
    $metadata=New-WonderNativeSessionMetadata $feature.runtime_sha256 $LocalAddress $SessionId $PeerId $runId $prelaunch ([DateTime]::UtcNow)
    $metadata['runtime']=$executable;$metadata['data_root']=$data;$metadata['launch_performed']=$false
    $metadata | ConvertTo-Json -Depth 8
    return
}
$claim=& (Join-Path $PSScriptRoot '../perf/game_test_reservation.ps1') -Action Acquire -Task 'Wonder game-native LAN experiment' -Workspace $PSScriptRoot -OwnerPid $PID | ConvertFrom-Json
if($claim.state -ne 'acquired'){$claim | ConvertTo-Json -Depth 5;exit 75}
$game=$null
try {
    if(Get-Process wonder,wonder-launcher,mkw,mkw-launcher -ErrorAction SilentlyContinue){throw 'A game started before this test. Try again after it closes.'}
    if(!$OutputRoot){$OutputRoot=Join-Path $data 'native-lan-runs'}
    $run=Join-Path $OutputRoot ([DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff'))
    New-Item -ItemType Directory -Path $run | Out-Null
    $started=[DateTime]::UtcNow
    New-WonderNativeSessionMetadata $feature.runtime_sha256 $LocalAddress $SessionId $PeerId $runId $prelaunch $started |
        ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $run 'session.json')
    $game=New-Object Diagnostics.Process
    $game.StartInfo=New-WonderNativeStartInfo $executable $data $LocalAddress $run
    if(!$game.Start()){throw 'Could not start Wonder.'}
    $retainedHandle=$game.Handle;$game.WaitForExit();$exitCode=$game.ExitCode
    $stats=Join-Path $data 'native-lan-stats.json'
    if((Test-Path -LiteralPath $stats) -and (Get-Item -LiteralPath $stats).LastWriteTimeUtc -ge $started){Copy-Item -LiteralPath $stats -Destination $run}
    @{exit_code=$exitCode;ended=[DateTime]::UtcNow.ToString('o')} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $run 'result.json')
} finally {
    if($game){$game.Dispose()}
    & (Join-Path $PSScriptRoot '../perf/game_test_reservation.ps1') -Action Release -LeaseId $claim.lease.lease_id
}
exit $exitCode
