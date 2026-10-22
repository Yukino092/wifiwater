@echo off
cd /d "%~dp0"
title Wifitry Firmware Flasher

echo ==================================================
echo    Wifitry Firmware Flasher
echo ==================================================
echo.
echo  Before flashing, please make sure:
echo    [1] Device connected to this PC with a USB DATA cable
echo    [2] Microphone is unplugged
echo    [3] Do NOT unplug or power off during flashing
echo.
echo  Press any key to start flashing...
pause >nul
echo.

echo [1/2] Flashing firmware, please wait about 30 seconds...
esptool.exe --chip esp32s3 -b 460800 write-flash --flash-mode dio --flash-size 16MB --flash-freq 80m 0x0 bootloader.bin 0x8000 partition-table.bin 0x10000 Wifitry.bin 0x610000 ota_data_initial.bin
if not errorlevel 1 goto ok

echo.
echo ==================================================
echo   Auto-detect serial port failed.
echo   Please type the COM port number and press Enter.
echo   Find it in: Device Manager - Ports (COM ^& LPT)
echo ==================================================
set /p PORT=COM port (e.g. COM5): 
esptool.exe --chip esp32s3 -p %PORT% -b 460800 write-flash --flash-mode dio --flash-size 16MB --flash-freq 80m 0x0 bootloader.bin 0x8000 partition-table.bin 0x10000 Wifitry.bin 0x610000 ota_data_initial.bin
if errorlevel 1 goto fail
goto ok

:fail
echo.
echo   FLASH FAILED. Please check:
echo     - USB cable is a DATA cable (not charge-only)
echo     - Correct COM port number
echo     - Microphone is unplugged
echo     - Antivirus did not block esptool.exe
echo.
pause
exit /b 1

:ok
echo.
echo ==================================================
echo   FLASH OK !  The device will restart automatically.
echo.
echo   After about 30 seconds, ask the device to report
echo   water quality by voice, to verify it works.
echo ==================================================
echo.
pause
exit /b 0
