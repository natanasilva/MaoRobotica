import argparse
from collections import deque
import copy
import math
import statistics
import time

from . import FINGERS
from .config import load_config, save_config
from .filters import Smoother
from .finger_mapping import normalize, raw_flexions
from .protocol import set_frame


def run_demo(seconds):
    start = time.monotonic()
    sequence = 0
    while time.monotonic() - start < seconds:
        elapsed = time.monotonic() - start
        values = [(1 - math.cos(elapsed * 2 + i * 0.4)) / 2 for i in range(5)]
        print(set_frame(sequence, values).decode().strip())
        sequence = (sequence + 1) % 65536
        time.sleep(0.1)


def run_manual(args):
    from .serial_client import SerialClient
    values = [v / 1000 for v in args.values]
    client = SerialClient(args.port)
    try:
        client.arm()
        print("Controle manual ativo; Ctrl+C interrompe.")
        deadline = time.monotonic() + args.seconds
        while time.monotonic() < deadline:
            client.send(values)
            if not client.armed:
                raise RuntimeError("Arduino desarmado durante controle manual")
            time.sleep(0.04)
    finally:
        client.close()


WINDOW = "Mao Robotica"


def fit_to_window(image, window):
    """Amplia a imagem para o tamanho da janela, mantendo a proporção (barras pretas)."""
    import cv2
    try:
        _, _, width, height = cv2.getWindowImageRect(window)
    except cv2.error:
        return image
    if width < 50 or height < 50:
        return image
    image_h, image_w = image.shape[:2]
    scale = min(width / image_w, height / image_h)
    new_w, new_h = max(1, round(image_w * scale)), max(1, round(image_h * scale))
    resized = cv2.resize(image, (new_w, new_h), interpolation=cv2.INTER_LINEAR)
    top, left = (height - new_h) // 2, (width - new_w) // 2
    return cv2.copyMakeBorder(resized, top, height - new_h - top, left, width - new_w - left,
                              cv2.BORDER_CONSTANT, value=(0, 0, 0))


def set_fullscreen(window, enabled):
    import cv2
    cv2.setWindowProperty(window, cv2.WND_PROP_FULLSCREEN,
                          cv2.WINDOW_FULLSCREEN if enabled else cv2.WINDOW_NORMAL)


def run_camera(args):
    import cv2
    from .serial_client import SerialClient
    from .tracking import Tracker, draw_hand

    config = load_config(args.config)
    tracker = camera = client = None
    try:
        tracker = Tracker(config)
        camera = cv2.VideoCapture(config["camera"])
        camera.set(cv2.CAP_PROP_FRAME_WIDTH, 1280)
        camera.set(cv2.CAP_PROP_FRAME_HEIGHT, 720)
        if not camera.isOpened():
            raise RuntimeError("Não foi possível abrir a webcam")
        # A webcam pode não suportar 1280x720; nesse caso o OpenCV usa outra resolução.
        print(f"Resolução da câmera: {int(camera.get(cv2.CAP_PROP_FRAME_WIDTH))}x"
              f"{int(camera.get(cv2.CAP_PROP_FRAME_HEIGHT))}")
        if args.port:
            client = SerialClient(args.port)
        smoother = Smoother(config["smoothing_seconds"])
        samples = deque(maxlen=120)
        pending_open = None
        last_valid = None
        valid_since = None
        next_send = 0.0
        values = [0.0] * 5
        message = "O: aberta | C: fechada | A: ativar | ESPACO: parar | F: tela cheia | Q: sair"
        print(message)
        # WINDOW_NORMAL permite redimensionar; a janela começa em tela cheia.
        cv2.namedWindow(WINDOW, cv2.WINDOW_NORMAL)
        fullscreen = True
        set_fullscreen(WINDOW, fullscreen)
        print("Calibração: mantenha cada postura por 1 segundo antes de O/C.")
        while True:
            ok, frame = camera.read()
            now = time.monotonic()
            if not ok:
                raise RuntimeError("Falha na leitura da webcam")
            # O MediaPipe classifica esquerda/direita supondo imagem espelhada (selfie).
            # Espelhar antes da inferência faz "Right" corresponder à mão direita da pessoa.
            # As flexões usam só ângulos e distâncias, então não mudam com o espelhamento.
            frame = cv2.flip(frame, 1)
            detection = tracker.detect(frame, now)
            raw = None
            if detection:
                try:
                    raw = raw_flexions(detection[0])
                except ValueError:
                    detection = None
            if detection:
                last_valid = now
                if valid_since is None:
                    valid_since = now
                samples.append((now, raw))
                values = smoother.update(normalize(raw, config["calibration"]), now)
                draw_hand(frame, detection[1])
            else:
                valid_since = None
                samples.clear()
                if last_valid is None or now - last_valid >= config["lost_seconds"]:
                    smoother.reset()
                    if client and client.armed:
                        client.stop()
                        message = "Mao perdida: desarmado. Mostre a mao e pressione A."
            if client:
                client.poll()
                # Nunca atualizar o watchdog com uma detecção antiga.
                if detection and client.armed and now >= next_send:
                    client.send(values)
                    next_send = now + 1 / config["send_hz"]
            display = fit_to_window(frame, WINDOW)
            # Texto desenhado após a ampliação, com escala proporcional, para ficar nítido.
            s = max(1.0, display.shape[0] / 480)
            thick = max(1, round(s))
            state = "SIMULACAO" if not client else ("ATIVO" if client.armed else "DESARMADO")
            cv2.putText(display, f"{state} | Mao: {config['hand']} | {'OK' if detection else 'AUSENTE/AMBIGUA'}",
                        (int(10*s), int(24*s)), cv2.FONT_HERSHEY_SIMPLEX, 0.55*s, (0, 220, 255), thick)
            for i, (finger, value) in enumerate(zip(FINGERS, values)):
                cv2.putText(display, f"{finger:6}: {value*100:5.1f}%", (int(10*s), int((55 + i*25)*s)),
                            cv2.FONT_HERSHEY_SIMPLEX, 0.55*s, (255, 255, 255), thick)
            cv2.putText(display, message[:95], (int(10*s), display.shape[0] - int(18*s)),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.38*s, (0, 220, 255), thick)
            cv2.imshow(WINDOW, display)
            key = cv2.waitKey(1) & 0xFF
            if key in (ord("q"), 27) or cv2.getWindowProperty(WINDOW, cv2.WND_PROP_VISIBLE) < 1:
                break
            if key == ord("f"):
                fullscreen = not fullscreen
                set_fullscreen(WINDOW, fullscreen)
            if key == 32 and client:
                client.stop()
                message = "Desarmado. A: ativar novamente."
            if key == ord("a") and client:
                if detection and valid_since is not None and now - valid_since >= 0.5:
                    client.arm()
                    next_send = 0.0
                    message = "Ativo. ESPACO: parar | Q: sair"
                else:
                    message = "Mantenha apenas a mao selecionada visivel por 0.5 s."
            if key in (ord("o"), ord("c")):
                if client:
                    client.stop()
                recent = [v for t, v in samples if now - t <= 1.0]
                if len(recent) < 10 or valid_since is None or now - valid_since < 1.0:
                    message = "Mantenha a postura por 1 segundo e tente novamente."
                    continue
                if any(max(v[i] for v in recent) - min(v[i] for v in recent) > 0.12 for i in range(5)):
                    message = "Mao instavel. Mantenha a postura e tente novamente."
                    continue
                measured = [statistics.median(v[i] for v in recent) for i in range(5)]
                if key == ord("o"):
                    pending_open = measured
                    message = "Abertura capturada. Feche a mao, aguarde 1 s e pressione C."
                elif pending_open is None:
                    message = "Primeiro capture a mao aberta com O."
                elif any(c - o < 0.08 for o, c in zip(pending_open, measured)):
                    message = "Amplitude insuficiente. Reposicione a mao e repita O/C."
                else:
                    updated = copy.deepcopy(config)
                    updated["calibration"] = {f: {"open": o, "closed": c}
                                              for f, o, c in zip(FINGERS, pending_open, measured)}
                    save_config(args.config, updated)
                    config = updated
                    pending_open = None
                    smoother.reset()
                    message = "Calibracao salva. A: ativar | Q: sair"
    finally:
        try:
            if client:
                client.close()
        finally:
            if camera:
                camera.release()
            if tracker:
                tracker.close()
            cv2.destroyAllWindows()


def main(argv=None):
    parser = argparse.ArgumentParser(description="Mão robótica: visão, simulação e Arduino/PCA9685")
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("ports", help="Listar portas seriais")
    demo = sub.add_parser("demo", help="Simulação sem webcam e sem Arduino")
    demo.add_argument("--seconds", type=float, default=3)
    camera = sub.add_parser("camera", help="Webcam; sem --port funciona em simulação")
    camera.add_argument("--config", default="config/hand.json")
    camera.add_argument("--port", help="Porta do Arduino, por exemplo COM3")
    manual = sub.add_parser("manual", help="Enviar posições sem webcam (movimenta servos)")
    manual.add_argument("--port", required=True)
    manual.add_argument("--values", type=int, nargs=5, required=True, metavar="N")
    manual.add_argument("--seconds", type=float, default=3)
    args = parser.parse_args(argv)
    if hasattr(args, "seconds") and (not math.isfinite(args.seconds) or not 0 < args.seconds <= 3600):
        parser.error("--seconds deve estar entre 0 e 3600")
    if args.command == "manual" and any(not 0 <= v <= 1000 for v in args.values):
        parser.error("--values deve conter cinco inteiros entre 0 e 1000")
    try:
        if args.command == "ports":
            from serial.tools import list_ports
            ports = list(list_ports.comports())
            for port in ports:
                print(f"{port.device}: {port.description}")
            if not ports:
                print("Nenhuma porta serial encontrada.")
        elif args.command == "demo":
            run_demo(args.seconds)
        elif args.command == "manual":
            run_manual(args)
        else:
            run_camera(args)
    except KeyboardInterrupt:
        return 130
    except ImportError as exc:
        print(f"Dependência ausente: {exc}. Instale: python -m pip install -e .[vision]")
        return 1
    except (OSError, ValueError, RuntimeError, KeyError, TypeError) as exc:
        print(f"Erro: {exc}")
        return 1
    return 0
