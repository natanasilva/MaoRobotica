#pragma once
#include <Arduino.h>

// Ajuste com os cabos dos dedos desacoplados. Só depois altere para true.
constexpr bool CALIBRATED = true;
constexpr uint8_t PCA_ADDRESS = 0x40;
constexpr uint8_t OE_PIN = 7;  // OE com pull-up externo de 1k para VCC (ver README).
constexpr uint16_t PWM_HZ = 50;
constexpr uint32_t COMMAND_TIMEOUT_MS = 500;
constexpr uint16_t STEP_INTERVAL_MS = 20;
constexpr uint16_t MAX_STEP_US = 20;  // 1000 us/s (curso todo em ~2,2 s), ajustar à mecânica.

// Limites absolutos aceitos pelo firmware. Além de ~500/2500 us muitos servos
// forçam o batente interno: se zumbir ou esquentar parado, reduza.
constexpr uint16_t PULSE_MIN_US = 400;
constexpr uint16_t PULSE_MAX_US = 2600;

// Valores ilustrativos, NÃO são limites universais dos servos.
constexpr uint8_t CHANNELS[5] = {0, 1, 2, 3, 4};
// Os cinco dedos usam servos de 180 graus, curso estendido (400 a 2600 us,
// além dos 180 graus nominais). Dedo invertido: troque os dois valores dele.
constexpr uint16_t OPEN_US[5] = {400, 400, 400, 400, 400};
constexpr uint16_t CLOSED_US[5] = {2600, 2600, 2600, 2600, 2600};
