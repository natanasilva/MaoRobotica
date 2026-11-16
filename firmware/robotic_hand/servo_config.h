#pragma once
#include <Arduino.h>

// Ajuste com os cabos dos dedos desacoplados. Só depois altere para true.
constexpr bool CALIBRATED = true;
constexpr uint8_t PCA_ADDRESS = 0x40;
constexpr uint8_t OE_PIN = 7;  // OE com pull-up externo de 1k para VCC (ver README).
constexpr uint16_t PWM_HZ = 50;
constexpr uint32_t COMMAND_TIMEOUT_MS = 500;
constexpr uint16_t STEP_INTERVAL_MS = 20;
constexpr uint16_t MAX_STEP_US = 20;  // 1000 us/s (~90 graus em 1 s), ajustar à mecânica.

// Valores ilustrativos, NÃO são limites universais dos servos.
constexpr uint8_t CHANNELS[5] = {0, 1, 2, 3, 4};
// Dedos 0-3: servos de 180 graus, curso de 90 graus (45 a 135 graus).
// Dedo 4 (mínimo): servo de 360 graus (rotação contínua). Pulso = velocidade,
// por isso fica em 1500 us (parado) nos dois extremos.
constexpr uint16_t OPEN_US[5] = {1000, 1000, 1000, 1000, 1500};
constexpr uint16_t CLOSED_US[5] = {2000, 2000, 2000, 2000, 1500};
