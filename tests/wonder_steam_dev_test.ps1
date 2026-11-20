$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '../tools/play/Start-Wonder-SteamDev.ps1')
function Check([bool]$Ok,[string]$Message){if(!$Ok){throw $Message}}
function MustFail([scriptblock]$Action,[string]$Message){$failed=$false;try{& $Action | Out-Null}catch{$failed=$true};Check $failed $Message}
function Write-TestSteamFeatures([string]$Executable,[string]$Game='wonder',[int]$Schema=1,
    [bool]$Enabled=$true,[string]$Hash='') {
    if(!$Hash){$Hash=(Get-FileHash -LiteralPath $Executable -Algorithm SHA256).Hash}
    $json=@{schema=$Schema;game=$Game;runtime_sha256=$Hash;
        features=@{native_lan_provider=$false;steam_dev_bootstrap=$Enabled}} | ConvertTo-Json -Depth 4
    [IO.File]::WriteAllText((Join-Path (Split-Path -Parent $Executable) 'wonder-features.json'),$json)
}
$sandbox=Join-Path ([IO.Path]::GetTempPath()) ('wonder-steam-dev-test-'+[Guid]::NewGuid().ToString('N'))
$sandbox=[IO.Path]::GetFullPath($sandbox)
$old=@{}
try {
    $runtimeRoot=Join-Path $sandbox 'source/runtime';$data=Join-Path $sandbox 'source/data';$sdk=Join-Path $sandbox 'sdk'
    foreach($dir in @($runtimeRoot,(Join-Path $data 'game'),(Join-Path $data 'user/save'),
        (Join-Path $sdk 'public/steam'),(Join-Path $sdk 'redistributable_bin/win64'))){New-Item -ItemType Directory -Path $dir -Force | Out-Null}
    $runtime=Join-Path $runtimeRoot 'wonder.exe'
    [IO.File]::WriteAllText($runtime,'mock runtime, never execute')
    Write-TestSteamFeatures $runtime
    [IO.File]::WriteAllText((Join-Path $runtimeRoot 'steam_appid.txt'),'123')
    [IO.File]::WriteAllText((Join-Path $runtimeRoot 'steam_api64.dll'),'unrelated installed DLL, never stage')
    [IO.File]::WriteAllText((Join-Path $runtimeRoot 'libc++.dll'),'mock dependency')
    [IO.File]::WriteAllText((Join-Path $data 'game/wonder_game.dll'),'mock prepared game library')
    [IO.File]::WriteAllText((Join-Path $data 'pc-settings.ini'),'original settings')
    [IO.File]::WriteAllText((Join-Path $data 'user/save/save.bin'),'original save')
    foreach($name in @('steam_api.h','isteamremoteplay.h','isteamnetworkingsockets.h')) {
        [IO.File]::WriteAllText((Join-Path $sdk ('public/steam/'+$name)),'mock SDK layout only')
    }
    $api=Join-Path $sdk 'redistributable_bin/win64/steam_api64.dll'
    [IO.File]::WriteAllText($api,'mock SDK DLL, never load')
    $sourceFiles=@($runtime,(Join-Path $runtimeRoot 'wonder-features.json'),(Join-Path $runtimeRoot 'steam_appid.txt'),(Join-Path $runtimeRoot 'steam_api64.dll'),
        (Join-Path $data 'game/wonder_game.dll'),(Join-Path $data 'pc-settings.ini'),(Join-Path $data 'user/save/save.bin'),$api)
    $hashes=@{};foreach($file in $sourceFiles){$hashes[$file]=(Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash}
    $dev=Join-Path $sandbox 'private/dev'
    $plan=New-WonderSteamDevPlan $runtime $data $dev $sdk '' $true
    Check ($plan.ready_to_prepare -and $plan.ready_to_launch_local_coop -and $plan.runtime_steam_bootstrap_verified) 'Mock layout/capability preflight failed'
    $dry=Invoke-WonderSteamDev $plan
    Check ($dry.mode -eq 'dry-run' -and !(Test-Path -LiteralPath $dev)) 'Default dry run wrote files'
    Check (!$dry.remote_play_together_verified -and !$dry.p2p_transport_verified -and !$dry.steam_api_initialized_by_launcher) 'Preflight invented Steam feature success'
    $missing=New-WonderSteamDevPlan $runtime $data (Join-Path $sandbox 'private/missing-sdk') (Join-Path $sandbox 'absent') '' $true
    Check (!$missing.ready_to_prepare) 'Missing SDK was accepted'
    MustFail {Invoke-WonderSteamDev $missing 'Prepare'} 'Missing SDK preparation did not stop'
    Check (!(Test-Path -LiteralPath $missing.dev_root)) 'Missing SDK created a fixture'
    $script:unexpectedLaunch=$false
    foreach($case in @('missing','disabled','wrong-game','wrong-schema','stale-hash')) {
        $oldRoot=Join-Path $sandbox ('old-runtime-'+$case);New-Item -ItemType Directory -Path $oldRoot | Out-Null
        $oldRuntime=Join-Path $oldRoot 'wonder.exe';[IO.File]::WriteAllText($oldRuntime,'old/unverified runtime')
        switch($case) {
            'disabled' {Write-TestSteamFeatures $oldRuntime -Enabled $false}
            'wrong-game' {Write-TestSteamFeatures $oldRuntime -Game 'other-game'}
            'wrong-schema' {Write-TestSteamFeatures $oldRuntime -Schema 2}
            'stale-hash' {Write-TestSteamFeatures $oldRuntime -Hash ('0'*64)}
        }
        $oldPlan=New-WonderSteamDevPlan $oldRuntime $data (Join-Path $sandbox ('private/old-'+$case)) $sdk '' $true
        Check (!$oldPlan.ready_to_prepare -and !$oldPlan.runtime_steam_bootstrap_verified) "Invalid capability $case was accepted"
        MustFail {Invoke-WonderSteamDev $oldPlan 'Launch' -ProcessLauncher {$script:unexpectedLaunch=$true;throw 'unexpected launch'}} "Invalid capability $case did not block launch"
        Check (!$script:unexpectedLaunch -and !(Test-Path -LiteralPath $oldPlan.dev_root)) "Invalid capability $case reached preparation/launch"
    }
    $bad=New-WonderSteamDevPlan $runtime $data (Join-Path $runtimeRoot 'private/nested') $sdk '' $true
    Check (!$bad.ready_to_prepare) 'Installed runtime directory was allowed as DevRoot'
    $bad=New-WonderSteamDevPlan $runtime $data (Join-Path $sandbox 'public/dev') $sdk '' $true
    Check (!$bad.ready_to_prepare) 'Non-private destination was accepted'
    $stopped=New-WonderSteamDevPlan $runtime $data (Join-Path $sandbox 'private/no-steam') $sdk '' $false
    MustFail {Invoke-WonderSteamDev $stopped 'Launch' -ProcessLauncher {throw 'must not launch'}} 'Missing Steam process did not block manual launch'
    Check (!(Test-Path -LiteralPath $stopped.dev_root)) 'Blocked launch changed files'
    $manifest=Invoke-WonderSteamDev $plan 'Prepare'
    Check ($manifest.game_library_sha256 -eq $hashes[(Join-Path $data 'game/wonder_game.dll')].ToLowerInvariant()) 'Prepared game-library bytes were not pinned independently of runtime and SDK DLL'
    Check (([IO.File]::ReadAllText((Join-Path $dev 'steam_appid.txt'))).Trim() -eq '480') 'Isolated AppID is wrong'
    Check ($manifest.steam_api_library -eq $api -and !(Test-Path -LiteralPath (Join-Path $dev 'steam_api64.dll'))) 'External SDK path was replaced with installed/unofficial DLL'
    Check (!(Test-Path -LiteralPath (Join-Path $dev 'wonder_game.dll')) -and !(Test-Path -LiteralPath (Join-Path $dev 'data'))) 'Game library/assets were copied'
    Check (Test-Path -LiteralPath (Join-Path $dev 'libc++.dll')) 'Known host dependency was not staged'
    $stageFeature=Join-Path $dev 'wonder-features.json'
    Check ((Get-FileHash -LiteralPath $stageFeature -Algorithm SHA256).Hash -eq $hashes[(Join-Path $runtimeRoot 'wonder-features.json')]) 'Hash-bound capability manifest was not staged unchanged'
    MustFail {Invoke-WonderSteamDev $plan 'Prepare'} 'Existing developer fixture was overwritten'
    $manifestPath=Join-Path $dev 'steam-dev-manifest.json'
    $manifestText=[IO.File]::ReadAllText($manifestPath)
    foreach($badIdentity in @($null,'not-a-file-digest',('f'*64))) {
        $changedManifest=$manifestText | ConvertFrom-Json
        if($null -eq $badIdentity){$changedManifest.PSObject.Properties.Remove('game_library_sha256')}
        else{$changedManifest.game_library_sha256=$badIdentity}
        $changedManifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $manifestPath -Encoding UTF8
        $script:unexpectedLaunch=$false
        try {
            MustFail {Invoke-WonderSteamDev $plan 'Launch' -ProcessLauncher {$script:unexpectedLaunch=$true;return 'must not launch'}} 'Missing/malformed/misbound game-library identity was accepted'
            Check (!$script:unexpectedLaunch) 'Unpinned game library reached the injected process launch'
        } finally {[IO.File]::WriteAllText($manifestPath,$manifestText)}
    }
    $gameLibrary=Join-Path $data 'game/wonder_game.dll'
    $originalGameBytes=[IO.File]::ReadAllBytes($gameLibrary)
    try {
        [IO.File]::WriteAllText($gameLibrary,'different generated game at the same path, never execute')
        $script:unexpectedLaunch=$false
        MustFail {Invoke-WonderSteamDev $plan 'Launch' -ProcessLauncher {$script:unexpectedLaunch=$true;return 'must not launch'}} 'Changed game-library bytes at the same prepared path were accepted'
        Check (!$script:unexpectedLaunch) 'Changed game library reached the injected process launch'
        Remove-Item -LiteralPath $gameLibrary
        MustFail {Invoke-WonderSteamDev $plan 'Launch' -ProcessLauncher {$script:unexpectedLaunch=$true;return 'must not launch'}} 'Missing prepared game library was accepted'
        Check (!$script:unexpectedLaunch) 'Missing game library reached the injected process launch'
    } finally {[IO.File]::WriteAllBytes($gameLibrary,$originalGameBytes)}
    $session=New-WonderSteamSessionMetadata $manifest 'fixture-run' ([DateTime]::UtcNow)
    $session=$session | ConvertTo-Json -Depth 5 | ConvertFrom-Json
    Check ($session.planned_game_library_sha256 -eq $manifest.game_library_sha256 -and
           $session.planned_game_library_identity.candidate_source -eq 'explicit_environment_override' -and
           $session.planned_game_library_identity.scope -match 'not loaded-module' -and
           !$session.game_library_sha256 -and !$session.source_build_id) 'Steam session promoted a planned file pin into loaded/source/ABI proof'
    $values=@{NX_LAN_MODE='join';NX_NATIVE_LAN='1';NX_LAN_LOBBY_AUTOSTART='join';NX_TEST_VIRTUAL_ONLY='1';NX_INPUT_FRAMES='1:A:2';NX_HEADLESS='1';NX_STEAM_API_LIBRARY='wrong';NX_GPU='discrete';SteamAppId='999';SteamGameId='999'}
    foreach($key in $values.Keys){$old[$key]=[Environment]::GetEnvironmentVariable($key);[Environment]::SetEnvironmentVariable($key,$values[$key])}
    $script:recorded=$null
    $result=Invoke-WonderSteamDev $plan 'Launch' -ProcessLauncher {param($info)$script:recorded=$info;return [pscustomobject]@{exit_code=0;mock=$true}}
    $info=$script:recorded
    Check ($result.mock -and $info.FileName -eq (Join-Path $dev 'wonder.exe') -and $info.WorkingDirectory -eq $dev -and $info.Arguments -eq '--play') 'Incorrect isolated local-coop process selection'
    foreach($key in @('NX_LAN_MODE','NX_NATIVE_LAN','NX_LAN_LOBBY_AUTOSTART','NX_TEST_VIRTUAL_ONLY','NX_INPUT_FRAMES','NX_HEADLESS')){Check (!$info.EnvironmentVariables.ContainsKey($key)) "Inherited room/test setting $key"}
    Check ($info.EnvironmentVariables['NX_STEAM_DEV'] -eq '1' -and $info.EnvironmentVariables['NX_STEAM_API_LIBRARY'] -eq $api) 'Optional runtime bootstrap flags missing'
    Check ($info.EnvironmentVariables['NX_DATA_ROOT'] -eq $data -and $info.EnvironmentVariables['NX_GAMELIB'] -eq (Join-Path $data 'game/wonder_game.dll')) 'Prepared game selection changed'
    Check ($info.EnvironmentVariables['NX_GPU'] -eq 'discrete' -and $info.EnvironmentVariables['SteamAppId'] -eq '480' -and !$info.UseShellExecute) 'Controlled child environment failed'
    Check ($env:NX_LAN_MODE -eq 'join' -and $env:SteamAppId -eq '999') 'Parent/game environment was altered'
    $evidence=Join-Path $dev 'runs/mock-only'
    $withEvidence=New-WonderSteamDevStartInfo $plan $manifest $evidence
    Check ($withEvidence.EnvironmentVariables['NX_STEAM_STATS'] -eq (Join-Path $evidence 'steam-dev-stats.json') -and
           $withEvidence.EnvironmentVariables['NX_LOG'] -eq (Join-Path $evidence 'native.log') -and
           $withEvidence.EnvironmentVariables['NX_FRAMETRACE'] -eq (Join-Path $evidence 'frametrace.csv')) 'Per-run diagnostic paths escaped selected evidence directory'
    Check (!(Test-Path -LiteralPath $evidence)) 'Process description created evidence files without a real launch'

    $stageFeatureOriginal=[IO.File]::ReadAllText($stageFeature)
    Write-TestSteamFeatures (Join-Path $dev 'wonder.exe') -Enabled $false
    MustFail {Invoke-WonderSteamDev $plan 'Launch' -ProcessLauncher {$script:unexpectedLaunch=$true;throw 'unexpected launch'}} 'Disabled staged bootstrap capability was accepted'
    Check (!$script:unexpectedLaunch) 'Disabled staged capability reached process launch'
    [IO.File]::WriteAllText($stageFeature,$stageFeatureOriginal)
    $copyPlan=New-WonderSteamDevPlan $runtime $data (Join-Path $sandbox 'private/copied-sdk') $sdk '' $true
    $copied=Invoke-WonderSteamDev $copyPlan 'Prepare' -CopyApi
    Check ($copied.steam_api_copied -and $copied.steam_api_library -eq (Join-Path $copyPlan.dev_root 'steam_api64.dll')) 'SDK copy escaped isolated directory'
    [IO.File]::WriteAllText($copied.steam_api_library,'tampered')
    $script:unexpectedLaunch=$false
    MustFail {Invoke-WonderSteamDev $copyPlan 'Launch' -ProcessLauncher {$script:unexpectedLaunch=$true;throw 'unexpected launch'}} 'Changed SDK DLL was accepted'
    Check (!$script:unexpectedLaunch) 'Changed SDK DLL reached process launch'
    [IO.File]::WriteAllText((Join-Path $dev 'steam_appid.txt'),'999')
    MustFail {Invoke-WonderSteamDev $plan 'Launch' -ProcessLauncher {$script:unexpectedLaunch=$true;throw 'unexpected launch'}} 'Changed AppID was accepted'
    Check (!$script:unexpectedLaunch) 'Changed AppID reached process launch'
    [IO.File]::WriteAllText((Join-Path $dev 'steam_appid.txt'),'480')
    [IO.File]::WriteAllText((Join-Path $dev 'wonder.exe'),'tampered runtime')
    MustFail {Invoke-WonderSteamDev $plan 'Launch' -ProcessLauncher {$script:unexpectedLaunch=$true;throw 'unexpected launch'}} 'Changed staged runtime was accepted'
    Check (!$script:unexpectedLaunch) 'Changed staged runtime reached process launch'
    foreach($file in $sourceFiles){Check ((Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash -eq $hashes[$file]) 'Source runtime/AppID/SDK/game library/config/save was modified'}
    Write-Output 'Steam developer fixture: read-only preflight, isolated files, SDK-path control, prepared game-library hash pin/reuse rejection, scoped session identity, source preservation and mocked local-coop process passed; no Steam/game/API/client launched.'
} finally {
    foreach($key in $old.Keys){[Environment]::SetEnvironmentVariable($key,$old[$key])}
    # Exact temporary test tree only; never pass paths between shells for deletion.
    $temp=[IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\','/')+[IO.Path]::DirectorySeparatorChar
    if(!$sandbox.StartsWith($temp,[StringComparison]::OrdinalIgnoreCase) -or
        !(Split-Path -Leaf $sandbox).StartsWith('wonder-steam-dev-test-')){throw 'Unexpected test cleanup path'}
    if(Test-Path -LiteralPath $sandbox){Remove-Item -LiteralPath $sandbox -Recurse -Force}
}
