@echo off
chcp 65001 >nul

echo === 查找 Visual Studio ===
set VSCMD=
for %%d in (
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
echo 未找到 Visual Studio 2022 x64 工具集
echo 请从 Visual Studio Developer Command Prompt 运行此脚本
pause
exit /b 1

:found
echo 找到: %VSCMD%
call "%VSCMD%" >nul

echo === 清理旧构建 ===
msbuild TranslationTool.vcxproj /p:Configuration=Debug /p:Platform=x64 /t:Clean

echo === 构建 ===
msbuild TranslationTool.vcxproj /p:Configuration=Debug /p:Platform=x64

echo === 完成 ===
if exist x64\Debug\TranslationTool.exe (
    echo 启动测试...
    start x64\Debug\TranslationTool.exe
) else (
    echo 没找到 exe，搜索中...
    dir /s TranslationTool.exe
)
pause
