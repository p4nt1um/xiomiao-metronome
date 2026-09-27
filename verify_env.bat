@echo off
REM ESP-IDF Environment Verification Script for xiomiao-metronome

echo ========================================
echo ESP-IDF Environment Verification
echo ========================================
echo.

if "%IDF_PATH%"=="" (
    echo [ERROR] IDF_PATH not set - ESP-IDF environment not activated
    echo.
    echo Please run first: E:\Espressif\frameworks\esp-idf-v5.5.2\export.bat
    echo.
    pause
    exit /b 1
)

echo [OK] IDF_PATH: %IDF_PATH%
echo [OK] IDF_PYTHON_ENV_PATH: %IDF_PYTHON_ENV_PATH%
echo [OK] IDF_TOOLS_PATH: %IDF_TOOLS_PATH%
echo.

echo Checking ESP-IDF version...
call idf.py --version
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] idf.py not found or failed to run
    pause
    exit /b 1
)

echo.
echo ========================================
echo ESP-IDF environment verified successfully!
echo ========================================
echo.
echo Build:  idf.py build
echo Flash:  idf.py -p COM8 flash monitor
echo.
pause
