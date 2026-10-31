// Placa NEC TK-80: CPU 8080, ROM monitor, EEPROM, RAM, 8255 y teclado.
// Mapa y comportamiento tomados del emulador notwendig/tk80.
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace tk80 {

constexpr uint16_t ROM_START = 0x0000, ROM_SIZE = 0x0300;   // monitor
constexpr uint16_t EEP_START = 0x0300, EEP_SIZE = 0x0100;   // zona de programa (EEPROM)
constexpr uint16_t RAM_START = 0x8000, RAM_SIZE = 0x0400;   // RAM de 1 KiB
constexpr uint16_t DISP_ADDR = 0x83F8;                      // 8 dígitos, el 0 es el de la izquierda

// Registros del monitor en RAM (según el ensamblador de la ROM), 16 bits little-endian
constexpr uint16_t MON_DATA  = 0x83EC;                      // valor tecleado
constexpr uint16_t MON_ADRES = 0x83EE;                      // registro de dirección

void init();                    // pone la placa en estado de encendido
int  step();                    // ejecuta una instrucción; devuelve estados T
                                // (devuelve 0 si RESET está pulsado)

// Bus de la CPU
uint8_t read(uint16_t addr);
void    write(uint16_t addr, uint8_t value);
uint16_t readWord(uint16_t addr);   // little-endian

// Cargador (Intel HEX): escribe en RAM y EEPROM; la ROM no se puede escribir.
bool    isLoadable(uint16_t addr);
void    loadByte(uint16_t addr, uint8_t value);

// Display: 8 bytes de segmentos (bit0=a ... bit6=g, bit7=punto)
const uint8_t* display();

// Teclado. Nombres: "0".."9","a".."f","run","ret","adrs","rinc","rdec","winc",
// "reset". "store" y "load" se aceptan pero no llegan a la ROM: los gestiona la
// interfaz (ver app::tapeStore). Devuelve false si el nombre no existe.
// Seguro para llamar desde otra tarea (un único escritor).
bool setKey(const char* name, bool pressed);

// Suelta todas las teclas (p. ej. al perderse la conexión con el navegador).
void releaseAllKeys();

// Para la tarea de emulación: aplica un RESET pendiente (tras soltar la tecla)
// y dice si la CPU debe permanecer parada.
void pollReset();

// Acceso de depuración (PC de la CPU)
uint16_t cpuPC();

}  // namespace tk80
