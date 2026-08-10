param(
    [Parameter(Mandatory = $true)]
    [string] $Dia2Dump,

    [Parameter(Mandatory = $true)]
    [string] $Pdb,

    [string] $SourceRoot = '',

    [string] $OutputDirectory = ''
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($SourceRoot)) {
    $SourceRoot = Split-Path -Parent $PSScriptRoot
}
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = $PSScriptRoot
}

$dia2DumpPath = (Resolve-Path -LiteralPath $Dia2Dump).Path
$pdbPath = (Resolve-Path -LiteralPath $Pdb).Path
$sourceRootPath = (Resolve-Path -LiteralPath $SourceRoot).Path

if (-not (Test-Path -LiteralPath $OutputDirectory)) {
    New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
}
$outputDirectoryPath = (Resolve-Path -LiteralPath $OutputDirectory).Path

$fileRecords = @{}
$compilandRecords = [System.Collections.Generic.List[object]]::new()
$currentCompiland = $null
$currentSources = [System.Collections.Generic.List[string]]::new()

function Add-CompilandRecord {
    param(
        [string] $Compiland,
        [System.Collections.Generic.List[string]] $Sources
    )

    if ([string]::IsNullOrWhiteSpace($Compiland)) {
        return
    }

    $objectName = [IO.Path]::GetFileName($Compiland)
    $objectBase = [IO.Path]::GetFileNameWithoutExtension($objectName)
    $matchingSources = @(
        $Sources |
            Where-Object {
                [IO.Path]::GetFileNameWithoutExtension($_) -ieq $objectBase
            }
    )
    $primarySource = if ($matchingSources.Count -gt 0) {
        $matchingSources[0]
    } elseif ($Sources.Count -gt 0) {
        $Sources[0]
    } else {
        ''
    }

    $compilandRecords.Add([pscustomobject]@{
        Object = $objectName
        Source = $primarySource
        OriginalCompiland = $Compiland
    })
}

& $dia2DumpPath -sf $pdbPath | ForEach-Object {
    $line = [string] $_

    if ($line -match '^Compiland = (.+)$') {
        Add-CompilandRecord -Compiland $currentCompiland -Sources $currentSources
        $currentCompiland = $Matches[1]
        $currentSources = [System.Collections.Generic.List[string]]::new()
        return
    }

    if ($line -notmatch '(?i)\\source\\public\\([^\r\n]+?) \(MD5: ([0-9A-F]{32})\)$') {
        return
    }

    $relativePath = ($Matches[1] -replace '\\', '/').ToLowerInvariant()
    $pdbMd5 = $Matches[2].ToUpperInvariant()
    $extension = [IO.Path]::GetExtension($relativePath).ToLowerInvariant()
    $localPath = Join-Path $sourceRootPath ($relativePath -replace '/', '\')

    if (-not $fileRecords.ContainsKey($relativePath)) {
        $status = 'missing'
        $localMd5 = ''
        if (Test-Path -LiteralPath $localPath -PathType Leaf) {
            $localMd5 = (Get-FileHash -LiteralPath $localPath -Algorithm MD5).Hash
            $status = if ($localMd5 -eq $pdbMd5) { 'exact' } else { 'different' }
        }

        $fileRecords[$relativePath] = [pscustomobject]@{
            Path = $relativePath
            Extension = $extension
            PdbMd5 = $pdbMd5
            LocalMd5 = $localMd5
            Status = $status
        }
    }

    if ($extension -in '.c', '.cc', '.cpp', '.cxx') {
        if (-not $currentSources.Contains($relativePath)) {
            $currentSources.Add($relativePath)
        }
    }
}

Add-CompilandRecord -Compiland $currentCompiland -Sources $currentSources

$fileManifestPath = Join-Path $outputDirectoryPath 'pdb_files.tsv'
$compilandManifestPath = Join-Path $outputDirectoryPath 'pdb_compilands.tsv'

$fileLines = [System.Collections.Generic.List[string]]::new()
$fileLines.Add("path`textension`tpdb_md5`tlocal_md5`tstatus")
$fileRecords.Values |
    Sort-Object Path |
    ForEach-Object {
        $fileLines.Add(
            "$($_.Path)`t$($_.Extension)`t$($_.PdbMd5)`t$($_.LocalMd5)`t$($_.Status)"
        )
    }
$fileLines | Set-Content -LiteralPath $fileManifestPath -Encoding utf8

$compilandLines = [System.Collections.Generic.List[string]]::new()
$compilandLines.Add("object`tsource`toriginal_compiland")
$compilandRecords |
    Sort-Object Object, Source -Unique |
    ForEach-Object {
        $compilandLines.Add(
            "$($_.Object)`t$($_.Source)`t$($_.OriginalCompiland)"
        )
    }
$compilandLines | Set-Content -LiteralPath $compilandManifestPath -Encoding utf8

Write-Output "PDB files:      $($fileRecords.Count)"
Write-Output "PDB compilands: $($compilandRecords.Count)"
Write-Output "File manifest:  $fileManifestPath"
Write-Output "Unit manifest:  $compilandManifestPath"
