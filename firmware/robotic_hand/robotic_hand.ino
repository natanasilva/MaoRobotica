#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>
#include "servo_config.h"

Adafruit_PWMServoDriver pwm(PCA_ADDRESS);
bool hardwareReady = false;
bool armed = false;
bool haveSequence = false;
uint16_t lastSequence = 0;
uint16_t currentUs[5];
uint16_t targetUs[5];
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

void disableOutputs() {
  digitalWrite(OE_PIN, HIGH);
  armed = false;
  haveSequence = false;
  for (uint8_t i = 0; i < 5; ++i) targetUs[i] = currentUs[i];
}

bool writePulse(uint8_t channel, uint16_t micros) {
  uint16_t ticks = uint32_t(micros) * PWM_HZ * 4096UL / 1000000UL;
  return pwm.setPWM(channel, 0, ticks) == 0;
}

bool configurationValid() {
  for (uint8_t i = 0; i < 5; ++i) {
    if (CHANNELS[i] > 15 || OPEN_US[i] < 500 || OPEN_US[i] > 2500 ||
        CLOSED_US[i] < 500 || CLOSED_US[i] > 2500) return false;
    for (uint8_t j = 0; j < i; ++j) if (CHANNELS[i] == CHANNELS[j]) return false;
  }
  return true;
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
      // Sem feedback de posição: o primeiro pulso pode reposicionar o eixo.
      for (uint8_t i = 0; i < 5; ++i) {
        if (!writePulse(CHANNELS[i], currentUs[i])) {
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
    targetUs[i] = OPEN_US[i] + (int32_t(CLOSED_US[i]) - OPEN_US[i]) * values[i] / 1000;
  }
  lastSequence = sequence;
  haveSequence = true;
  lastCommand = millis();
  char response[18];
  snprintf(response, sizeof(response), "ACK,%u", sequence);
  reply(response);
}

void setup() {
  digitalWrite(OE_PIN, HIGH);
  pinMode(OE_PIN, OUTPUT);
  Serial.begin(115200);
  Wire.begin();
#if defined(WIRE_HAS_TIMEOUT)
  Wire.setWireTimeout(25000, true);
#endif
  for (uint8_t i = 0; i < 5; ++i) currentUs[i] = targetUs[i] = OPEN_US[i];
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
  // Limita o trabalho de recepção para não impedir o watchdog e a rampa.
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
    lastStep = now;
    for (uint8_t i = 0; i < 5; ++i) {
      int32_t difference = int32_t(targetUs[i]) - currentUs[i];
      if (difference > MAX_STEP_US) difference = MAX_STEP_US;
      if (difference < -int32_t(MAX_STEP_US)) difference = -int32_t(MAX_STEP_US);
      currentUs[i] += difference;
      if (!writePulse(CHANNELS[i], currentUs[i])) {
        disableOutputs(); reply("FAULT,I2C"); break;
      }
    }
  }
}
