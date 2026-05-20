import cv2
import numpy as np
import time
import threading
import websocket
from collections import deque
from ultralytics import YOLO
# you can use 160 320 or 620 for the image size
modelVersion = "yolov11"
image_size = 320
CAM_IP      = "192.168.50.18"
WS_URL      = f"ws://{CAM_IP}:81/ws"
MODEL_PATH  = f"/home/badboii/MineModels/{modelVersion}/{modelVersion}pfm1_{image_size}.onnx"
CONFIDENCE  = 0.30
WINDOW_NAME = "PFM-1 Detection"

latest_frame = [None]   # single-slot buffer; reader thread always overwrites
lock         = threading.Lock()


def reader():
    """Continuously read JPEGs from WS into the single-slot buffer.
    Older frames are dropped automatically because we only keep [-1]."""
    while True:
        try:
            ws = websocket.WebSocket()
            ws.connect(WS_URL, timeout=5)
            print(f"Connected to {WS_URL}")
            while True:
                data = ws.recv()
                if not data or isinstance(data, str):
                    continue
                with lock:
                    latest_frame[0] = data
        except Exception as e:
            print(f"WS error: {e} â€” reconnecting in 2s")
            time.sleep(2)


def main():
    model = YOLO(MODEL_PATH, task="detect")

    cv2.namedWindow(WINDOW_NAME, cv2.WINDOW_NORMAL)
    cv2.resizeWindow(WINDOW_NAME, 320, 320)


    threading.Thread(target=reader, daemon=True).start()

    last, fps = time.time(), 0.0

    while True:
        with lock:
            data = latest_frame[0]
            latest_frame[0] = None    # consume

        if data is None:
            time.sleep(0.01)
            continue

        arr   = np.frombuffer(data, dtype=np.uint8)
        frame = cv2.imdecode(arr, cv2.IMREAD_COLOR)
        if frame is None:
            continue

        results   = model(frame, conf=CONFIDENCE, imgsz=image_size,  verbose=False)
        annotated = results[0].plot()

        now  = time.time()
        fps  = 0.9 * fps + 0.1 * (1.0 / max(now - last, 1e-6))
        last = now
        cv2.putText(annotated, f"{fps:5.1f} FPS  |  {len(results[0].boxes)} Objects",
                    (10, 25), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 255, 0), 2)

        cv2.imshow(WINDOW_NAME, annotated)
        if cv2.waitKey(1) & 0xFF == ord("q"):
            break

    cv2.destroyAllWindows()


if __name__ == "__main__":
    main()
