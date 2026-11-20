$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '../tools/play/Start-Wonder-LocalCoop.ps1')
$values=@{NX_NATIVE_LAN='1';NX_STEAM_DEV='1';SteamAppId='480';SteamGameId='480';NX_LAN_MODE='join';NX_LAN_LOBBY_AUTOSTART='join';NX_LAN_ADDRESS='192.168.2.1';NX_TEST_VIRTUAL_ONLY='1';NX_TEST_QUIT_FRAME='12';NX_INPUT_FRAMES='1:A:10';NX_HEADLESS='1';NX_HEADLESS_PRESENT='1';NX_NO_LAUNCHER='0';NX_DATA_ROOT='old';NX_GPU='discrete';NX_FRAMEGEN='2';NX_PLAY_SHOTS='map=map+680';NX_PLAY_FALLBACK='bot';NX_TRACE_SHADOW_SHOT='map';NX_TRACE_SHADOW_REQUEST='1';NX_DUMP_FRAMES='1';NX_DUMP_TEXTURES='5000000000';NX_OVERLAY_INPUT_PROBE='1';NX_FRAMEGEN_PROBE='1';NX_LAUNCHER_TOUR='30';NX_PIPELINE_MISS_TRACE='old.csv';NX_PIPELINE_CACHE_TRACE='1'}
$old=@{}
try {
    foreach($key in $values.Keys){$old[$key]=[Environment]::GetEnvironmentVariable($key);[Environment]::SetEnvironmentVariable($key,$values[$key])}
    $info=New-WonderLocalCoopStartInfo 'C:/Games/Wonder/wonder.exe' 'C:/Games/Wonder/data'
    foreach($key in $values.Keys){
        if($key -in @('NX_NO_LAUNCHER','NX_DATA_ROOT','NX_GPU','NX_FRAMEGEN')){continue}
        if($info.EnvironmentVariables.ContainsKey($key)){throw "Local co-op inherited diagnostic/room setting $key"}
    }
    if($info.EnvironmentVariables['NX_DATA_ROOT'] -ne 'C:/Games/Wonder/data' -or $info.EnvironmentVariables['NX_NO_LAUNCHER'] -ne '1'){throw 'Local co-op data selection failed'}
    if($info.EnvironmentVariables['NX_GPU'] -ne 'discrete'){throw 'A user GPU choice was lost'}
    if($info.EnvironmentVariables['NX_FRAMEGEN'] -ne '2'){throw 'A user rendering preference was lost'}
    if($env:NX_PLAY_SHOTS -ne 'map=map+680' -or $env:NX_TRACE_SHADOW_SHOT -ne 'map'){throw 'Parent diagnostic environment was changed'}
    if($env:NX_LAN_MODE -ne 'join'){throw 'The parent shell environment was changed'}
    if($info.UseShellExecute -or $info.Arguments -ne '--play'){throw 'Incorrect local game launch'}
    Write-Output 'Local co-op launch: independent child environment, physical controllers and data selection passed; no game launched.'
} finally {foreach($key in $old.Keys){[Environment]::SetEnvironmentVariable($key,$old[$key])}}
