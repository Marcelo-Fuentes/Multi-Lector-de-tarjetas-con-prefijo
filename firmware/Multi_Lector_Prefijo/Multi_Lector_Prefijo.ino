/*
  Multi_Lector_Prefijo.ino  (placa GV_CableCedula v2.0, ATmega328P)
  ------------------------------------------------------------------
  Lector de tarjetas HID que antepone un prefijo al numero de la
  tarjeta antes de enviarlo al reloj. Ya no lee cedulas.

  Switches (se leen en cada lectura, no hace falta reiniciar):
    SW1 y SW2 eligen el prefijo de la tabla PREFIJOS:
       SW1 OFF, SW2 OFF -> PREFIJOS[0]
       SW1 ON,  SW2 OFF -> PREFIJOS[1]
       SW1 OFF, SW2 ON  -> PREFIJOS[2]
       SW1 ON,  SW2 ON  -> PREFIJOS[3]
    SW3 ON -> modo prueba: envia el prefijo elegido cada 5 s
              (si el prefijo es 0 envia 123456)

  Numero enviado = prefijo seguido del numero de la tarjeta con
  DIGITOS_TARJETA digitos (ceros a la izquierda). Ejemplo con 5:
    prefijo 569, tarjeta 12345 -> 56912345
    prefijo 569, tarjeta 799   -> 56900799
  Con prefijo 0 se envia solo el numero de la tarjeta.
  Se envia como Wiegand 34 (32 bits, igual que salian las cedulas),
  asi caben numeros de hasta 4294967295.

  Conexiones:
    Lector DATA0 (verde) -> J3 pin 5 (A4), DATA1 (blanco) -> J3 pin 6 (A5)
    Reloj: D8 = DATA0, D9 = DATA1
    Switch 1 = D4, switch 2 = D3, switch 3 = D2 (ON = GND)
  Formatos de tarjeta:
    26 bits (H10301):          numero = bits 9..24
    35 bits (Corporate 1000):  numero = bits 14..33
    36 bits (BCD, 9 digitos):  numero = digitos 4..8 (bits 12..31),
                               ej. 0x79700799A -> 00799
    otros:                     24 bits bajos sin la paridad final
  ------------------------------------------------------------------
*/

#include <avr/interrupt.h>
#include <avr/io.h>

// ===================== CONFIGURACION ==============================

// Prefijos que se eligen con SW1 y SW2 (0 = sin prefijo)
const uint32_t PREFIJOS[4] = { 569, 0, 0, 0 };

// Digitos que ocupa la tarjeta detras del prefijo (0 = pegar el
// numero tal cual, sin ceros de relleno)
const uint8_t DIGITOS_TARJETA = 5;

const uint8_t RELOJ_D0 = (1 << 0);     // D8 = PB0
const uint8_t RELOJ_D1 = (1 << 1);     // D9 = PB1
const uint8_t SW1 = (1 << 4);          // D4 = PD4
const uint8_t SW2 = (1 << 3);          // D3 = PD3
const uint8_t SW3 = (1 << 2);          // D2 = PD2

const uint8_t MASK_IN_D0 = (1 << 4);   // A4
const uint8_t MASK_IN_D1 = (1 << 5);   // A5
const uint8_t MASK_IN = MASK_IN_D0 | MASK_IN_D1;

const uint16_t MIN_LOW_US = 20;
const uint16_t MAX_LOW_US = 250;
const uint32_t FIN_TRAMA_US = 120000UL;

// ===================== LECTURA ====================================

volatile uint64_t bitsLeidos = 0;
volatile uint8_t  cantBits   = 0;
volatile uint32_t ultimoPulsoUs = 0;
volatile uint32_t bajada[2] = {0, 0};
volatile uint8_t  estadoPrevio = MASK_IN;

ISR(PCINT1_vect) {
  uint8_t ahora8 = PINC & MASK_IN;
  uint8_t cambio = ahora8 ^ estadoPrevio;
  estadoPrevio = ahora8;
  uint32_t t = micros();

  for (uint8_t linea = 0; linea < 2; linea++) {
    uint8_t mask = linea ? MASK_IN_D1 : MASK_IN_D0;
    if (!(cambio & mask)) continue;
    if (!(ahora8 & mask)) {
      bajada[linea] = t;
    } else {
      uint32_t w = t - bajada[linea];
      if (w >= MIN_LOW_US && w <= MAX_LOW_US && cantBits < 64) {
        bitsLeidos = (bitsLeidos << 1) | linea;
        cantBits++;
        ultimoPulsoUs = t;
      }
    }
  }
}

// Bits [desde..hasta], contando desde el primero recibido (indice 0)
static uint32_t tomarBits(uint64_t raw, uint8_t n, uint8_t desde, uint8_t hasta) {
  uint8_t ancho = hasta - desde + 1;
  return (uint32_t)((raw >> (n - 1 - hasta)) & ((1ULL << ancho) - 1));
}

// Numero de la tarjeta segun su formato
static uint32_t numeroTarjeta(uint64_t raw, uint8_t n) {
  if (n == 36) {                       // BCD: 5 digitos en bits 12..31
    uint32_t bcd = tomarBits(raw, n, 12, 31), num = 0;
    for (int8_t i = 4; i >= 0; i--) {
      uint8_t dig = (bcd >> (4 * i)) & 0xF;
      if (dig > 9) return tomarBits(raw, n, n - 25, n - 2);
      num = num * 10 + dig;
    }
    return num;
  }
  if (n == 35) return tomarBits(raw, n, 14, 33);
  if (n == 26) return tomarBits(raw, n, 9, 24);
  if (n > 26)  return tomarBits(raw, n, n - 25, n - 2);
  return tomarBits(raw, n, 0, n - 1);
}

// ===================== SALIDA =====================================

static void pulso(uint8_t mask) {
  PORTB &= ~mask;
  delayMicroseconds(40);
  PORTB |= mask;
  delayMicroseconds(2100);
}

// Envia un numero de 32 bits como Wiegand 34 (paridad par sobre los
// 16 bits altos, impar sobre los 16 bajos), igual que las cedulas
static void enviarW34(uint32_t d) {
  uint8_t unosAlto = 0, unosBajo = 0;
  for (uint8_t i = 0; i < 16; i++) {
    unosBajo += (d >> i) & 1;
    unosAlto += (d >> (i + 16)) & 1;
  }
  pulso((unosAlto & 1) ? RELOJ_D1 : RELOJ_D0);
  for (int8_t i = 31; i >= 0; i--) pulso(((d >> i) & 1) ? RELOJ_D1 : RELOJ_D0);
  pulso(((unosBajo & 1) ^ 1) ? RELOJ_D1 : RELOJ_D0);
}

// prefijo seguido de la tarjeta (con DIGITOS_TARJETA digitos)
static uint32_t conPrefijo(uint32_t prefijo, uint32_t tarjeta) {
  if (prefijo == 0) return tarjeta;
  uint32_t escala = 1;
  for (uint8_t i = 0; i < DIGITOS_TARJETA; i++) escala *= 10;
  while (escala <= tarjeta) escala *= 10;   // tarjeta mas larga: no se corta
  return prefijo * escala + tarjeta;
}

static uint32_t prefijoElegido() {
  uint8_t i = (!(PIND & SW1) ? 1 : 0) | (!(PIND & SW2) ? 2 : 0);
  return PREFIJOS[i];
}

// ===================== PROGRAMA PRINCIPAL =========================

void setup() {
  PORTB |= RELOJ_D0 | RELOJ_D1;         // reposo en alto
  DDRB  |= RELOJ_D0 | RELOJ_D1;
  DDRD  &= ~(SW1 | SW2 | SW3);
  PORTD |= SW1 | SW2 | SW3;             // pull-up switches
  DDRC  &= ~MASK_IN;
  PORTC |= MASK_IN;                     // pull-up A4/A5
  estadoPrevio = PINC & MASK_IN;
  PCMSK1 |= (1 << PCINT12) | (1 << PCINT13);
  PCIFR  |= (1 << PCIF1);
  PCICR  |= (1 << PCIE1);
}

void loop() {
  if (!(PIND & SW3)) {                 // modo prueba
    uint32_t p = prefijoElegido();
    enviarW34(p ? p : 123456);
    delay(5000);
    return;
  }

  noInterrupts();
  uint8_t n = cantBits;
  uint32_t ultimo = ultimoPulsoUs;
  interrupts();

  if (n == 0 || (micros() - ultimo) <= FIN_TRAMA_US) return;

  noInterrupts();
  uint64_t raw = bitsLeidos;
  n = cantBits;
  cantBits = 0; bitsLeidos = 0;
  interrupts();

  if (n < 4) return;                   // ruido
  enviarW34(conPrefijo(prefijoElegido(), numeroTarjeta(raw, n)));
}
