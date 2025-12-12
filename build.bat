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
cmake -G "Visual Studio 16 2019" ..

REM Check if CMake failed.
IF %ERRORLEVEL% NEQ 0 (
    echo CMake configuration failed.
    goto :error
)

REM Run CMake to build the project (both app and tests).
echo.
echo [Step 2/4] Building project...
cmake --build .

REM Check if build failed.
IF %ERRORLEVEL% NEQ 0 (
    echo Project build failed.
    goto :error
)

@REM REM Run the unit tests using CTest.
@REM echo.
@REM echo [Step 3/4] Running unit tests...
@REM ctest --output-on-failure

@REM REM Check if tests failed.
@REM IF %ERRORLEVEL% NEQ 0 (
@REM     echo Unit tests failed. Aborting run.
@REM     goto :error
@REM )

REM Find and run the main application executable.
echo.
echo [Step 4/4] Running main application...
echo ---------------------------------
IF EXIST "build\Debug\main_app.exe" (
    call "build\Debug\main_app.exe"
) ELSE IF EXIST "build\Release\main_app.exe" (
    call "build\Release\main_app.exe"
) ELSE IF EXIST "build\main_app.exe" (
    call "build\main_app.exe"
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