#pragma once
#include <Arduino.h>

// Ajuste com os cabos dos dedos desacoplados. Só depois altere para true.
constexpr bool CALIBRATED = true;
constexpr uint8_t PCA_ADDRESS = 0x40;
constexpr uint8_t OE_PIN = 7;  // OE com pull-up externo de 1k para VCC (ver README).
constexpr uint16_t PWM_HZ = 50;
constexpr uint32_t COMMAND_TIMEOUT_MS = 500;

// Limites absolutos de pulso aceitos pelo firmware.
constexpr uint16_t PULSE_MIN_US = 1000;
constexpr uint16_t PULSE_MAX_US = 2000;

constexpr uint8_t CHANNELS[5] = {0, 1, 2, 3, 4};

// ---------------- Servos de 360 graus (rotação contínua) ----------------
// Nesses servos o pulso define VELOCIDADE e SENTIDO, não posição: no neutro
// (~1500 us) o eixo fica parado. O firmware estima a posição de cada dedo pelo
// tempo que o servo girou (sem sensor), partindo da posição inicial = ABERTO.
// Por isso: ligue/reinicie o Arduino com os dedos na posição aberta.
//
// O servo só gira quando o dedo detectado pela câmera muda mais que DEADBAND;
// no resto do tempo o pulso é cortado e o servo fica parado.
//                                 polegar indicador médio anelar mínimo
// Pulso em que o servo fica parado (ajuste se ele "arrastar" no neutro).
constexpr uint16_t NEUTRAL_US[5] = {1500,   1500,     1500, 1500,  1500};
// Quanto somar ao neutro para FECHAR o dedo. Valor maior = mais rápido.
// Dedo gira ao contrário (abre quando devia fechar): troque o sinal aqui E em OPEN_SPEED_US.
constexpr int16_t CLOSE_SPEED_US[5] = {200,  200,      200,  200,   200};
// Quanto subtrair do neutro para ABRIR o dedo (mesmo sinal de CLOSE_SPEED_US).
// Muitos servos 360 giram mais rápido num sentido que no outro: se o dedo não volta
// ao ponto inicial após fechar e abrir, ajuste este valor até a ida e a volta, com o
// mesmo tempo, percorrerem o mesmo curso (teste com o comando 't' do teste_servos).
constexpr int16_t OPEN_SPEED_US[5]  = {200,  200,      200,  200,   200};
// Tempo (ms) girando com essas velocidades para ir de aberto a fechado e de fechado
// a aberto. Mantenha os dois IGUAIS: o firmware gira exatamente o mesmo tempo para
// fechar e para abrir o mesmo trecho. Meça com firmware/teste_servos (r <us> <ms>).
constexpr uint16_t CLOSE_TIME_MS[5] = {500,  500,      500,  500,   500};
constexpr uint16_t OPEN_TIME_MS[5]  = {500,  500,      500,  500,   500};
// Variação mínima do dedo (escala 0-1000, 60 = 6%) para o servo começar a girar.
// Evita que o ruído da detecção faça o servo se mexer com a mão parada. Alvos a menos
// de DEADBAND dos extremos viram exatamente aberto (0) ou fechado (1000).
constexpr uint16_t DEADBAND = 60;
// true: parado = sem pulso (garante que o servo não "arrasta"). false: envia NEUTRAL_US.
constexpr bool STOP_WITHOUT_PULSE = true;
