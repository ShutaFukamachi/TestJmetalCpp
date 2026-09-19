$vcvars = "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
$buildDir = "C:\Users\PC2504\TestProject\TestJmetalCpp\cmake-build-release"

# vcvars を呼び出して環境変数を取得し、現在のセッションに適用する
$tempFile = [System.IO.Path]::GetTempFileName() + ".bat"
@"
@echo off
call "$vcvars"
set > "$($tempFile.Replace('.bat','_env.txt'))"
"@ | Set-Content $tempFile

cmd /c $tempFile

$envFile = $tempFile.Replace('.bat','_env.txt')
if (Test-Path $envFile) {
    Get-Content $envFile | ForEach-Object {
        if ($_ -match '^([^=]+)=(.*)$') {
            [System.Environment]::SetEnvironmentVariable($matches[1], $matches[2])
        }
    }
}

# nmake でビルド
Set-Location $buildDir
$nmake = "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64\nmake.exe"
& $nmake RCPSPMIPCpp
Write-Host "Exit code: $LASTEXITCODE"
