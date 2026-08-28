@ECHO OFF
REM Copy engine headers to the location the client includes from.
REM DummyClient does not see Source\ directly - only this copy.
REM
REM NOTE: keep this file ASCII only. cmd.exe reads .bat as the system codepage,
REM so UTF-8 comments turn into garbage and break parsing.

pushd "%~dp0"

SET "INCLUDE_DIR=..\..\Libraries\Include\CraftEngine"

REM Clear the destination before copying.
REM XCOPY never removes stale files, so a deleted or renamed header leaves an
REM old copy behind and the client silently keeps compiling against it.
IF EXIST "%INCLUDE_DIR%" RMDIR /S /Q "%INCLUDE_DIR%"

XCOPY Source\*.h "%INCLUDE_DIR%" /E /Y /I /Q
XCOPY Source\*.hpp "%INCLUDE_DIR%" /E /Y /I /Q

popd
