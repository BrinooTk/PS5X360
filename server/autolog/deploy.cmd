@echo off
setlocal
cd /d "%~dp0"
call npm.cmd ci
if errorlevel 1 exit /b 1
call npm.cmd test
if errorlevel 1 exit /b 1
call npx.cmd wrangler login
if errorlevel 1 exit /b 1
echo Paste the private Discord channel webhook in the following secret prompt.
call npx.cmd wrangler secret put DISCORD_WEBHOOK_URL
if errorlevel 1 exit /b 1
echo Choose an upload token; save it in your password manager for collector setup.
call npx.cmd wrangler secret put UPLOAD_TOKEN
if errorlevel 1 exit /b 1
call npm.cmd run deploy
if errorlevel 1 exit /b 1
echo Copy the deployed HTTPS URL. Collector endpoint: URL/v1/reports
pause
