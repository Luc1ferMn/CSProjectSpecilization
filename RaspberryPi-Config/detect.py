import cv2
import numpy as np
import time
import threading
import websocket
from collections import deque
from ultralytics import YOLO

CAM_IP      = "192.168.50.18"
WS_URL      = f"ws://{CAM_IP}:81/ws"
MODEL_PATH  = "/home/badboii/pfm1.pt"
CONFIDENCE  = 0.5
WINDOW_NAME = "PFM-1 Detection"

latest_frame = [None]   # single-slot buffer; reader thread always overwrites
lock = threading.Lock()


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
            print(f"WS error: {e} — reconnecting in 2s")
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