param([Parameter(Mandatory)][string]$Sampler)
$ErrorActionPreference='Stop'
$dir=Join-Path ([IO.Path]::GetTempPath()) ('native-cpu-sampler-'+[guid]::NewGuid().ToString('N'))
$null=New-Item -ItemType Directory -Path $dir
$stop=Join-Path $dir 'stop'
$script=Join-Path $dir 'owned-worker.ps1'
[IO.File]::WriteAllText($script, @'
param([string]$StopFile)
$end=[DateTime]::UtcNow.AddSeconds(20)
while([DateTime]::UtcNow -lt $end -and !(Test-Path -LiteralPath $StopFile)){
 for($i=1;$i -lt 10000;$i++){$v=[Math]::Sqrt($i)}
}
'@)
$worker=$null
try {
 $worker=Start-Process powershell.exe -ArgumentList @('-NoProfile','-File',$script,$stop) -WindowStyle Hidden -PassThru
 Start-Sleep -Milliseconds 500
 $started=$worker.StartTime.ToUniversalTime().ToFileTimeUtc().ToString()
 $exe=$worker.Path
 $output=Join-Path $dir 'samples.csv'
 & $Sampler $worker.Id '1' $exe $output 1 4
 if($LASTEXITCODE -ne 3 -or (Test-Path $output)){throw 'Creation-time guard failed'}
 & $Sampler $worker.Id $started ($exe+'.wrong') $output 1 4
 if($LASTEXITCODE -ne 3 -or (Test-Path $output)){throw 'Executable-path guard failed'}
 & $Sampler $worker.Id $started $exe $output 61 4
 if($LASTEXITCODE -ne 2 -or (Test-Path $output)){throw 'Duration bound failed'}
 & $Sampler $worker.Id $started $exe $output 2 4
 if($LASTEXITCODE -ne 0){throw 'Owned process sampling failed'}
 $rows=@(Import-Csv -LiteralPath $output)
 if($rows.Count -lt 10 -or !@($rows|Where-Object {[uint64]$_.cpu_100ns -gt 0}).Count){throw 'No useful samples'}
 $hash=(Get-FileHash -LiteralPath $output).Hash
 & $Sampler $worker.Id $started $exe $output 1 4
 if($LASTEXITCODE -ne 4 -or (Get-FileHash -LiteralPath $output).Hash -ne $hash){throw 'Evidence overwrite guard failed'}
 [IO.File]::WriteAllText($stop,'normal quit')
 if(!$worker.WaitForExit(5000) -or $worker.ExitCode -ne 0){throw 'Worker did not resume and exit normally'}
 Write-Output ('PASS: identity/duration/evidence guards, '+$rows.Count+' samples, normal worker exit. Evidence: '+$dir)
} finally {
 [IO.File]::WriteAllText($stop,'normal quit')
 if($worker -and !$worker.HasExited){if(!$worker.WaitForExit(5000)){$worker.Kill();$worker.WaitForExit()}}
}
