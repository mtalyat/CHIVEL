@echo off
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