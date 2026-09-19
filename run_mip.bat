@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cd /d "C:\Users\PC2504\TestProject\TestJmetalCpp\cmake-build-release"
echo [RUN] j3011_1
RCPSPMIPCpp.exe j30.sm/j3011_1.sm
echo [RUN] j3017_1
RCPSPMIPCpp.exe j30.sm/j3017_1.sm
echo [RUN] j3026_1
RCPSPMIPCpp.exe j30.sm/j3026_1.sm
echo [RUN] j3047_1
RCPSPMIPCpp.exe j30.sm/j3047_1.sm
echo [RUN] j309_1
RCPSPMIPCpp.exe j30.sm/j309_1.sm
echo ALL_DONE
