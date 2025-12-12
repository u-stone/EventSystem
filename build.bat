@echo off
setlocal

REM Script to configure, build, and run the CMake project.

REM Define the name of the build directory.
SET BUILD_DIR=build

REM Create the build directory if it doesn't exist.
IF NOT EXIST "%BUILD_DIR%" (
    echo Creating build directory: %BUILD_DIR%
    mkdir "%BUILD_DIR%"
)

REM Change to the build directory.
cd "%BUILD_DIR%"

REM Run CMake to configure the project.
echo.
echo [Step 1/3] Configuring project with CMake...
cmake ..

REM Check if CMake failed.
IF %ERRORLEVEL% NEQ 0 (
    echo CMake configuration failed.
    goto :error
)

REM Run CMake to build the project.
echo.
echo [Step 2/3] Building project...
cmake --build .

REM Check if build failed.
IF %ERRORLEVEL% NEQ 0 (
    echo Project build failed.
    goto :error
)

REM Find and run the executable.
echo.
echo [Step 3/3] Running executable...
echo ---------------------------------
IF EXIST "Debug\main.exe" (
    call "Debug\main.exe"
) ELSE IF EXIST "Release\main.exe" (
    call "Release\main.exe"
) ELSE (
    echo Executable 'main.exe' not found in Debug or Release folders.
)
echo ---------------------------------
echo.
echo Script finished successfully.
goto :eof

:error
echo.
echo Script failed.
exit /b 1

:eof
endlocal
