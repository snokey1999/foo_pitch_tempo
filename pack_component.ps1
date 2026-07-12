param(
    [string]$BassDir = "D:\rj\yc\ym\c++\fb2000\bass",
    [string]$BassFxDir = "D:\rj\yc\ym\c++\fb2000\bass_fx"
)
$ErrorActionPreference = "Stop"
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
Write-Host "=== foo_pitch_tempo 组件打包脚本 ===" -ForegroundColor Cyan
Write-Host ""
$projectDir = $scriptDir
$releaseX86Dll = Join-Path $projectDir "Release\foo_pitch_tempo.dll"
$releaseX64Dll = Join-Path $projectDir "x64\Release\foo_pitch_tempo.dll"
$tolkX86Dir = Join-Path $projectDir "tolk_86"
$tolkX64Dir = Join-Path $projectDir "tolk_64"
$openalX86Dll = Join-Path $projectDir "openal\bin\Win32\soft_oal.dll"
$openalX64Dll = Join-Path $projectDir "openal\bin\Win64\soft_oal.dll"
$outputX86 = Join-Path $projectDir "foo_pitch_tempo-x86.fb2k-component"
$outputX64 = Join-Path $projectDir "foo_pitch_tempo-x64.fb2k-component"
function Pack-Component {
    param(
        [string]$Arch,         
        [string]$DllPath,      
        [string]$TolkSourceDir, 
        [string]$OpenALDll,    
        [string]$OutputPath    
    )
    Write-Host "正在打包 $Arch ..." -ForegroundColor Yellow
    if (-not (Test-Path $DllPath)) {
        Write-Host "  [跳过] 未找�?$Arch 编译结果: $DllPath" -ForegroundColor Red
        return
    }
    $stagingDir = Join-Path $projectDir "_staging_$Arch"
    if (Test-Path $stagingDir) { Remove-Item $stagingDir -Recurse -Force }
    New-Item -ItemType Directory -Path $stagingDir | Out-Null
    Write-Host "  复制 foo_pitch_tempo.dll ..."
    Copy-Item $DllPath (Join-Path $stagingDir "foo_pitch_tempo.dll")
    $bassDll = if ($Arch -eq "x64") { Join-Path $BassDir "x64\bass.dll" } else { Join-Path $BassDir "bass.dll" }
    $bassFxDll = if ($Arch -eq "x64") { Join-Path $BassFxDir "x64\bass_fx.dll" } else { Join-Path $BassFxDir "bass_fx.dll" }
    if (Test-Path $bassDll) {
        Write-Host "  复制 bass.dll ($Arch) ..."
        Copy-Item $bassDll (Join-Path $stagingDir "bass.dll")
    } else {
        Write-Host "  [警告] 未找�?bass.dll ($bassDll)" -ForegroundColor DarkYellow
    }
    if (Test-Path $bassFxDll) {
        Write-Host "  复制 bass_fx.dll ($Arch) ..."
        Copy-Item $bassFxDll (Join-Path $stagingDir "bass_fx.dll")
    } else {
        Write-Host "  [警告] 未找�?bass_fx.dll ($bassFxDll)" -ForegroundColor DarkYellow
    }
    if (Test-Path $TolkSourceDir) {
        $tolkDest = Join-Path $stagingDir "tolk"
        Write-Host "  复制 Tolk ($TolkSourceDir -> tolk/) ..."
        New-Item -ItemType Directory -Path $tolkDest | Out-Null
        Get-ChildItem -Path $TolkSourceDir -File | ForEach-Object {
            Copy-Item $_.FullName (Join-Path $tolkDest $_.Name)
        }
        Get-ChildItem -Path $TolkSourceDir -File -Filter "*.conf" | ForEach-Object {
            Copy-Item $_.FullName (Join-Path $tolkDest $_.Name)
        }
        Get-ChildItem -Path $TolkSourceDir -File -Filter "*.ini" | ForEach-Object {
            Copy-Item $_.FullName (Join-Path $tolkDest $_.Name)
        }
    } else {
        Write-Host "  [警告] 未找�?Tolk 目录: $TolkSourceDir" -ForegroundColor DarkYellow
    }
    if (Test-Path $OpenALDll) {
        $oalDest = Join-Path $stagingDir "openal"
        New-Item -ItemType Directory -Path $oalDest | Out-Null
        Write-Host "  复制 soft_oal.dll -> openal/ ..."
        Copy-Item $OpenALDll (Join-Path $oalDest "soft_oal.dll")
    } else {
        Write-Host "  [警告] 未找�?OpenAL DLL: $OpenALDll" -ForegroundColor DarkYellow
    }
    Write-Host "  创建 $OutputPath ..."
    if (Test-Path $OutputPath) { Remove-Item $OutputPath -Force }
    $zipPath = $OutputPath + ".zip"
    Compress-Archive -Path (Join-Path $stagingDir "*") -DestinationPath $zipPath -Force
    Move-Item $zipPath $OutputPath -Force
    Remove-Item $stagingDir -Recurse -Force
    $size = (Get-Item $OutputPath).Length
    Write-Host "  完成! 大小: $([math]::Round($size / 1024)) KB" -ForegroundColor Green
    Write-Host "  包内�?" -ForegroundColor DarkGray
    Copy-Item $OutputPath ($OutputPath + ".verify.zip") -Force
    $verifyDir = Join-Path $projectDir "_verify_$Arch"
    if (Test-Path $verifyDir) { Remove-Item $verifyDir -Recurse -Force }
    Expand-Archive -Path ($OutputPath + ".verify.zip") -DestinationPath $verifyDir -Force
    Get-ChildItem -Recurse $verifyDir -File | ForEach-Object {
        $rel = $_.FullName.Substring($verifyDir.Length + 1)
        Write-Host "    $rel ($([math]::Round($_.Length / 1024)) KB)" -ForegroundColor DarkGray
    }
    Remove-Item $verifyDir -Recurse -Force
    Remove-Item ($OutputPath + ".verify.zip") -Force
}
Write-Host ""
Pack-Component -Arch "x86" `
    -DllPath $releaseX86Dll `
    -TolkSourceDir $tolkX86Dir `
    -OpenALDll $openalX86Dll `
    -OutputPath $outputX86
Write-Host ""
Pack-Component -Arch "x64" `
    -DllPath $releaseX64Dll `
    -TolkSourceDir $tolkX64Dir `
    -OpenALDll $openalX64Dll `
    -OutputPath $outputX64
Write-Host ""
Write-Host "=== 打包完成 ===" -ForegroundColor Cyan
