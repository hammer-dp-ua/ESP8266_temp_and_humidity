@echo off

set SDK_PATH=/c/Users/USER/ESP8266/ESP8266_RTOS_SDK_v3
set BIN_PATH_WIN=Z:\USER\workspace\ESP8266_temp_and_humidity\bin
set BIN_PATH=/z/USER/workspace/ESP8266_temp_and_humidity/bin
set FOTA_PATH=Z:\USER\workspace\ESP8266_FOTA
set IDF_PATH=/c/Users/USER/ESP8266/ESP8266_RTOS_SDK_v3

::set out_file=%~dp0.output\eagle\debug\image\eagle.app.v6.out

erase /f /s /q .output
make clean
make --environment-overrides PATH=/c/Users/USER/ESP8266/xtensa-lx106-elf-win32-1.22.0-92/bin all
::make --environment-overrides PATH=/c/Users/USER/ESP8266/xtensa-lx106-elf-win32-1.22.0-92/bin:/c/MinGW/msys/1.0/bin:/c/Python27 COMPILE= BOOT=new APP=1 SPI_SPEED=40 SPI_MODE=QIO SPI_SIZE_MAP=4
::cp %BIN_PATH_WIN%\upgrade\user1.4096.new.6.bin %FOTA_PATH%\user1.bin
::xtensa-lx106-elf-objdump -dgl %out_file% > %~dp0\disassembled1.txt

::erase /f /s /q .output
::make clean
::make COMPILE= BOOT=new APP=2 SPI_SPEED=40 SPI_MODE=QIO SPI_SIZE_MAP=4
::cp %BIN_PATH_WIN%\upgrade\user2.4096.new.6.bin %FOTA_PATH%\user2.bin
::xtensa-lx106-elf-objdump -dgl %out_file% > %~dp0\disassembled2.txt

:end