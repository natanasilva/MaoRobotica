# Validação da implementação

Validação realizada em 6 de novembro de 2026, Windows, Python 3.14.

## Software Python

- Ambiente virtual instalado em `.venv` com as dependências de visão.
- Versões resolvidas registradas em `requirements-tested.txt`.
- `pip check`: nenhuma dependência incompatível.
- Simulação `python -m mao_robotica demo`: geração de cinco valores e mensagens com checksum.
- Testes unitários: geometria, filtro, configuração, checksum, comunicação simulada, perda de confirmação, ACK estranho e reset do Arduino.
- Modelo oficial float16, versão 1, baixado em `models/hand_landmarker.task`.
- SHA256 do modelo: `fbc2a30080c3c557093b5ddfc334698132eb341044ccee322ccf8bcf3607cde1`.
- Inferência em imagem vazia: nenhuma mão aceita.
- Inferência na [imagem pública do exemplo oficial](https://storage.googleapis.com/mediapipe-tasks/hand_landmarker/woman_hands.jpg): duas mãos detectadas, cena rejeitada pelo controle.
- Recortes individuais da imagem, cada um em uma sessão nova do detector: mão esquerda e direita aceitas, 21 pontos e cinco flexões finitas na faixa esperada.

## Firmware

Compilado para `arduino:avr:uno`, sem upload:

- Arduino AVR Boards 1.8.8.
- Adafruit PWM Servo Driver Library 3.0.3.
- Adafruit BusIO 1.17.4.
- Versão distribuída com `CALIBRATED = false`: 10.510 bytes de flash e 752 bytes de RAM global.

As ferramentas e bibliotecas de compilação ficam em `.tools` e `.arduino`, ignoradas pelo Git. O firmware e sua configuração são os arquivos em `firmware/robotic_hand`, independentes dessas pastas.

## Validação física pendente

Não foi feito upload, acionamento de servos, teste de webcam ao vivo nem medição de latência ponta a ponta. As portas seriais listadas neste ambiente eram Bluetooth; nenhuma foi aberta para controlar hardware.

Antes do uso físico, verificar o modelo da placa e do módulo PWM, tensão, corrente de pico, torque, pulsos, sentido de cada servo, nível de OE no reset, perda de detecção e desconexão USB. Os limites iniciais são exemplos; devem ser substituídos pelos valores medidos na montagem, seguindo o README.
