@echo off
cd /d "%~dp0..\.."
node ui\tests\ui-command-test.mjs
if errorlevel 1 exit /b 1
node ui\tests\sounds-test.mjs
if errorlevel 1 exit /b 1
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /I"ui\tests\stubs" "ui\tests\link-test.cpp" /Fo"build\ui-command-tests\link-test.obj" /Fe"build\ui-command-tests\link-test.exe"
if errorlevel 1 exit /b 1
build\ui-command-tests\link-test.exe
exit /b %errorlevel%
