@ECHO OFF
REM Generate a Unicode string constant from git's output, including branch
REM name and an abbreviated commit hash, and write it to build_version.txt
FOR /F %%b IN ('git branch --show-current') DO SET branch=%%b
FOR /F %%h IN ('git rev-parse --short HEAD') DO SET hash=%%h
ECHO L"%branch%, %hash%" > build_version.txt
