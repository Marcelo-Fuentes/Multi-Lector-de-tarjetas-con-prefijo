/*
  Multi_Lector_Prefijo.ino  (placa GV_CableCedula v2.0, ATmega328P)
  ------------------------------------------------------------------
  Lector de tarjetas HID. Al leer una tarjeta busca su formato en la
  tabla de formatos (EEPROM), saca el numero impreso y el prefijo, y
  envia al reloj el prefijo seguido del numero impreso.

  La tabla se arma con la herramienta "Herramienta_Formatos.html" y
  se graba con "2 Cargar formatos.bat" (solo EEPROM, no toca el
  programa). Con el fusible EESAVE la tabla no se borra al volver a
  grabar el programa.

  Cada formato (16 bytes en la EEPROM) dice:
    - cuantos bits tiene la tarjeta
    - en que bits esta el numero impreso (binario o BCD)
    - en que bits esta el prefijo (binario o BCD)
    - modo 1: el prefijo se lee de la tarjeta tal cual
      modo 2: si esos bits valen "clave" se envia el prefijo "prefijo"
      modo 0: sin prefijo
    - con cuantos digitos se rellena el numero impreso
  Se usa el primer formato que coincide. Si ninguno coincide se envia
  el numero como antes (26, 35 y 36 bits), sin prefijo.

  Ejemplo: prefijo 569, numero impreso 12345, 5 digitos -> 56912345.
  Se envia como Wiegand 34 (32 bits, igual que salian las cedulas).

  Switches (ON = GND, se leen en cada lectura):
    todos OFF -> normal: prefijo + numero impreso
    SW1 ON    -> diagnostico: al pasar una tarjeta envia
                 1xxxxx = cantidad de bits (100000 + bits)
                 2xxxxx = bits 0..15  (200000 + valor)
                 3xxxxx = bits 16..31 (300000 + valor)
                 4xxxxx = bits 32..47 (si la tarjeta tiene mas de 32)
                 5xxxxx = bits 48..63 (si tiene mas de 48)
                 Esos numeros se escriben en la herramienta.
    SW2 ON    -> prueba: envia cada 5 s los prefijos fijos cargados
                 (uno por vez); si no hay ninguno envia 123456
    SW3 ON    -> sin prefijo: solo el numero impreso

  Conexiones:
    Lector DATA0 (verde) -> J3 pin 5 (A4), DATA1 (blanco) -> J3 pin 6 (A5)
    Reloj: D8 = DATA0, D9 = DATA1
    Switch 1 = D4, switch 2 = D3, switch 3 = D2
  ------------------------------------------------------------------
*/

#include <avr/eeprom.h>
#include <avr/interrupt.h>
#include <avr/io.h>

// ===================== CONFIGURACION ==============================

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

// ===================== TABLA DE FORMATOS ==========================

struct Formato {          // 16 bytes, igual que en la herramienta
  uint8_t  bits;          // 0 o 0xFF = fin de la tabla
  uint8_t  numDesde, numAncho;
  uint8_t  preDesde, preAncho;
  uint8_t  bcd;           // bit0 = numero en BCD, bit1 = prefijo en BCD
  uint8_t  modo;          // 0 sin prefijo, 1 leido, 2 fijo por clave
  uint8_t  digitos;       // relleno del numero impreso
  uint32_t clave;
  uint32_t prefijo;
};

const uint8_t MAX_FORMATOS = 1024 / sizeof(Formato);

static bool leerFormato(uint8_t i, Formato &f) {
  eeprom_read_block(&f, (const void *)(i * sizeof(Formato)), sizeof(Formato));
  return f.bits != 0 && f.bits != 0xFF;
}

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

// Campo de "ancho" bits desde "desde", en binario o BCD.
// Devuelve false si no cabe en la tarjeta o el BCD trae un no-digito.
static bool leerCampo(uint64_t raw, uint8_t n, uint8_t desde, uint8_t ancho,
                      bool bcd, uint32_t &valor) {
  if (ancho == 0 || ancho > 32 || desde + ancho > n) return false;
  uint32_t v = tomarBits(raw, n, desde, desde + ancho - 1);
  if (!bcd) { valor = v; return true; }
  uint32_t num = 0;
  for (int8_t i = ancho / 4 - 1; i >= 0; i--) {
    uint8_t dig = (v >> (4 * i)) & 0xF;
    if (dig > 9) return false;
    num = num * 10 + dig;
  }
  valor = num;
  return true;
}

// Numero de la tarjeta sin tabla (formatos conocidos)
static uint32_t numeroPorDefecto(uint64_t raw, uint8_t n) {
  uint32_t v;
  if (n == 36 && leerCampo(raw, n, 12, 20, true, v)) return v;  // BCD
  if (n == 35) return tomarBits(raw, n, 14, 33);
  if (n == 26) return tomarBits(raw, n, 9, 24);
  if (n > 26)  return tomarBits(raw, n, n - 25, n - 2);
  return tomarBits(raw, n, 0, n - 1);
}

// prefijo seguido del numero (con "digitos" digitos)
static uint32_t conPrefijo(uint32_t prefijo, uint32_t numero, uint8_t digitos) {
  uint32_t escala = 1;
  for (uint8_t i = 0; i < digitos; i++) escala *= 10;
  while (escala <= numero) escala *= 10;    // numero mas largo: no se corta
  return prefijo * escala + numero;
}

// Busca el formato de la tarjeta y arma el numero a enviar
static uint32_t numeroAEnviar(uint64_t raw, uint8_t n, bool usarPrefijo) {
  Formato f;
  for (uint8_t i = 0; i < MAX_FORMATOS && leerFormato(i, f); i++) {
    if (f.bits != n) continue;
    uint32_t numero, campo = 0;
    if (!leerCampo(raw, n, f.numDesde, f.numAncho, f.bcd & 1, numero)) continue;
    // modo 2 sin campo (preAncho 0): basta con la cantidad de bits
    if (f.modo != 0 && (f.modo == 1 || f.preAncho) &&
        !leerCampo(raw, n, f.preDesde, f.preAncho, f.bcd & 2, campo)) continue;
    if (f.modo == 2 && f.preAncho && campo != f.clave) continue;
    if (!usarPrefijo || f.modo == 0) return numero;
    return conPrefijo(f.modo == 2 ? f.prefijo : campo, numero, f.digitos);
  }
  return numeroPorDefecto(raw, n);
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

// ===================== MODOS ======================================

static void modoPrueba() {
  static uint8_t siguiente = 0;
  Formato f;
  for (uint8_t vuelta = 0; vuelta < 2; vuelta++) {
    for (uint8_t i = siguiente; i < MAX_FORMATOS && leerFormato(i, f); i++) {
      if (f.modo == 2) {
        enviarW34(f.prefijo);
        siguiente = i + 1;
        delay(5000);
        return;
      }
    }
    if (siguiente == 0) break;              // no hay prefijos fijos
    siguiente = 0;
  }
  enviarW34(123456);
  delay(5000);
}

static void diagnostico(uint64_t raw, uint8_t n) {
  uint64_t a = raw << (64 - n);           // primer bit = bit mas alto
  enviarW34(100000UL + n);
  for (uint8_t k = 0; k < 4 && k * 16 < n; k++) {
    delay(1500);
    enviarW34((k + 2) * 100000UL + (uint16_t)(a >> (48 - 16 * k)));
  }
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
  if (!(PIND & SW2)) {                 // prueba
    modoPrueba();
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
  if (!(PIND & SW1)) diagnostico(raw, n);
  else enviarW34(numeroAEnviar(raw, n, PIND & SW3));
}
