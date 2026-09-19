@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
cd /d "C:\Users\PC2504\TestProject\TestJmetalCpp\cmake-build-release"
nmake RCPSPMIPCpp
echo BUILD_EXIT_CODE=%ERRORLEVEL%
