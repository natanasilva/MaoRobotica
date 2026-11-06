"""Baixa o modelo oficial, versão 1. Execute na raiz do projeto."""
from pathlib import Path
import hashlib
import urllib.request
import zipfile

URL = "https://storage.googleapis.com/mediapipe-models/hand_landmarker/hand_landmarker/float16/1/hand_landmarker.task"
SHA256 = "fbc2a30080c3c557093b5ddfc334698132eb341044ccee322ccf8bcf3607cde1"


def main():
    target = Path(__file__).resolve().parents[1] / "models" / "hand_landmarker.task"
    target.parent.mkdir(parents=True, exist_ok=True)
    temporary = target.with_suffix(".download")
    try:
        with urllib.request.urlopen(URL, timeout=60) as response, temporary.open("wb") as output:
            while chunk := response.read(1024 * 1024):
                output.write(chunk)
        if hashlib.sha256(temporary.read_bytes()).hexdigest() != SHA256:
            raise ValueError("Checksum do modelo diferente da versão verificada")
        if not zipfile.is_zipfile(temporary):
            raise ValueError("Download não é um pacote MediaPipe válido")
        with zipfile.ZipFile(temporary) as archive:
            if archive.testzip() is not None or not {"hand_detector.tflite", "hand_landmarks_detector.tflite"}.issubset(archive.namelist()):
                raise ValueError("Conteúdo do modelo inválido")
        temporary.replace(target)
        print(f"Modelo salvo em {target}")
        print(f"SHA256: {hashlib.sha256(target.read_bytes()).hexdigest()}")
    finally:
        temporary.unlink(missing_ok=True)


if __name__ == "__main__":
    main()
