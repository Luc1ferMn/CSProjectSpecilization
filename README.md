# CS Project Specialization — Raspberry Pi Local SoftAP + PFM-1 Detection

We got inspired to make this project because we realised how many people lose their life or get their life dramatically changed for the worse because of PFM-1 anti-personal landmines. We want to make a system that can help detect that kind of landmine. Based on what model is being used, this system has potential to be used for more than just that landmine.

---

## 0 — Before you begin

Flash the SD card with a Raspberry Pi image matching the model you are going to use.

Before you can use the Raspberry Pi remember to run — update the system with the following commands, or if you are in desktop mode you can use the update program in the top bar:

```bash
sudo apt update
sudo apt upgrade
```

---

## 01 — Install required packages

For the Raspberry Pi to be able to turn on soft AP mode, you need two packages:
- **hostapd** to create the Wi-Fi AP
- **dnsmasq** to hand out local IP addresses

Run these commands to download the needed packages:

```bash
sudo apt install hostapd dnsmasq
```

hostapd is by default masked, that just means it cannot run so we need to unmask it — you can always undo it:

```bash
sudo systemctl unmask hostapd
```

We need to configure the configs of the packages before we can use them. For that to be possible we stop them so they are not running:

```bash
sudo systemctl stop hostapd dnsmasq
```

---

## 02 — Assign a static IP to wlan0

Your Pi must have a fixed address for the SoftAP network. If you are using an older Raspberry Pi you might need to edit `/etc/dhcpcd.conf`. Create or edit this file if it already exists — it's the same process:

```bash
sudo nano /etc/systemd/network/filename.network
```

Then add the following configuration — the address can almost be anything:

```ini
[Match]
Name=wlan0

[Network]
# give it a static address — you can choose whatever, it just has to be under 256
Address=192.168.4.1/24
ConfigureWithoutCarrier=yes
```

> **Subnet conflict warning.** If your home/ISP router also uses `192.168.4.x` (common with several EU/Scandinavian ISP routers), badboyNet will collide with it and routing breaks. Pick a different range — `192.168.50.1/24` is a safe choice. Use the same subnet in section 03.

---

## 03 — Configure dnsmasq for local-only DHCP

This gives your other devices an IP. Create or edit this file if it already exists — it's the same process:

```bash
sudo nano /etc/dnsmasq.conf
```

Then add the following configuration — remember to have the same base address in `dhcp-range` as you set in the `.network` file:

```
# This gives other devices an IP but does not forward the internet
interface=wlan0

bind-dynamic

dhcp-range=192.168.4.10,192.168.4.20,12h

dhcp-option=3,192.168.4.1
dhcp-option=6,192.168.4.1
```

The two numbers `192.168.4.10` and `192.168.4.20` define the range of IP addresses that can be handed out in this network. This does not restrict other users from getting access to the network though. `192.168.4.14` would be 5 IPs to give out. This creates a tiny LAN with no upstream gateway.

---

## 04 — Create the hostapd Wi-Fi access point

This defines your SSID, password, and Wi-Fi mode and handles the Wi-Fi security — here we use WPA2. Create a new file called `hostapd.conf`:

```bash
sudo nano /etc/hostapd/hostapd.conf
```

Here is an example config — this one uses WPA2:

```
interface=wlan0
driver=nl80211
ssid=name_of_wifi
hw_mode=g
channel=6
auth_algs=1
wpa=2
wpa_passphrase=a_password_that_is_minimum_8_character
wpa_key_mgmt=WPA-PSK
rsn_pairwise=CCMP
```

Afterwards you need to point to this file in `/etc/default/hostapd`:

```bash
sudo nano /etc/default/hostapd
```

Change `DAEMON_CONF=""` to:

```
DAEMON_CONF="/etc/hostapd/hostapd.conf"
```

---

## 05 — Start services and test

Here you have two options — one where the Raspberry Pi has an ethernet connection, and one where we only use Wi-Fi. Only Wi-Fi can cause more problems because it is harder to troubleshoot.

You need to turn off the systems that enable the device to be a Wi-Fi client (the ability to connect to a Wi-Fi network). When this is done you will lose connection to the device if you are using Wi-Fi. If you are on ethernet it's fine — you will keep the connection.

Restart the systems we have configured so the new settings will be read:

```bash
sudo systemctl restart systemd-networkd
sudo systemctl restart hostapd
sudo systemctl restart dnsmasq
```

Then because a system called `wpa_supplicant` will try to use wlan0, we need to disable it — when this is done you **WILL** lose connection if you are on a Wi-Fi connection to the Raspberry Pi:

```bash
sudo systemctl stop wpa_supplicant
sudo systemctl stop NetworkManager 2>/dev/null || true
```

This is a temporary disable of `wpa_supplicant` so you won't lose access permanently — after a reboot it will be enabled again.

If you want to disable this permanently run this command — don't do this unless you are on ethernet or are sure that you won't need it anymore:

```bash
sudo systemctl mask wpa_supplicant
```

If you do this on Wi-Fi you need to run them all together like this:

```bash
sudo systemctl stop wpa_supplicant
sudo systemctl stop NetworkManager 2>/dev/null || true
sudo systemctl restart systemd-networkd
sudo systemctl restart hostapd
sudo systemctl restart dnsmasq
```

Now you should be able to find your network, connect to it, and SSH to the Raspberry Pi again.

---

## 06 — Flash the ESP32-CAM

The ESP32-CAM streams JPEG frames as binary WebSocket messages to the Pi. All inference happens on the Pi.

**Requirements:**
- Arduino IDE with **ESP32 board package v3.0+** (older versions don't enable `CONFIG_HTTPD_WS_SUPPORT`; the firmware refuses to compile if it's missing)
- *Tools → Board → AI Thinker ESP32-CAM*
- *Tools → Partition Scheme → Huge APP (3MB No OTA / 1MB SPIFFS)*

If your SSID/password differ, edit `CameraWebServer.ino`:

```cpp
const char *ssid     = "badboyNet";
const char *password = "Badboy12345";
```

Flash the board, open Serial Monitor at 115200. You'll see:

```
WiFi connected
Stream:  ws://192.168.50.18:81/ws
Capture: http://192.168.50.18/capture
Status:  http://192.168.50.18/status
```

**The IP is whatever DHCP hands out.** Note it — every step downstream uses it.

Verify the WebSocket firmware is on the board:

```bash
curl http://<cam-ip>/status
```

The JSON must include `"ws_connected": false`. If that field is missing, an older firmware build is still flashed — re-flash before continuing.

---

## 07 — Pi network setup: ethernet + AP at the same time

The Pi 4's WiFi chip can do **either** access-point **or** client mode on `wlan0`, not both. To keep SSH/internet access while badboyNet is running, plug an **ethernet cable from the Pi's RJ45 port to your home router**. You then have two independent interfaces:

- `eth0` → home LAN (SSH from your laptop, internet for `apt`/`pip`)
- `wlan0` → badboyNet AP (ESP32-CAM connects here)

Find eth0's IP — that's your SSH target from your laptop:

```bash
ip -4 addr show eth0
# inet 192.168.X.Y/24 ...
```

> **If `eth0` lands in `192.168.4.x`**, your home router uses badboyNet's default subnet. Edit the configs from sections 02 and 03 to use `192.168.50.x` instead, then continue.

Tell NetworkManager to leave wlan0 alone, mask wpa_supplicant, enable AP services on boot, and bring everything up:

```bash
sudo tee /etc/NetworkManager/conf.d/unmanaged.conf > /dev/null << 'EOF'
[keyfile]
unmanaged-devices=interface-name:wlan0
EOF

sudo systemctl mask wpa_supplicant
sudo systemctl enable systemd-networkd hostapd dnsmasq
sudo systemctl restart NetworkManager
sleep 3
sudo systemctl restart systemd-networkd hostapd dnsmasq
```

Verify:

```bash
ip -4 addr show wlan0      # only 192.168.50.1/24 (or whatever you chose)
nmcli device status        # wlan0 -> unmanaged, eth0 -> connected
sudo systemctl status hostapd --no-pager
```

Power-cycle the ESP32-CAM so it requests a fresh DHCP lease in the new subnet — the Serial Monitor will print the new IP.

---

## 08 — Install Python dependencies on the Pi

### Pre-flight checks

```bash
uname -m         # must be aarch64 (YOLOv8/v11 needs 64-bit ARM)
df -h /tmp       # /tmp is a ~1.9 GB tmpfs — install routes around it
```

### System libraries

```bash
sudo apt install -y python3-pip python3-venv \
                    libgl1 libglib2.0-0 libgtk-3-0 \
                    libjpeg-dev libopenblas-dev \
                    cmake build-essential
```

> On Pi OS Trixie, apt may report `Note, selecting 'libgtk-3-0t64' instead...` — harmless, accept the t64 variant. `libatlas-base-dev` was dropped from Trixie; OpenBLAS replaces it.

### Virtual environment

```bash
python3 -m venv ~/detector-env
source ~/detector-env/bin/activate
pip install --upgrade pip wheel setuptools
```

### Install the detection stack — the gotcha section

Two things will bite you if you skip them:

1. **`/tmp` is a 1.9 GB tmpfs.** PyTorch + OpenCV + ultralytics combined exceed it during install → `OSError: [Errno 28] No space left on device`.
2. **Pip's wheel cache** burns another ~1 GB if you don't disable it.

Redirect tmp to your home and skip caching:

```bash
mkdir -p ~/pip-tmp
TMPDIR=~/pip-tmp pip install --no-cache-dir \
    "torch==2.6.0" "torchvision==0.21.0" \
    ultralytics opencv-python websocket-client
rm -rf ~/pip-tmp
```

> **`torch==2.6.0` is a hard requirement on Pi 4.** PyTorch 2.7+ wheels emit ARMv8.2-A instructions (FP16, BF16) that the Pi 4's Cortex-A72 cannot decode — you get `Illegal instruction` crashes during inference. Pi 5 (Cortex-A76) doesn't have this constraint.

### Verify

```bash
python3 -c "from ultralytics import YOLO; import websocket, cv2; print('OK')"
python3 -c "import cv2; cv2.namedWindow('t'); cv2.destroyAllWindows(); print('GUI works')"
```

If the second test fails with `function is not implemented`, pip pulled `opencv-python-headless` (sometimes dragged in by ultralytics). Replace it:

```bash
pip uninstall opencv-python opencv-python-headless -y
TMPDIR=~/pip-tmp pip install --no-cache-dir opencv-python
```

---

## 09 — Train the PFM-1 model on Google Colab

The Roboflow Universe project (https://universe.roboflow.com/university-of-southern-california-zbvtl/yolo8-detection-pfm-1) publishes the **dataset only** — no pre-trained weights. Roboflow's hosted training works but **paywalls the resulting `.pt` download** on the free tier. Train on Colab instead: free GPU, fully owned output.

### 9.1 — Roboflow setup

1. Create a free account at https://app.roboflow.com.
2. Open the project URL and **Fork Project** to your workspace.
3. After forking, your URL looks like `app.roboflow.com/<workspace>/<project-with-random-suffix>/`. Note the workspace slug and the full project slug (with the suffix).
4. Grab your API key from https://app.roboflow.com/settings/api.

### 9.2 — Generate a dataset version

In your forked project: **Generate → Generate New Version**.

- **Preprocessing:** Auto-Orient ON, Resize 640×640 (Stretch). Leave the rest off.
- **Augmentation (only these):**
  | Toggle | Value |
  |---|---|
  | Flip | Horizontal only |
  | 90° Rotate | On |
  | Rotation | ±15° |
  | Brightness | ±20% |
  | Exposure | ±20% |
  | Blur | up to 1.5 px |
  | Noise | up to 2% |
- **Output multiplier:** 3x.

Skip Mosaic, Saturation, Hue, Crop, Cutout, Shear, and the entire Bounding-Box-Level Augmentations panel.

Click **Generate** — note the version number (typically `1`).

### 9.3 — Train on Colab

Open https://colab.research.google.com → New Notebook → *Runtime → T4 GPU*. Run:

```python
!pip install ultralytics roboflow

from roboflow import Roboflow
rf      = Roboflow(api_key="YOUR_API_KEY")
project = rf.workspace("YOUR_WORKSPACE").project("YOUR_PROJECT_SLUG")
dataset = project.version(1).download("yolov8")

!yolo train model=yolo11n.pt data={dataset.location}/data.yaml epochs=80 imgsz=640 device=0

from google.colab import files
files.download('runs/detect/train/weights/best.pt')
```

~25 minutes on T4. Expect mAP@50 ≥ 0.85. `best.pt` lands in your laptop's Downloads folder.

### 9.4 — Copy `best.pt` to the Pi

From your **laptop's** terminal (not the Pi's). Use the eth0 IP from section 07:

**Windows (`cmd`):**
```cmd
cd %USERPROFILE%\Downloads
scp best.pt badboii@<eth0-ip>:~/pfm1.pt
```

**Mac/Linux:**
```bash
scp ~/Downloads/best.pt badboii@<eth0-ip>:~/pfm1.pt
```

Verify on the Pi:

```bash
ls -lh ~/pfm1.pt   # ~5–6 MB for YOLOv11 nano
```

---

## 10 — Create detect.py and run

The detector reads JPEGs from the WebSocket, drops stale frames automatically (so latency stays low), runs YOLO inference, and shows boxes + class labels + confidence in an OpenCV window on the Pi's HDMI monitor.

```bash
cat > ~/detect.py << 'EOF'
import cv2
import numpy as np
import time
import threading
import websocket
from collections import deque
from ultralytics import YOLO
# you can use 160 320 or 640 for the image size
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
            print(f"WS error: {e} ” reconnecting in 2s")
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
EOF
```

Set `CAM_IP` to your camera's actual IP, then run.

**From a terminal on the Pi's desktop (recommended):**

```bash
source ~/detector-env/bin/activate
python3 ~/detect.py
```

**From SSH (window still appears on the Pi's monitor):**

```bash
source ~/detector-env/bin/activate
export DISPLAY=:0
python3 ~/detect.py
```

Expect ~4–5 FPS on a Pi 4 with YOLOv11 nano. Press `q` in the window to quit.

---

## 11 — Tune the camera if the stream lags

If you see `corrupt JPEG data` warnings or the boxes lag noticeably behind the dummy mine, the camera is producing more bytes than the Pi can chew. Drop frame size and quality at runtime:

```bash
curl "http://<cam-ip>/control?var=framesize&val=6"   # CIF (400x296)
curl "http://<cam-ip>/control?var=quality&val=20"    # higher number = lower quality
```

Settings persist until power-cycle. To make them firmware defaults, edit `CameraWebServer.ino` and re-flash.

---

## 12 — Auto-start on boot (optional)

Use the LXDE desktop autostart so the script runs after the GUI is up:

```bash
mkdir -p ~/.config/autostart
nano ~/.config/autostart/pfm1-detector.desktop
```

```ini
[Desktop Entry]
Type=Application
Name=PFM-1 Detector
Exec=/home/badboii/detector-env/bin/python3 /home/badboii/detect.py
Terminal=true
X-GNOME-Autostart-enabled=true
```

Reboot — the detector window opens automatically once the desktop loads.

## System architecture

```
[Dummy PFM-1]
     │
     ▼
[ESP32-CAM]  ──ws:// binary JPEG──>  [Raspberry Pi 4]
                                     ┌──────────────────────────────────┐
                                     │ wlan0 = 192.168.50.1 (badboyNet) │
                                     │ eth0  = 192.168.X.Y (LAN + SSH)  │
                                     │ YOLOv11 nano on PyTorch 2.6      │
                                     │ reader thread drops stale frames │
                                     │ cv2.imshow → HDMI monitor        │
                                     └──────────────────────────────────┘
```

- **ESP32-CAM** captures and pushes binary WebSocket frames — no HTTP per-frame overhead.
- **Pi 4** is the AP, the WebSocket client, the inference engine, and the display surface.
- **WPA2** on badboyNet protects JPEG payloads on air. To take this off a private AP, swap `ws://` for `wss://` and terminate TLS on the Pi.
- **Trained model** comes from training the Roboflow Universe dataset on Colab (section 9). The class label and confidence on each detection are produced by `results[0].plot()` using the labels baked into your trained `pfm1.pt`.
