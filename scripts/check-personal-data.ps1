# Fails if personal information appears in the given files, folders or zip
# archives: the current Windows user name and profile path, the git user name
# and e-mail, any absolute C:\Users\... path, Xbox profile/save folders and
# game files. Run it on the repository before every push and on every release
# archive before uploading (build-release.ps1 does this automatically).
param([Parameter(Mandatory)][string[]]$Path, [string[]]$ExtraPattern = @())
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem

$patterns = [System.Collections.Generic.List[string]]::new()
foreach ($value in @($env:USERNAME, (git config user.name 2>$null), (git config user.email 2>$null)) + $ExtraPattern) {
    if ($value -and $value.Length -ge 3) { $patterns.Add([regex]::Escape($value)) }
}
if ($env:USERPROFILE) {
    $patterns.Add([regex]::Escape($env:USERPROFILE))
    $patterns.Add([regex]::Escape($env:USERPROFILE.Replace('\', '/')))
}
$patterns.Add('[A-Za-z]:[\\/]Users[\\/]')
$regex = [regex]::new(($patterns -join '|'), 'IgnoreCase')
# Files that must never be published (saves, profiles, logs, game data).
$forbidden = [regex]::new('(^|[\\/])(userdata|logs|captures)([\\/]|$)|\.(log|dmp|rdc|iso|xex|xexp|sfd|xwb|xgs|afs|stfs)$|(^|[\\/])launcher\.ini$|B13EBABEBABEBABE', 'IgnoreCase')
# Files allowed to mention C:\Users in documentation of this check itself.
$selfName = 'check-personal-data.ps1'

$problems = [System.Collections.Generic.List[string]]::new()
function Test-Text([string]$name, [byte[]]$bytes) {
    if ($forbidden.IsMatch($name)) { $problems.Add("forbidden file: $name"); return }
    if ($name -like "*$selfName") { return }
    foreach ($text in @([Text.Encoding]::UTF8.GetString($bytes), [Text.Encoding]::Unicode.GetString($bytes))) {
        $m = $regex.Match($text)
        if ($m.Success) {
            $start = [Math]::Max(0, $m.Index - 30)
            $snippet = $text.Substring($start, [Math]::Min(90, $text.Length - $start)) -replace '[^\x20-\x7E]', '.'
            $problems.Add("$name : ...$snippet...")
            return
        }
    }
}

foreach ($item in $Path) {
    $resolved = Resolve-Path -LiteralPath $item
    if ((Get-Item -LiteralPath $resolved).PSIsContainer) {
        $base = (Get-Item -LiteralPath $resolved).FullName
        Get-ChildItem -LiteralPath $base -Recurse -File -Force |
            Where-Object { $_.FullName -notmatch '[\\/]\.git[\\/]' -and $_.FullName -notmatch '[\\/]sdk[\\/]rexglue-sdk[\\/](thirdparty|out)[\\/]' } |
            ForEach-Object { Test-Text $_.FullName.Substring($base.Length + 1) ([IO.File]::ReadAllBytes($_.FullName)) }
    } elseif ($resolved.Path -like '*.zip') {
        $zip = [IO.Compression.ZipFile]::OpenRead($resolved.Path)
        try {
            foreach ($entry in $zip.Entries) {
                if (-not $entry.Name) { continue }
                if ($entry.FullName -match '(^|/)sdk/rexglue-sdk/thirdparty/') { if ($forbidden.IsMatch($entry.FullName)) { $problems.Add("forbidden file: $($entry.FullName)") }; continue }
                $stream = $entry.Open(); $memory = [IO.MemoryStream]::new()
                $stream.CopyTo($memory); $stream.Dispose()
                Test-Text "$($resolved.Path | Split-Path -Leaf)!$($entry.FullName)" $memory.ToArray()
            }
        } finally { $zip.Dispose() }
    } else {
        Test-Text $resolved.Path ([IO.File]::ReadAllBytes($resolved.Path))
    }
}

if ($problems.Count) {
    $problems | ForEach-Object { Write-Host "PERSONAL DATA: $_" -ForegroundColor Red }
    throw "$($problems.Count) file(s) contain personal data or must not be published."
}
Write-Host "No personal data found in: $($Path -join ', ')" -ForegroundColor Green
