@echo off 

set "CC=gcc"
set "CC_FLAGS=-march=skylake -Ofast -mavx2 -mfma -Wall -Wextra -Wpedantic"
set "LD_FLAGS="
set "NAME=simdbrot"
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
    if not exist bin\ mkdir bin

    if "cl"=="%1" (

        REM call the 2022 version of MSVC
        if "%VisualStudioVersion%"=="" call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x64
        REM try the 2019 version if you don't have the 2022 version
        REM if "%VisualStudioVersion%"=="" call "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvarsall.bat" x64

        pushd bin
            cl /Zi /O2 "/Fe%NAME%.exe" "%SRC_DIR%\build.c" ^
                /link user32.lib kernel32.lib gdi32.lib
        popd 
    ) else (
        %CC% %CC_FLAGS% -Wall -Wextra -Wpedantic -o "bin\%NAME%" "%SRC_DIR%\build.c" %LD_FLAGS%^
            -luser32 -lkernel32 -lgdi32
    )


    echo:
    if ERRORLEVEL 1 (
        echo        Build failed with exit code 1
    ) else (
        echo        Build finished
    )
    echo:
)

