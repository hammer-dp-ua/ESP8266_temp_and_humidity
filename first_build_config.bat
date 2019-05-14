:: Executed outside Eclipse at first time to configure project’s ESP8266_RTOS_SDK settings

:: 1. Update MSYS2 as in %IDF_PATH%\tools\windows\windows_install_prerequisites.sh script or just execute it

set IDF_PATH=C:\Users\USER\ESP8266\ESP8266_RTOS_SDK_v3
set MSYSTEM=MINGW32
set Path=%Path%;C:\msys32\usr\bin

make menuconfig
