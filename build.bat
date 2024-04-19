@echo off 

set "SRC_DIR=%CD%"

if "clean"=="%1" (

    REM remove the build directory (if exist)
    if exist bin\ (
        rmdir /q /s bin

        echo:
        echo        Removed build directory and binary
        echo:
    ) else (
        echo:
        echo        No build directory was created in the first place
        echo:
    )
) else (

    REM call the 2022 version of MSVC
    if "%VisualStudioVersion%"=="" call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x64
    REM try the 2019 version if you don't have the 2022 version
    REM if "%VisualStudioVersion%"=="" call "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvarsall.bat" x64


    if not exist bin\ mkdir bin
    pushd bin
        cl /O2 /Fesimdbrot.exe "%SRC_DIR%\build.c" ^
            /link user32.lib kernel32.lib gdi32.lib
    popd 


    echo:
    if ERRORLEVEL 1 (
        echo        Build failed with exit code 1
    ) else (
        echo        Build finished
    )
    echo:
)

