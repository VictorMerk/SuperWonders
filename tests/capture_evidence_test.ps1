$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '../tools/perf/capture_evidence.ps1')
function Check($Value,[string]$Message){if(!$Value){throw $Message}}
$tempBase=[IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$leaf='wonder-capture-evidence-'+[guid]::NewGuid().ToString('N')
$root=[IO.Path]::GetFullPath((Join-Path $tempBase $leaf))
$observer=$null
try {
    Check ((Get-WonderCaptureCompletionReason 'exited' 299 300) -eq 'exited') 'Ordinary completion below capture budget stays successful'
    Check ((Get-WonderCaptureCompletionReason 'exited' 300 300) -eq 'capture budget reached') 'Final exact-limit capture cannot pass between polls'
    Check ((Get-WonderCaptureCompletionReason 'exited' 301 300) -eq 'capture budget reached') 'Final oversized capture cannot pass after ordinary exit'
    Check ((Get-WonderCaptureCompletionReason 'timeout' 301 300) -eq 'timeout') 'Budget metadata preserves earlier timeout reason'
    Check ((Get-WonderCaptureCompletionReason 'foreign game started' 301 300) -eq 'foreign game started') 'Budget metadata preserves foreign-game reason'
    $source=Join-Path $root 'frames';$output=Join-Path $root 'evidence'
    New-Item -ItemType Directory -Path $source,$output | Out-Null
    Publish-WonderCaptureRequest -DataRoot $root -Count 3 -Every 1
    $request=Join-Path $root 'dump_frames'
    Check ([IO.File]::ReadAllText($request) -eq "3 1`n") 'Native request becomes visible with complete count and interval'
    $blocked=$false
    try{Publish-WonderCaptureRequest -DataRoot $root -Count 7 -Every 2}catch{$blocked=$true}
    Check ($blocked -and [IO.File]::ReadAllText($request) -eq "3 1`n") 'A pending native request cannot be overwritten'
    Check (@(Get-ChildItem -LiteralPath $root -Filter 'dump_frames.pending-*').Count -eq 0) 'Failed publication cleans only its temporary file'
    Remove-Item -LiteralPath $request
    # A real concurrent consumer exercises visibility, not a mirror of Move().
    Add-Type -TypeDefinition @'
using System;
using System.IO;
using System.Threading;
using System.Collections.Generic;
public sealed class WonderCaptureRequestObserver {
    public readonly List<string> Seen = new List<string>();
    public string Error;
    private readonly string path;
    private readonly Thread thread;
    private volatile bool stop;
    public WonderCaptureRequestObserver(string request) {
        path=request;thread=new Thread(Run);thread.IsBackground=true;thread.Start();
    }
    private void Run() {
        try {
            while(!stop) {
                if(File.Exists(path)) {
                    try { var bytes=File.ReadAllText(path);File.Delete(path);Seen.Add(bytes); }
                    catch(IOException) { }
                }
                Thread.Sleep(1);
            }
        } catch(Exception e) { Error=e.GetType().Name; }
    }
    public bool Finish() {stop=true;return thread.Join(5000);}
}
'@
    $observer=New-Object WonderCaptureRequestObserver($request)
    for($i=1;$i -le 48;++$i) {
        $deadline=[DateTime]::UtcNow.AddSeconds(5)
        while([IO.File]::Exists($request) -and [DateTime]::UtcNow -lt $deadline){Start-Sleep -Milliseconds 2}
        Check (![IO.File]::Exists($request)) 'Concurrent consumer completed within its deadline'
        Publish-WonderCaptureRequest -DataRoot $root -Count $i -Every 1
    }
    $deadline=[DateTime]::UtcNow.AddSeconds(5)
    while([IO.File]::Exists($request) -and [DateTime]::UtcNow -lt $deadline){Start-Sleep -Milliseconds 2}
    Check ($observer.Finish() -and !$observer.Error -and $observer.Seen.Count -eq 48) 'Concurrent native request consumer completed every publication'
    for($i=1;$i -le 48;++$i){Check ($observer.Seen[$i-1] -eq "$i 1`n") 'Consumer never observes an empty or partial request'}
    $observer=$null
    Assert-WonderEmptyCaptureDirectory (Join-Path $root 'missing')
    Assert-WonderEmptyCaptureDirectory $source
    $since=[DateTime]::UtcNow.AddMinutes(-1)
    $old=Join-Path $source 'old.ppm'
    [IO.File]::WriteAllBytes($old,(New-Object byte[] 50))
    (Get-Item -LiteralPath $old).LastWriteTimeUtc=$since.AddMinutes(-1)
    $blocked=$false
    try{Assert-WonderEmptyCaptureDirectory $source}catch{$blocked=$true}
    Check $blocked 'Preflight refuses even old PPMs before runtime can overwrite fixed names'
    $blocked=$false
    try{Assert-WonderEmptyCaptureDirectory $old}catch{$blocked=$true}
    Check $blocked 'Capture path cannot be an ordinary file'
    # Exercise the real runner's early guard too. This ordinary text file can
    # never execute as a game even if that guard regresses; no GPU is involved.
    $fakeRuntime=Join-Path $root 'not-a-game.exe'
    [IO.File]::WriteAllText($fakeRuntime,'not executable')
    $blockedOutput=Join-Path $root 'blocked-output'
    $failure=''
    try {
        & (Join-Path $PSScriptRoot '../tools/perf/run_wonder_source_smoke.ps1') `
            -Runtime $fakeRuntime -DataRoot $root -Output $blockedOutput
    } catch {$failure=$_.Exception.Message}
    Check ($failure -like 'Fixture captures already exist*' -and
        !(Test-Path -LiteralPath $blockedOutput) -and (Get-Item -LiteralPath $old).Length -eq 50) `
        'Actual runner rejects old captures before creating output or starting a process'
    foreach($entry in @(@('one.ppm',100),@('two.ppm',200),@('other.raw',500))) {
        [IO.File]::WriteAllBytes((Join-Path $source $entry[0]),(New-Object byte[] $entry[1]))
    }
    $usage=Get-WonderCaptureUsage $source $since
    Check ($usage.count -eq 2 -and $usage.bytes -eq 300) 'Budget counts only fresh PPM evidence'
    $stream=[IO.File]::OpenWrite((Join-Path $source 'one.ppm'))
    try{$stream.SetLength(110)}finally{$stream.Dispose()}
    $usage=Get-WonderCaptureUsage $source $since
    Check ($usage.bytes -eq 310) 'Budget sees capture growth'
    Move-WonderCaptureEvidence $usage.frames $output
    Check ((Get-WonderCaptureUsage $source $since).count -eq 0) 'Fresh captures are not left duplicated'
    Check ((Test-Path -LiteralPath $old) -and (Test-Path -LiteralPath (Join-Path $source 'other.raw'))) 'Older and unrelated evidence preserved'
    Check ((Get-Item -LiteralPath (Join-Path $output 'one.ppm')).Length -eq 110) 'Exact captured bytes retained'
    $duplicate=Join-Path $source 'one.ppm'
    [IO.File]::WriteAllBytes($duplicate,(New-Object byte[] 99))
    $rejected=$false
    try{Move-WonderCaptureEvidence @((Get-Item -LiteralPath $duplicate)) $output}catch{$rejected=$true}
    Check ($rejected -and (Get-Item -LiteralPath $duplicate).Length -eq 99 -and
        (Get-Item -LiteralPath (Join-Path $output 'one.ppm')).Length -eq 110) 'Existing output rejects replacement and retains source'
    $empty=Get-WonderCaptureUsage (Join-Path $root 'missing') $since
    Check ($empty.count -eq 0 -and $empty.bytes -eq 0) 'Missing capture directory has zero usage'
    Write-Output 'Capture evidence: fresh-byte accounting, growth, verified transfer and preservation passed'
} finally {
    if($observer){$null=$observer.Finish()}
    # This exact test-owned directory is the only recursive cleanup target.
    $resolved=[IO.Path]::GetFullPath($root)
    if($resolved -ne (Join-Path $tempBase $leaf) -or
       !$resolved.StartsWith($tempBase.TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)) {
        throw 'Refusing cleanup outside the test-owned temporary directory'
    }
    if(Test-Path -LiteralPath $resolved){Remove-Item -LiteralPath $resolved -Recurse -Force}
}
