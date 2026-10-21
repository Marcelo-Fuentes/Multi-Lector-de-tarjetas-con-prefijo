.\avr\bin\avrdude -C .\avr\etc\avrdude.conf -v -p m328p -c usbtiny -B 2 -U lfuse:w:0xff:m -U hfuse:w:0xd1:m -U efuse:w:0xfd:m -U flash:w:.\Multi_Lector_Prefijo.hex:i
pause
