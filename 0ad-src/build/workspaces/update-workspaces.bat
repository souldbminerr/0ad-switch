@echo off
rem ** Create Visual Studio Workspaces on Windows **

cd /D "%~dp0"
cd ..\bin
if not exist ..\workspaces\vs2022\SKIP_PREMAKE_HERE premake5.exe --file="../premake/premake5.lua" --outpath="../workspaces/vs2022" %* vs2022 || exit /b 1
cd ..\workspaces
