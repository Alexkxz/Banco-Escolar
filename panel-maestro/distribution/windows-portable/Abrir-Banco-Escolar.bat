@echo off
setlocal
chcp 65001 >nul
title Banco Escolar - Panel Maestro

set "PANEL_ROOT=%~dp0"
set "SERVER_FILE=%PANEL_ROOT%server\static-server.mjs"
set "DIST_INDEX=%PANEL_ROOT%dist\index.html"
set "NODE_EXE="

if not exist "%SERVER_FILE%" goto missing_server
if not exist "%DIST_INDEX%" goto missing_build

if exist "%PANEL_ROOT%runtime\node.exe" set "NODE_EXE=%PANEL_ROOT%runtime\node.exe"
if not defined NODE_EXE (
  for /d %%D in ("%PANEL_ROOT%runtime\node-v*-win-x64") do if not defined NODE_EXE if exist "%%~fD\node.exe" set "NODE_EXE=%%~fD\node.exe"
)
if not defined NODE_EXE (
  for /d %%D in ("%PANEL_ROOT%runtime\node-v*-win-arm64") do if not defined NODE_EXE if exist "%%~fD\node.exe" set "NODE_EXE=%%~fD\node.exe"
)

if not defined NODE_EXE (
  for /f "delims=" %%N in ('where.exe node.exe 2^>nul') do if not defined NODE_EXE set "NODE_EXE=%%N"
)
if not defined NODE_EXE goto missing_node

echo Iniciando Banco Escolar - Panel Maestro...
echo Runtime: "%NODE_EXE%"
echo Direccion local: http://127.0.0.1:4173/
echo Para cerrar Banco Escolar, escribe salir y presiona Enter.
echo Los errores se registran en %%LOCALAPPDATA%%\BancoEscolar\logs\panel-server.log
echo.

"%NODE_EXE%" "%SERVER_FILE%" --open
set "SERVER_EXIT=%ERRORLEVEL%"
if not "%SERVER_EXIT%"=="0" (
  echo.
  echo El Panel no pudo iniciar o termino con un error. Revisa el mensaje anterior y el registro local.
  pause
)
exit /b %SERVER_EXIT%

:missing_server
echo Error: no se encontro el servidor local:
echo   "%SERVER_FILE%"
goto fail

:missing_build
echo Error: no se encontro el Panel compilado:
echo   "%DIST_INDEX%"
echo Ejecuta el build del Panel en una etapa de desarrollo autorizada.
goto fail

:missing_node
echo Error: no se encontro Node.js compatible.
echo Incluye runtime\node.exe en la distribucion portable o instala Node.js compatible para desarrollo.
echo Se requiere Node.js 20.19+ o 22.12+.
goto fail

:fail
echo.
pause
exit /b 1
