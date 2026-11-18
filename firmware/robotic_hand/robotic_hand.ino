#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>
#include "servo_config.h"

// Firmware para cinco servos de 360 graus (rotação contínua).
// O pulso controla velocidade/sentido, então a posição de cada dedo é estimada
// pelo tempo de giro (0 = aberto, FULL = fechado). Os servos ficam parados até
// o alvo recebido (SET) se afastar da posição estimada mais que DEADBAND.

constexpr int32_t FULL = 1000000L;  // posição em milionésimos do curso (0-1000 x 1000)

Adafruit_PWMServoDriver pwm(PCA_ADDRESS);
bool hardwareReady = false;
bool armed = false;
bool haveSequence = false;
uint16_t lastSequence = 0;
int32_t position[5];   // posição estimada; persiste entre ARM/STOP (o servo não se move desarmado)
int32_t target[5];
int8_t motion[5];      // +1 fechando, -1 abrindo, 0 parado
uint16_t lastPulse[5]; // último pulso escrito (0 = sem pulso)
uint32_t lastCommand = 0;
uint32_t lastStep = 0;
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
  if (!motion[finger]) return stopPulse(finger);
  return uint16_t(int32_t(NEUTRAL_US[finger]) + motion[finger] * int32_t(CLOSE_SPEED_US[finger]));
}

int32_t stepSize(uint8_t finger, int8_t direction, uint32_t elapsed) {
  uint16_t travel = direction > 0 ? CLOSE_TIME_MS[finger] : OPEN_TIME_MS[finger];
  return int32_t(FULL / travel * elapsed);
}

// Soma à posição estimada o trecho percorrido durante "elapsed" ms de giro.
void advance(uint8_t finger, uint32_t elapsed) {
  if (!motion[finger]) return;
  if (elapsed > 200) elapsed = 200;
  position[finger] += motion[finger] * stepSize(finger, motion[finger], elapsed);
  if (position[finger] < 0) position[finger] = 0;
  if (position[finger] > FULL) position[finger] = FULL;
}

void disableOutputs() {
  // Contabiliza o giro desde o último passo antes de parar.
  if (armed) {
    uint32_t elapsed = millis() - lastStep;
    for (uint8_t i = 0; i < 5; ++i) advance(i, elapsed);
  }
  digitalWrite(OE_PIN, HIGH);
  // Também zera os registradores do PCA9685: sem isso, o último pulso fica gravado
  // e volta a ser emitido quando o Arduino reinicia (ao abrir a porta serial o OE
  // fica solto até o setup), fazendo os servos girarem antes de qualquer comando.
  for (uint8_t i = 0; i < 5; ++i) {
    motion[i] = 0;
    target[i] = position[i];
    if (hardwareReady) writePulse(i, 0);
  }
  armed = false;
  haveSequence = false;
}

bool configurationValid() {
  for (uint8_t i = 0; i < 5; ++i) {
    int32_t closing = int32_t(NEUTRAL_US[i]) + CLOSE_SPEED_US[i];
    int32_t opening = int32_t(NEUTRAL_US[i]) - CLOSE_SPEED_US[i];
    if (CHANNELS[i] > 15 || CLOSE_SPEED_US[i] == 0 ||
        NEUTRAL_US[i] < PULSE_MIN_US || NEUTRAL_US[i] > PULSE_MAX_US ||
        closing < PULSE_MIN_US || closing > PULSE_MAX_US ||
        opening < PULSE_MIN_US || opening > PULSE_MAX_US ||
        CLOSE_TIME_MS[i] < STEP_INTERVAL_MS || OPEN_TIME_MS[i] < STEP_INTERVAL_MS) return false;
    for (uint8_t j = 0; j < i; ++j) if (CHANNELS[i] == CHANNELS[j]) return false;
  }
  return DEADBAND <= 1000;
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
      lastStep = millis();
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
  for (uint8_t i = 0; i < 5; ++i) target[i] = int32_t(values[i]) * 1000L;
  lastSequence = sequence;
  haveSequence = true;
  lastCommand = millis();
  char response[18];
  snprintf(response, sizeof(response), "ACK,%u", sequence);
  reply(response);
}

// Atualiza posição estimada e decide, por dedo, se gira ou fica parado.
bool stepMotion(uint32_t elapsed) {
  for (uint8_t i = 0; i < 5; ++i) {
    advance(i, elapsed);
    if (motion[i]) {
      // Para quando faltar menos de meio passo (evita passar do alvo).
      int32_t remaining = (target[i] - position[i]) * motion[i];
      if (remaining <= stepSize(i, motion[i], STEP_INTERVAL_MS) / 2) motion[i] = 0;
    }
    if (!motion[i]) {
      int32_t error = target[i] - position[i];
      int32_t threshold = int32_t(DEADBAND) * 1000L;
      if (error > threshold) motion[i] = 1;
      else if (error < -threshold) motion[i] = -1;
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
    position[i] = target[i] = 0;
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
  // Limita o trabalho de recepção para não impedir o watchdog e o controle.
  uint8_t budget = 64;
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
  uint32_t now = millis();
  if (armed && uint32_t(now - lastCommand) >= COMMAND_TIMEOUT_MS) {
    disableOutputs();
    reply("FAULT,TIMEOUT");
  }
  if (armed && uint32_t(now - lastStep) >= STEP_INTERVAL_MS) {
    uint32_t elapsed = now - lastStep;
    lastStep = now;
    if (!stepMotion(elapsed)) {
      disableOutputs(); reply("FAULT,I2C");
    }
  }
}
