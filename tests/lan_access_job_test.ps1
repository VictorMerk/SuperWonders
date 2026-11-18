$ErrorActionPreference='Stop'
$script=Join-Path $PSScriptRoot '../tools/perf/start_lan_access.ps1'
$tokens=$null; $errors=$null
$null=[Management.Automation.Language.Parser]::ParseFile((Resolve-Path $script).Path,[ref]$tokens,[ref]$errors)
if ($errors.Count) { throw ($errors | Out-String) }
$source=Get-Content -LiteralPath $script -Raw
$definition=[regex]::Match($source,"(?s)Add-Type -TypeDefinition @'\r?\n(.*?)\r?\n'@")
if (!$definition.Success) { throw 'No process-container definition found.' }
Add-Type -TypeDefinition $definition.Groups[1].Value
$root=Join-Path ([IO.Path]::GetTempPath()) ('superwonders-ssh-job-test-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $root | Out-Null
$pidFile=Join-Path $root 'child.txt'
$job=New-Object LanSshJob
$parent=$null; $child=$null
try {
    $body='Start-Sleep -Milliseconds 500; $p=Start-Process powershell.exe -ArgumentList ''-NoProfile -Command Start-Sleep -Seconds 60'' -WindowStyle Hidden -PassThru; $p.Id | Set-Content -LiteralPath '''+$pidFile.Replace("'","''")+'''; Start-Sleep -Seconds 60'
    $encoded=[Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($body))
    $parent=Start-Process powershell.exe -ArgumentList ('-NoProfile -EncodedCommand '+$encoded) -WindowStyle Hidden -PassThru
    $job.Assign($parent.Handle)
    $until=(Get-Date).AddSeconds(5)
    while (!(Test-Path -LiteralPath $pidFile)) {
        if ((Get-Date) -gt $until) { throw 'Contained parent did not create its child.' }
        Start-Sleep -Milliseconds 50
    }
    $child=Get-Process -Id ([int](Get-Content -LiteralPath $pidFile))
    $null=$child.Handle
    $job.Dispose()
    if (!$parent.WaitForExit(5000) -or !$child.WaitForExit(5000)) { throw 'A contained process survived access expiry.' }
    Write-Output 'LAN access: syntax and real parent/descendant process expiry passed.'
} finally {
    $job.Dispose()
    foreach($process in @($parent,$child)) {
        if($process) { $process.Refresh(); if(!$process.HasExited) { $process.Kill() } }
    }
    $full=[IO.Path]::GetFullPath($root)
    $temp=[IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\')+'\'
    if (!$full.StartsWith($temp,[StringComparison]::OrdinalIgnoreCase) -or [IO.Path]::GetFileName($full) -notmatch '^superwonders-ssh-job-test-[a-f0-9]{32}$') { throw 'Unsafe test cleanup path.' }
    Remove-Item -LiteralPath $full -Recurse -Force
}
