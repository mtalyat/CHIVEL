@echo off
setlocal

cd /d "%~dp0.."

if not exist ".venv\Scripts\python.exe" (
  echo ERROR: .venv\Scripts\python.exe not found.
  echo Create and configure your virtual environment first.
  exit /b 1
)

if "%TWINE_USERNAME%"=="" (
  set "TWINE_USERNAME=__token__"
)

if "%TWINE_PASSWORD%"=="" (
  echo ERROR: TWINE_PASSWORD is not set.
  echo Set it to your PyPI API token first.
  echo.
  echo PowerShell ^(current session^):
  echo   $env:TWINE_PASSWORD="pypi-xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
  echo PowerShell ^(persist for future terminals^):
  echo   setx TWINE_PASSWORD "pypi-xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
  echo CMD ^(current session^):
  echo   set TWINE_PASSWORD=pypi-xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
  exit /b 1
)

if /I not "%TWINE_USERNAME%"=="__token__" (
  echo WARNING: TWINE_USERNAME is "%TWINE_USERNAME%". PyPI API token uploads should use "__token__".
)

if not exist "dist\*" (
  echo ERROR: No dist artifacts found.
  echo Run scripts\build.bat first.
  exit /b 1
)

echo Uploading distributions to PyPI...
.venv\Scripts\python.exe -m twine upload dist\* %* --username %TWINE_USERNAME% --password %TWINE_PASSWORD%
if errorlevel 1 exit /b 1

echo.
echo Upload complete.
exit /b 0
