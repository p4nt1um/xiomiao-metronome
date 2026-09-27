@echo off
REM Activate ESP-IDF v5.5.2 env then run idf.py in project root.
REM Usage: cmd /c tools\idf_cmd.bat build  (any idf.py subcommand)
REM MSYSTEM is set when cmd is spawned from Git Bash; export.bat refuses to run then.
set MSYSTEM=
call E:\Espressif\frameworks\esp-idf-v5.5.2\export.bat
if errorlevel 1 (
    echo [ERROR] failed to activate ESP-IDF environment
    exit /b 1
)
cd /d "%~dp0.."
idf.py %*
