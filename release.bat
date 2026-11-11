@echo off
rem Usage:  release.bat 1.0.1
rem Commits current changes, tags v1.0.1 and pushes. GitHub then builds the installer
rem and publishes it as the latest Release (users get "Update available" in the plugin).
if "%~1"=="" (
  echo Usage: release.bat 1.0.1
  exit /b 1
)
git add -A
git commit -m "Release v%~1"
git tag v%~1
git push
git push origin v%~1
echo.
echo Pushed v%~1. Build progress: open the "Actions" tab of your GitHub repository.
