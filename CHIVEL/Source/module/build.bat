@echo off

set "CURRENT_DIR=%~dp0"
echo %CURRENT_DIR% | findstr /i "Release" >nul
if %errorlevel% neq 0 (
	echo Error: This script must be run from within the Release directory.
	exit /b 1
)

echo Building the project...
cd %~dp0
python -m build

if %errorlevel% neq 0 (
	echo Build failed. Exiting...
	pause
	exit /b %errorlevel%
)

set /p UPLOAD=Upload to PyPI? (y/n):
if /i "%UPLOAD%"=="y" (
	echo Uploading to PyPI...
	python -m twine upload dist/*
) else (
	echo Skipping upload.
	exit /b 0
)
pause