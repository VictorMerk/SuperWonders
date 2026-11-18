param(
    [ValidateSet('Status','Acquire','Release')][string]$Action='Status',
    [string]$Task='',
    [string]$Workspace=(Get-Location).Path,
    [int]$OwnerPid=$PID,
    [string]$LeaseId='',
    [string]$LeaseDirectory=(Join-Path $env:USERPROFILE '.t3/shared/game-tests'),
    [string[]]$GameProcessNames=@('wonder','wonder-launcher','mkw','mkw-launcher')
)
$ErrorActionPreference='Stop'
$directory=[IO.Path]::GetFullPath($LeaseDirectory)
$current=Join-Path $directory 'current.json'
$null=New-Item -ItemType Directory -Path $directory -Force

function Read-Lease {
    if(!(Test-Path -LiteralPath $current)){return $null}
    try {
        $value=Get-Content -LiteralPath $current -Raw | ConvertFrom-Json
        if(!$value.lease_id -or !$value.owner_pid -or !$value.owner_started_utc){return [pscustomobject]@{unreadable=$true}}
        return $value
    }
    catch { return [pscustomobject]@{unreadable=$true} }
}
function Owner-Alive($lease) {
    if(!$lease -or $lease.unreadable){return $false}
    $owner=Get-Process -Id $lease.owner_pid -ErrorAction SilentlyContinue
    if(!$owner){return $false}
    try { return $owner.StartTime.ToUniversalTime().ToString('o') -eq $lease.owner_started_utc }
    catch { return $true } # uncertain ownership is busy, never permission to steal
}
function Running-Games {
    return @(Get-Process -Name $GameProcessNames -ErrorAction SilentlyContinue | ForEach-Object {
        [pscustomobject]@{pid=$_.Id;name=$_.ProcessName;path=$_.Path}
    })
}
function Emit($value) { $value | ConvertTo-Json -Depth 6 -Compress }

if($Action -eq 'Status') {
    $lease=Read-Lease; $games=@(Running-Games)
    $state=if($games.Count){'game-running'}elseif(!$lease){'free'}elseif($lease.unreadable){'busy-unreadable'}elseif(Owner-Alive $lease){'reserved'}else{'stale'}
    Emit ([ordered]@{state=$state;lease_file=$current;lease=$lease;games=$games})
    return
}
if($Action -eq 'Release') {
    if(!$LeaseId){throw 'Release requires the lease ID returned by your own Acquire call.'}
    # Share delete, but deny competing readers/writers while validating and
    # atomically archiving this exact file. A newer lease cannot be removed.
    $stream=[IO.File]::Open($current,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Delete)
    try {
        $reader=New-Object IO.StreamReader($stream)
        $lease=$reader.ReadToEnd() | ConvertFrom-Json
        if($lease.lease_id -ne $LeaseId){throw 'This reservation belongs to another test; leave it untouched.'}
        $archive=Join-Path $directory ('released-'+$lease.lease_id+'.json')
        [IO.File]::Move($current,$archive)
    } finally { $stream.Dispose() }
    Emit ([ordered]@{state='released';lease_id=$LeaseId})
    return
}
if(!$Task.Trim()){throw 'Acquire requires a short task name.'}
$games=@(Running-Games)
if($games.Count){Emit ([ordered]@{state='busy';reason='A game is running; continue non-GPU work or wait.';games=$games});return}
$owner=Get-Process -Id $OwnerPid -ErrorAction Stop
$lease=[ordered]@{
    version=1;lease_id=[Guid]::NewGuid().ToString('N');owner_pid=$OwnerPid
    owner_started_utc=$owner.StartTime.ToUniversalTime().ToString('o')
    task=$Task;workspace=[IO.Path]::GetFullPath($Workspace)
    acquired_utc=[DateTime]::UtcNow.ToString('o')
}
if(Test-Path -LiteralPath $current) {
    $previous=Read-Lease
    if($previous.unreadable -or (Owner-Alive $previous)) {
        Emit ([ordered]@{state='busy';reason='Another live owner has reserved testing.';lease=$previous});return
    }
    # Reclaim only after both the recorded PID/start-time and game inventory
    # show inactivity. Preserve the stale record instead of deleting evidence.
    try {
        $stream=[IO.File]::Open($current,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Delete)
        try {
            $reader=New-Object IO.StreamReader($stream)
            $checked=$reader.ReadToEnd() | ConvertFrom-Json
            if($checked.lease_id -ne $previous.lease_id -or (Owner-Alive $checked) -or @(Running-Games).Count){
                Emit ([ordered]@{state='busy';reason='Reservation changed during stale check.'});return
            }
            [IO.File]::Move($current,(Join-Path $directory ('stale-'+[Guid]::NewGuid().ToString('N')+'.json')))
        } finally {$stream.Dispose()}
    } catch [IO.IOException] { Emit ([ordered]@{state='busy';reason='Another agent is updating the reservation.'});return }
}
try { $stream=[IO.File]::Open($current,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read) }
catch [IO.IOException] { Emit ([ordered]@{state='busy';reason='Another agent acquired the reservation first.'});return }
try {
    $bytes=[Text.Encoding]::UTF8.GetBytes(($lease | ConvertTo-Json -Compress))
    $stream.Write($bytes,0,$bytes.Length)
    $stream.Flush()
} finally {$stream.Dispose()}
# A legacy launcher may have started without consulting the reservation.
if(@(Running-Games).Count) {
    & $PSCommandPath -Action Release -LeaseId $lease.lease_id -LeaseDirectory $directory | Out-Null
    Emit ([ordered]@{state='busy';reason='A game started during acquisition.'});return
}
Emit ([ordered]@{state='acquired';lease_file=$current;lease=$lease})
