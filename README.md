# Multi Lector de tarjetas con prefijo

Firmware para la placa GV_CableCedula v2.0 (ATmega328P). Lee tarjetas HID, detecta el prefijo de cada tarjeta y envía al reloj **prefijo + número impreso**. No lee cédulas.

Ejemplo: prefijo 569 y número impreso 12345 → el reloj recibe 56912345.

## Qué hay en la carpeta

| Archivo | Para qué |
|---|---|
| `1 Cargar programa.bat` | Graba el programa (`Multi_Lector_Prefijo.hex`) y los fusibles. Se hace una vez. |
| `2 Cargar formatos.bat` | Graba solo la tabla de formatos (`formatos.hex`) en la EEPROM. Se repite cada vez que se agregan prefijos. |
| `Herramienta_Formatos.html` | Se abre con doble clic en el navegador (no necesita internet). Arma `formatos.hex`. |
| `formatos.hex` | Tabla de formatos. La que viene en el repo está vacía. |
| `avr/` | avrdude para el USBtiny. |

Fusibles: lfuse 0xFF, **hfuse 0xD1**, efuse 0xFD. El 0xD1 activa EESAVE, así que volver a grabar el programa no borra la tabla de formatos.

## Paso a paso

1. Con la placa desconectada del reloj, conectar el USBtiny al ICSP (J3) y ejecutar `1 Cargar programa.bat`. Si se corta, cambiar `-B 2` por `-B 4`.
2. Conectar la placa al reloj, poner **SW1 en ON** (diagnóstico) y pasar la tarjeta. El reloj recibe varios números: `1xxxxx`, `2xxxxx`, `3xxxxx` y a veces `4xxxxx`.
3. Abrir `Herramienta_Formatos.html`, escribir esos números, el número impreso y el prefijo, y pulsar **Analizar**. La herramienta muestra qué se enviará al reloj. Si está bien, **Agregar a la lista**.
4. Repetir el paso 2 y 3 con una tarjeta de cada prefijo distinto.
5. **Descargar formatos.hex**, ponerlo en esta carpeta (reemplazando el que está) y ejecutar `2 Cargar formatos.bat`.
6. Volver SW1 a OFF.

La herramienta recuerda la lista en el navegador. Con **Abrir un formatos.hex** se puede recuperar y modificar una tabla ya grabada.

## Cómo encuentra el prefijo

La herramienta busca en los bits de la tarjeta el número impreso y el prefijo:

* **El prefijo viene escrito en la tarjeta** (por ejemplo, como código de instalación o en BCD): el lector lo lee de cada tarjeta. Una sola tarjeta enseña todo el formato y sirve para cualquier prefijo.
* **El prefijo no viene escrito tal cual**: se asocia el código de instalación de la tarjeta a ese prefijo (por ejemplo, código 144 → prefijo 569). Hay que pasar una tarjeta de cada prefijo.

Si una tarjeta no coincide con ningún formato se envía su número sin prefijo, igual que antes.

El número impreso se rellena con ceros hasta 5 dígitos por defecto (569 + 6136 = 56906136). Se puede cambiar en la herramienta. Todo se envía como **Wiegand 34** (32 bits, el mismo formato con que salían las cédulas), así que el total no puede pasar de 4294967295.

## Switches

| Switch ON | Qué hace |
|---|---|
| ninguno | Normal: prefijo + número impreso |
| SW1 | Diagnóstico: envía los números para la herramienta |
| SW2 | Prueba: envía cada 5 s los prefijos fijos cargados, uno por vez (si no hay, 123456) |
| SW3 | Sin prefijo: envía solo el número impreso |

## Conexiones

* Lector DATA0 (verde) → J3 pin 5 (A4), DATA1 (blanco) → J3 pin 6 (A5)
* Reloj: D8 = DATA0, D9 = DATA1
* Switch 1 = D4, switch 2 = D3, switch 3 = D2 (ON = GND)

## Tabla en la EEPROM

16 bytes por formato desde la dirección 0, terminada en un byte 0xFF: bits de la tarjeta, número (desde, ancho), prefijo o código (desde, ancho), BCD (bit0 número, bit1 prefijo), modo (0 sin prefijo, 1 leído, 2 fijo por código), dígitos, código (uint32) y prefijo fijo (uint32).

## Simulación

`firmware/Multi_Lector_Prefijo/simulacion_simavr.c` prueba el programa en simavr con tarjetas de 26, 35 y 36 bits, con y sin tabla, y los modos diagnóstico y prueba.
