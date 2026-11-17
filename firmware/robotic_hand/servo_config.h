#pragma once
#include <Arduino.h>

// Ajuste com os cabos dos dedos desacoplados. Só depois altere para true.
constexpr bool CALIBRATED = true;
constexpr uint8_t PCA_ADDRESS = 0x40;
constexpr uint8_t OE_PIN = 7;  // OE com pull-up externo de 1k para VCC (ver README).
constexpr uint16_t PWM_HZ = 50;
constexpr uint32_t COMMAND_TIMEOUT_MS = 500;
constexpr uint16_t STEP_INTERVAL_MS = 20;
constexpr uint16_t MAX_STEP_US = 20;  // 1000 us/s (curso todo em ~1,9 s), ajustar à mecânica.

// Limites absolutos aceitos pelo firmware. Além de ~500/2500 us muitos servos
// forçam o batente interno: se zumbir ou esquentar parado, reduza.
constexpr uint16_t PULSE_MIN_US = 500;
constexpr uint16_t PULSE_MAX_US = 2400;

// Valores ilustrativos, NÃO são limites universais dos servos.
constexpr uint8_t CHANNELS[5] = {0, 1, 2, 3, 4};
// Servos SG90: curso completo de ~180 graus em 500 a 2400 us.
// Além disso o SG90 força o batente e pode espanar as engrenagens de plástico.
constexpr uint16_t OPEN_US[5] = {500, 500, 500, 500, 500};
constexpr uint16_t CLOSED_US[5] = {2400, 2400, 2400, 2400, 2400};

// Opcional: dedo com servo de 360 graus (rotação contínua): 0 polegar ... 4 mínimo, -1 nenhum.
// Sem sensor, a posição (0 = aberto, 1000 = fechado) é estimada pelo tempo girando.
constexpr int8_t CONTINUOUS_FINGER = -1;  // todos os dedos são SG90 de 180 graus
constexpr uint16_t CONT_NEUTRAL_US = 1500;  // pulso em que o servo fica parado
constexpr int16_t CONT_SPEED_US = 300;      // desvio do neutro ao girar; negativo inverte o sentido
constexpr uint16_t CONT_TRAVEL_MS = 1000;   // tempo para ir de aberto a fechado nessa velocidade (medir)
