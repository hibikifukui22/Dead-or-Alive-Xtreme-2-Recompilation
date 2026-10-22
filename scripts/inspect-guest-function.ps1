[CmdletBinding()]
param(
    [Parameter(Mandatory)][string[]]$Address,
    [switch]$Assembly
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$names = ($Address | ForEach-Object {
    $name = $_ -replace '^(sub_|0x)', ''
    if ($name -notmatch '^[0-9a-fA-F]{8}$') { throw "Invalid guest address: $_" }
    $name.ToUpperInvariant()
}) -join '|'
$files = & rg -l "DEFINE_REX_FUNC\(sub_($names)\)" (Join-Path $root 'generated/default') -g '*.cpp'
foreach ($file in $files) {
    $raw = Get-Content -Raw -LiteralPath $file
    foreach ($match in [regex]::Matches($raw, '(?ms)^DEFINE_REX_FUNC\(sub_(' + $names + ')\).*?(?=^DEFINE_REX_FUNC|\z)')) {
        "SOURCE: $file"
        if ($Assembly) {
            $match.Value -split "`n" | Where-Object { $_ -match '^DEFINE|^loc_|// ' }
        } else {
            $match.Value
        }
    }
}
