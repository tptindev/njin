[CmdletBinding()]
param(
    [string]$Blender = 'C:\Program Files\Blender Foundation\Blender 4.4\blender.exe',
    [ValidateSet('All', 'Building', 'Street', 'Railings')]
    [string]$Pack = 'All'
)

# Explicit/manual entry point. Never called by authoring scripts or game startup.
$ErrorActionPreference = 'Stop'
$taskRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
if (-not (Test-Path -LiteralPath $Blender -PathType Leaf)) {
    throw "Blender executable not found. Pass -Blender 'C:\path\to\blender.exe'."
}
$taskLogRoot = Join-Path ([IO.Path]::GetTempPath()) ('sandtable-retro-export-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $taskLogRoot | Out-Null

$taskJobs = @(
    @{ Name = 'Building'; Folder = 'assets/models/procedural_building/retro'; Source = 'modular_building_kit.blend';
       Exporter = 'assets/models/procedural_building/export_modules.py'; Validator = 'assets/models/procedural_building/validate_exports.py';
       Catalog = 'kit_manifest.json'; Entries = 'modules'; ReportCount = 'modules_checked'; Revision = 'kit_revision' },
    @{ Name = 'Street'; Folder = 'assets/models/street_retro'; Source = 'street_retro_kit.blend';
       Exporter = 'assets/models/street_retro/export_street_kit.py'; Validator = 'assets/models/street_retro/validate_exports.py';
       Catalog = 'asset_manifest.json'; Entries = 'assets'; ReportCount = 'assets_checked'; Revision = 'revision' },
    @{ Name = 'Railings'; Folder = 'assets/models/railings_retro'; Source = 'railings_retro_kit.blend';
       Exporter = 'assets/models/railings_retro/export_railings.py'; Validator = 'assets/models/railings_retro/validate_exports.py';
       Catalog = 'asset_manifest.json'; Entries = 'assets'; ReportCount = 'assets_checked'; Revision = 'revision' }
)
if ($Pack -ne 'All') { $taskJobs = @($taskJobs | Where-Object { $_.Name -eq $Pack }) }

function Read-TaskJson([string]$Path) {
    return Get-Content -Raw -LiteralPath $Path -Encoding UTF8 | ConvertFrom-Json
}
function Write-TaskJson([string]$Path, $Value) {
    [IO.File]::WriteAllText($Path, (($Value | ConvertTo-Json -Depth 100) + "`n"), [Text.UTF8Encoding]::new($false))
}
function Set-TaskProperty($Object, [string]$Name, $Value) {
    $Object | Add-Member -NotePropertyName $Name -NotePropertyValue $Value -Force
}
function Invoke-TaskBlender([string]$Name, [string[]]$Arguments) {
    $taskLog = Join-Path $taskLogRoot ($Name + '.log')
    & $Blender @Arguments *> $taskLog
    if ($LASTEXITCODE -ne 0) {
        Get-Content -LiteralPath $taskLog -Tail 20 | Write-Host
        throw "Blender failed: $Name. Full log: $taskLog"
    }
}

# Check all selected source/script paths before changing any export.
foreach ($taskJob in $taskJobs) {
    foreach ($taskRelative in @((Join-Path $taskJob.Folder $taskJob.Source),
             (Join-Path $taskJob.Folder $taskJob.Catalog), $taskJob.Exporter, $taskJob.Validator)) {
        if (-not (Test-Path -LiteralPath (Join-Path $taskRoot $taskRelative) -PathType Leaf)) {
            throw "Required file missing: $taskRelative"
        }
    }
}

$taskResults = @()
Write-Host 'Exporting saved Blender sources; unsaved edits in Blender are not included.'
foreach ($taskJob in $taskJobs) {
    $taskFolder = Join-Path $taskRoot $taskJob.Folder
    $taskSource = Join-Path $taskFolder $taskJob.Source
    $taskStatusPath = Join-Path $taskFolder 'source_status.json'
    $taskStatus = if (Test-Path -LiteralPath $taskStatusPath) { Read-TaskJson $taskStatusPath } else { [pscustomobject]@{} }
    Set-TaskProperty $taskStatus 'export_required' $true
    Set-TaskProperty $taskStatus 'export_validated' $false
    Set-TaskProperty $taskStatus 'glb_export_run' $false
    Set-TaskProperty $taskStatus 'note' 'Manual export in progress; this source is not marked ready until validation passes.'
    Write-TaskJson $taskStatusPath $taskStatus
    Write-Host ($taskJob.Name + ': exporting and validating...')
    Invoke-TaskBlender ($taskJob.Name + '-export') @('--factory-startup', '-b', $taskSource,
        '--python-exit-code', '1', '--python', (Join-Path $taskRoot $taskJob.Exporter))
    Invoke-TaskBlender ($taskJob.Name + '-validate') @('--factory-startup', '-b',
        '--python-exit-code', '1', '--python', (Join-Path $taskRoot $taskJob.Validator))
    $taskManifest = Read-TaskJson (Join-Path $taskFolder 'export_manifest.json')
    $taskReport = Read-TaskJson (Join-Path $taskFolder 'export_validation.json')
    $taskCatalog = Read-TaskJson (Join-Path $taskFolder $taskJob.Catalog)
    $taskEntries = @($taskManifest.($taskJob.Entries))
    $taskCatalogEntries = @($taskCatalog.($taskJob.Entries))
    if ($taskJob.Name -eq 'Building') { $taskCatalogEntries = @($taskCatalogEntries | Where-Object { $_.asset }) }
    $taskHash = (Get-FileHash -LiteralPath $taskSource -Algorithm SHA256).Hash.ToLowerInvariant()
    if (-not $taskReport.passed -or $taskReport.($taskJob.ReportCount) -ne $taskEntries.Count -or
        $taskEntries.Count -ne $taskCatalogEntries.Count -or
        $taskManifest.($taskJob.Revision) -ne $taskCatalog.revision -or $taskHash -ne $taskManifest.source_sha256) {
        throw "Incomplete or stale export for $($taskJob.Name). See $taskLogRoot"
    }
    $taskBytes = 0L
    foreach ($taskEntry in $taskEntries) { $taskBytes += (Get-Item -LiteralPath (Join-Path $taskFolder $taskEntry.path)).Length }
    Set-TaskProperty $taskCatalog 'glb_export_run' $true
    Write-TaskJson (Join-Path $taskFolder $taskJob.Catalog) $taskCatalog
    Set-TaskProperty $taskStatus 'kit_revision' $taskCatalog.revision
    Set-TaskProperty $taskStatus 'source_saved' $true
    Set-TaskProperty $taskStatus 'export_required' $false
    Set-TaskProperty $taskStatus 'export_validated' $true
    Set-TaskProperty $taskStatus 'glb_export_run' $true
    Set-TaskProperty $taskStatus 'source_sha256' $taskHash
    Set-TaskProperty $taskStatus 'note' 'Saved source exported and validated: hashes, complete catalog, pivots, bounds and animation where applicable.'
    Write-TaskJson $taskStatusPath $taskStatus
    $taskResults += [pscustomobject]@{ pack = $taskJob.Name; revision = $taskCatalog.revision; models = $taskEntries.Count; bytes = $taskBytes; validated = $true }
    Write-Host ("{0}: PASS, {1} GLBs, {2:N2} MB" -f $taskJob.Name, $taskEntries.Count, ($taskBytes / 1000000))
}
$taskSummary = [pscustomobject]@{ passed = $true; selection = $Pack; packs = $taskResults;
    total_models = ($taskResults | Measure-Object -Property models -Sum).Sum;
    total_bytes = ($taskResults | Measure-Object -Property bytes -Sum).Sum; logs = $taskLogRoot }
Write-TaskJson (Join-Path $taskRoot 'assets/models/retro_export_status.json') $taskSummary
Write-Host ("DONE: {0} validated GLBs. Logs: {1}" -f $taskSummary.total_models, $taskLogRoot)
