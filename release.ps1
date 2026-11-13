param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('\Av[0-9][0-9A-Za-z]*(?:[._+-][0-9A-Za-z]+)*\z')]
    [ValidateLength(2, 80)]
    [string]$Version,
    [Parameter(Mandatory = $true)]
    [string]$ChangesPath,
    [switch]$Test
)
$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $PSScriptRoot

# The caller supplies both the release version and its actual changes; neither is inferred.
$changes = (Get-Content -LiteralPath $ChangesPath -Raw -Encoding UTF8)
if ([string]::IsNullOrWhiteSpace($changes)) { throw 'Release changes must not be empty.' }
$changes = $changes.Trim()
$name = "sdr-eotf-patcher-$Version"
$dist = Join-Path $PSScriptRoot 'dist'
if (Test-Path -LiteralPath $dist) {
    if ((Get-Item -LiteralPath $dist).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Release output cannot be a link.' }
}
$releaseDirectory = Join-Path $dist $name
$archive = Join-Path $dist ($name + '.zip')
foreach ($destination in @($releaseDirectory, $archive, ($archive + '.sha256'))) {
    if (Test-Path -LiteralPath $destination) { throw "Release already exists; history will not be overwritten: $destination" }
}

& (Join-Path $PSScriptRoot 'build.ps1') -Test:$Test
. (Join-Path $PSScriptRoot 'tools\package_manifest.ps1')
$package = Join-Path $PSScriptRoot 'build\package'
$files = @(Get-PackageFileNames -PackageDirectory $package)
# A unique staging directory avoids touching any historical release, including on failure.
$stage = Join-Path $PSScriptRoot ('build\release-' + [Guid]::NewGuid().ToString('N'))
$stagedDirectory = Join-Path $stage $name
New-Item -ItemType Directory -Path $stagedDirectory -Force | Out-Null
foreach ($file in $files) { Copy-Item -LiteralPath (Join-Path $package $file) -Destination $stagedDirectory }

$encoding = New-Object Text.UTF8Encoding($false)
foreach ($readme in @('README.md', 'README_zh-CN.md')) {
    $path = Join-Path $stagedDirectory $readme
    $text = [IO.File]::ReadAllText($path)
    if ($text -notmatch '\A# [^\r\n]+') { throw "Missing README title: $readme" }
    $text = [regex]::Replace($text, '\A# [^\r\n]+', '# SDR EOTF Patcher for Windows HDR')
    [IO.File]::WriteAllText($path, $text, $encoding)
}
$notes = [IO.File]::ReadAllText((Join-Path $package 'RELEASE_NOTES.md'))
$notes = [regex]::Replace($notes, '\A# [^\r\n]+\r?\n\s*', '')
$binaryHeader = [regex]::Match($notes, '(?s)\A## Applicable binary / 适用的二进制\r?\n.*?(?=\r?\n## |\z)')
if (-not $binaryHeader.Success -or $binaryHeader.Value -notmatch '\bdwmcore\.dll\b' -or $binaryHeader.Value -notmatch '\b[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+\b' -or $binaryHeader.Value -notmatch '\b[0-9A-Fa-f]{64}\b') {
    throw 'Release notes must start with the applicable binary version and SHA-256 declaration.'
}
$binaryDeclaration = $binaryHeader.Value.TrimEnd()
$notes = $notes.Substring($binaryHeader.Length).TrimStart()
$releaseNotes = "# Release Notes / 发布说明`r`n`r`nRelease binary version / 发布程序版本: $Version`r`n`r`n$binaryDeclaration`r`n`r`n## $Version`r`n`r`n### Version changes / 版本变化`r`n`r`n$changes`r`n`r`n" + $notes
[IO.File]::WriteAllText((Join-Path $stagedDirectory 'RELEASE_NOTES.md'), $releaseNotes, $encoding)
$stagedArchive = Join-Path $stage ($name + '.zip')
# Include the named parent folder as the archive's sole top-level directory.
Compress-Archive -LiteralPath $stagedDirectory -DestinationPath $stagedArchive
$digest = (Get-FileHash -LiteralPath $stagedArchive -Algorithm SHA256).Hash.ToLowerInvariant()
$stagedChecksum = $stagedArchive + '.sha256'
[IO.File]::WriteAllText($stagedChecksum, $digest + '  ' + $name + '.zip' + "`r`n", [Text.Encoding]::ASCII)

New-Item -ItemType Directory -Path $dist -Force | Out-Null
# Recheck after building, then move without -Force so another invocation cannot be overwritten.
foreach ($destination in @($releaseDirectory, $archive, ($archive + '.sha256'))) {
    if (Test-Path -LiteralPath $destination) { throw "Release already exists; history will not be overwritten: $destination" }
}
[IO.Directory]::Move($stagedDirectory, $releaseDirectory)
[IO.File]::Move($stagedArchive, $archive)
[IO.File]::Move($stagedChecksum, ($archive + '.sha256'))
Write-Output "Release files: $releaseDirectory"
Write-Output "ZIP: $archive"
Write-Output "SHA-256: $archive.sha256"
