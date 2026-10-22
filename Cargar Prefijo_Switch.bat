.\avr\bin\avrdude -C .\avr\etc\avrdude.conf -v -p m328p -c usbtiny -B 2 -U lfuse:w:0xff:m -U hfuse:w:0xd9:m -U efuse:w:0xfd:m -U flash:w:.\Prefijo_Switch.hex:i
pause
