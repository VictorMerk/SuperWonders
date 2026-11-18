$ErrorActionPreference='Stop'
$base=Join-Path ([IO.Path]::GetTempPath()) ('SuperWonders-LAN-Installer-'+[guid]::NewGuid().ToString('N'))
# Retain this tiny synthetic kit for inspection; it contains no game or user data.
$package=Join-Path $base 'package';$kit=Join-Path $base 'kit';$data=Join-Path $kit 'data'
New-Item -ItemType Directory $package,"$data/game","$data/user/save","$data/spirv" | Out-Null
Copy-Item (Join-Path $PSScriptRoot '../tools/perf/install_lan_update.ps1') "$package/Install-LAN-Update.ps1"
foreach($file in @('wonder-launcher.exe','wonder.exe','libc++.dll','libunwind.dll','README-LAN.txt','PLAY LAN.cmd','LAN-TEST-RESULTS.json')){[IO.File]::WriteAllText("$package/$file",('fixture '+$file))}
[IO.File]::WriteAllText("$kit/wonder-launcher.exe",'original runtime')
[IO.File]::WriteAllText("$data/game/wonder_game.dll",'fixture game code')
[IO.File]::WriteAllText("$data/user/save/test.sav",'original save')
[IO.File]::WriteAllText("$data/spirv.pack",'warm packed shaders')
[IO.File]::WriteAllText("$data/spirv.journal",'warm journal shaders')
[IO.File]::WriteAllText("$data/pc-settings.ini","width=1920`nheight=1080`nframegen=1`npipeline_skip_draws=1`n")
$info=@{compatible_game_sha256=@('wrong');files=@{}}
foreach($file in @('wonder-launcher.exe','wonder.exe','libc++.dll','libunwind.dll')){$info.files[$file]=(Get-FileHash "$package/$file").Hash.ToLowerInvariant()}
$info | ConvertTo-Json -Depth 4 | Set-Content "$package/BUILD-INFO.json" -Encoding UTF8
$rejected=$false
try{& "$package/Install-LAN-Update.ps1" -GameRoot $kit | Out-Null}catch{if($_ -notmatch 'matching tested r8'){throw};$rejected=$true}
if(!$rejected -or (Test-Path "$kit/LAN-Performance-R5")){throw 'Mismatched game code was not rejected before mutation'}
$info.compatible_game_sha256=@((Get-FileHash "$data/game/wonder_game.dll").Hash.ToLowerInvariant())
$info | ConvertTo-Json -Depth 4 | Set-Content "$package/BUILD-INFO.json" -Encoding UTF8
$null=& "$package/Install-LAN-Update.ps1" -GameRoot $kit
$dest="$kit/LAN-Performance-R5/data"
if([IO.File]::ReadAllText("$dest/spirv.journal") -ne 'warm journal shaders'){throw 'Shader journal missing'}
if([IO.File]::ReadAllText("$dest/spirv.pack") -ne 'warm packed shaders'){throw 'Shader pack missing'}
if([IO.File]::ReadAllText("$dest/launcher.ini") -notmatch 'lan_rate=30\nlan_pipeline=1'){throw 'LAN defaults missing'}
if([IO.File]::ReadAllText("$dest/pc-settings.ini") -notmatch 'pipeline_skip_draws=0'){throw 'Performance settings missing'}
if([IO.File]::ReadAllText("$data/user/save/test.sav") -ne 'original save'){throw 'Original save changed'}
if([IO.File]::ReadAllText("$kit/wonder-launcher.exe") -ne 'original runtime'){throw 'Original runtime changed'}
if((Get-Item "$dest/user").LinkType -ne 'Junction'){throw 'User data sharing missing'}
# A validated QA cache can seed the separate install without touching original caches.
$warm=Join-Path $base 'warm';$kit2=Join-Path $base 'kit-warm'
New-Item -ItemType Directory "$warm/game","$kit2/data/game" | Out-Null
[IO.File]::WriteAllText("$kit2/data/game/wonder_game.dll",'fixture game code')
[IO.File]::WriteAllText("$kit2/wonder-launcher.exe",'original runtime')
[IO.File]::WriteAllText("$warm/game/wonder_game.dll",'different game code')
[IO.File]::WriteAllText("$warm/spirv.journal",'verified warm journal')
$rejected=$false
try{& "$package/Install-LAN-Update.ps1" -GameRoot $kit2 -ShaderCacheRoot $warm | Out-Null}catch{if($_ -notmatch 'different game code'){throw};$rejected=$true}
if(!$rejected -or (Test-Path "$kit2/LAN-Performance-R5")){throw 'Wrong warm-cache identity was not rejected before mutation'}
Copy-Item "$data/game/wonder_game.dll" "$warm/game/wonder_game.dll" -Force
$null=& "$package/Install-LAN-Update.ps1" -GameRoot $kit2 -ShaderCacheRoot $warm
if([IO.File]::ReadAllText("$kit2/LAN-Performance-R5/data/spirv.journal") -ne 'verified warm journal'){throw 'Verified QA cache was not retained'}
# Recompiled libraries for the same supported game may share an identical shader catalogue.
$kit3=Join-Path $base 'kit-compatible';New-Item -ItemType Directory "$kit3/data/game" | Out-Null
Copy-Item "$data/game/wonder_game.dll" "$kit3/data/game/wonder_game.dll"
[IO.File]::WriteAllText("$kit3/wonder-launcher.exe",'original runtime')
[IO.File]::WriteAllText("$kit3/data/shader_programs.bin",'matching catalogue')
[IO.File]::WriteAllText("$warm/game/wonder_game.dll",'compatible recompiled game')
$info.compatible_game_sha256+=@((Get-FileHash "$warm/game/wonder_game.dll").Hash.ToLowerInvariant())
$info | ConvertTo-Json -Depth 4 | Set-Content "$package/BUILD-INFO.json" -Encoding UTF8
[IO.File]::WriteAllText("$warm/shader_programs.bin",'different catalogue')
$rejected=$false
try{& "$package/Install-LAN-Update.ps1" -GameRoot $kit3 -ShaderCacheRoot $warm | Out-Null}catch{if($_ -notmatch 'catalogue differs'){throw};$rejected=$true}
if(!$rejected -or (Test-Path "$kit3/LAN-Performance-R5")){throw 'Mismatched compatible-build catalogue accepted'}
Copy-Item "$kit3/data/shader_programs.bin" "$warm/shader_programs.bin" -Force
$null=& "$package/Install-LAN-Update.ps1" -GameRoot $kit3 -ShaderCacheRoot $warm
if([IO.File]::ReadAllText("$kit3/LAN-Performance-R5/data/spirv.journal") -ne 'verified warm journal'){throw 'Matching compatible-build cache was not retained'}
[ordered]@{success=$true;checks=@('reject wrong game code before mutation','retain both shader cache files','persist 30Hz and overlap defaults','set performance options','preserve original runtime and save','create expected user junction','reject mismatched warm cache','retain verified QA cache','reject differing shader catalogue','reuse matching catalogue across supported compiled libraries');fixture=$base} | ConvertTo-Json
