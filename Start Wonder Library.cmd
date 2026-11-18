@echo off
cd /d "%~dp0"
pyw -3 tools\wonder_library.py %*
if errorlevel 1 (
    echo Wonder Library needs Python 3 with Tk support. Run tools\wonder_library.py with that Python.
    pause
)
