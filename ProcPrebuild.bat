pushd %~dp0

XCOPY Source\*.h "../../Includes" /E /Y /I
XCOPY Source\*.hpp "../../Includes" /E /Y /I

PAUSE