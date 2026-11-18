#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>
#include "servo_config.h"

// Firmware para cinco servos de 360 graus (rotação contínua).
// O pulso controla velocidade/sentido, então a posição de cada dedo é estimada
// pelo tempo de giro (0 = aberto, FULL = fechado). Cada giro tem duração calculada
// a partir da distância até o alvo e termina no instante exato (medido em µs), de
// modo que ir de A até B e voltar de B até A consome exatamente o mesmo tempo
// (com CLOSE_TIME_MS = OPEN_TIME_MS). Os servos ficam parados até o alvo recebido
// (SET) se afastar da posição estimada mais que DEADBAND; os extremos aberto e
// fechado são sempre alcançados por completo.

constexpr int32_t FULL = 1000000L;  // posição em milionésimos do curso (0-1000 x 1000)

Adafruit_PWMServoDriver pwm(PCA_ADDRESS);
bool hardwareReady = false;
bool armed = false;
bool haveSequence = false;
uint16_t lastSequence = 0;
int32_t position[5];    // posição estimada (válida quando parado / no início do giro)
int32_t target[5];      // alvo recebido pelo SET
int8_t motion[5];       // +1 fechando, -1 abrindo, 0 parado
int32_t moveTo[5];      // destino do giro em andamento
uint32_t moveStart[5];  // micros() no início do giro
uint32_t moveTime[5];   // duração do giro em µs
uint16_t lastPulse[5];  // último pulso escrito (0 = sem pulso)
uint32_t lastCommand = 0;
char input[96];
uint8_t inputLength = 0;
bool overflow = false;

void reply(const char *payload) {
  uint8_t checksum = 0;
  for (const char *p = payload; *p; ++p) checksum ^= uint8_t(*p);
  Serial.print(payload);
  Serial.print('*');
  if (checksum < 16) Serial.print('0');
  Serial.println(checksum, HEX);
}

// micros = 0 corta o pulso do canal.
bool writePulse(uint8_t finger, uint16_t micros) {
  uint16_t ticks = micros ? uint16_t(uint32_t(micros) * PWM_HZ * 4096UL / 1000000UL) : 0;
  bool ok = (micros ? pwm.setPWM(CHANNELS[finger], 0, ticks)
                    : pwm.setPWM(CHANNELS[finger], 0, 4096)) == 0;
  if (ok) lastPulse[finger] = micros;
  return ok;
}

uint16_t stopPulse(uint8_t finger) {
  return STOP_WITHOUT_PULSE ? 0 : NEUTRAL_US[finger];
}

uint16_t pulseFor(uint8_t finger) {
  if (motion[finger] > 0) return uint16_t(int32_t(NEUTRAL_US[finger]) + CLOSE_SPEED_US[finger]);
  if (motion[finger] < 0) return uint16_t(int32_t(NEUTRAL_US[finger]) - OPEN_SPEED_US[finger]);
  return stopPulse(finger);
}

uint16_t travelMs(uint8_t finger, int8_t direction) {
  return direction > 0 ? CLOSE_TIME_MS[finger] : OPEN_TIME_MS[finger];
}

// Distância (0..FULL) -> tempo de giro em µs, e o inverso.
uint32_t distanceToMicros(uint8_t finger, int8_t direction, int32_t distance) {
  return uint32_t(uint64_t(distance) * travelMs(finger, direction) / 1000UL);
}

int32_t microsToDistance(uint8_t finger, int8_t direction, uint32_t elapsed) {
  uint64_t distance = uint64_t(elapsed) * 1000UL / travelMs(finger, direction);
  return distance > uint64_t(FULL) ? FULL : int32_t(distance);
}

// Posição estimada agora, inclusive no meio de um giro.
int32_t currentPosition(uint8_t finger, uint32_t now) {
  if (!motion[finger]) return position[finger];
  uint32_t elapsed = now - moveStart[finger];
  if (elapsed >= moveTime[finger]) return moveTo[finger];
  int32_t p = position[finger] + motion[finger] * microsToDistance(finger, motion[finger], elapsed);
  return p < 0 ? 0 : (p > FULL ? FULL : p);
}

// Interrompe o giro registrando onde o dedo parou.
void halt(uint8_t finger, uint32_t now) {
  position[finger] = currentPosition(finger, now);
  motion[finger] = 0;
}

void disableOutputs() {
  uint32_t now = micros();
  digitalWrite(OE_PIN, HIGH);
  // Também zera os registradores do PCA9685: sem isso, o último pulso fica gravado
  // e volta a ser emitido quando o Arduino reinicia (ao abrir a porta serial o OE
  // fica solto até o setup), fazendo os servos girarem antes de qualquer comando.
  for (uint8_t i = 0; i < 5; ++i) {
    halt(i, now);
    target[i] = position[i];
    if (hardwareReady) writePulse(i, 0);
  }
  armed = false;
  haveSequence = false;
}

bool configurationValid() {
  for (uint8_t i = 0; i < 5; ++i) {
    int32_t closing = int32_t(NEUTRAL_US[i]) + CLOSE_SPEED_US[i];
    int32_t opening = int32_t(NEUTRAL_US[i]) - OPEN_SPEED_US[i];
    bool sameSign = (CLOSE_SPEED_US[i] > 0) == (OPEN_SPEED_US[i] > 0);
    if (CHANNELS[i] > 15 || CLOSE_SPEED_US[i] == 0 || OPEN_SPEED_US[i] == 0 || !sameSign ||
        NEUTRAL_US[i] < PULSE_MIN_US || NEUTRAL_US[i] > PULSE_MAX_US ||
        closing < PULSE_MIN_US || closing > PULSE_MAX_US ||
        opening < PULSE_MIN_US || opening > PULSE_MAX_US ||
        CLOSE_TIME_MS[i] == 0 || OPEN_TIME_MS[i] == 0) return false;
    for (uint8_t j = 0; j < i; ++j) if (CHANNELS[i] == CHANNELS[j]) return false;
  }
  return DEADBAND < 500;
}

int hexDigit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

bool number(char *token, uint16_t maximum, uint16_t &value) {
  if (!token || !*token) return false;
  uint32_t result = 0;
  for (char *p = token; *p; ++p) {
    if (*p < '0' || *p > '9') return false;
    result = result * 10 + (*p - '0');
    if (result > maximum) return false;
  }
  value = uint16_t(result);
  return true;
}

void processLine(char *line) {
  char *star = strchr(line, '*');
  if (!star || strlen(star + 1) != 2) { reply("ERR,FRAME"); return; }
  int high = hexDigit(star[1]), low = hexDigit(star[2]);
  uint8_t checksum = 0;
  for (char *p = line; p < star; ++p) checksum ^= uint8_t(*p);
  if (high < 0 || low < 0 || checksum != ((high << 4) | low)) {
    reply("ERR,CHECKSUM"); return;
  }
  *star = '\0';
  if (!strcmp(line, "HELLO")) {
    disableOutputs();
    reply("READY,1");
    return;
  }
  if (!strcmp(line, "STOP")) {
    disableOutputs();
    reply("DISARMED");
    return;
  }
  if (!strcmp(line, "ARM")) {
    if (!hardwareReady) { reply("ERR,HARDWARE"); return; }
    if (!CALIBRATED) { reply("ERR,NOT_CALIBRATED"); return; }
    if (!armed) {
      // Arma com todos os servos PARADOS: o alvo é a própria posição atual,
      // então nada gira até chegar um SET com o dedo em outra posição.
      for (uint8_t i = 0; i < 5; ++i) {
        motion[i] = 0;
        target[i] = position[i];
        if (!writePulse(i, stopPulse(i))) {
          disableOutputs(); reply("FAULT,I2C"); return;
        }
      }
      digitalWrite(OE_PIN, LOW);
      armed = true;
      haveSequence = false;
    }
    lastCommand = millis();
    reply("ARMED");
    return;
  }
  // Divisão preservando campos vazios, para rejeitar mensagens malformadas.
  char *tokens[8];
  uint8_t count = 1;
  tokens[0] = line;
  for (char *p = line; *p; ++p) {
    if (*p == ',') {
      *p = '\0';
      if (count >= 8) { reply("ERR,FIELDS"); return; }
      tokens[count++] = p + 1;
    }
  }
  if (count != 7 || strcmp(tokens[0], "SET")) { reply("ERR,FIELDS"); return; }
  uint16_t sequence, values[5];
  if (!number(tokens[1], 65535, sequence)) { reply("ERR,VALUE"); return; }
  for (uint8_t i = 0; i < 5; ++i) {
    if (!number(tokens[i+2], 1000, values[i])) { reply("ERR,VALUE"); return; }
  }
  if (!armed) { reply("ERR,DISARMED"); return; }
  uint16_t delta = sequence - lastSequence;
  if (haveSequence && (delta == 0 || delta >= 32768)) { reply("ERR,SEQUENCE"); return; }
  for (uint8_t i = 0; i < 5; ++i) {
    // Perto dos extremos, vai exatamente ao extremo: garante que "mão aberta"
    // devolva o dedo à posição inicial e "mão fechada" feche por completo.
    uint16_t v = values[i];
    if (v <= DEADBAND) v = 0;
    else if (v >= 1000 - DEADBAND) v = 1000;
    target[i] = int32_t(v) * 1000L;
  }
  lastSequence = sequence;
  haveSequence = true;
  lastCommand = millis();
  char response[18];
  snprintf(response, sizeof(response), "ACK,%u", sequence);
  reply(response);
}

// Decide, por dedo, se começa, ajusta, termina ou mantém o giro.
bool updateMotion(uint32_t now) {
  for (uint8_t i = 0; i < 5; ++i) {
    if (motion[i]) {
      if (now - moveStart[i] >= moveTime[i]) {
        // Fim do giro no tempo calculado: a posição é exatamente o destino.
        position[i] = moveTo[i];
        motion[i] = 0;
      } else if (moveTo[i] != target[i]) {
        if ((target[i] - currentPosition(i, now)) * motion[i] > 0) {
          // Alvo mudou mas continua à frente, no mesmo sentido: só recalcula o fim.
          moveTo[i] = target[i];
          moveTime[i] = distanceToMicros(i, motion[i], (moveTo[i] - position[i]) * motion[i]);
        } else {
          // Alvo ficou para trás: para aqui e replaneja abaixo.
          halt(i, now);
        }
      }
    }
    if (!motion[i]) {
      int32_t error = target[i] - position[i];
      int32_t distance = error < 0 ? -error : error;
      bool endpoint = target[i] == 0 || target[i] == FULL;
      // Fora dos extremos exige passar do DEADBAND; nos extremos vai até o fim
      // (ignora só restos menores que 0,1% do curso).
      if (distance > (endpoint ? 1000L : int32_t(DEADBAND) * 1000L)) {
        motion[i] = error > 0 ? 1 : -1;
        moveTo[i] = target[i];
        moveStart[i] = now;
        moveTime[i] = distanceToMicros(i, motion[i], distance);
      }
    }
    uint16_t pulse = pulseFor(i);
    if (pulse != lastPulse[i] && !writePulse(i, pulse)) return false;
  }
  return true;
}

void setup() {
  digitalWrite(OE_PIN, HIGH);
  pinMode(OE_PIN, OUTPUT);
  Serial.begin(115200);
  Wire.begin();
#if defined(WIRE_HAS_TIMEOUT)
  Wire.setWireTimeout(25000, true);
#endif
  // Posição inicial assumida: dedos abertos.
  for (uint8_t i = 0; i < 5; ++i) {
    position[i] = target[i] = moveTo[i] = 0;
    motion[i] = 0;
    lastPulse[i] = 0;
  }
  hardwareReady = configurationValid() && pwm.begin();
  if (hardwareReady) {
    pwm.setPWMFreq(PWM_HZ);
    for (uint8_t channel = 0; channel < 16; ++channel) {
      if (pwm.setPWM(channel, 0, 4096) != 0) hardwareReady = false;
    }
  }
  reply("READY,1");
}

void loop() {
  // Limita o trabalho de recepção para não atrasar o fim dos giros.
  uint8_t budget = 16;
  while (Serial.available() && budget--) {
    char c = Serial.read();
    if (c == '\n') {
      if (overflow) reply("ERR,OVERFLOW");
      else if (inputLength) { input[inputLength] = '\0'; processLine(input); }
      inputLength = 0;
      overflow = false;
    } else if (c != '\r') {
      if (c < 32 || c > 126 || inputLength >= sizeof(input) - 1) overflow = true;
      if (!overflow) input[inputLength++] = c;
    }
  }
  if (armed && uint32_t(millis() - lastCommand) >= COMMAND_TIMEOUT_MS) {
    disableOutputs();
    reply("FAULT,TIMEOUT");
  }
  // Verificado a cada volta do loop (não mais a cada 20 ms): o giro termina
  // poucos µs depois do tempo calculado, igual nos dois sentidos.
  if (armed && !updateMotion(micros())) {
    disableOutputs(); reply("FAULT,I2C");
  }
}
