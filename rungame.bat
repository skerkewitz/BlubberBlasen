@echo off
rem Starts the game from the build folder; all arguments are passed on,
rem for example: rungame --replay build\Debug\recordings\<file>.bbr
rem Prefers the Debug build (developer options, recordings), then Release.
setlocal
set "ROOT=%~dp0"
for %%E in ("build\Debug\blubberblasen.exe" "build\Release\blubberblasen.exe" "build\blubberblasen.exe" "build-release\Release\blubberblasen.exe") do (
    if exist "%ROOT%%%~E" (
        "%ROOT%%%~E" %*
        exit /b %ERRORLEVEL%
    )
)
echo No blubberblasen.exe found. Build first: cmake --build build --config Debug
exit /b 1
