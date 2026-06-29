@echo off
chcp 65001 >nul 2>&1

setlocal enabledelayedexpansion

set VSWHERE="C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"

echo === Looking for Visual Studio (x64 tools) ===
set VSCMD=

for %%d in (
    "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
    "C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Auxiliary\Build\vcvars64.bat"
    "C:\Program Files\Microsoft Visual Studio\18\Enterprise\VC\Auxiliary\Build\vcvars64.bat"
    "C:\Program Files\Microsoft Visual Studio\2026\Community\VC\Auxiliary\Build\vcvars64.bat"
    "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
    "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat"
    "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat"
    "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
) do (
    if exist %%d (
        set VSCMD=%%d
        goto :found
    )
)

if exist %VSWHERE% (
    for /f "usebackq tokens=*" %%i in (`%VSWHERE% -latest -products * -property installationPath`) do (
        if exist "%%i\VC\Auxiliary\Build\vcvars64.bat" (
            set VSCMD="%%i\VC\Auxiliary\Build\vcvars64.bat"
            goto :found
        )
    )
)

echo ERROR: Visual Studio (x64 tools) not found.
echo Install Visual Studio with "Desktop development with C++" workload.
pause
exit /b 1

:found
echo Found: %VSCMD%
call %VSCMD% >nul 2>&1
if !ERRORLEVEL! neq 0 (
    echo ERROR: Failed to initialize VS environment.
    pause
    exit /b 1
)

echo.
echo === Building TranslationTool (Release x64) ===
msbuild "%~dp0TranslationTool.vcxproj" /p:Configuration=Release /p:Platform=x64 /t:Rebuild
if !ERRORLEVEL! neq 0 (
    echo ERROR: Build failed (exit code !ERRORLEVEL!).
    pause
    exit /b !ERRORLEVEL!
)

echo.
echo === Build succeeded ===
echo Output: %~dp0x64\Release\TranslationTool.exe
pause
