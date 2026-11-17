from pathlib import Path

CONNECTIONS = ((0, 1), (1, 2), (2, 3), (3, 4), (0, 5), (5, 6), (6, 7), (7, 8),
               (5, 9), (9, 10), (10, 11), (11, 12), (9, 13), (13, 14), (14, 15),
               (15, 16), (13, 17), (0, 17), (17, 18), (18, 19), (19, 20))


class Tracker:
    def __init__(self, config):
        import mediapipe as mp
        self.mp = mp
        self.config = config
        if not Path(config["model"]).is_file():
            raise ValueError("Modelo ausente. Execute: python scripts/download_model.py")
        options = mp.tasks.vision.HandLandmarkerOptions(
            base_options=mp.tasks.BaseOptions(model_asset_path=config["model"]),
            running_mode=mp.tasks.vision.RunningMode.VIDEO,
            num_hands=2, min_hand_detection_confidence=0.6,
            min_hand_presence_confidence=0.6, min_tracking_confidence=0.6)
        self.detector = mp.tasks.vision.HandLandmarker.create_from_options(options)
        self.timestamp = -1

    def detect(self, frame, now):
        import cv2
        self.timestamp = max(self.timestamp + 1, int(now * 1000))
        rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
        result = self.detector.detect_for_video(
            self.mp.Image(image_format=self.mp.ImageFormat.SRGB, data=rgb), self.timestamp)
        # Recusa duas mãos visíveis para evitar troca silenciosa de operador.
        if len(result.hand_landmarks) != 1:
            return None
        category = result.handedness[0][0]
        if self.config["hand"] not in ("Any", category.category_name) or category.score < self.config["min_hand_score"]:
            return None
        points = [(p.x, p.y, p.z) for p in result.hand_world_landmarks[0]]
        return points, result.hand_landmarks[0]

    def close(self):
        self.detector.close()


def draw_hand(frame, landmarks):
    import cv2
    h, w = frame.shape[:2]
    points = [(int(p.x * w), int(p.y * h)) for p in landmarks]
    for a, b in CONNECTIONS:
        cv2.line(frame, points[a], points[b], (60, 220, 80), 2)
    for p in points:
        cv2.circle(frame, p, 4, (255, 170, 50), -1)
