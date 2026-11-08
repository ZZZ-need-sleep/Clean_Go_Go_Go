@echo off
setlocal EnableExtensions
cd /d "%~dp0"

set "TARGET=%~1"
if "%TARGET%"=="" set "TARGET=Genshin"
if /I not "%TARGET%"=="Genshin" if /I not "%TARGET%"=="YuanShen" if /I not "%TARGET%"=="LXOZVQE" (
    echo [ERROR] TARGET must be one of: Genshin, YuanShen, LXOZVQE
    echo         The application refuses to start under any other file name.
    exit /b 1
)

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo [ERROR] vswhere.exe not found. Install Visual Studio 2022 Build Tools with C++ workload.
    exit /b 1
)
set "VSPATH="
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"
if not defined VSPATH (
    echo [ERROR] Visual C++ build tools not found.
    exit /b 1
)
call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if errorlevel 1 (
    echo [ERROR] Failed to initialize Visual C++ environment.
    exit /b 1
)

if not exist build mkdir build
if not exist dist mkdir dist
del /q build\*.obj build\*.res 2>nul

echo [1/3] Compiling resources...
rc /nologo /I src /fo build\app.res src\app.rc
if errorlevel 1 exit /b 1

echo [2/3] Compiling C sources...
cl /nologo /c /O2 /MT /W4 /std:c11 /utf-8 /DUNICODE /D_UNICODE /I src /Fo:build\util.obj src\util.c
if errorlevel 1 exit /b 1
cl /nologo /c /O2 /MT /W4 /std:c11 /utf-8 /DUNICODE /D_UNICODE /I src /Fo:build\store.obj src\store.c
if errorlevel 1 exit /b 1
cl /nologo /c /O2 /MT /W4 /std:c11 /utf-8 /DUNICODE /D_UNICODE /I src /Fo:build\scan.obj src\scan.c
if errorlevel 1 exit /b 1
cl /nologo /c /O2 /MT /W4 /std:c11 /utf-8 /DUNICODE /D_UNICODE /I src /Fo:build\list.obj src\list.c
if errorlevel 1 exit /b 1
cl /nologo /c /O2 /MT /W4 /std:c11 /utf-8 /DUNICODE /D_UNICODE /I src /Fo:build\ui.obj src\ui.c
if errorlevel 1 exit /b 1
cl /nologo /c /O2 /MT /W4 /std:c11 /utf-8 /DUNICODE /D_UNICODE /I src /Fo:build\main.obj src\main.c
if errorlevel 1 exit /b 1

echo [3/3] Linking...
link /nologo /OUT:"dist\%TARGET%.exe" /SUBSYSTEM:WINDOWS /MANIFEST:NO /OPT:REF /OPT:ICF /DYNAMICBASE /NXCOMPAT /INCREMENTAL:NO ^
    build\util.obj build\store.obj build\scan.obj build\list.obj build\ui.obj build\main.obj build\app.res ^
    user32.lib gdi32.lib comctl32.lib shell32.lib shlwapi.lib ole32.lib uuid.lib advapi32.lib uxtheme.lib
if errorlevel 1 exit /b 1

echo.
echo Built: %CD%\dist\%TARGET%.exe
for %%F in ("dist\%TARGET%.exe") do echo Size : %%~zF bytes
echo Allowed runtime names: LXOZVQE, YuanShen, Genshin (extension .exe optional in comparison)
endlocal
