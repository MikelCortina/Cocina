@echo off
for /f "tokens=2 delims=:, " %%a in ('findstr "MinorVersion" "%~1"') do (
    echo %%a
    goto :eof
)