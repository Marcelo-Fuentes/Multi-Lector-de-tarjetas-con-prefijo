/*
  Prefijo_Simple.ino  (placa GV_CableCedula v2.0, ATmega328P)
  ------------------------------------------------------------------
  Lee la tarjeta HID y envia al reloj el prefijo de la tarjeta (su
  codigo de instalacion) seguido del numero de tarjeta, con el numero
  rellenado a DIGITOS_TARJETA digitos:
    prefijo 569, tarjeta 12345 -> 56912345
    prefijo 144, tarjeta 6136  -> 14406136
  Se envia como Wiegand 34 (32 bits, igual que salian las cedulas).
  No usa switches.

  Formatos:
    26 bits (H10301):          prefijo = bits 1..8,   numero = bits 9..24
    34 bits:                   prefijo = bits 1..16,  numero = bits 17..32
    35 bits (Corporate 1000):  prefijo = bits 2..13,  numero = bits 14..33
    36 bits (BCD, 9 digitos):  prefijo = digitos 1..3, numero = digitos 4..8
                               ej. 0x79700799A -> 797 y 00799
    37 bits (H10304):          prefijo = bits 1..16,  numero = bits 17..35
    otros:                     sin prefijo, 24 bits bajos sin la paridad

  Conexiones:
    Lector DATA0 (verde) -> J3 pin 5 (A4), DATA1 (blanco) -> J3 pin 6 (A5)
    Reloj: D8 = DATA0, D9 = DATA1
  ------------------------------------------------------------------
*/

#include <avr/interrupt.h>
#include <avr/io.h>

// ===================== CONFIGURACION ==============================

// Digitos del numero de tarjeta detras del prefijo (ceros a la izquierda)
const uint8_t DIGITOS_TARJETA = 5;

const uint8_t RELOJ_D0 = (1 << 0);     // D8 = PB0
const uint8_t RELOJ_D1 = (1 << 1);     // D9 = PB1

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

// Digitos BCD (4 bits cada uno); false si alguno no es digito
static bool bcd(uint32_t v, uint8_t digitos, uint32_t &num) {
  num = 0;
  for (int8_t i = digitos - 1; i >= 0; i--) {
    uint8_t dig = (v >> (4 * i)) & 0xF;
    if (dig > 9) return false;
    num = num * 10 + dig;
  }
  return true;
}

// Saca prefijo y numero segun el formato de la tarjeta
static void decodificar(uint64_t raw, uint8_t n, uint32_t &prefijo, uint32_t &numero) {
  prefijo = 0;
  if (n == 36 && bcd(tomarBits(raw, n, 0, 11), 3, prefijo) &&
                 bcd(tomarBits(raw, n, 12, 31), 5, numero)) return;
  prefijo = 0;
  if      (n == 26) { prefijo = tomarBits(raw, n, 1, 8);  numero = tomarBits(raw, n, 9, 24); }
  else if (n == 34) { prefijo = tomarBits(raw, n, 1, 16); numero = tomarBits(raw, n, 17, 32); }
  else if (n == 35) { prefijo = tomarBits(raw, n, 2, 13); numero = tomarBits(raw, n, 14, 33); }
  else if (n == 37) { prefijo = tomarBits(raw, n, 1, 16); numero = tomarBits(raw, n, 17, 35); }
  else if (n > 26)  numero = tomarBits(raw, n, n - 25, n - 2);
  else              numero = tomarBits(raw, n, 0, n - 1);
}

// prefijo seguido del numero (con DIGITOS_TARJETA digitos)
static uint32_t conPrefijo(uint32_t prefijo, uint32_t numero) {
  if (prefijo == 0) return numero;
  uint32_t escala = 1;
  for (uint8_t i = 0; i < DIGITOS_TARJETA; i++) escala *= 10;
  while (escala <= numero) escala *= 10;    // numero mas largo: no se corta
  return prefijo * escala + numero;
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

// ===================== PROGRAMA PRINCIPAL =========================

void setup() {
  PORTB |= RELOJ_D0 | RELOJ_D1;         // reposo en alto
  DDRB  |= RELOJ_D0 | RELOJ_D1;
  DDRC  &= ~MASK_IN;
  PORTC |= MASK_IN;                     // pull-up A4/A5
  estadoPrevio = PINC & MASK_IN;
  PCMSK1 |= (1 << PCINT12) | (1 << PCINT13);
  PCIFR  |= (1 << PCIF1);
  PCICR  |= (1 << PCIE1);
}

void loop() {
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
  uint32_t prefijo, numero;
  decodificar(raw, n, prefijo, numero);
  enviarW34(conPrefijo(prefijo, numero));
}
