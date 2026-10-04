@echo off
setlocal
cd /d "%~dp0"
echo Discord: private channel settings, Integrations, Webhooks, New Webhook.
echo Paste its URL only in the hidden prompt below. Do not send it in chat.
call npx.cmd wrangler secret put DISCORD_WEBHOOK_URL
if errorlevel 1 goto failed
if not exist "..\..\build" mkdir "..\..\build"
echo configured>"..\..\build\autolog-webhook-configured.txt"
echo Webhook configured. You can close this window.
pause
exit /b 0
:failed
echo Webhook was not configured. Run this file again.
pause
exit /b 1
