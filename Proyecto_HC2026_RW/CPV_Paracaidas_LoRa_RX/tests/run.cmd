@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cd /d "%~dp0..\..\.."
if not exist build\cpv-rx-tests mkdir build\cpv-rx-tests
cl /nologo /std:c++17 /EHsc /W4 /I"Proyecto_HC2026_RW\CPV_Paracaidas_LoRa_RX\tests\stubs" "Proyecto_HC2026_RW\CPV_Paracaidas_LoRa_RX\tests\test.cpp" /Fo"build\cpv-rx-tests\test.obj" /Fe"build\cpv-rx-tests\test.exe"
if errorlevel 1 exit /b 1
build\cpv-rx-tests\test.exe SIM\350deg_AR_4_57ms_Aire.e
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /W3 /I"Proyecto_HC2026_RW\CPV_Paracaidas_LoRa_RX\tests\radio-stubs" "Proyecto_HC2026_RW\CPV_Paracaidas_LoRa_RX\tests\radio-test.cpp" /Fo"build\cpv-rx-tests\radio-test.obj" /Fe"build\cpv-rx-tests\radio-test.exe"
if errorlevel 1 exit /b 1
build\cpv-rx-tests\radio-test.exe
exit /b %errorlevel%
