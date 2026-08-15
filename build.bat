@echo off
setlocal enabledelayedexpansion

rem LiquidCam command line build.
rem   build.bat            -> Release x64
rem   build.bat Debug      -> Debug x64
rem   build.bat Release clean

set CONFIG=%~1
if "%CONFIG%"=="" set CONFIG=Release

set TARGET=Build
if /i "%~2"=="clean" set TARGET=Rebuild

rem ---- locate Qt --------------------------------------------------------
if "%QTDIR%"=="" (
  if exist "C:\Qt\Qt5.14.2\5.14.2\msvc2017_64\bin\moc.exe" (
    set QTDIR=C:\Qt\Qt5.14.2\5.14.2\msvc2017_64
  ) else if exist "C:\Qt\5.14.2\msvc2017_64\bin\moc.exe" (
    set QTDIR=C:\Qt\5.14.2\msvc2017_64
  )
)
if "%QTDIR%"=="" (
  echo.
  echo   Qt was not found. Set QTDIR to your Qt 5.14.2 msvc2017_64 folder:
  echo       setx QTDIR C:\Qt\Qt5.14.2\5.14.2\msvc2017_64
  echo.
  exit /b 1
)
if not exist "%QTDIR%\bin\moc.exe" (
  echo   QTDIR is set to "%QTDIR%" but moc.exe is not there.
  exit /b 1
)
echo   Qt:      %QTDIR%

rem ---- locate MSBuild ---------------------------------------------------
set MSBUILD=
set VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe
if exist "%VSWHERE%" (
  for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -prerelease -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) do set MSBUILD=%%i
)
if "%MSBUILD%"=="" (
  where msbuild.exe >nul 2>&1 && set MSBUILD=msbuild.exe
)
if "%MSBUILD%"=="" (
  echo   MSBuild was not found. Run this from a Developer Command Prompt,
  echo   or install the Desktop development with C++ workload.
  exit /b 1
)
echo   MSBuild: %MSBUILD%
echo   Config:  %CONFIG% ^| x64 ^| %TARGET%
echo.

"%MSBUILD%" "%~dp0LiquidCam.sln" /t:%TARGET% /p:Configuration=%CONFIG% /p:Platform=x64 /m /nologo /v:minimal
if errorlevel 1 (
  echo.
  echo   Build failed.
  exit /b 1
)

echo.
echo   Done: %~dp0build\x64\%CONFIG%\LiquidCam.exe
endlocal
