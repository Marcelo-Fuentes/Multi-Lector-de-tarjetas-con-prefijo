# Multi Lector de tarjetas con prefijo

Firmware para la placa GV_CableCedula v2.0 (ATmega328P). Lee tarjetas HID y le antepone un prefijo al número antes de enviarlo al reloj. Esta versión ya no lee cédulas.

## Cómo cargar

1. Copiar la carpeta completa (con `avr/`, `Multi_Lector_Prefijo.hex` y el `.bat`).
2. Conectar el USBtiny al ICSP (J3) con la placa desconectada del reloj.
3. Ejecutar `Cargar Multi_Lector_Prefijo.bat` (graba con `-B 2` y verifica). Si se corta, cambiar `-B 2` por `-B 4`.

Fusibles: lfuse 0xFF, **hfuse 0xD9**, efuse 0xFD. El programa arranca en la dirección 0, así que ya no se usa 0xD8 (esa era para el firmware con cédulas en la sección de arranque).

## Switches

| SW1 | SW2 | Prefijo |
|---|---|---|
| OFF | OFF | `PREFIJOS[0]` (569) |
| ON | OFF | `PREFIJOS[1]` (0 = sin prefijo) |
| OFF | ON | `PREFIJOS[2]` (0 = sin prefijo) |
| ON | ON | `PREFIJOS[3]` (0 = sin prefijo) |

**SW3 ON = modo prueba:** envía al reloj el prefijo elegido cada 5 s (si el prefijo elegido es 0, envía 123456).

Los switches se leen en cada lectura, no hace falta reiniciar.

## Cómo se arma el número

Número enviado = prefijo + número de la tarjeta con `DIGITOS_TARJETA` dígitos (ceros a la izquierda, 5 por defecto):

| Prefijo | Tarjeta | Se envía |
|---|---|---|
| 569 | 12345 | 56912345 |
| 569 | 6136 | 56906136 |
| 569 | 00799 (36 bits BCD) | 56900799 |
| 0 | 6136 | 6136 |

Con `DIGITOS_TARJETA = 0` el número de la tarjeta se pega tal cual (569 + 6136 = 5696136). Si una tarjeta tiene más dígitos que `DIGITOS_TARJETA`, se pega completa sin cortarla.

Se envía como **Wiegand 34** (32 bits), el mismo formato con que el firmware de cédulas mandaba los RUT al reloj, porque un número de 8 dígitos como 56912345 no cabe en Wiegand 26 (máximo 16777215). El número total no puede pasar de 4294967295.

## Cambiar los prefijos

Editar al inicio de `firmware/Multi_Lector_Prefijo/Multi_Lector_Prefijo.ino`:

```cpp
const uint32_t PREFIJOS[4] = { 569, 0, 0, 0 };
const uint8_t DIGITOS_TARJETA = 5;
```

y volver a compilar (o pedir el `.hex` nuevo).

## Conexiones

* Lector DATA0 (verde) → J3 pin 5 (A4), DATA1 (blanco) → J3 pin 6 (A5)
* Reloj: D8 = DATA0, D9 = DATA1
* Switch 1 = D4, switch 2 = D3, switch 3 = D2 (ON = GND)

Formatos de tarjeta: 26 bits (H10301), 35 bits (Corporate 1000), 36 bits BCD (ej. 0x79700799A → 00799); otros se toman con sus 24 bits bajos.

## Simulación

`firmware/Multi_Lector_Prefijo/simulacion_simavr.c` prueba el firmware en simavr con tarjetas de 26, 35 y 36 bits y el modo prueba, y revisa las paridades del Wiegand 34.
