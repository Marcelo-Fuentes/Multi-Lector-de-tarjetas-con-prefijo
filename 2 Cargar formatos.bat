.\avr\bin\avrdude -C .\avr\etc\avrdude.conf -v -p m328p -c usbtiny -B 2 -U eeprom:w:.\formatos.hex:i
pause
