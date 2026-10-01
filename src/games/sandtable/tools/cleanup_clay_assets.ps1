# Remove generated review files, never source models, textures, rules or live game GLBs.
[CmdletBinding(SupportsShouldProcess)]
param()
$ErrorActionPreference = 'Stop'
$taskRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$buildingRoot = Join-Path $taskRoot 'assets/models/procedural_building'
$clayRoot = Join-Path $buildingRoot 'clay'
$streetRoot = Join-Path $taskRoot 'assets/models/street_clay'
$targets = [Collections.Generic.List[string]]::new()
foreach ($folder in @($buildingRoot, $clayRoot, $streetRoot)) {
    foreach ($file in Get-ChildItem -LiteralPath $folder -File) {
        if ($file.Extension -eq '.log' -or $file.Name -match '\.blend[0-9]+$' -or
            ($file.Extension -in @('.png', '.gif') -and $file.Name -notin @('clay_full_kit.png', 'clay_shutter_animation.gif', 'street_catalog.png'))) {
            $targets.Add($file.FullName)
        }
    }
    $cache = Join-Path $folder '__pycache__'
    if (Test-Path -LiteralPath $cache) { $targets.Add($cache) }
}
foreach ($relative in @('door_animation_frames', 'module_kit_glb.zip', 'clay/shutter_animation_frames')) {
    $candidate = Join-Path $buildingRoot $relative
    if (Test-Path -LiteralPath $candidate) { $targets.Add($candidate) }
}
# Only discard clay exports when they are explicitly obsolete. Legacy game exports stay.
$status = Get-Content -LiteralPath (Join-Path $clayRoot 'source_status.json') -Raw | ConvertFrom-Json
$manifestPath = Join-Path $clayRoot 'export_manifest.json'
if ($status.export_required -and (Test-Path -LiteralPath $manifestPath)) {
    $export = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    $kit = Get-Content -LiteralPath (Join-Path $clayRoot 'kit_manifest.json') -Raw | ConvertFrom-Json
    if ($export.kit_revision -ne $kit.revision) {
        foreach ($name in @('modules', 'export_manifest.json', 'export_validation.json', 'clay_module_kit_glb.zip')) {
            $candidate = Join-Path $clayRoot $name
            if (Test-Path -LiteralPath $candidate) { $targets.Add($candidate) }
        }
    }
}
$removed = @()
foreach ($candidate in ($targets | Sort-Object -Unique)) {
    $resolved = (Resolve-Path -LiteralPath $candidate).Path
    if (-not $resolved.StartsWith($buildingRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase) -and
        -not $resolved.StartsWith($streetRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Cleanup target outside asset kit: $resolved"
    }
    $item = Get-Item -LiteralPath $resolved
    if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Refusing linked target: $resolved" }
    if ($item.PSIsContainer) {
        $files = @(Get-ChildItem -LiteralPath $resolved -Recurse -File)
        $bytes = ($files | Measure-Object -Property Length -Sum).Sum
        $count = $files.Count
    } else { $bytes = $item.Length; $count = 1 }
    if ($PSCmdlet.ShouldProcess($resolved, 'Remove obsolete generated asset files')) {
        Remove-Item -LiteralPath $resolved -Recurse -Force
        $removed += [pscustomobject]@{ path = [IO.Path]::GetRelativePath($taskRoot, $resolved); files = $count; bytes = $bytes }
    }
}
if ($removed.Count) {
    $report = [ordered]@{ removed = $removed; files = ($removed | Measure-Object -Property files -Sum).Sum; bytes = ($removed | Measure-Object -Property bytes -Sum).Sum }
    $report | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $clayRoot 'cleanup_report.json') -Encoding utf8
    [pscustomobject]@{ files = $report.files; bytes = $report.bytes } | ConvertTo-Json
}
