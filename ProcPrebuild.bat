pushd %~dp0

XCOPY Source\*.h "../../Libraries/Include/CraftEngine" /E /Y /I
XCOPY Source\*.hpp "../../Libraries/Include/CraftEngine" /E /Y /I

PAUSE