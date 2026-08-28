@echo off
setlocal EnableExtensions

cd /d "%~dp0"
set "ROOT=%CD%"
set "BUILD_DIR=%ROOT%\build\windows"

if /I "%~1"=="clean" (
    echo [build] Removing "%BUILD_DIR%"...
    if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"
)

where cmake >nul 2>nul
if errorlevel 1 (
    echo [build] ERROR: CMake was not found on PATH.
    exit /b 1
)

set "PYTHON_EXE="

rem Можно задать вручную: set PYTHON=C:\Path\to\python.exe
if defined PYTHON if exist "%PYTHON%" set "PYTHON_EXE=%PYTHON%"

rem py / py -3 пробуем раньше, чем python (на Windows это правильный лаунчер)
if not defined PYTHON_EXE (
    for %%C in ("py -3" "py" "python" "python3") do (
        if not defined PYTHON_EXE (
            %%~C --version >nul 2>nul
            if not errorlevel 1 (
                for /f "delims=" %%P in ('%%~C -c "import sys; print(sys.executable)" 2^>nul') do (
                    if exist "%%P" set "PYTHON_EXE=%%P"
                )
            )
        )
    )
)

if not defined PYTHON_EXE (
    echo [build] ERROR: Python 3 was not found.
    echo [build] Install Python 3 with the "py" launcher, or add python.exe to PATH.
    echo [build] Or set PYTHON to the full path of python.exe.
    exit /b 1
)

echo [build] Python: "%PYTHON_EXE%"
echo [build] Checking Python dependencies...
"%PYTHON_EXE%" -c "import capstone, elftools; assert hasattr(capstone, 'CS_ARCH_RISCV')" >nul 2>nul
if errorlevel 1 (
    echo [build] Installing packages from requirements.txt...
    "%PYTHON_EXE%" -m pip install -r "%ROOT%\requirements.txt"
    if errorlevel 1 (
        echo [build] ERROR: Python dependencies could not be installed.
        exit /b 1
    )
)

set "RISCV_COMPILER="
if defined RISCV_GCC set "RISCV_COMPILER=%RISCV_GCC:"=%"
if not defined RISCV_COMPILER if exist "%ROOT%\toolchains\riscv\bin\riscv64-unknown-elf-gcc.exe" set "RISCV_COMPILER=%ROOT%\toolchains\riscv\bin\riscv64-unknown-elf-gcc.exe"
if not defined RISCV_COMPILER if exist "%ROOT%\toolchains\riscv\bin\riscv64-elf-gcc.exe" set "RISCV_COMPILER=%ROOT%\toolchains\riscv\bin\riscv64-elf-gcc.exe"
if not defined RISCV_COMPILER if exist "%ROOT%\toolchains\riscv\bin\riscv32-unknown-elf-gcc.exe" set "RISCV_COMPILER=%ROOT%\toolchains\riscv\bin\riscv32-unknown-elf-gcc.exe"
if not defined RISCV_COMPILER if exist "%ROOT%\toolchains\riscv\bin\riscv32-none-elf-gcc.exe" set "RISCV_COMPILER=%ROOT%\toolchains\riscv\bin\riscv32-none-elf-gcc.exe"

if not defined RISCV_COMPILER (
    for %%G in (riscv64-unknown-elf-gcc.exe riscv64-elf-gcc.exe riscv32-unknown-elf-gcc.exe riscv32-none-elf-gcc.exe) do (
        if not defined RISCV_COMPILER (
            for /f "delims=" %%P in ('where %%G 2^>nul') do if not defined RISCV_COMPILER set "RISCV_COMPILER=%%P"
        )
    )
)

if not defined RISCV_COMPILER (
    echo [build] ERROR: RISC-V GCC was not found.
    echo [build] Put the toolchain under:
    echo [build]   toolchains\riscv\bin\riscv64-unknown-elf-gcc.exe
    echo [build] or set RISCV_GCC to the compiler's full path.
    exit /b 1
)

"%RISCV_COMPILER%" --version >nul 2>nul
if errorlevel 1 (
    echo [build] ERROR: "%RISCV_COMPILER%" cannot be executed.
    exit /b 1
)

if not exist "%ROOT%\doom\data\doom.wad" (
    echo [build] ERROR: doom\data\doom.wad is missing.
    echo [build] Copy your legally obtained WAD there before building the BF chain.
    exit /b 1
)

for %%I in ("%RISCV_COMPILER%") do set "RISCV_BIN=%%~dpI"
if "%RISCV_BIN:~-1%"=="\" set "RISCV_BIN=%RISCV_BIN:~0,-1%"

if exist "%RISCV_BIN%\riscv64-unknown-elf-as.exe" if not exist "%RISCV_BIN%\as.exe" (
    copy /y "%RISCV_BIN%\riscv64-unknown-elf-as.exe" "%RISCV_BIN%\as.exe" >nul
)
if exist "%RISCV_BIN%\riscv64-unknown-elf-ld.exe" if not exist "%RISCV_BIN%\ld.exe" (
    copy /y "%RISCV_BIN%\riscv64-unknown-elf-ld.exe" "%RISCV_BIN%\ld.exe" >nul
)

set "RISCV_LIBGCC=%RISCV_BIN%\..\lib\gcc\riscv64-unknown-elf\10.2.0\rv32i\ilp32"
if not exist "%RISCV_LIBGCC%\libgcc.a" (
    echo [build] ERROR: libgcc.a not found:
    echo [build]   "%RISCV_LIBGCC%\libgcc.a"
    echo [build] Copy rv32ic/ilp32/libgcc.a there first.
    exit /b 1
)

set "PATH=%RISCV_BIN%;%PATH%"
set "COMPILER_PATH=%RISCV_BIN%"
set "RISCV_BFLAG=-B %RISCV_BIN%\"
set "LIBRARY_PATH=%RISCV_LIBGCC%"

echo [build] RISC-V bin: "%RISCV_BIN%"
echo [build] RISC-V -B:  %RISCV_BFLAG%
echo [build] libgcc:     "%RISCV_LIBGCC%\libgcc.a"


if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

if not defined DOOMBF_GENERATOR if defined CMAKE_GENERATOR set "DOOMBF_GENERATOR=%CMAKE_GENERATOR%"

echo [build] RISC-V compiler: "%RISCV_COMPILER%"
echo [build] Configuring CMake...

if not defined DOOMBF_GENERATOR goto configure_default

echo %DOOMBF_GENERATOR% | findstr /I /C:"Visual Studio" >nul
if errorlevel 1 goto configure_custom_no_arch

cmake -S "%ROOT%" -B "%BUILD_DIR%" -G "%DOOMBF_GENERATOR%" -A x64 ^
    "-DPython3_EXECUTABLE:FILEPATH=%PYTHON_EXE%" ^
    "-DPython_EXECUTABLE:FILEPATH=%PYTHON_EXE%" ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DDOOMBF_BUILD_NATIVE=ON ^
    -DDOOMBF_BUILD_FRONTEND=ON ^
    -DDOOMBF_BUILD_IBF=ON ^
    -DDOOMBF_BUILD_BF=ON ^
    "-DRISCV_GCC_EXECUTABLE:FILEPATH=%RISCV_COMPILER%" ^
	"-DRISCV_GCC_FLAGS:STRING=%RISCV_BFLAG%" ^
    "-DRISCV_CFLAGS:STRING=%RISCV_BFLAG%"
set "CONFIGURE_RESULT=%ERRORLEVEL%"
goto configure_done

:configure_custom_no_arch
cmake -S "%ROOT%" -B "%BUILD_DIR%" -G "%DOOMBF_GENERATOR%" ^
    "-DPython3_EXECUTABLE:FILEPATH=%PYTHON_EXE%" ^
    "-DPython_EXECUTABLE:FILEPATH=%PYTHON_EXE%" ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DDOOMBF_BUILD_NATIVE=ON ^
    -DDOOMBF_BUILD_FRONTEND=ON ^
    -DDOOMBF_BUILD_IBF=ON ^
    -DDOOMBF_BUILD_BF=ON ^
    "-DRISCV_GCC_EXECUTABLE:FILEPATH=%RISCV_COMPILER%" ^
	"-DRISCV_GCC_FLAGS:STRING=%RISCV_BFLAG%" ^
    "-DRISCV_CFLAGS:STRING=%RISCV_BFLAG%"
set "CONFIGURE_RESULT=%ERRORLEVEL%"
goto configure_done

:configure_default
cmake -S "%ROOT%" -B "%BUILD_DIR%" -A x64 ^
    "-DPython3_EXECUTABLE:FILEPATH=%PYTHON_EXE%" ^
    "-DPython_EXECUTABLE:FILEPATH=%PYTHON_EXE%" ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DDOOMBF_BUILD_NATIVE=ON ^
    -DDOOMBF_BUILD_FRONTEND=ON ^
    -DDOOMBF_BUILD_IBF=ON ^
    -DDOOMBF_BUILD_BF=ON ^
    "-DRISCV_GCC_EXECUTABLE:FILEPATH=%RISCV_COMPILER%" ^
	"-DRISCV_GCC_FLAGS:STRING=%RISCV_BFLAG%" ^
    "-DRISCV_CFLAGS:STRING=%RISCV_BFLAG%"
set "CONFIGURE_RESULT=%ERRORLEVEL%"

:configure_done
if not "%CONFIGURE_RESULT%"=="0" (
    echo [build] ERROR: CMake configuration failed.
    echo [build] Install Visual Studio Build Tools with the C++ workload.
    echo [build] For MinGW, set DOOMBF_GENERATOR=MinGW Makefiles and rerun with clean.
    exit /b 1
)

echo [build] Building native Doom, ibf, WinAPI frontend, ELF and doom.bpk...
cmake --build "%BUILD_DIR%" --config Release --parallel --target doombf_all
if errorlevel 1 (
    echo [build] ERROR: build failed.
    exit /b 1
)

echo.
echo [build] Done. Artifacts:
echo [build]   %BUILD_DIR%\bin\win_doom.exe
echo [build]   %BUILD_DIR%\bin\ibf.exe
echo [build]   %BUILD_DIR%\bin\frnt.exe
echo [build]   %BUILD_DIR%\bin\bfk_doom.elf
echo [build]   %BUILD_DIR%\bin\doom.bpk
echo [build]   %BUILD_DIR%\bin\doom.bpk.addr
echo [build] Run the BF version with: run_bf.cmd
exit /b 0
