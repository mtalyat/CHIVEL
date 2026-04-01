@echo off
setlocal

cd /d "%~dp0.."

if not exist ".venv\Scripts\python.exe" (
  echo ERROR: .venv\Scripts\python.exe not found.
  echo Create and configure your virtual environment first.
  exit /b 1
)

echo Installing/Updating packaging tools...
.venv\Scripts\python.exe -m pip install --upgrade build twine
if errorlevel 1 exit /b 1

echo Cleaning old build artifacts...
if exist dist rmdir /s /q dist
if exist build rmdir /s /q build

echo Building sdist and wheel...
.venv\Scripts\python.exe -m build --sdist --wheel
if errorlevel 1 exit /b 1

echo Running twine check...
.venv\Scripts\python.exe -m twine check dist\*
if errorlevel 1 exit /b 1

echo.
echo Build complete. Files in dist\ are ready for upload.
exit /b 0
