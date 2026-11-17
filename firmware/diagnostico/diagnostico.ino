// Diagnóstico da ligação Arduino -> PCA9685 -> servos.
// Grave, abra o Monitor Serial a 115200 baud e leia o relatório.
// Depois do relatório, os canais 0-4 ficam em 1500 us com OE em LOW, e o
// canal 0 alterna entre 1000 e 2000 us a cada 1,5 s por 30 s.

#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>

const uint8_t PCA_ADDRESS = 0x40;
const uint8_t OE_PIN = 7;
const uint16_t PWM_HZ = 50;

Adafruit_PWMServoDriver pwm(PCA_ADDRESS);

uint16_t usParaTicks(uint16_t us) { return uint32_t(us) * PWM_HZ * 4096UL / 1000000UL; }

void escaneiaI2C() {
  Serial.print(F("I2C encontrado em:"));
  uint8_t n = 0;
  for (uint8_t a = 1; a < 127; ++a) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      Serial.print(F(" 0x"));
      if (a < 16) Serial.print('0');
      Serial.print(a, HEX);
      ++n;
    }
  }
  if (!n) Serial.print(F(" NENHUM (verifique SDA=A4, SCL=A5, VCC e GND do PCA9685)"));
  Serial.println();
}

uint8_t leRegistrador(uint8_t reg) {
  Wire.beginTransmission(PCA_ADDRESS);
  Wire.write(reg);
  if (Wire.endTransmission() != 0) return 0xFF;
  Wire.requestFrom(PCA_ADDRESS, uint8_t(1));
  return Wire.available() ? Wire.read() : 0xFF;
}

// Com o pull-up externo de 1k ligado, o pino volta a HIGH em microssegundos
// depois de solto. Se o fio estiver solto, o pino fica flutuando perto de LOW.
void testaOE() {
  pinMode(OE_PIN, OUTPUT);
  digitalWrite(OE_PIN, LOW);
  delay(5);
  pinMode(OE_PIN, INPUT);
  delayMicroseconds(50);
  int leitura = digitalRead(OE_PIN);
  Serial.print(F("OE (pino 7) solto pelo Arduino le: "));
  Serial.println(leitura ? F("HIGH -> pull-up de 1k presente (fio do OE ligado)")
                         : F("LOW -> SEM pull-up: fio do OE solto, em outro pino, ou resistor ausente"));
  digitalWrite(OE_PIN, HIGH);
  pinMode(OE_PIN, OUTPUT);
}

void setup() {
  digitalWrite(OE_PIN, HIGH);
  pinMode(OE_PIN, OUTPUT);
  Serial.begin(115200);
  delay(200);
  Wire.begin();
  Serial.println(F("=== DIAGNOSTICO ==="));
  escaneiaI2C();

  if (!pwm.begin()) { Serial.println(F("PCA9685 NAO responde em 0x40.")); while (true) {} }
  pwm.setOscillatorFrequency(27000000);
  pwm.setPWMFreq(PWM_HZ);

  uint8_t mode1 = leRegistrador(0x00);
  Serial.print(F("MODE1 = 0x")); Serial.print(mode1, HEX);
  Serial.println((mode1 & 0x10) ? F(" (SLEEP ligado: oscilador PARADO)") : F(" (oscilador rodando)"));
  Serial.print(F("PRESCALE = ")); Serial.print(pwm.readPrescale());
  Serial.println(F(" (esperado ~131 para 50 Hz com oscilador de 27 MHz)"));

  bool ok = true;
  for (uint8_t ch = 0; ch < 5; ++ch) {
    pwm.setPWM(ch, 0, usParaTicks(1500));
    uint16_t lido = pwm.getPWM(ch, true);  // registrador OFF
    Serial.print(F("Canal ")); Serial.print(ch);
    Serial.print(F(": escrito ")); Serial.print(usParaTicks(1500));
    Serial.print(F(" lido ")); Serial.println(lido);
    if (lido != usParaTicks(1500)) ok = false;
  }
  Serial.println(ok ? F("Registradores PWM OK: o PCA9685 esta gerando os pulsos.")
                    : F("Registradores PWM DIFERENTES do escrito: problema no I2C."));

  testaOE();

  digitalWrite(OE_PIN, LOW);
  Serial.println(F("OE em LOW: saidas ATIVAS. Canais 0-4 em 1500 us."));
  Serial.println(F("Canal 0 (polegar) vai alternar 1000/2000 us por 30 s."));
  Serial.println(F("Meca: OE (perto de 0 V), V+ do borne dos servos (5-6 V), GND comum."));
}

void loop() {
  static uint32_t inicio = millis(), ultimo = 0;
  static bool alto = false;
  uint32_t agora = millis();
  if (agora - inicio > 30000UL) {
    static bool fim = false;
    if (!fim) {
      for (uint8_t ch = 0; ch < 16; ++ch) pwm.setPWM(ch, 0, 4096);
      digitalWrite(OE_PIN, HIGH);
      Serial.println(F("FIM: saidas desligadas."));
      fim = true;
    }
    return;
  }
  if (agora - ultimo >= 1500) {
    ultimo = agora;
    alto = !alto;
    pwm.setPWM(0, 0, usParaTicks(alto ? 2000 : 1000));
    Serial.print(F("canal 0 -> ")); Serial.println(alto ? 2000 : 1000);
  }
}
