// Teste e calibração dos servos da mão robótica, um de cada vez.
// Grave este sketch no lugar do robotic_hand.ino, abra o Monitor Serial
// (115200 baud, "Nova linha" ou "Ambos NL e CR") e digite "ajuda".
//
// Montagem: igual ao firmware principal (PCA9685 em 0x40, OE no pino 7).
// Teste com o cabo/elo do dedo DESACOPLADO até conhecer os limites.

#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>

// ---------------- Configuração ----------------
const uint8_t PCA_ADDRESS = 0x40;
const uint8_t OE_PIN = 7;           // LOW = saídas ativas, HIGH = desligadas
const uint16_t PWM_HZ = 50;
const uint8_t CHANNELS[5] = {0, 1, 2, 3, 4};
const char *const NOMES[5] = {"polegar", "indicador", "medio", "anelar", "minimo"};

// Quais dedos usam servo de 360 graus (polegar, indicador, medio, anelar, minimo).
// Num servo de 360 graus (rotação contínua) o pulso controla VELOCIDADE e
// sentido, não posição: 1500 us = parado, abaixo gira para um lado, acima para o outro.
// Todos os dedos usam servos de 360 graus.
const bool SERVO_360[5] = {true, true, true, true, true};

// Faixa usada para converter pulso <-> graus nos servos de 180 graus.
// Muitos servos (MG996R, MG90S, SG90) ficam perto de 500..2500 us, mas confira o seu.
const uint16_t PULSO_0_GRAUS = 500;
const uint16_t PULSO_180_GRAUS = 2500;
const uint16_t PULSO_MIN = 500;     // limite absoluto aceito por este sketch
const uint16_t PULSO_MAX = 2500;
const uint16_t NEUTRO_360 = 1500;   // ajuste se o servo 360 "arrasta" parado

// Rampa: no máximo PASSO_US a cada 20 ms (evita trancos nos servos de 180).
const uint16_t PASSO_US = 10;
const uint16_t INTERVALO_MS = 20;
// ------------------------------------------------

Adafruit_PWMServoDriver pwm(PCA_ADDRESS);
uint8_t dedo = 1;                  // dedo selecionado
bool ativo = false;
bool iniciado[5];                 // canal já recebeu pulso?
uint16_t atualUs[5], alvoUs[5];
int16_t abertoUs[5], fechadoUs[5]; // -1 = ainda não marcado
uint32_t ultimoPasso = 0;
uint32_t pararGiroEm = 0;          // para o servo 360 após "gira"
uint8_t dedoGiro = 0;              // dedo que está girando com "r"
bool varrendo = false;
bool varrendoParaFechado = true;
char linha[48];
uint8_t tam = 0;

bool eh360(uint8_t d) { return SERVO_360[d]; }

uint16_t grausParaUs(float g) {
  return PULSO_0_GRAUS + (PULSO_180_GRAUS - PULSO_0_GRAUS) * g / 180.0;
}

float usParaGraus(uint16_t us) {
  return (float(us) - PULSO_0_GRAUS) * 180.0 / (PULSO_180_GRAUS - PULSO_0_GRAUS);
}

void escrevePulso(uint8_t d, uint16_t us) {
  uint16_t ticks = uint32_t(us) * PWM_HZ * 4096UL / 1000000UL;
  pwm.setPWM(CHANNELS[d], 0, ticks);
}

void desligaTudo() {
  digitalWrite(OE_PIN, HIGH);
  for (uint8_t i = 0; i < 5; ++i) pwm.setPWM(CHANNELS[i], 0, 0);
  ativo = false;
  varrendo = false;
  for (uint8_t i = 0; i < 5; ++i) iniciado[i] = false;
  pararGiroEm = 0;
  Serial.println(F("Saidas DESLIGADAS."));
}

void liga() {
  if (!ativo) {
    digitalWrite(OE_PIN, LOW);
    ativo = true;
    Serial.println(F("Saidas LIGADAS."));
  }
}

void mostraEstado() {
  Serial.print(F("Dedo "));
  Serial.print(dedo);
  Serial.print(F(" ("));
  Serial.print(NOMES[dedo]);
  Serial.print(F(", canal "));
  Serial.print(CHANNELS[dedo]);
  Serial.print(eh360(dedo) ? F(", 360 graus") : F(", 180 graus"));
  Serial.print(F(") alvo="));
  Serial.print(alvoUs[dedo]);
  Serial.print(F(" us"));
  if (eh360(dedo)) {
    int16_t v = int16_t(alvoUs[dedo]) - NEUTRO_360;
    Serial.print(v == 0 ? F(" -> parado") : (v > 0 ? F(" -> gira sentido +") : F(" -> gira sentido -")));
  } else {
    Serial.print(F(" ~ "));
    Serial.print(usParaGraus(alvoUs[dedo]), 1);
    Serial.print(F(" graus"));
  }
  Serial.println();
}

void defineAlvo(uint16_t us) {
  if (us < PULSO_MIN || us > PULSO_MAX) {
    Serial.print(F("Fora da faixa "));
    Serial.print(PULSO_MIN);
    Serial.print(F(".."));
    Serial.println(PULSO_MAX);
    return;
  }
  liga();
  alvoUs[dedo] = us;
  // O 360 não tem posição: aplica direto, sem rampa.
  if (eh360(dedo)) { atualUs[dedo] = us; escrevePulso(dedo, us); }
  else if (!iniciado[dedo]) escrevePulso(dedo, atualUs[dedo]);  // 1º pulso: centro, depois rampa
  iniciado[dedo] = true;
  mostraEstado();
}

void imprimeConfig() {
  Serial.println(F("--- Copie para firmware/robotic_hand/servo_config.h ---"));
  const char *rotulos[2] = {"OPEN_US", "CLOSED_US"};
  for (uint8_t k = 0; k < 2; ++k) {
    Serial.print(F("constexpr uint16_t "));
    Serial.print(rotulos[k]);
    Serial.print(F("[5] = {"));
    for (uint8_t i = 0; i < 5; ++i) {
      int16_t v = k == 0 ? abertoUs[i] : fechadoUs[i];
      if (v < 0) Serial.print(F("????")); else Serial.print(v);
      if (i < 4) Serial.print(F(", "));
    }
    Serial.println(F("};"));
  }
  Serial.println(F("Servos 360: no firmware principal use NEUTRAL_US, CLOSE_SPEED_US e"));
  Serial.println(F("CLOSE_TIME_MS/OPEN_TIME_MS (meca com 'r <us> <ms>'), nao OPEN_US/CLOSED_US."));
}

void ajuda() {
  Serial.println(F(
    "\nComandos (Enter no final):\n"
    "  d <0-4>       seleciona dedo (0 polegar,1 indicador,2 medio,3 anelar,4 minimo)\n"
    "  g <graus>     vai para o angulo (servos 180), ex: g 90\n"
    "  p <us>        envia pulso em microssegundos, ex: p 1500\n"
    "  + / -         +10 / -10 us     ++ / --   +50 / -50 us\n"
    "  c             centro (1500 us; no 360 = parado)\n"
    "  a             marca a posicao atual como ABERTO deste dedo\n"
    "  f             marca a posicao atual como FECHADO deste dedo\n"
    "  v             varre devagar entre aberto e fechado (qualquer comando para)\n"
    "  r <us> <ms>   (servo 360) gira com pulso <us> por <ms> e para, ex: r 1700 500\n"
    "  l             mostra estado e imprime OPEN_US / CLOSED_US\n"
    "  x             DESLIGA todas as saidas\n"
    "  ajuda         esta lista"));
}

void processa(char *cmd) {
  char *op = strtok(cmd, " ");
  char *a1 = strtok(nullptr, " ");
  char *a2 = strtok(nullptr, " ");
  if (!op) return;
  varrendo = false;

  if (!strcmp(op, "ajuda") || !strcmp(op, "?")) ajuda();
  else if (!strcmp(op, "x")) desligaTudo();
  else if (!strcmp(op, "d") && a1) {
    int n = atoi(a1);
    if (n < 0 || n > 4) { Serial.println(F("Dedo deve ser 0..4")); return; }
    dedo = n;
    mostraEstado();
  }
  else if (!strcmp(op, "g") && a1) {
    if (eh360(dedo)) { Serial.println(F("Servo 360 nao tem angulo. Use p, r ou c.")); return; }
    float g = atof(a1);
    if (g < 0 || g > 180) { Serial.println(F("Angulo deve ser 0..180")); return; }
    defineAlvo(grausParaUs(g));
  }
  else if (!strcmp(op, "p") && a1) defineAlvo(atoi(a1));
  else if (!strcmp(op, "+"))  defineAlvo(alvoUs[dedo] + 10);
  else if (!strcmp(op, "-"))  defineAlvo(alvoUs[dedo] - 10);
  else if (!strcmp(op, "++")) defineAlvo(alvoUs[dedo] + 50);
  else if (!strcmp(op, "--")) defineAlvo(alvoUs[dedo] - 50);
  else if (!strcmp(op, "c"))  defineAlvo(eh360(dedo) ? NEUTRO_360 : 1500);
  else if (!strcmp(op, "a") || !strcmp(op, "f")) {
    (op[0] == 'a' ? abertoUs : fechadoUs)[dedo] = alvoUs[dedo];
    Serial.print(op[0] == 'a' ? F("ABERTO") : F("FECHADO"));
    Serial.print(F(" do "));
    Serial.print(NOMES[dedo]);
    Serial.print(F(" = "));
    Serial.println(alvoUs[dedo]);
  }
  else if (!strcmp(op, "v")) {
    if (eh360(dedo)) { Serial.println(F("Varredura nao se aplica ao servo 360.")); return; }
    if (abertoUs[dedo] < 0 || fechadoUs[dedo] < 0) { Serial.println(F("Marque antes 'a' e 'f'.")); return; }
    liga();
    varrendo = true;
    varrendoParaFechado = true;
    alvoUs[dedo] = fechadoUs[dedo];
    Serial.println(F("Varrendo... digite qualquer comando para parar."));
  }
  else if (!strcmp(op, "r") && a1 && a2) {
    if (!eh360(dedo)) { Serial.println(F("'r' e para o servo 360.")); return; }
    defineAlvo(atoi(a1));
    if (ativo && alvoUs[dedo] == uint16_t(atoi(a1))) { dedoGiro = dedo; pararGiroEm = millis() + atol(a2); }
  }
  else if (!strcmp(op, "l")) { mostraEstado(); imprimeConfig(); }
  else Serial.println(F("Comando desconhecido. Digite 'ajuda'."));
}

void setup() {
  digitalWrite(OE_PIN, HIGH);  // começa desligado
  pinMode(OE_PIN, OUTPUT);
  Serial.begin(115200);
  Wire.begin();
  if (!pwm.begin()) {
    Serial.println(F("PCA9685 nao encontrado. Verifique SDA/SCL/VCC/GND."));
    while (true) {}
  }
  pwm.setOscillatorFrequency(27000000);
  pwm.setPWMFreq(PWM_HZ);
  for (uint8_t i = 0; i < 5; ++i) {
    atualUs[i] = alvoUs[i] = eh360(i) ? NEUTRO_360 : 1500;
    abertoUs[i] = fechadoUs[i] = -1;
    pwm.setPWM(CHANNELS[i], 0, 0);
  }
  Serial.println(F("Teste de servos. Saidas desligadas ate o primeiro movimento."));
  Serial.println(F("O primeiro comando pode levar o servo direto ao alvo (posicao inicial desconhecida)."));
  ajuda();
  mostraEstado();
}

void loop() {
  while (Serial.available()) {
    char ch = Serial.read();
    if (ch == '\n' || ch == '\r') {
      if (tam) { linha[tam] = 0; processa(linha); tam = 0; }
    } else if (tam < sizeof(linha) - 1) {
      linha[tam++] = ch;
    }
  }

  uint32_t agora = millis();
  if (pararGiroEm && int32_t(agora - pararGiroEm) >= 0) {
    pararGiroEm = 0;
    uint8_t d = dedoGiro;
    alvoUs[d] = atualUs[d] = NEUTRO_360;
    escrevePulso(d, NEUTRO_360);
    Serial.println(F("Servo 360 parado."));
  }

  if (!ativo || agora - ultimoPasso < INTERVALO_MS) return;
  ultimoPasso = agora;
  for (uint8_t i = 0; i < 5; ++i) {
    if (eh360(i) || !iniciado[i]) continue;
    int32_t dif = int32_t(alvoUs[i]) - atualUs[i];
    if (dif > PASSO_US) dif = PASSO_US;
    if (dif < -int32_t(PASSO_US)) dif = -int32_t(PASSO_US);
    if (dif) { atualUs[i] += dif; escrevePulso(i, atualUs[i]); }
  }
  if (varrendo && atualUs[dedo] == alvoUs[dedo]) {
    varrendoParaFechado = !varrendoParaFechado;
    alvoUs[dedo] = varrendoParaFechado ? fechadoUs[dedo] : abertoUs[dedo];
  }
}
