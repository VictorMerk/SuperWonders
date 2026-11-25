# Developer-only Spacewar fixture. Default is a read-only preflight. Steam API
# initialization belongs to the optional runtime bootstrap, never this script.
param([string]$Runtime,[string]$DataRoot,[string]$DevRoot,[string]$SdkRoot,[string]$SteamClient,
      [switch]$DryRun,[switch]$Prepare,[switch]$Launch,[switch]$CopySteamApi)
$ErrorActionPreference='Stop'

function Test-WonderSteamNestedPath([string]$Child,[string]$Parent) {
    if(!$Child -or !$Parent){return $false}
    $a=[IO.Path]::GetFullPath($Child).TrimEnd('\','/')
    $b=[IO.Path]::GetFullPath($Parent).TrimEnd('\','/')
    return $a.Equals($b,[StringComparison]::OrdinalIgnoreCase) -or
        $a.StartsWith($b+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)
}

function Get-WonderSteamSdkLayout([string]$Root) {
    if(!$Root){return [pscustomobject]@{root=$null;complete=$false;missing=@('Steamworks SDK root');library=$null}}
    $full=[IO.Path]::GetFullPath($Root)
    if(!(Test-Path -LiteralPath (Join-Path $full 'public/steam/steam_api.h')) -and
        (Test-Path -LiteralPath (Join-Path $full 'sdk/public/steam/steam_api.h'))){$full=Join-Path $full 'sdk'}
    $required=@('public/steam/steam_api.h','public/steam/isteamremoteplay.h',
        'public/steam/isteamnetworkingsockets.h','redistributable_bin/win64/steam_api64.dll')
    $missing=@($required | Where-Object {!(Test-Path -LiteralPath (Join-Path $full $_) -PathType Leaf)})
    return [pscustomobject]@{root=$full;complete=($missing.Count -eq 0);missing=$missing;
        library=(Join-Path $full 'redistributable_bin/win64/steam_api64.dll')}
}

function Assert-WonderSteamCapability([string]$Executable) {
    $path=Join-Path (Split-Path -Parent $Executable) 'wonder-features.json'
    if(!(Test-Path -LiteralPath $path -PathType Leaf)){throw 'This runtime has no Steam development capability manifest.'}
    if((Get-Item -LiteralPath $path).Length -gt 65536){throw 'Invalid Steam development capability manifest.'}
    $feature=Get-Content -LiteralPath $path -Raw | ConvertFrom-Json
    if($feature.schema -ne 1 -or $feature.game -ne 'wonder' -or
        $feature.features.steam_dev_bootstrap -isnot [bool] -or !$feature.features.steam_dev_bootstrap) {
        throw 'This runtime does not provide the optional Steam development bootstrap.'
    }
    if((Get-FileHash -LiteralPath $Executable -Algorithm SHA256).Hash -ne $feature.runtime_sha256) {
        throw 'Runtime and Steam capability manifest do not match. Update both together.'
    }
    return $feature
}

function New-WonderSteamDevPlan([string]$Executable,[string]$Data,[string]$PrivateRoot,
    [string]$Sdk,[string]$Client,[Nullable[bool]]$ClientRunning=$null) {
    if(!$Executable -or !$Data -or !$PrivateRoot){throw 'Specify Runtime, DataRoot and an isolated private DevRoot.'}
    $runtimePath=[IO.Path]::GetFullPath($Executable);$dataPath=[IO.Path]::GetFullPath($Data)
    $devPath=[IO.Path]::GetFullPath($PrivateRoot)
    $problems=New-Object 'Collections.Generic.List[string]'
    if(!(Test-Path -LiteralPath $runtimePath -PathType Leaf)){$problems.Add('Runtime file is missing.')}
    $capability=$null
    if(Test-Path -LiteralPath $runtimePath -PathType Leaf) {
        try {$capability=Assert-WonderSteamCapability $runtimePath}
        catch {$problems.Add($_.Exception.Message)}
    }
    if(!(Test-Path -LiteralPath $dataPath -PathType Container)){$problems.Add('Prepared data folder is missing.')}
    if(!(($devPath -split '[\\/]') -icontains 'private')){$problems.Add('DevRoot must be inside a private directory.')}
    $ancestor=$devPath
    while($ancestor) {
        if(Test-Path -LiteralPath $ancestor) {
            if((Get-Item -LiteralPath $ancestor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) {
                $problems.Add('DevRoot and its existing ancestors must not be links.');break
            }
        }
        $parent=Split-Path -Parent $ancestor
        if(!$parent -or $parent -eq $ancestor){break};$ancestor=$parent
    }
    $sdkLayout=Get-WonderSteamSdkLayout $Sdk
    if(!$sdkLayout.complete){$problems.Add('A complete caller-provided Steamworks SDK layout is required; no SDK is downloaded.')}
    $runtimeDir=Split-Path -Parent $runtimePath
    foreach($source in @($runtimeDir,$dataPath,$sdkLayout.root)) {
        if($source -and ((Test-WonderSteamNestedPath $devPath $source) -or (Test-WonderSteamNestedPath $source $devPath))) {
            $problems.Add('DevRoot must be separate from runtime, prepared data and SDK directories.');break
        }
    }
    $library=$null
    foreach($candidate in @((Join-Path $runtimeDir 'wonder_game.dll'),(Join-Path $dataPath 'game/wonder_game.dll'))) {
        if(Test-Path -LiteralPath $candidate -PathType Leaf){$library=[IO.Path]::GetFullPath($candidate);break}
    }
    if(!$library){$problems.Add('Prepared wonder_game.dll is missing; this script never compiles game code.')}
    $clientPath=$null
    $clients=@($Client)
    if(${env:ProgramFiles(x86)}){$clients+=Join-Path ${env:ProgramFiles(x86)} 'Steam/steam.exe'}
    if($env:ProgramFiles){$clients+=Join-Path $env:ProgramFiles 'Steam/steam.exe'}
    foreach($candidate in $clients){if($candidate -and (Test-Path -LiteralPath $candidate -PathType Leaf)){$clientPath=[IO.Path]::GetFullPath($candidate);break}}
    $running=if($null -eq $ClientRunning){[bool](Get-Process steam -ErrorAction SilentlyContinue)}else{[bool]$ClientRunning}
    return [pscustomobject]@{schema=1;workflow='wonder-steam-dev';app_id=480;mode='dry-run';
        source_runtime=$runtimePath;source_data=$dataPath;game_library=$library;dev_root=$devPath;
        sdk=$sdkLayout;steam_client=$clientPath;steam_running=$running;
        ready_to_prepare=($problems.Count -eq 0);ready_to_launch_local_coop=($problems.Count -eq 0 -and $running);
        problems=@($problems);runtime_steam_support='Hash-bound build capability checked; actual Steam API initialization remains untested';
        runtime_steam_bootstrap_verified=($null -ne $capability);
        capability_manifest=(Join-Path $runtimeDir 'wonder-features.json');
        steam_api_initialized_by_launcher=$false;remote_play_together_verified=$false;p2p_transport_verified=$false;
        scope='Read-only paths/process inventory. AppID 480 alone does not initialize Steam or enable Remote Play Together.'}
}

function New-WonderSteamDevStartInfo($Plan,$Manifest,[string]$Run='') {
    $info=New-Object Diagnostics.ProcessStartInfo
    $info.FileName=Join-Path $Plan.dev_root 'wonder.exe'
    $info.WorkingDirectory=$Plan.dev_root;$info.Arguments='--play';$info.UseShellExecute=$false
    foreach($key in @($info.EnvironmentVariables.Keys)) {
        if(($key -like 'NX_*' -and $key -ne 'NX_GPU') -or $key -like 'Steam*'){$info.EnvironmentVariables.Remove($key)}
    }
    $info.EnvironmentVariables['NX_DATA_ROOT']=$Plan.source_data
    $info.EnvironmentVariables['NX_GAMELIB']=$Plan.game_library
    $info.EnvironmentVariables['NX_NO_LAUNCHER']='1'
    $info.EnvironmentVariables['NX_STEAM_DEV']='1'
    $info.EnvironmentVariables['NX_STEAM_API_LIBRARY']=$Manifest.steam_api_library
    $info.EnvironmentVariables['SteamAppId']='480';$info.EnvironmentVariables['SteamGameId']='480'
    if($Run) {
        $info.EnvironmentVariables['NX_LOG']=Join-Path $Run 'native.log'
        $info.EnvironmentVariables['NX_FRAMETRACE']=Join-Path $Run 'frametrace.csv'
        $info.EnvironmentVariables['NX_STEAM_STATS']=Join-Path $Run 'steam-dev-stats.json'
    }
    return $info
}

function Get-WonderSteamGameLibraryHash([string]$Library) {
    if(!(Test-Path -LiteralPath $Library -PathType Leaf) -or (Get-Item -LiteralPath $Library).Length -gt 512MB){throw 'Prepared game library is missing or exceeds the diagnostic hash limit. Choose a fresh DevRoot after preparing the intended game library.'}
    return (Get-FileHash -LiteralPath $Library -Algorithm SHA256).Hash.ToLowerInvariant()
}
function Assert-WonderSteamGameLibraryIdentity($Manifest) {
    if($Manifest.game_library_sha256 -isnot [string] -or $Manifest.game_library_sha256 -notmatch '^[0-9a-fA-F]{64}$') {
        throw 'Development manifest has no valid prepared game-library identity. Choose a fresh DevRoot; an older fixture is not silently trusted.'
    }
    if((Get-WonderSteamGameLibraryHash $Manifest.game_library) -ne $Manifest.game_library_sha256) {
        throw 'Prepared game library changed. Choose a fresh DevRoot for the intended game library; no recompile is required by this launcher.'
    }
}
function New-WonderSteamSessionMetadata($Manifest,[string]$RunId,[DateTime]$Started) {
    return [ordered]@{schema=1;mode='steam_dev';run_id=$RunId;runtime_sha256=$Manifest.runtime_sha256;
        planned_game_library_sha256=$Manifest.game_library_sha256;
        planned_game_library_identity=@{schema=1;status='file_hashed';candidate_source='explicit_environment_override';
            scope='Prepared file hash rechecked before launch; not loaded-module, source-build or ABI proof. The file can change after this observation.'};
        started=$Started.ToUniversalTime().ToString('o');app_id=480;
        scope='Local Steam API lifecycle test; no Remote Play or two-PC synchronization certification.'}
}

# Interpret only a fresh run's bounded runtime counters. Process exit zero and
# Steam's process presence cannot substitute for this evidence; these counters
# still do not prove a Remote Play session, controller delivery or P2P traffic.
function Read-WonderSteamDevEvidence([string]$Run) {
    $result=[ordered]@{state='missing';initialization_attempted=$null;initialization_succeeded=$null;
        callbacks_completed=$null;shutdown_completed=$null;runtime_status=$null;
        loader_error=$null;missing_exports=$null;remote_play_together_verified=$false;p2p_transport_verified=$false;
        scope='Runtime-reported local API counters only; no session, controller, transport or gameplay verification.'}
    $path=Join-Path $Run 'steam-dev-stats.json'
    if(!(Test-Path -LiteralPath $path -PathType Leaf)){return [pscustomobject]$result}
    $result.state='invalid'
    try {
        if((Get-Item -LiteralPath $path).Length -gt 65536){return [pscustomobject]$result}
        $stats=Get-Content -LiteralPath $path -Raw | ConvertFrom-Json
        if($stats.schema -ne 1 -or $stats.source -ne 'steam_dev_bootstrap' -or $stats.app_id -ne 480 -or
            $stats.checkpoint -notin @('initialization','shutdown','callback-failure') -or
            $stats.status -notin @('disabled','invalid-path','unsupported-platform','load-failed','missing-exports',
                'already-active','init-failed','init-exception','ready','busy','stopped','deferred','callback-failed','shutdown-failed')) {
            return [pscustomobject]$result
        }
        foreach($name in @('init_attempted','initialized_ever','remote_play_together_verified','p2p_transport_verified')) {
            if($stats.$name -isnot [bool]){return [pscustomobject]$result}
        }
        if($stats.remote_play_together_verified -or $stats.p2p_transport_verified){return [pscustomobject]$result}
        foreach($name in @('callback_calls','callback_completed','callback_failures','shutdown_calls','shutdown_completed')) {
            $value=$stats.$name
            if(($value -isnot [int] -and $value -isnot [long] -and $value -isnot [uint64]) -or $value -lt 0){return [pscustomobject]$result}
        }
        if(($stats.initialized_ever -and !$stats.init_attempted) -or
            ($stats.callback_completed -gt $stats.callback_calls) -or
            ($stats.callback_failures -gt ($stats.callback_calls-$stats.callback_completed)) -or
            ($stats.shutdown_calls -gt 1) -or ($stats.shutdown_completed -gt $stats.shutdown_calls) -or
            (!$stats.initialized_ever -and ($stats.callback_calls -ne 0 -or $stats.shutdown_calls -ne 0)) -or
            ($stats.status -eq 'ready' -and !$stats.initialized_ever)) {return [pscustomobject]$result}
        # The two diagnostic fields were added to schema 1; older valid runtime
        # snapshots remain readable, with unavailable diagnostics kept unknown.
        foreach($name in @('loader_error','missing_exports')) {
            if($null -ne $stats.$name) {
                $value=$stats.$name
                if(($value -isnot [int] -and $value -isnot [long]) -or $value -lt 0 -or $value -gt [uint32]::MaxValue){return [pscustomobject]$result}
                $result[$name]=$value
            }
        }
        $result.state='runtime-reported'
        $result.initialization_attempted=$stats.init_attempted
        $result.initialization_succeeded=$stats.initialized_ever
        $result.callbacks_completed=$stats.callback_completed
        $result.shutdown_completed=($stats.initialized_ever -and $stats.shutdown_calls -eq 1 -and $stats.shutdown_completed -eq 1)
        $result.runtime_status=$stats.status
    } catch {return [pscustomobject]$result}
    return [pscustomobject]$result
}

function Invoke-WonderSteamDev($Plan,[ValidateSet('DryRun','Prepare','Launch')][string]$Mode='DryRun',
    [switch]$CopyApi,[scriptblock]$ProcessLauncher) {
    if($Mode -eq 'DryRun'){return $Plan}
    if(!$Plan.ready_to_prepare){throw ('Steam development preflight failed: '+($Plan.problems -join ' '))}
    # A copied older runtime would silently ignore NX_STEAM_DEV. Recheck the
    # current source artifact even if the plan was prepared earlier.
    $null=Assert-WonderSteamCapability $Plan.source_runtime
    if($Mode -eq 'Launch' -and !$Plan.steam_running){throw 'Steam is not running. Start it manually before this later developer test; this script never starts Steam.'}
    $manifestPath=Join-Path $Plan.dev_root 'steam-dev-manifest.json'
    if(Test-Path -LiteralPath $Plan.dev_root) {
        if($Mode -eq 'Prepare' -or !(Test-Path -LiteralPath $manifestPath -PathType Leaf)){throw 'Choose a fresh DevRoot; existing files are never overwritten.'}
        if((Get-Item -LiteralPath $manifestPath).Length -gt 65536){throw 'Invalid development manifest.'}
        $manifest=Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
        if($manifest.workflow -ne 'wonder-steam-dev' -or $manifest.schema -ne 1 -or
            $manifest.source_runtime -ne $Plan.source_runtime -or $manifest.source_data -ne $Plan.source_data -or
            $manifest.game_library -ne $Plan.game_library){throw 'Development manifest does not match this runtime/data selection.'}
        if((Get-FileHash -LiteralPath (Join-Path $Plan.dev_root 'wonder.exe') -Algorithm SHA256).Hash -ne $manifest.runtime_sha256 -or
            (Get-FileHash -LiteralPath $Plan.source_runtime -Algorithm SHA256).Hash -ne $manifest.runtime_sha256){throw 'Prepared runtime differs; choose a fresh DevRoot for the new build.'}
        if([IO.File]::ReadAllText((Join-Path $Plan.dev_root 'steam_appid.txt')).Trim() -ne '480'){throw 'Development AppID file changed.'}
        $null=Assert-WonderSteamCapability (Join-Path $Plan.dev_root 'wonder.exe')
        $expectedApi=if($manifest.steam_api_copied){Join-Path $Plan.dev_root 'steam_api64.dll'}else{$Plan.sdk.library}
        if($manifest.steam_api_library -ne $expectedApi -or
            (Get-FileHash -LiteralPath $manifest.steam_api_library -Algorithm SHA256).Hash -ne $manifest.sdk_library_sha256 -or
            (Get-FileHash -LiteralPath $Plan.sdk.library -Algorithm SHA256).Hash -ne $manifest.sdk_library_sha256){throw 'Development SDK DLL selection changed; choose a fresh DevRoot.'}
    } else {
        New-Item -ItemType Directory -Path $Plan.dev_root | Out-Null
        Copy-Item -LiteralPath $Plan.source_runtime -Destination (Join-Path $Plan.dev_root 'wonder.exe')
        Copy-Item -LiteralPath $Plan.capability_manifest -Destination (Join-Path $Plan.dev_root 'wonder-features.json')
        $null=Assert-WonderSteamCapability (Join-Path $Plan.dev_root 'wonder.exe')
        # Only known host support libraries; never copy game/Steam DLLs from an
        # arbitrary installed game or overwrite the normal runtime directory.
        foreach($name in @('libc++.dll','libunwind.dll','libwinpthread-1.dll','libgcc_s_seh-1.dll','libstdc++-6.dll','SDL3.dll')) {
            $dependency=Join-Path (Split-Path -Parent $Plan.source_runtime) $name
            if(Test-Path -LiteralPath $dependency -PathType Leaf){Copy-Item -LiteralPath $dependency -Destination (Join-Path $Plan.dev_root $name)}
        }
        $api=$Plan.sdk.library
        if($CopyApi){$api=Join-Path $Plan.dev_root 'steam_api64.dll';Copy-Item -LiteralPath $Plan.sdk.library -Destination $api}
        [IO.File]::WriteAllText((Join-Path $Plan.dev_root 'steam_appid.txt'),"480`n",[Text.Encoding]::ASCII)
        $manifest=[pscustomobject]@{schema=1;workflow='wonder-steam-dev';app_id=480;
            source_runtime=$Plan.source_runtime;source_data=$Plan.source_data;game_library=$Plan.game_library;
            game_library_sha256=(Get-WonderSteamGameLibraryHash $Plan.game_library);
            runtime_sha256=(Get-FileHash -LiteralPath $Plan.source_runtime -Algorithm SHA256).Hash;
            steam_api_library=$api;sdk_root=$Plan.sdk.root;steam_api_copied=[bool]$CopyApi;
            sdk_library_sha256=(Get-FileHash -LiteralPath $Plan.sdk.library -Algorithm SHA256).Hash;
            sdk_provenance='Caller-provided Steamworks SDK layout; origin is not cryptographically certified by this launcher';
            steam_api_initialized_by_launcher=$false;remote_play_together_verified=$false;p2p_transport_verified=$false;
            scope='Isolated developer fixture. No game code/assets copied or compiled. Prepared data/configs/saves are untouched by this launcher; normal manual gameplay uses that data and may save normally.'}
        $manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $manifestPath -Encoding UTF8
    }
    if($Mode -eq 'Prepare'){return $manifest}
    Assert-WonderSteamGameLibraryIdentity $manifest
    if(!(Test-Path -LiteralPath $manifest.steam_api_library -PathType Leaf)){throw 'The caller-provided Steam SDK DLL is no longer available.'}
    $info=New-WonderSteamDevStartInfo $Plan $manifest
    if($ProcessLauncher){return & $ProcessLauncher $info} # CPU tests inject a recorder, never an app.
    $reservation=Join-Path $PSScriptRoot '../perf/game_test_reservation.ps1'
    if(!(Test-Path -LiteralPath $reservation -PathType Leaf)){throw 'Game reservation helper is required.'}
    $claim=& $reservation -Action Acquire -Task 'Wonder optional Steam developer local co-op test' -Workspace $Plan.dev_root -OwnerPid $PID | ConvertFrom-Json
    if($claim.state -ne 'acquired'){return [pscustomobject]@{state='blocked';launch_attempted=$false;reservation=$claim}}
    $game=$null
    try {
        if(Get-Process wonder,wonder-launcher,mkw,mkw-launcher -ErrorAction SilentlyContinue){throw 'Another game started; developer launch deferred.'}
        if(!(Get-Process steam -ErrorAction SilentlyContinue)){throw 'Steam stopped after preflight; developer launch deferred.'}
        $runId=[DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8)
        $run=Join-Path $Plan.dev_root ('runs/'+$runId)
        New-Item -ItemType Directory -Path $run | Out-Null
        New-WonderSteamSessionMetadata $manifest $runId ([DateTime]::UtcNow) |
            ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $run 'session.json') -Encoding UTF8
        $info=New-WonderSteamDevStartInfo $Plan $manifest $run
        $game=New-Object Diagnostics.Process;$game.StartInfo=$info
        if(!$game.Start()){throw 'Could not start the developer runtime.'}
        $handle=$game.Handle;$game.WaitForExit()
        $result=[pscustomobject]@{state='exited';exit_code=$game.ExitCode;steam_api_success_unverified=$true;
            steam_api_evidence=(Read-WonderSteamDevEvidence $run);dev_root=$Plan.dev_root;run_directory=$run}
        $result | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $run 'result.json') -Encoding UTF8
        return $result
    } finally {
        if($game){$game.Dispose()}
        & $reservation -Action Release -LeaseId $claim.lease.lease_id | Out-Null
    }
}

if($MyInvocation.InvocationName -eq '.'){return}
if(($DryRun -and ($Prepare -or $Launch)) -or ($Prepare -and $Launch)){throw 'Choose DryRun, Prepare or Launch.'}
if(!$DevRoot){$DevRoot=Join-Path $PSScriptRoot '../../private/steam-dev'}
if(!$SdkRoot){$SdkRoot=$env:STEAMWORKS_SDK}
if(!$SdkRoot){$SdkRoot=$env:STEAM_SDK_ROOT}
$plan=New-WonderSteamDevPlan $Runtime $DataRoot $DevRoot $SdkRoot $SteamClient
$mode=if($Launch){'Launch'}elseif($Prepare){'Prepare'}else{'DryRun'}
$result=Invoke-WonderSteamDev $plan $mode -CopyApi:$CopySteamApi
$result | ConvertTo-Json -Depth 8
if($mode -eq 'Launch' -and $null -ne $result.exit_code){exit $result.exit_code}
