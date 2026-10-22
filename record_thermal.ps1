param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[A-Za-z0-9_-]+$')]
    [string]$Session,

    [string]$Port = 'COM7'
)

$ErrorActionPreference = 'Stop'
$folder = Join-Path $PSScriptRoot 'data\raw'
New-Item -ItemType Directory -Path $folder -Force | Out-Null

$stamp = Get-Date -Format 'yyyyMMdd_HHmmss_fff'
$base = Join-Path $folder "${Session}_$stamp"
$rows = [System.Collections.Generic.List[object]]::new()
$log = [System.Collections.Generic.List[string]]::new()

$rejected = 0
$timeouts = 0
$sequenceBreaks = 0
$timeBreaks = 0
$readErrors = 0
$previousSequence = $null
$previousTime = $null
$lastPhase = ''

$serial = [System.IO.Ports.SerialPort]::new(
    $Port, 115200,
    [System.IO.Ports.Parity]::None,
    8,
    [System.IO.Ports.StopBits]::One
)
$serial.ReadTimeout = 2500

Write-Host '420-second training recording.'
Write-Host 'Keep the sensor supported and the wires slack.'
$null = Read-Host 'Leave the sensor untouched, then press Enter'

try {
    $serial.Open()
    $serial.DiscardInBuffer()

    # Discard a possible partial line after opening the port.
    $null = $serial.ReadLine()

    $timer = [System.Diagnostics.Stopwatch]::StartNew()

    while ($timer.Elapsed.TotalSeconds -lt 420) {
        $elapsed = $timer.Elapsed.TotalSeconds

        $phase = if ($elapsed -lt 60) {
            'REST: leave sensor untouched'
        } elseif ($elapsed -lt 120) {
            'WARM: hold hand close to sensor'
        } elseif ($elapsed -lt 240) {
            'COOL: remove hand; leave sensor untouched'
        } elseif ($elapsed -lt 300) {
            'WARM: hold hand close to sensor'
        } else {
            'COOL: remove hand; leave sensor untouched'
        }

        if ($phase -ne $lastPhase) {
            Write-Host (
                '[{0}s] {1}' -f [int]$elapsed, $phase
            ) -ForegroundColor Cyan
            $lastPhase = $phase
        }

        try {
            $line = $serial.ReadLine().Trim()
        }
        catch [System.TimeoutException] {
            $timeouts++
            continue
        }

        $log.Add($line)

        if ($line.StartsWith('#') -or
            $line -eq 'seq,time_ms,temperature_centi_c,status') {
            continue
        }

        if ($line -notmatch '^(\d+),(\d+),(-?\d*),(OK|READ_ERROR)$') {
            $rejected++
            continue
        }

        $sequence = [long]$Matches[1]
        $deviceTime = [long]$Matches[2]
        $temperature = $Matches[3]
        $status = $Matches[4]

        if (($status -eq 'OK' -and $temperature -notmatch '^-?\d+$') -or
            ($status -eq 'READ_ERROR' -and $temperature -ne '')) {
            $rejected++
            continue
        }

        if ($null -ne $previousSequence) {
            if (($sequence - $previousSequence) -ne 1) {
                $sequenceBreaks++
            }

            $step = $deviceTime - $previousTime
            if ($step -lt 950 -or $step -gt 1050) {
                $timeBreaks++
            }
        }

        if ($status -ne 'OK') {
            $readErrors++
        }

        $rows.Add([PSCustomObject]@{
            session_id = $Session
            host_elapsed_s = [math]::Round(
                $timer.Elapsed.TotalSeconds, 3
            )
            seq = $sequence
            time_ms = $deviceTime
            temperature_centi_c = $temperature
            status = $status
        })

        $previousSequence = $sequence
        $previousTime = $deviceTime
    }
}
finally {
    if ($serial.IsOpen) {
        $serial.Close()
    }
    $serial.Dispose()

    if ($rows.Count -gt 0) {
        $rows | Export-Csv "$base.csv" -NoTypeInformation -Encoding ascii
    }
    $log | Set-Content "$base.serial.txt" -Encoding ascii
}

[PSCustomObject]@{
    File = "$base.csv"
    Samples = $rows.Count
    RejectedLines = $rejected
    Timeouts = $timeouts
    SequenceBreaks = $sequenceBreaks
    TimeBreaks = $timeBreaks
    SensorReadErrors = $readErrors
} | Format-List
