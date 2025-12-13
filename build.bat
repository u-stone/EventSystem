@echo off
setlocal

REM Script to configure, build, test, and run the project.

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
echo [Step 1/4] Configuring project with CMake...

REM Detect Visual Studio version
set "VS_GENERATOR="

REM Check for Visual Studio 2022
"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -version "[17.0,18.0)" -property installationPath > nul 2> nul
if %errorlevel% == 0 (
    echo Found Visual Studio 2022.
    set "VS_GENERATOR=Visual Studio 17 2022"
) else (
    REM Check for Visual Studio 2019
    "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -version "[16.0,17.0)" -property installationPath > nul 2> nul
    if %errorlevel% == 0 (
        echo Found Visual Studio 2019.
        set "VS_GENERATOR=Visual Studio 16 2019"
    ) else (
        REM Check for Visual Studio 2017
        "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -version "[15.0,16.0)" -property installationPath > nul 2> nul
        if %errorlevel% == 0 (
            echo Found Visual Studio 2017.
            set "VS_GENERATOR=Visual Studio 15 2017"
        )
    )
)

if not defined VS_GENERATOR (
    echo Neither Visual Studio 2022, 2019, nor 2017 were found by vswhere.
    goto :error
)

cmake -G "%VS_GENERATOR%" ..

REM Check if CMake failed.
IF %ERRORLEVEL% NEQ 0 (
    echo CMake configuration failed.
    echo If you see an error about existing project files, try deleting the '%BUILD_DIR%' directory.
    goto :error
)

REM Run CMake to build the project (both app and tests).
echo.
echo [Step 2/4] Building project...

echo Building main application...
cmake --build . --config Debug --target main_app
IF %ERRORLEVEL% NEQ 0 goto :error

echo Building unit tests...
cmake --build . --config Debug --target test_event_system
IF %ERRORLEVEL% NEQ 0 goto :error


REM Run the unit tests using CTest.
echo.
echo [Step 3/4] Running unit tests...
ctest -C Debug --output-on-failure

REM Check if tests failed.
IF %ERRORLEVEL% NEQ 0 (
    echo Unit tests failed. Aborting run.
    goto :error
)

REM Find and run the main application executable.
echo.
echo [Step 4/4] Running main application...
echo ---------------------------------
IF EXIST "Debug\main_app.exe" (
    call "Debug\main_app.exe"
) ELSE IF EXIST "Release\main_app.exe" (
    call "Release\main_app.exe"
) ELSE IF EXIST "main_app.exe" (
    call "main_app.exe"
) ELSE (
    echo Executable 'main_app.exe' not found.
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