@echo off
setlocal

rem -- 1. Make sure the .ico files exist (regenerate via PowerShell helper).
if not exist full.ico  ( call :gen_icons || exit /b 1 )
if not exist empty.ico ( call :gen_icons || exit /b 1 )

rem -- 2. Sanity-check the MSVC toolchain is on PATH.
where rc >nul 2>nul || ( echo ERROR: rc.exe not found.  Run from a "Developer Command Prompt for VS" or call vcvarsall.bat first. & exit /b 1 )
where cl >nul 2>nul || ( echo ERROR: cl.exe not found.  Run from a "Developer Command Prompt for VS" or call vcvarsall.bat first. & exit /b 1 )

rem -- 3. Compile resources, then C++.
rc /nologo /fo amped.res amped.rc || exit /b 1
cl /nologo /std:c++20 /W4 /EHsc /O2 /utf-8 ^
   main.cpp amped.res ^
   /link /SUBSYSTEM:WINDOWS /OUT:amped.exe ^
   user32.lib shell32.lib || exit /b 1

echo.
echo Built amped.exe successfully.
exit /b 0

:gen_icons
powershell -ExecutionPolicy Bypass -File "%~dp0make_icons.ps1"
exit /b %ERRORLEVEL%
