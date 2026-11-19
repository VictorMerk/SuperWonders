$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '../tools/play/Start-Wonder-NativeLan.ps1')
function Check($Value,$Message){if(!$Value){throw $Message}}
function Rejects([scriptblock]$Action,[string]$Message){$rejected=$false;try{& $Action | Out-Null}catch{$rejected=$true};Check $rejected $Message}
function Candidate([string]$Address='192.168.50.7',[string]$Mask='255.255.255.0') {
    return [pscustomobject]@{ipv4=$Address;mask=$Mask;up=$true;loopback=$false;point_to_point=$false;gateway_known=$false;gateway=$null;dns_known=$false}
}
function Report($Selected,$Candidates,[string]$Address='') {
    return [pscustomobject]@{schema=1;source='host_os_metadata';status='ok';config_status='ok';captured_utc_unix_ms=[DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds();
        candidates=@($Candidates);eligible_candidates=@($Candidates).Count;matching_candidates=1;selected=$Selected;requested_address=$Address}
}
$original=@{}
$keys=@('NX_LAN_MODE','NX_LAN_CODE','NX_NATIVE_LAN','NX_STEAM_DEV','NX_TEST_QUIT_FRAME','NX_INPUT_FRAMES','NX_HEADLESS','NX_DATA_ROOT','NX_FRAMEGEN')
try {
    foreach($key in $keys){$original[$key]=[Environment]::GetEnvironmentVariable($key);[Environment]::SetEnvironmentVariable($key,'stale')}
    $info=New-WonderNativeStartInfo 'C:\fixture\wonder.exe' 'C:\fixture\data' '192.168.1.10' 'C:\fixture\logs'
    Check ($info.EnvironmentVariables['NX_NATIVE_LAN'] -eq '1') 'Native provider flag missing'
    Check ($info.EnvironmentVariables['NX_NATIVE_LAN_ADDRESS'] -eq '192.168.1.10') 'Local interface selection missing'
    foreach($key in @('NX_LAN_MODE','NX_LAN_CODE','NX_STEAM_DEV','NX_TEST_QUIT_FRAME','NX_INPUT_FRAMES','NX_HEADLESS')){Check (!$info.EnvironmentVariables.ContainsKey($key)) "Stale $key leaked into native session"}
    Check ($info.EnvironmentVariables['NX_DATA_ROOT'] -eq 'C:\fixture\data') 'Prepared data root changed'
    Check ($info.EnvironmentVariables['NX_FRAMEGEN'] -eq 'stale') 'Unrelated preference was modified'
    $temp=Join-Path ([IO.Path]::GetTempPath()) ('wonder-native-launch-'+[Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $temp | Out-Null
    $runtime=Join-Path $temp 'wonder.exe';[IO.File]::WriteAllText($runtime,'fixture, never executable')
    $manifest=Join-Path $temp 'wonder-features.json'
    @{schema=1;game='wonder';runtime_sha256=(Get-FileHash -LiteralPath $runtime).Hash;features=@{native_lan_provider=$true}} | ConvertTo-Json | Set-Content -LiteralPath $manifest
    $null=Assert-WonderNativeCapability $runtime
    [IO.File]::AppendAllText($runtime,'different runtime')
    $rejected=$false;try{Assert-WonderNativeCapability $runtime | Out-Null}catch{$rejected=$true}
    Check $rejected 'Stale capability manifest accepted a different binary'
    Assert-WonderNativeDiagnosticIds 'session-a' 'peer-1'
    Assert-WonderNativeDiagnosticIds '' ''
    Rejects {Assert-WonderNativeDiagnosticIds 'session-a' ''} 'Unpaired diagnostic IDs accepted'
    Rejects {Assert-WonderNativeDiagnosticIds 'bad identifier' 'peer-1'} 'Unsafe diagnostic ID accepted'
    Rejects {Assert-WonderNativeDiagnosticIds ('x'*65) 'peer-1'} 'Unbounded diagnostic ID accepted'
    $a=Candidate;$b=Candidate '192.168.50.8'
    $valid=Report $a @($a,$b) $a.ipv4
    $checked=Assert-WonderNativePreflightReport $valid $a.ipv4
    Check ($checked.selected.ipv4 -eq $a.ipv4 -and $checked.eligible_candidates -eq 2) 'Explicit interface selection did not preserve eligible alternatives'
    $a | Add-Member NoteProperty id 'fixture-private-guid-never-log'
    $checked=Assert-WonderNativePreflightReport (Report $a @($a) $a.ipv4) $a.ipv4
    Check (($checked | ConvertTo-Json -Depth 8) -notmatch 'fixture-private-guid-never-log') 'Helper identifier leaked into persisted preflight evidence'
    Rejects {Assert-WonderNativePreflightReport (Report $a @($a,$b)) ''} 'Malformed successful report guessed one of multiple interfaces'
    Rejects {Assert-WonderNativePreflightReport $valid $b.ipv4} 'Different helper-selected address accepted'
    $stale=Report $a @($a);$stale.captured_utc_unix_ms-=121000
    Rejects {Assert-WonderNativePreflightReport $stale ''} 'Stale interface report accepted'
    $future=Report $a @($a);$future.captured_utc_unix_ms+=10000
    Rejects {Assert-WonderNativePreflightReport $future ''} 'Future-dated interface report accepted'
    $unknown=Report $a @($a);$unknown.schema=2
    Rejects {Assert-WonderNativePreflightReport $unknown ''} 'Unknown report schema accepted'
    $typed=Report $a @($a);$typed.schema='1'
    Rejects {Assert-WonderNativePreflightReport $typed ''} 'String schema number accepted as valid report'
    $typed=Report $a @($a);$typed.matching_candidates=$true
    Rejects {Assert-WonderNativePreflightReport $typed ''} 'Boolean candidate count accepted as integer'
    $malformed=Report $a @($a);$malformed.source='cached_fixture'
    Rejects {Assert-WonderNativePreflightReport $malformed ''} 'Wrong evidence source accepted'
    $noTime=Report $a @($a);$noTime.captured_utc_unix_ms=$null
    Rejects {Assert-WonderNativePreflightReport $noTime ''} 'Missing report timestamp accepted'
    $ambiguous=Report $null @($a,$b);$ambiguous.status='ambiguous';$ambiguous.config_status='ambiguous';$ambiguous.matching_candidates=2
    $hint='';try{Assert-WonderNativePreflightReport $ambiguous '' 2 | Out-Null}catch{$hint=$_.Exception.Message}
    Check ($hint -match '192.168.50.7' -and $hint -match '192.168.50.8' -and $hint -match 'LocalAddress') 'Ambiguity error did not explain explicit eligible choices'
    $offline=Report $null @();$offline.status='no_interface';$offline.config_status='no_interface';$offline.matching_candidates=0
    Rejects {Assert-WonderNativePreflightReport $offline '' 2} 'Offline preflight report accepted'
    Rejects {Assert-WonderNativePreflightReport (Report $a @($a)) '' 2} 'Nonzero helper exit code ignored'
    $assigned=Assert-WonderNativeAssignedAddress $a.ipv4 @($a,$b)
    Check ($assigned.source -eq 'dotnet_explicit_address' -and $assigned.selected.ipv4 -eq $a.ipv4) 'Assigned explicit fallback selection changed'
    Check (($assigned | ConvertTo-Json -Depth 8) -notmatch 'fixture-private-guid-never-log') 'Fallback evidence retained identifying extra fields'
    $metadata=New-WonderNativeSessionMetadata ('a'*64) $a.ipv4 'shared-session-a' 'peer-2' 'local-run-fixture' $checked ([DateTime]::UtcNow)
    $roundtrip=$metadata | ConvertTo-Json -Depth 8 | ConvertFrom-Json
    Check ($roundtrip.mode -eq 'native_lan' -and $roundtrip.runtime_sha256 -eq ('a'*64) -and
           $roundtrip.session_id -eq 'shared-session-a' -and $roundtrip.peer_id -eq 'peer-2' -and
           $roundtrip.run_id -eq 'local-run-fixture') 'Cross-PC diagnostic metadata was not preserved'
    $ungrouped=New-WonderNativeSessionMetadata ('a'*64) '' '' '' 'local-run-fixture' $checked ([DateTime]::UtcNow)
    Check (!$ungrouped.session_id -and !$ungrouped.peer_id) 'Launcher invented a shared session or peer identifier'
    Rejects {Assert-WonderNativeAssignedAddress '192.168.50.99' @($a,$b)} 'Unassigned but private address accepted'
    Rejects {Assert-WonderNativeAssignedAddress $a.ipv4 @($a,$a)} 'Same IPv4 on multiple adapters silently guessed'
    foreach($address in @('8.8.8.8','127.0.0.1','192.168.50.0','192.168.50.255')) {
        $invalid=Candidate $address
        Rejects {Assert-WonderNativeAssignedAddress $address @($invalid)} 'Public, loopback or subnet endpoint was eligible'
    }
    foreach($mask in @('0.0.0.0','255.0.255.0')){Check (!(Test-WonderNativeCandidate (Candidate $a.ipv4 $mask))) 'Noncontiguous/zero mask accepted'}
    foreach($field in @('up','loopback','point_to_point')) {
        $invalid=Candidate;$invalid.$field=($field -ne 'up')
        Check (!(Test-WonderNativeCandidate $invalid)) 'Down/loopback/point-to-point adapter accepted'
    }
    Check (Test-WonderNativeCandidate (Candidate '10.0.0.0' '255.255.255.254')) '/31 usable endpoint incorrectly rejected'
    Check (Test-WonderNativeCandidate (Candidate '169.254.1.2' '255.255.255.255')) '/32 link-local host incorrectly rejected'
    $savedWarning=$WarningPreference;$WarningPreference='SilentlyContinue'
    try{$missing=Get-WonderNativePrelaunchEvidence $runtime ''}finally{$WarningPreference=$savedWarning}
    Check ($missing.status -eq 'helper_missing' -and !$missing.selected) 'Missing optional helper guessed an interface or unexpectedly blocked baseline startup'
    Rejects {Resolve-WonderNativePreflight $runtime (Join-Path $temp 'missing.exe')} 'Explicitly required missing helper silently ignored'
    Rejects {Resolve-WonderNativePreflight $runtime $runtime} 'Game runtime accepted as metadata-only helper'
    Rejects {Resolve-WonderNativePreflight $runtime (Join-Path $temp 'native_lan_preflight.exe')} 'Missing explicitly named metadata helper silently ignored'
    $helper=Join-Path $temp 'native_lan_preflight.exe';[IO.File]::WriteAllText($helper,'fixture, never executable')
    Check ((Resolve-WonderNativePreflight $runtime '') -eq $helper) 'Adjacent preflight helper was not discovered'
    Write-Output 'Native launcher CPU checks passed: fresh/explicit interface validation, ambiguity/offline failures, safe missing-helper fallback and diagnostic IDs; no process or game launched.'
} finally {
    foreach($key in $keys){[Environment]::SetEnvironmentVariable($key,$original[$key])}
    if($temp) {
        # Only exact fixture files created by this test; no recursive cleanup.
        foreach($file in @($runtime,$manifest,$helper)){if($file -and (Test-Path -LiteralPath $file)){Remove-Item -LiteralPath $file}}
        Remove-Item -LiteralPath $temp
    }
}
