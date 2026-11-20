@echo off
REM Builds ddr5ramtest.exe (GUI), ddr5test_cli.exe and unit_tests.exe into build\ with CMake, using clang-cl and lld-link (LLVM).
REM Run from a "x64 Native Tools Command Prompt for VS": that sets INCLUDE and LIB for the Windows SDK and the C++ runtime
REM headers/libraries. The MSVC compiler itself (cl.exe) is not used. clang-cl and lld-link come from LLVM, which must be
REM on PATH (the GitHub runners have it; otherwise install LLVM or the VS component "C++ Clang tools for Windows").
REM Set DDR5_WARNINGS_AS_ERRORS=ON first to build with /WX (the GitHub Actions job does).
setlocal
where clang-cl >nul 2>&1 || (echo clang-cl not found on PATH. Install LLVM, or the Visual Studio component C++ Clang tools for Windows. & exit /b 1)
where lld-link >nul 2>&1 || (echo lld-link not found on PATH. It ships with LLVM. & exit /b 1)
if not exist build mkdir build
REM Ninja ships with Visual Studio and the GitHub runners; use CMake's default generator only if it is missing. A real
REM configure error is not swallowed: it stops the script with CMake's own message.
set "GEN="
where ninja >nul 2>&1 && set "GEN=-G Ninja"
set "WX="
if /i "%DDR5_WARNINGS_AS_ERRORS%"=="ON" set "WX=-DDDR5_WARNINGS_AS_ERRORS=ON"
cmake -S . -B build\cmake %GEN% -DCMAKE_CXX_COMPILER=clang-cl -DCMAKE_LINKER=lld-link -DCMAKE_BUILD_TYPE=Release %WX% || exit /b 1
cmake --build build\cmake --config Release --target ddr5ramtest ddr5test_cli unit_tests || exit /b 1
echo.
echo Built build\ddr5ramtest.exe, build\ddr5test_cli.exe and build\unit_tests.exe
