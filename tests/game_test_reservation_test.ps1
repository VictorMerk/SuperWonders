$ErrorActionPreference='Stop'
$tool=Join-Path $PSScriptRoot '../tools/perf/game_test_reservation.ps1'
$root=Join-Path ([IO.Path]::GetTempPath()) ('wonder-reservation-test-'+[Guid]::NewGuid().ToString('N'))
$null=New-Item -ItemType Directory -Path $root
$common=@{LeaseDirectory=$root;GameProcessNames=@('nonexistent-wonder-unit-test-process');Workspace=$root}
function Check($ok,$message){if(!$ok){throw $message}}
$raceJobs=@()
try {
    $first=& $tool -Action Acquire -Task 'unit-first' @common | ConvertFrom-Json
    Check ($first.state -eq 'acquired') 'First owner acquires atomically'
    $second=& $tool -Action Acquire -Task 'unit-second' @common | ConvertFrom-Json
    Check ($second.state -eq 'busy') 'A live owner prevents another acquisition'
    $rejected=$false
    try { & $tool -Action Release -LeaseId 'wrong-owner' @common | Out-Null } catch {$rejected=$true}
    Check $rejected 'Another owner cannot release the lease'
    $status=& $tool -Action Status @common | ConvertFrom-Json
    Check ($status.lease.lease_id -eq $first.lease.lease_id) 'Wrong release preserves current owner'
    & $tool -Action Release -LeaseId $first.lease.lease_id @common | Out-Null
    Check (!(Test-Path -LiteralPath (Join-Path $root 'current.json'))) 'Owner releases without touching other files'
    $stale=@{lease_id='stale-unit';owner_pid=$PID;owner_started_utc='1900-01-01T00:00:00Z'}
    $stale | ConvertTo-Json | Set-Content (Join-Path $root 'current.json')
    $next=& $tool -Action Acquire -Task 'unit-reclaim' @common | ConvertFrom-Json
    Check ($next.state -eq 'acquired') 'PID reuse/start-time mismatch can be reclaimed when no game runs'
    Check (@(Get-ChildItem $root -Filter 'stale-*.json').Count -eq 1) 'Stale evidence retained'
    & $tool -Action Release -LeaseId $next.lease.lease_id @common | Out-Null
    $raceJobs=@(1..2 | ForEach-Object {
        Start-Job -ScriptBlock {
            param($script,$folder,$owner)
            & $script -Action Acquire -Task 'unit-race' -OwnerPid $owner -Workspace $folder -LeaseDirectory $folder -GameProcessNames @('nonexistent-wonder-unit-test-process')
        } -ArgumentList $tool,$root,$PID
    })
    $null=$raceJobs | Wait-Job -Timeout 10
    Check (@($raceJobs | Where-Object State -ne 'Completed').Count -eq 0) 'Concurrent helper workers complete'
    $race=@($raceJobs | Receive-Job | ForEach-Object {$_ | ConvertFrom-Json})
    Check (@($race | Where-Object state -eq 'acquired').Count -eq 1) 'Exactly one simultaneous candidate wins'
    Check (@($race | Where-Object state -eq 'busy').Count -eq 1) 'The other simultaneous candidate waits'
    $winner=$race | Where-Object state -eq 'acquired'
    & $tool -Action Release -LeaseId $winner.lease.lease_id @common | Out-Null
    $busy=& $tool -Action Acquire -Task 'unit-game-busy' -LeaseDirectory $root -GameProcessNames @((Get-Process -Id $PID).ProcessName) | ConvertFrom-Json
    Check ($busy.state -eq 'busy') 'A running process blocks acquisition even without a lease'
    Write-Output 'Test reservation: exclusive owner, wrong-token protection, PID reuse, archival and process gate passed'
} finally {
    foreach($job in $raceJobs){if($job.State -eq 'Running'){Stop-Job $job};Remove-Job $job -Force}
    $resolved=[IO.Path]::GetFullPath($root)
    $temp=[IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\')+'\'
    if(!$resolved.StartsWith($temp,[StringComparison]::OrdinalIgnoreCase) -or !(Split-Path $resolved -Leaf).StartsWith('wonder-reservation-test-')){throw 'Unsafe temporary cleanup path'}
    Remove-Item -LiteralPath $resolved -Recurse -Force
}
