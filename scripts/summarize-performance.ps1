param([Parameter(Mandatory = $true)][string]$CsvPath)

$samples = Import-Csv -LiteralPath $CsvPath | Where-Object { [double]$_.fps -gt 0 }
if (-not $samples) {
    Write-Host 'No usable frame samples were written.'
    exit 0
}
$fps = $samples | ForEach-Object { [double]$_.fps }
$frameTimes = $samples | ForEach-Object { [double]$_.frame_time_us / 1000 }
$averageFps = ($fps | Measure-Object -Average).Average
$averageMs = ($frameTimes | Measure-Object -Average).Average
$sortedFps = $fps | Sort-Object
$lowIndex = [Math]::Max(0, [Math]::Floor(($sortedFps.Count - 1) * 0.01))
Write-Host ("Performance summary: {0:N1} FPS average, {1:N1} ms average frame time, {2:N1} FPS 1%% low ({3} frames)." -f $averageFps, $averageMs, $sortedFps[$lowIndex], $samples.Count)
