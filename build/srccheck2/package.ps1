# Build a standalone EXE and a source archive.
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $Root

$Version = "1.0.0"
$Release = Join-Path $Root "release"
$Dist = Join-Path $Root "dist"
$Stage = Join-Path $Release "FileCleaner-$Version-source"

& (Join-Path $Root "build.bat") Genshin
if ($LASTEXITCODE -ne 0) { throw "build.bat failed" }

if (Test-Path $Release) { Remove-Item -Recurse -Force $Release }
New-Item -ItemType Directory -Force $Release | Out-Null
New-Item -ItemType Directory -Force $Stage | Out-Null

Copy-Item (Join-Path $Root "src") (Join-Path $Stage "src") -Recurse
New-Item -ItemType Directory -Force (Join-Path $Stage "tools") | Out-Null
Copy-Item (Join-Path $Root "tools\make_icon.py") (Join-Path $Stage "tools")
Copy-Item (Join-Path $Root "tools\ui_probe.py") (Join-Path $Stage "tools")
Copy-Item (Join-Path $Root "build.bat") $Stage
Copy-Item (Join-Path $Root "package.ps1") $Stage
Copy-Item (Join-Path $Root "README.md") $Stage
Copy-Item (Join-Path $Root "LICENSE") $Stage
Copy-Item (Join-Path $Root ".gitignore") $Stage
if (Test-Path (Join-Path $Root "logo.png")) {
    Copy-Item (Join-Path $Root "logo.png") $Stage
}
if (Test-Path (Join-Path $Root "docs")) {
    Copy-Item (Join-Path $Root "docs") (Join-Path $Stage "docs") -Recurse
}

# Deliverable 1: raw standalone EXE (the only file you need to distribute).
Copy-Item (Join-Path $Dist "Genshin.exe") (Join-Path $Release "Genshin.exe")

# Deliverable 2: EXE-only zip.
$ExeZipStage = Join-Path $Release "exe-zip-stage"
New-Item -ItemType Directory -Force $ExeZipStage | Out-Null
Copy-Item (Join-Path $Dist "Genshin.exe") $ExeZipStage
Add-Type -AssemblyName System.IO.Compression.FileSystem
$ExeZip = Join-Path $Release "Genshin-$Version-exe.zip"
if (Test-Path $ExeZip) { Remove-Item -Force $ExeZip }
[System.IO.Compression.ZipFile]::CreateFromDirectory($ExeZipStage, $ExeZip)
Remove-Item -Recurse -Force $ExeZipStage

# Deliverable 3: source archive for open-source upload.
$SrcZip = Join-Path $Release "FileCleaner-$Version-source.zip"
if (Test-Path $SrcZip) { Remove-Item -Force $SrcZip }
[System.IO.Compression.ZipFile]::CreateFromDirectory($Stage, $SrcZip)
Remove-Item -Recurse -Force $Stage

Get-ChildItem $Release | Select-Object Name, Length
Get-FileHash (Join-Path $Release "Genshin.exe") -Algorithm SHA256 |
    ForEach-Object { "$($_.Hash)  Genshin.exe" } |
    Set-Content (Join-Path $Release "SHA256SUMS.txt") -Encoding ascii
Get-Content (Join-Path $Release "SHA256SUMS.txt")
