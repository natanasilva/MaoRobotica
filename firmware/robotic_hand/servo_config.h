#pragma once
#include <Arduino.h>

// Ajuste com os cabos dos dedos desacoplados. Só depois altere para true.
constexpr bool CALIBRATED = true;
constexpr uint8_t PCA_ADDRESS = 0x40;
constexpr uint8_t OE_PIN = 7;  // OE com pull-up externo de 1k para VCC (ver README).
constexpr uint16_t PWM_HZ = 50;
constexpr uint32_t COMMAND_TIMEOUT_MS = 500;
constexpr uint16_t STEP_INTERVAL_MS = 20;
constexpr uint16_t MAX_STEP_US = 8;  // 400 us/s, ajustar à mecânica.

// Valores ilustrativos, NÃO são limites universais dos servos.
constexpr uint8_t CHANNELS[5] = {0, 1, 2, 3, 4};
constexpr uint16_t OPEN_US[5] = {1300, 1300, 1300, 1300, 1300};
constexpr uint16_t CLOSED_US[5] = {1700, 1700, 1700, 1700, 1700};
