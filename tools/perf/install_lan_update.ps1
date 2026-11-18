param([Parameter(Mandatory=$true)][string]$GameRoot,[switch]$DesktopShortcut,[string]$ShaderCacheRoot)
$ErrorActionPreference='Stop'
$root=(Resolve-Path -LiteralPath $GameRoot).Path
$source=Join-Path $root 'data'
if(!(Test-Path -LiteralPath (Join-Path $source 'game/wonder_game.dll'))){throw 'Choose the prepared SuperWonders folder containing data/game/wonder_game.dll'}
$active=Get-Process wonder,wonder-launcher,wonder-lan-updated,mkw,mkw-launcher -ErrorAction SilentlyContinue | Where-Object {
 try { !$_.Path -or $_.Path.StartsWith($root+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase) } catch { $true }
}
if($active){throw 'Close games running from this installation before installing the LAN copy'}
$info=[IO.File]::ReadAllText((Join-Path $PSScriptRoot 'BUILD-INFO.json')) | ConvertFrom-Json
if(!$info.compatible_game_sha256){throw 'Package is missing its matching game-code identities'}
$gameHash=(Get-FileHash -LiteralPath (Join-Path $source 'game/wonder_game.dll')).Hash.ToLowerInvariant()
if($gameHash -notin $info.compatible_game_sha256){throw 'This LAN update requires the matching tested r8 game code; no files changed'}
$cacheSource=$source
if($ShaderCacheRoot){
 $cacheSource=(Resolve-Path -LiteralPath $ShaderCacheRoot).Path
 $cacheGame=Join-Path $cacheSource 'game/wonder_game.dll'
 if(!(Test-Path -LiteralPath $cacheGame) -or (Get-FileHash -LiteralPath $cacheGame).Hash.ToLowerInvariant() -ne $gameHash){throw 'Warm shader cache belongs to different game code; no files changed'}
}
foreach($file in $info.files.PSObject.Properties){
 if((Get-FileHash -LiteralPath (Join-Path $PSScriptRoot $file.Name)).Hash.ToLowerInvariant() -ne $file.Value){throw ('Package hash mismatch: '+$file.Name)}
}
$dest=Join-Path $root 'LAN-Performance-R5'
if(Test-Path -LiteralPath $dest){throw 'LAN-Performance-R5 already exists; no files changed'}
New-Item -ItemType Directory -Path $dest | Out-Null
foreach($name in @('wonder-launcher.exe','wonder.exe','libc++.dll','libunwind.dll','BUILD-INFO.json','README-LAN.txt','PLAY LAN.cmd','LAN-TEST-RESULTS.json')){
 Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination (Join-Path $dest $name)
}
$data=Join-Path $dest 'data';New-Item -ItemType Directory -Path $data | Out-Null
foreach($name in @('analysis','extract','fonts','game','mods','spirv','user')){
 $target=Join-Path $source $name
 if(Test-Path -LiteralPath $target){New-Item -ItemType Junction -Path (Join-Path $data $name) -Target $target | Out-Null}
}
foreach($file in Get-ChildItem -LiteralPath $cacheSource -File){
 if($file.Name -in @('spirv.pack','spirv.journal') -or $file.Name -eq 'shader_programs.bin' -or $file.Name.StartsWith('pipeline') -or $file.Name.StartsWith('shader_prepare')){
  Copy-Item -LiteralPath $file.FullName -Destination (Join-Path $data $file.Name)
 }
}
$settingsPath=Join-Path $source 'pc-settings.ini'
$text=if(Test-Path -LiteralPath $settingsPath){[IO.File]::ReadAllText($settingsPath)}else{''}
foreach($pair in @(@('width',1280),@('height',720),@('internal_resolution',1),@('low_vram_profile',1),@('framegen',0),@('pipeline_skip_draws',0),@('vsync',1),@('antialiasing',0),@('anisotropy',0),@('upscaler',1),@('fullscreen',0),@('render_ahead',0),@('metric_game_speed',1),@('metric_frametime_graph',0))){
 $pattern='(?m)^'+$pair[0]+'=.*$'
 if($text -match $pattern){$text=$text -replace $pattern,($pair[0]+'='+$pair[1])}else{$text+="`r`n"+$pair[0]+'='+$pair[1]}
}
[IO.File]::WriteAllText((Join-Path $data 'pc-settings.ini'),$text,[Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllText((Join-Path $data 'launcher.ini'),"lan_rate=30`nlan_pipeline=1`n",[Text.UTF8Encoding]::new($false))
foreach($file in $info.files.PSObject.Properties){
 if((Get-FileHash -LiteralPath (Join-Path $dest $file.Name)).Hash.ToLowerInvariant() -ne $file.Value){throw ('Installed hash mismatch: '+$file.Name)}
}
if($DesktopShortcut){
 $link=Join-Path ([Environment]::GetFolderPath('Desktop')) 'SuperWonders LAN Updated.lnk'
 $shell=New-Object -ComObject WScript.Shell;$shortcut=$shell.CreateShortcut($link)
 if(Test-Path -LiteralPath $link){
  $old=Join-Path (Join-Path $root 'LAN-Performance-R4') 'PLAY LAN.cmd'
  if($shortcut.TargetPath -ne $old){throw 'Desktop shortcut points elsewhere; LAN folder installed without changing it'}
 }
 $shortcut.TargetPath=Join-Path $dest 'PLAY LAN.cmd';$shortcut.WorkingDirectory=$dest
 $shortcut.IconLocation=Join-Path $dest 'wonder-launcher.exe';$shortcut.Description='LAN runtime update; 30 Hz simulation and networking overlap selected';$shortcut.Save()
}
[ordered]@{installed=$dest;runtime_sha256=(Get-FileHash (Join-Path $dest 'wonder-launcher.exe')).Hash;original_runtime_sha256=(Get-FileHash (Join-Path $root 'wonder-launcher.exe')).Hash;settings_separate=$true;existing_saves_shared=(Test-Path -LiteralPath (Join-Path $source 'user'));desktop_shortcut=[bool]$DesktopShortcut} | ConvertTo-Json
