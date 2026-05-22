import cv2
import numpy as np
import time
import threading
import websocket
from collections import deque
from ultralytics import YOLO

CAM_IPS     = ["192.168.50.18", "192.168.50.11"]  # ESP32-CAM 1 and 2
MODEL_PATH  = "/home/badboii/yolo11pfm1_320.onnx"  # ONNX is faster than .pt on Pi CPU
CONFIDENCE  = 0.5
WINDOW_NAME = "PFM-1 Detection"

latest_frame = [None]   # single-slot buffer; reader thread always overwrites
lock = threading.Lock()


def reader():
    """Continuously read JPEGs from WS into the single-slot buffer.
    Tries CAM_IPS[0] first; only tries CAM_IPS[1] if the first one fails.
    Once connected to a cam, stays with it until the connection drops."""
    active_ip = None
    while True:
        try:
            if active_ip is None:
                if len(CAM_IPS) > 0:
                    try:
                        ws = websocket.WebSocket()
                        ws.connect(f"ws://{CAM_IPS[0]}:81/ws", timeout=5)
                        active_ip = CAM_IPS[0]
                    except Exception:
                        print(f"Could not connect to {CAM_IPS[0]}, trying {CAM_IPS[1]}")
                        ws = websocket.WebSocket()
                        ws.connect(f"ws://{CAM_IPS[1]}:81/ws", timeout=5)
                        active_ip = CAM_IPS[1]
                print(f"Connected to ws://{active_ip}:81/ws")
            else:
                ws = websocket.WebSocket()
                ws.connect(f"ws://{active_ip}:81/ws", timeout=5)
                print(f"Reconnected to ws://{active_ip}:81/ws")

            while True:
                data = ws.recv()
                if not data or isinstance(data, str):
                    continue
                with lock:
                    latest_frame[0] = data
        except Exception as e:
            print(f"WS error: {e} — reconnecting in 2s")
            active_ip = None
            time.sleep(2)


def main():
    model = YOLO(MODEL_PATH)

    cv2.namedWindow(WINDOW_NAME, cv2.WINDOW_NORMAL)
    cv2.resizeWindow(WINDOW_NAME, 960, 720)

    threading.Thread(target=reader, daemon=True).start()

    last, fps = time.time(), 0.0

    while True:
        with lock:
            data = latest_frame[0]
            latest_frame[0] = None    # consume

        if data is None:
            time.sleep(0.01)
            continue

        arr = np.frombuffer(data, dtype=np.uint8)
        frame = cv2.imdecode(arr, cv2.IMREAD_COLOR)
        if frame is None:
            continue

        results = model(frame, conf=CONFIDENCE, imgsz=320, verbose=False)
        annotated = results[0].plot()

        now = time.time()
        fps = 0.9 * fps + 0.1 * (1.0 / max(now - last, 1e-6))
        last = now
        cv2.putText(annotated, f"{fps:5.1f} FPS | {len(results[0].boxes)} det", (10, 25), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 255, 0), 2)

        cv2.imshow(WINDOW_NAME, annotated)
        if cv2.waitKey(1) & 0xFF == ord("q"):
            break

    cv2.destroyAllWindows()

if __name__ == "__main__":
    main()