@echo off
rem Usage:  release.bat 1.0.1
rem Writes the version into VERSION, commits and pushes to main.
rem GitHub then builds the installer and publishes release v1.0.1
rem (users get "Update available" in the plugin).
if "%~1"=="" (
  echo Usage: release.bat 1.0.1
  exit /b 1
)
echo %~1> VERSION
git add -A
git commit -m "Release v%~1"
git push origin main
echo.
echo Pushed v%~1. Build progress: open the "Actions" tab of your GitHub repository.
