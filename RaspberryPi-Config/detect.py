import cv2
import numpy as np
import time
import threading
import websocket
from ultralytics import YOLO

modelVersion = "yolov11"
image_size = 320

# To kameraer
CAM_IPS = ["192.168.50.18", "192.168.50.19"]  # Eller fra command line
CONFIDENCE = 0.30
WINDOW_NAME = "PFM-1 Detection - Dual"

latest_frames = [None, None]  # [cam0, cam1]
lock = threading.Lock()
connected = [False, False]    # Track forbindelsesstatus


def reader(cam_idx):
    """Read fra kamera ved indeks cam_idx"""
    ws_url = f"ws://{CAM_IPS[cam_idx]}:81/ws"
    while True:
        try:
            ws = websocket.WebSocket()
            ws.connect(ws_url, timeout=5)
            print(f"[CAM{cam_idx}] Connected to {ws_url}")
            connected[cam_idx] = True
            
            while True:
                data = ws.recv()
                if not data or isinstance(data, str):
                    continue
                with lock:
                    latest_frames[cam_idx] = data
                    
        except Exception as e:
            print(f"[CAM{cam_idx}] WS error: {e} – reconnecting in 2s")
            connected[cam_idx] = False
            time.sleep(2)


def main():
    model = YOLO(f"/home/badboii/MineModels/{modelVersion}/{modelVersion}pfm1_{image_size}.onnx", task="detect")

    # Start reader threads for begge kameraer
    for i in range(2):
        threading.Thread(target=reader, args=(i,), daemon=True).start()

    # Vent til begge er forbundne
    print("Venter på forbindelse til begge kameraer...")
    while not (connected[0] and connected[1]):
        time.sleep(0.5)
    print("Begge kameraer forbundne!")

    cv2.namedWindow(WINDOW_NAME, cv2.WINDOW_NORMAL)
    cv2.resizeWindow(WINDOW_NAME, 640, 320)

    last, fps = time.time(), 0.0

    while True:
        with lock:
            data0 = latest_frames[0]
            data1 = latest_frames[1]
            latest_frames[0] = None
            latest_frames[1] = None

        if data0 is None or data1 is None:
            time.sleep(0.01)
            continue

        # Dekod begge frames
        arr0 = np.frombuffer(data0, dtype=np.uint8)
        frame0 = cv2.imdecode(arr0, cv2.IMREAD_COLOR)
        
        arr1 = np.frombuffer(data1, dtype=np.uint8)
        frame1 = cv2.imdecode(arr1, cv2.IMREAD_COLOR)

        if frame0 is None or frame1 is None:
            continue

        # Run YOLO på begge
        results0 = model(frame0, conf=CONFIDENCE, imgsz=image_size, verbose=False)
        annotated0 = results0[0].plot()
        
        results1 = model(frame1, conf=CONFIDENCE, imgsz=image_size, verbose=False)
        annotated1 = results1[0].plot()

        # Kombiner side by side
        combined = np.hstack([annotated0, annotated1])

        # FPS + info
        now = time.time()
        fps = 0.9 * fps + 0.1 * (1.0 / max(now - last, 1e-6))
        last = now
        
        cv2.putText(combined, f"CAM0: {len(results0[0].boxes)} | CAM1: {len(results1[0].boxes)} | FPS: {fps:5.1f}",
                    (10, 25), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 255, 0), 2)

        cv2.imshow(WINDOW_NAME, combined)
        if cv2.waitKey(1) & 0xFF == ord("q"):
            break

    cv2.destroyAllWindows()


if __name__ == "__main__":
    main()