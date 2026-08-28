@echo off
setlocal EnableExtensions
cd /d "%~dp0"

set "ROOT=%CD%"
if defined DOOMBF_BUILD_DIR (
    set "BUILD_DIR=%DOOMBF_BUILD_DIR%"
) else (
    set "BUILD_DIR=%ROOT%\build\windows"
)

set "PYTHON_CMD=python"
python --version >nul 2>nul
if errorlevel 1 set "PYTHON_CMD=py -3"

%PYTHON_CMD% "%ROOT%\tools\run_pipeline.py" ^
    --ibf "%BUILD_DIR%\bin\ibf.exe" ^
    --frontend "%BUILD_DIR%\bin\frnt.exe" ^
    --program "%BUILD_DIR%\bin\doom.bpk" %*
exit /b %ERRORLEVEL%
