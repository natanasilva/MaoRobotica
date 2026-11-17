# Mão robótica com Python e Arduino

Aplicação local que acompanha uma mão pela webcam e transforma a flexão dos cinco dedos em comandos para Arduino + PCA9685. Cada dedo controla um servo, na ordem **polegar, indicador, médio, anelar e mínimo**.

O Python usa OpenCV para a câmera e MediaPipe Tasks Hand Landmarker para os 21 pontos da mão. O Arduino aplica os limites de pulso e a rampa de velocidade. A inferência roda localmente, sem depender de uma API remota. A instalação e o download do modelo precisam de internet; bibliotecas de terceiros podem emitir telemetria própria.

## Começar no Windows

Execute os comandos no PowerShell, dentro de `C:\ProjetosPython\MaoRobotica`:

```powershell
python -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r requirements.txt
.\.venv\Scripts\python.exe scripts/download_model.py
```

O `requirements.txt` instala todas as bibliotecas nas versões testadas e o próprio projeto. O modelo (`models/hand_landmarker.task`) não fica no Git: baixe-o com o script acima ou copie a pasta `models` de outro computador.

Python 3.11 ou superior. A instalação com visão exige que MediaPipe, OpenCV e NumPy tenham pacotes disponíveis para sua plataforma. As versões efetivamente verificadas neste ambiente ficam em `requirements-tested.txt` após a validação. Não é necessário ativar o ambiente virtual nem mudar a política de execução do PowerShell.

As versões fixadas foram verificadas no Windows com Python 3.14. Em outra versão do Python, se alguma não tiver pacote disponível, use `python -m pip install -e ".[vision]"` para deixar o pip escolher versões compatíveis. Consulte [o registro de validação](docs/VALIDACAO.md) para o que foi testado e o que depende do hardware.

Teste primeiro sem câmera e sem Arduino:

```powershell
.\.venv\Scripts\python.exe -m mao_robotica demo --seconds 3
```

Para abrir a webcam em **simulação**, sem enviar comandos:

```powershell
.\.venv\Scripts\python.exe -m mao_robotica camera
```

Em `config/hand.json`, `camera` é o índice da câmera e `hand` é `Any` (padrão: qualquer mão), `Right` ou `Left`, relativo à pessoa. A inferência recebe a imagem original; só a visualização é espelhada. Drivers que já espelham a captura podem exigir trocar a seleção. O programa aceita somente uma mão visível e rejeita a cena se encontrar duas.

## Controle automático pela mão espalmada

O modo `camera` procura **continuamente** uma mão espalmada. Não é preciso apertar nenhuma tecla para começar:

1. **PROCURANDO MÃO ABERTA**: mostre a mão aberta, dedos esticados, polegar afastado e palma de frente para a câmera. Uma barra amarela mostra o progresso enquanto a mão fica parada.
2. Depois de `open_hold_seconds` (0,8 s) estável, essa postura vira a **referência de 0%** de cada dedo. Os servos são ativados e o estado muda para **CONTROLANDO**.
3. Feche e abra os dedos: o fechamento de cada um é medido em relação à mão aberta capturada e enviado ao servo correspondente.
4. Se a mão sumir por `lost_seconds` (0,4 s), ou o Arduino desarmar ou falhar, os servos são desligados e o sistema volta a procurar a mão aberta. Para retomar, basta mostrá-la de novo.

A mão é considerada espalmada quando todos os dedos estão estendidos (`open_max_bend`), as pontas ficam mais distantes do punho que as articulações médias, o polegar está afastado da base do mínimo e a palma está voltada para a câmera (`open_min_facing`, 1 = totalmente de frente). Esse teste usa só a geometria dos 21 pontos e não depende da calibração.

A referência de abertura é refeita a cada retomada, então posição e distância da câmera podem mudar entre sessões. A amplitude até o punho fechado vem de `calibration` (diferença `closed - open` de cada dedo). Os valores padrão são aproximados; para mais precisão, calibre a amplitude:

## Calibrar a amplitude dos dedos (opcional)

1. Mantenha a mão aberta, de frente para a câmera, estável por pelo menos um segundo. Pressione **O**. O controle é pausado.
2. Feche os cinco dedos, incluindo o polegar sobre a palma, mantenha por um segundo e pressione **C**.
3. Verifique se cada percentual varia aproximadamente entre 0 e 100% conforme move o dedo. Pressione **Espaço** para voltar ao controle automático.

A calibração usa a mediana das amostras recentes, rejeita movimento excessivo e amplitudes insuficientes. Somente um par completo válido é salvo no JSON. É possível manter perfis separados:

```powershell
Copy-Item config/hand.json config/minha-mao.local.json
.\.venv\Scripts\python.exe -m mao_robotica camera --config config/minha-mao.local.json
```

O polegar combina flexão e proximidade à base do mínimo. Trata-se de uma aproximação para um único servo; oposição e flexão não são controladas separadamente. Oclusões, perfil da mão e iluminação ruim afetam a leitura. A classificação esquerda/direita não identifica a pessoa.

## Montagem elétrica

Referência inicial: **Arduino Uno R3 ou Nano clássico de 5 V**, PCA9685 em `0x40`, cinco servos posicionais e fonte externa dimensionada para eles. Não usar servos de rotação contínua para esse controle de posição. Para outras placas, confirme pinagem I²C e tensão lógica antes de ligar.

| Origem | Destino |
|---|---|
| Arduino 5 V | VCC lógico do PCA9685 |
| Arduino GND | GND do PCA9685 e negativo da fonte dos servos |
| Arduino A4 / SDA | SDA do PCA9685 |
| Arduino A5 / SCL | SCL do PCA9685 |
| Arduino D7 | OE do PCA9685 |
| Resistor de 1 kΩ | Entre OE e VCC, mantendo saídas desabilitadas no reset |
| Positivo da fonte dos servos | V+ / borne de alimentação dos servos |
| Negativo da fonte dos servos | GND comum |
| Canais 0, 1, 2, 3, 4 | Polegar, indicador, médio, anelar, mínimo |

Confira a ordem sinal/V+/GND de cada conector. **Não alimente os cinco servos pelo 5 V do Arduino.** A tensão depende do modelo do servo; a fonte, fios e distribuição devem suportar a corrente de pico simultânea. Mantenha V+ dos motores separado do 5 V do Arduino. Um interruptor na alimentação dos servos permite cortar a energia independentemente do software.

O sinal OE faz parte desta implementação. Muitos módulos têm pull-down em OE; o pull-up externo deve prevalecer sobre ele. O valor de 1 kΩ atende ao caso usual de pull-down de 10 kΩ: confirme o circuito do seu módulo e meça OE em nível alto durante o reset. Parar o PWM não corta a alimentação e não garante retenção de carga; o comportamento sem sinal depende do servo. Esta versão é destinada à bancada sem objetos apoiados na mão.

## Firmware e calibração mecânica

Abra `firmware/robotic_hand/robotic_hand.ino` na Arduino IDE. Instale **Adafruit PWM Servo Driver Library** pelo gerenciador de bibliotecas, incluindo **Adafruit BusIO**. Selecione a placa e a porta e carregue o sketch.

As configurações ficam em `firmware/robotic_hand/servo_config.h`:

- `CHANNELS`: canal de cada dedo, sem repetição.
- `OPEN_US` e `CLOSED_US`: pulsos medidos para abertura e fechamento. A inversão é feita trocando os extremos.
- `PWM_HZ`: 50 Hz inicialmente; confirme a frequência suportada pelos servos.
- `MAX_STEP_US`: passo máximo a cada 20 ms. Com 20 µs (1000 µs/s), o curso completo leva cerca de 2,2 s; aumente para os dedos acompanharem a mão mais rápido.
- `CALIBRATED`: `true` libera o `ARM`; com `false` o firmware recusa o acionamento.

A configuração atual usa servos SG90 de 180° nos cinco dedos, com 500–2400 µs em todos (curso completo do SG90; além disso ele força o batente e pode espanar as engrenagens). `PULSE_MIN_US`/`PULSE_MAX_US` limitam os valores aceitos pelo firmware. Se um dedo fechar quando deveria abrir, troque os dois valores dele. Se um servo forçar ou zumbir em um extremo (batente mecânico ou fim de curso), reduza aquele extremo em passos de 50 µs. Para comissionar:

1. Desacople os cabos/elos e conecte inicialmente apenas um servo. Consulte os pulsos e a tensão especificados pelo fabricante.
2. Configure inicialmente os dois extremos desse servo para um pulso central conhecido, e coloque `CALIBRATED = true` para o ensaio de bancada. Regrave o firmware.
3. Use o comando manual abaixo. Desenergize antes de mudar ligações. Amplie os extremos em pequenos passos, regravando o firmware, e encontre a faixa necessária sem atingir batentes.
4. Repita por dedo, confira o sentido, acople a mecânica sem tensão excessiva e reduza os limites se necessário. Só então conecte os cinco servos e execute com visão.

### Sketch de teste dos servos

`firmware/teste_servos/teste_servos.ino` é um sketch separado para testar cada servo pelo Monitor Serial (115200 baud), sem Python nem webcam. Grave-o no lugar do firmware principal, digite `ajuda` e use `d <dedo>`, `g <graus>`, `p <us>`, `+`/`-`, `a`/`f` para marcar aberto/fechado, `v` para varrer e `l` para imprimir as linhas `OPEN_US`/`CLOSED_US` prontas para o `servo_config.h`. `x` desliga as saídas. Depois, regrave `robotic_hand.ino`.

Servo de 360 graus (rotação contínua): o pulso define velocidade e sentido, não posição. A montagem atual não usa nenhum (`DEDO_360 = -1`). Se trocar algum dedo por um servo desse tipo, indique-o em `DEDO_360` e use `r <us> <ms>` para girá-lo por um tempo. No firmware principal, indique-o em `CONTINUOUS_FINGER` (hoje `-1`): ele passa a ser controlado por tempo de giro, com a posição estimada sem sensor, girando a `CONT_SPEED_US` do neutro (`CONT_NEUTRAL_US`) durante a fração de `CONT_TRAVEL_MS` correspondente. Meça quanto tempo o dedo leva para fechar e ajuste `CONT_TRAVEL_MS`; o erro se acumula com o uso e, ao reiniciar, o firmware assume o dedo aberto.

Sem sensores externos, o Arduino não conhece a posição real após ligar ou desabilitar os servos. **O primeiro pulso ao ativar pode reposicionar o eixo**, mesmo com a rampa aplicada aos comandos seguintes. Não há controle de força nem detecção de obstáculos.

## Operar com Arduino

Feche o Monitor Serial da IDE antes de abrir a porta pelo Python:

```powershell
.\.venv\Scripts\python.exe -m mao_robotica ports
.\.venv\Scripts\python.exe -m mao_robotica manual --port COM3 --values 0 0 0 0 0 --seconds 3
.\.venv\Scripts\python.exe -m mao_robotica camera --port COM3
```

Troque `COM3` pela porta listada. No modo manual, os valores vão de `0` a `1000`; **esse comando ativa os servos imediatamente** e envia o alvo pelo período solicitado. Teste um dedo usando valores pequenos, com os demais em zero. Ao terminar, o PWM é desabilitado.

Na janela da câmera:

| Tecla | Ação |
|---|---|
| — | Ativação automática ao detectar a mão espalmada e estável |
| Espaço | Pausar (servos desligados, sem rearme automático) / retomar a busca |
| R | Descartar a referência e procurar a mão aberta de novo |
| O / C | Calibrar a amplitude: aberta / fechada; pausa o controle |
| F | Alternar tela cheia / janela redimensionável (abre em tela cheia) |
| Q / Esc | Encerrar e desabilitar o acionamento |

A perda de mão por 400 ms desarma o controle; perdas menores não atualizam os alvos. O firmware desabilita as saídas após 500 ms sem comando válido, inclusive se o Python travar. Erros e timeouts informados pelo Arduino desarmam e voltam à busca da mão aberta, com 2 s de espera antes de reativar. A aplicação só termina com Q/Esc ou se a porta serial ou a webcam falharem.

## Protocolo e implementação

USB serial a 115200 baud, ASCII, uma mensagem por linha. Ambos os lados usam `PAYLOAD*HH\n`, sendo `HH` o XOR hexadecimal dos bytes do payload. O checksum detecta erros simples; não é autenticação nem garantia contra toda corrupção.

| Payload | Função |
|---|---|
| `HELLO` | Desarma e solicita `READY,1` |
| `ARM` | Solicita acionamento; responde `ARMED` ou `ERR,...` |
| `SET,seq,p,i,m,a,n` | Sequência 0–65535 e cinco valores 0–1000 |
| `STOP` | Desabilita OE e responde `DISARMED` |
| `ACK,seq` | Confirma aceitação do alvo, não a posição física |
| `FAULT,TIMEOUT` / `FAULT,I2C` | Falha que desarma o controle |

As mensagens são montadas em `protocol.py`. O firmware valida a linha inteira antes de atualizar qualquer dedo, rejeita números fora da faixa e sequências repetidas/antigas, e usa buffer fixo sem `String` dinâmica. A rampa não usa `delay()`. Configuração de servo fica no firmware para que os limites continuem valendo independentemente do Python.

```text
src/mao_robotica/
  main.py            CLI, janela, calibração e ciclo de controle automático
  reference.py       Captura estável da referência de mão aberta
  tracking.py        MediaPipe Tasks e desenho dos pontos
  finger_mapping.py  Ângulos 3D, detecção de mão espalmada e normalização
  filters.py         Suavização exponencial dependente do tempo
  serial_client.py   Conexão, ativação e confirmações
  protocol.py        Enquadramento, checksum e mensagens
  config.py          Validação e persistência do perfil
firmware/robotic_hand/
  robotic_hand.ino   Recepção, watchdog e controle do PCA9685
  servo_config.h    Canais e limites mecânicos
scripts/download_model.py
tests/
```

## Verificação

```powershell
.\.venv\Scripts\python.exe -m unittest discover -s tests -v
```

Os testes cobrem geometria, invariância a rotação/escala, filtros, calibração, corrupção de mensagens, porta simulada, timeout e reset. A validação física depende da montagem: confira primeiro um servo, depois os cinco, a perda da mão, desconexão USB e corte de alimentação. Testes de software não verificam torque, alimentação ou batentes mecânicos.

## Referências

- [MediaPipe Hand Landmarker](https://developers.google.com/edge/mediapipe/solutions/vision/hand_landmarker?tab=Python)
- [Modelo oficial float16, versão 1](https://storage.googleapis.com/mediapipe-models/hand_landmarker/hand_landmarker/float16/1/hand_landmarker.task)
- [Ligação e alimentação do PCA9685 — Adafruit](https://learn.adafruit.com/16-channel-pwm-servo-driver?view=all)
- [Biblioteca Arduino — Adafruit](https://github.com/adafruit/Adafruit-PWM-Servo-Driver-Library)
- [pySerial](https://pyserial.readthedocs.io/en/latest/pyserial_api.html)
