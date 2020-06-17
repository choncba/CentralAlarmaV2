## Trough Console, using arduino avrdude (Mightycore must be instaled)
```
C:\Program Files (x86)\Arduino\hardware\tools\avr\bin\avrdude.exe -DV -p ATmega2560 -Pnet:192.168.1.120:23 -carduino -b115200 -U flash:w:C:\PATH\TO\firmware.hex:i -C C:\Program Files (x86)\Arduino\hardware\tools\avr\etc\avrdude.conf
```
## Directly from Platformio console (Upload doesn't work) - CHECK platformio.ini
``` 
platformio run -t program 
``` 