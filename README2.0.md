# Remote Offline Landmine Detection System — Proof of Concept

**A low-cost, low-energy, offline computer-vision system for detecting PFM-1 anti-personnel landmines.**

> Specialization Project · Physical Computing in Computer Science (F2026) · Roskilde Universitet
> Joakim Dorph Broager & Malthe Tranberg Ørsted · Supervisor: Abdul Halim Bin Abdul Rahman
> Repository: <https://github.com/Luc1ferMn/CSProjectSpecilization>

This repository is the practical companion to the project report *"Proof of Concept: Remote Offline Landmine Detection System."* The report explains **why** the system is built this way and what the experiments showed; this README explains **how** to reproduce it end to end. Where useful, sections below point back to the report (e.g. *Report §4.1*) so the two stay in sync.

---

## Why this project exists

Anti-personnel landmines still contaminate ~57 countries and territories, and clearing them is slow, dangerous, manual work. The single safest thing a deminer can do is increase the distance between themselves and the mine. This system is a proof of concept that a **camera at the hazard** and an **operator at a safe standoff** — connected over a self-hosted offline network, with detection running on cheap hardware — is technically viable.

We focus on the **PFM-1** specifically because it is small, scatter-deployed, easily concealed, and visually similar to soil, leaves, and debris, which makes it one of the harder mines to spot. Possessing a real AP mine is illegal in Denmark, so all testing used a **3D-printed PFM-1 replica** cleared by FMI and the Police's Administrative Centre for Weapons and Permits (*Report Appendix 1*).

**Research question:** *How can a low-cost, low-energy system using computer-vision models be used to optimize safety within landmine-detection efforts?*

> ⚠️ **Safety scope.** This is a proof of concept and a **human-in-the-loop aid**, not a certified detector. It does not replace a deminer, and it does not reach the standoff distances required for unsupervised operation (see [Limitations & safety](#limitations--safety)). Do not use it to make real clearance decisions.

---

## What the system is

```
            [ PFM-1 replica ]
                   │  (camera points at the suspected hazard)
                   ▼
            ┌─────────────────┐        ws:// binary JPEG (port 81)
            │   ESP32-CAM      │ ───────────────────────────────────►┐
            │  CameraWebServer │   joins badboyNet, serves :81/ws     │
            └─────────────────┘                                      │
                                                                     ▼
                                            ┌──────────────────────────────────────┐
                                            │            Raspberry Pi 4             │
                                            │  wlan0 = 192.168.50.1  (badboyNet AP) │
                                            │  eth0  = home LAN      (SSH + apt/pip)│
                                            │  hostapd + dnsmasq  → SoftAP          │
                                            │  detect.py = WebSocket CLIENT         │
                                            │  YOLO (ONNX) inference @ 320×320      │
                                            │  cv2.imshow → HDMI / VNC display      │
                                            └──────────────────────────────────────┘
                                                          │
                                                          ▼
                                            [ Operator sees the feed + detection boxes ]
```

- **The Pi hosts the network and does the thinking.** It runs the SoftAP (`hostapd` + `dnsmasq`), connects out to the camera as a WebSocket *client*, runs YOLO inference on each frame, and draws the boxes.
- **The ESP32-CAM is the eyes.** It joins `badboyNet`, runs a WebSocket *server* on port 81, and pushes compressed JPEG frames as binary messages. No inference happens on the camera.
- **Fully offline.** No internet, no router, no cloud. Mine-contaminated areas rarely have reliable infrastructure, so the system carries its own network (*Report §6.3*).
- **WebSocket, not HTTP.** A single persistent connection avoids per-frame handshakes and headers → lower latency and lower power for a continuous stream (*Report §3.4*).
- **Detection is model-defined, not hard-coded.** Nothing in the pipeline is PFM-1-specific. Swap the weights + class label and the same hardware detects something else (*Report §6.1*).

> **Why `hostapd` + `dnsmasq` and not `nmcli`?** `nmcli` is quicker to set up but is a high-level abstraction that hides the low-level control we wanted (e.g. AP-layer MAC policies). The project deliberately chose `hostapd`/`dnsmasq` (*Report §3.2*). An older `nmcli`-based prototype still lives in the repo — see [Repository layout](#repository-layout) — but **this guide follows the `hostapd` path.**

---

## Repository layout

| Path | What it is | Status |
|---|---|---|
| `ESP32-Config/CameraWebServer/` | ESP32-CAM firmware — WebSocket JPEG streamer on `:81/ws` (`CameraWebServer.ino`, `app_httpd.cpp`, `board_config.h`, `camera_pins.h`, `partitions.csv`). **Flash this.** | ✅ Current |
| `RaspberryPi-Config/detect.py` | Main detector. Single camera, one model variant, draws boxes + FPS. | ✅ Current |
| `RaspberryPi-Config/detect_grayscale.py` | Grayscale variant (~10% FPS gain; needs a grayscale-trained model to be meaningful — *Report §4.1.2*). | ✅ Current |
| `RaspberryPi-Config/twocamdetect.py` | Two-camera failover demo (tries cam 1, falls back to cam 2) — illustrates scalability (*Report §3.4*). | ✅ Current |
| `Detection Model/` | Trained weights: `pfm1.pt` + ONNX exports at 160/320/640 for YOLOv8 & YOLOv11. | ✅ Current |
| `Detection Model/Detection Model Enlarged Dataset/` | YOLOv11-Large weights trained on the enlarged dataset (`Largepfm1.pt`, `Largeyolo11pfm1_*.onnx`). | ✅ Current |
| `Detection Model/trainModel.txt` | Colab training snippet (Roboflow dataset → `pfm1.pt`). | ✅ Current |
| `Raspberry-pi-softAP-setup.txt` | **Earlier** SoftAP design: `nmcli`, Pi-as-WS-server on `:8080`, token auth, `192.168.4.1`. | 🗄️ Legacy |
| `ESP-32_ws_client.ino`, `ws_server.py` | Earlier ESP32-as-client + Pi-echo-server prototype that pairs with the `nmcli` guide. | 🗄️ Legacy |
| `How-to-setup-CLI-Raspberry.txt` | Unrelated side note: running a small Ollama LLM on a Pi 3 with USB swap. | 🧪 Tangential |

> The 🗄️ legacy files document an earlier iteration where the **Pi** was the WebSocket server and the **camera** was the client. The shipped system is the reverse (camera serves, Pi connects). They're kept for history; ignore them when following this README.

---

## Hardware (bill of materials)

| Component | Used in this project | Role |
|---|---|---|
| **Raspberry Pi 4 Model B** | newest available to us | SoftAP host + inference + display |
| **ESP32-CAM (AI-Thinker)** + **ESP32-CAM-MB** | combo | Camera node + USB programmer board |
| microSD card | 32 GB+ | Raspberry Pi OS (64-bit) |
| **Power bank** | Xiaomi Super Slim Magnetic 5000 mAh (5V/3A) | Powers the Pi (~4–6 h, *Report §4.4*) |
| **Li-ion cell** | Samsung INR18650-35E (3.6V, 3400 mAh) | Powers the ESP32-CAM (~21–34 h streaming) |
| Ethernet cable | — | SSH + `apt`/`pip` during setup (see [§7](#7--keep-ssh-while-the-ap-runs-ethernet--ap-together)) |
| *Optional:* USB Wi-Fi dongle | — | Extends range from ~50 m to ~90–100 m (*Report §4.2.1*) |
| *Optional:* Seeed Studio 2.4G A-02 FPC antenna | on the ESP32-CAM | Improves link **stability** at range (*Report §4.2.2*) |
| **PFM-1 3D-printed replica** | FMI-cleared | Test target only — no explosive, no fuse |

**Naming/addresses used throughout (from the deployed config):**

| Thing | Value |
|---|---|
| SSID | `badboyNet` |
| Wi-Fi password | `Badboy12345` |
| Pi (SoftAP) IP | `192.168.50.1` |
| ESP32-CAM IP | DHCP, e.g. `192.168.50.18` (whatever Serial Monitor prints) |
| Pi login user | `badboii` |

---

## Setup overview

| Step | What you do |
|---|---|
| [0](#0--before-you-begin) | Flash + update Raspberry Pi OS |
| [1](#1--install-the-softap-packages) | Install `hostapd` + `dnsmasq` |
| [2](#2--static-ip-for-wlan0) | Give `wlan0` a static IP |
| [3](#3--dhcp-with-dnsmasq) | Hand out local IPs |
| [4](#4--the-hostapd-access-point) | Define `badboyNet` (WPA2) |
| [5](#5--start-services--first-test) | Bring the AP up |
| [6](#6--flash-the-esp32-cam) | Flash the camera firmware |
| [7](#7--keep-ssh-while-the-ap-runs-ethernet--ap-together) | Ethernet + AP at the same time |
| [8](#8--python-environment-on-the-pi) | Python deps (incl. ONNX runtime) |
| [9](#9--train-the-pfm-1-model-colab) | Train on Colab, export to ONNX |
| [10](#10--put-the-models-on-the-pi) | Lay out `~/MineModels/` |
| [11](#11--run-the-detector) | Run `detect.py` |
| [12](#12--tune-the-camera-if-the-stream-lags) | Lower framesize/quality if laggy |
| [13](#13--auto-start-on-boot-optional) | Optional autostart |

---

## 0 — Before you begin

Flash the SD card with a **64-bit** Raspberry Pi OS image (YOLO/PyTorch require `aarch64`), boot, then update:

```bash
sudo apt update && sudo apt upgrade -y
```

---

## 1 — Install the SoftAP packages

```bash
sudo apt install hostapd dnsmasq
```

`hostapd` ships masked (it won't start). Unmask it, then stop both services so we can configure them first:

```bash
sudo systemctl unmask hostapd
sudo systemctl stop hostapd dnsmasq
```

---

## 2 — Static IP for `wlan0`

The AP interface needs a fixed address. On a `systemd-networkd` Pi, create:

```bash
sudo nano /etc/systemd/network/08-wlan0.network
```

```ini
[Match]
Name=wlan0

[Network]
Address=192.168.50.1/24
ConfigureWithoutCarrier=yes
```

> **Subnet-conflict warning.** If your home/ISP router also uses `192.168.50.x`, pick another private range (e.g. `192.168.60.1/24`) and keep it consistent in step 3. We use `192.168.50.x` because that's what the shipped `detect.py` expects.

---

## 3 — DHCP with `dnsmasq`

```bash
sudo nano /etc/dnsmasq.conf
```

```conf
interface=wlan0
bind-dynamic

dhcp-range=192.168.50.10,192.168.50.20,12h
dhcp-option=3,192.168.50.1
dhcp-option=6,192.168.50.1
```

This hands out `192.168.50.10–.20` with **no upstream gateway** — a tiny isolated LAN. (The range only sizes the pool; it is not an access restriction.)

---

## 4 — The `hostapd` access point

```bash
sudo nano /etc/hostapd/hostapd.conf
```

```conf
interface=wlan0
driver=nl80211
ssid=badboyNet
hw_mode=g
channel=6
auth_algs=1
wpa=2
wpa_passphrase=Badboy12345
wpa_key_mgmt=WPA-PSK
rsn_pairwise=CCMP
```

Point the daemon at the file:

```bash
sudo nano /etc/default/hostapd
# set: DAEMON_CONF="/etc/hostapd/hostapd.conf"
```

> WPA2-PSK/CCMP is a reasonable baseline for a proof of concept. WPA3-SAE, a MAC allowlist, and mTLS on the WebSocket are the recommended hardening steps for a real deployment (*Report §6.3*).

---

## 5 — Start services & first test

If you're on **Wi-Fi** to the Pi, you'll drop the connection in this step (Wi-Fi becomes the AP). Do this on **ethernet**, or be ready to reconnect via `badboyNet`.

```bash
# stop the Wi-Fi client stack that wants wlan0
sudo systemctl stop wpa_supplicant
sudo systemctl stop NetworkManager 2>/dev/null || true

# bring up our services
sudo systemctl restart systemd-networkd
sudo systemctl restart hostapd
sudo systemctl restart dnsmasq
```

`badboyNet` should now appear in your Wi-Fi list. Connect with `Badboy12345`, then SSH back in at `192.168.50.1`. (Disabling `wpa_supplicant` this way is temporary; it returns on reboot. `sudo systemctl mask wpa_supplicant` makes it permanent — only do that on ethernet.)

---

## 6 — Flash the ESP32-CAM

Firmware lives in [`ESP32-Config/CameraWebServer/`](ESP32-Config/CameraWebServer/). It joins `badboyNet` and serves JPEG frames over a WebSocket on **port 81**.

**Arduino IDE setup:**
- **ESP32 board package v3.0+** (older versions don't enable `CONFIG_HTTPD_WS_SUPPORT`; the firmware won't compile)
- Library: **WebSockets_Generic v2.16.1** (see `esp-32-libraries.txt`)
- *Tools → Board →* **AI Thinker ESP32-CAM**
- *Tools → Partition Scheme →* **Huge APP (3MB No OTA / 1MB SPIFFS)**

Credentials are already set in `CameraWebServer.ino`; change them only if you changed the AP:

```cpp
const char *ssid     = "badboyNet";
const char *password = "Badboy12345";
```

Flash, open **Serial Monitor @ 115200**. You'll see:

```
WiFi connected
Stream:  ws://192.168.50.18:81/ws
Capture: http://192.168.50.18/capture
Status:  http://192.168.50.18/status
```

**Note the IP — every downstream step uses it.** Confirm the WebSocket firmware is really on the board:

```bash
curl http://<cam-ip>/status     # JSON must contain "ws_connected": false
```

If that field is missing, an older MJPEG build is flashed — re-flash before continuing.

---

## 7 — Keep SSH while the AP runs (ethernet + AP together)

The Pi 4's Wi-Fi radio can be **either** an AP **or** a client on `wlan0`, not both. To keep SSH/internet while `badboyNet` runs, plug **ethernet from the Pi to your home router**:

- `eth0` → home LAN (SSH from your laptop, internet for `apt`/`pip`)
- `wlan0` → `badboyNet` AP (ESP32-CAM connects here)

```bash
ip -4 addr show eth0      # this is your SSH target from your laptop
```

Make the split permanent (NetworkManager leaves `wlan0` alone; AP services start on boot):

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
ip -4 addr show wlan0     # only 192.168.50.1/24
nmcli device status       # wlan0 -> unmanaged, eth0 -> connected
sudo systemctl status hostapd --no-pager
```

Power-cycle the ESP32-CAM so it pulls a fresh lease and prints its new IP.

---

## 8 — Python environment on the Pi

### Pre-flight

```bash
uname -m         # must be aarch64
df -h /tmp       # /tmp is a ~1.9 GB tmpfs — installs route around it below
```

### System libraries

```bash
sudo apt install -y python3-pip python3-venv \
                    libgl1 libglib2.0-0 libgtk-3-0 \
                    libjpeg-dev libopenblas-dev \
                    cmake build-essential
```

> On Pi OS Trixie, apt may swap `libgtk-3-0` → `libgtk-3-0t64` (harmless). `libatlas-base-dev` is gone on Trixie; OpenBLAS replaces it.

### Virtual environment + detection stack

```bash
python3 -m venv ~/detector-env
source ~/detector-env/bin/activate
pip install --upgrade pip wheel setuptools

mkdir -p ~/pip-tmp
TMPDIR=~/pip-tmp pip install --no-cache-dir \
    "torch==2.6.0" "torchvision==0.21.0" \
    ultralytics onnxruntime opencv-python websocket-client
rm -rf ~/pip-tmp
```

Two non-obvious requirements:

- **`onnxruntime` is needed because the Pi runs the `.onnx` models** (not the `.pt`). ONNX gives roughly a 3× CPU speedup over native PyTorch inference (*Report §2.3.4*), and installing it now keeps the system fully offline at run time.
- **`torch==2.6.0` is pinned on Pi 4.** PyTorch 2.7+ wheels emit ARMv8.2-A instructions the Pi 4's Cortex-A72 can't decode → `Illegal instruction` crashes. (Pi 5's A76 is fine.)

### Verify

```bash
python3 -c "from ultralytics import YOLO; import websocket, cv2, onnxruntime; print('OK')"
python3 -c "import cv2; cv2.namedWindow('t'); cv2.destroyAllWindows(); print('GUI works')"
```

If the GUI test fails with `function is not implemented`, the headless OpenCV slipped in (ultralytics sometimes drags it). Replace it:

```bash
pip uninstall opencv-python opencv-python-headless -y
TMPDIR=~/pip-tmp pip install --no-cache-dir opencv-python
```

---

## 9 — Train the PFM-1 model (Colab)

The Roboflow Universe project publishes the **dataset only** (no weights), and Roboflow's hosted training paywalls the `.pt` download on the free tier. Train on Colab — free GPU, fully owned output. The repo's snippet is [`Detection Model/trainModel.txt`](Detection%20Model/trainModel.txt).

**Roboflow side:** create a free account, **Fork** the [PFM-1 project](https://universe.roboflow.com/university-of-southern-california-zbvtl/yolo8-detection-pfm-1) into your workspace, then **Generate → New Version** with the preprocessing/augmentation used in this project (*Report §3.6*):

- Preprocessing: Auto-Orient **on**, Resize **640×640 (Stretch)**.
- Augmentation: Flip Horizontal · 90° Rotate · Rotation ±15° · Brightness ±20% · Exposure ±20% · Blur ≤1.5 px · Noise ≤2% · **3× output**. (Leave everything else off.)

**Colab** (`Runtime → T4 GPU`):

```python
!pip install ultralytics roboflow

from roboflow import Roboflow
rf      = Roboflow(api_key="YOUR_API_KEY")           # ← your key, not a shared one
project = rf.workspace("YOUR_WORKSPACE").project("YOUR_PROJECT_SLUG")
dataset = project.version(1).download("yolov8")

# Base weights: yolov8n.pt or yolo11n.pt (nano variants for constrained hardware)
!yolo train model=yolo11n.pt data={dataset.location}/data.yaml epochs=80 imgsz=640 device=0
```

~25 min on a T4; expect mAP@50 ≥ 0.85. Then **export to ONNX at each resolution you'll run** (this is what the Pi loads):

```python
from ultralytics import YOLO
from pathlib import Path

PREFIX = "yolov11"          # match your base model: "yolov8" or "yolov11"
m = YOLO('runs/detect/train/weights/best.pt')

for sz in (160, 320, 640):
    out = m.export(format='onnx', imgsz=sz, opset=12)   # returns the path to best.onnx
    Path(out).rename(f'{PREFIX}pfm1_{sz}.onnx')          # → yolov11pfm1_160.onnx, _320, _640

from google.colab import files
for sz in (160, 320, 640):
    files.download(f'{PREFIX}pfm1_{sz}.onnx')            # the three ONNX files the Pi needs
files.download('runs/detect/train/weights/best.pt')     # keep the .pt too
```

> 🔒 **Security note:** Earlier commits of `trainModel.txt` contained a real Roboflow API key (since replaced with a placeholder on all branches). Because the old value is still recoverable from git history, the key was **rotated/revoked in Roboflow** — that is what actually disarms a leaked secret. Rule of thumb: never commit a real key; keep committed code on `YOUR_API_KEY` and paste your real key only into your live Colab session (or a git-ignored `.env`).

---

## 10 — Put the models on the Pi

`detect.py` resolves the model path from two variables:

```python
modelVersion = "yolov11"      # "yolov8" or "yolov11"
image_size   = 320            # 160, 320, or 640
MODEL_PATH = f"/home/badboii/MineModels/{modelVersion}/{modelVersion}pfm1_{image_size}.onnx"
```

So it expects this exact tree on the Pi:

```
/home/badboii/MineModels/
├── yolov8/
│   ├── yolov8pfm1_160.onnx
│   ├── yolov8pfm1_320.onnx
│   └── yolov8pfm1_640.onnx
└── yolov11/
    ├── yolov11pfm1_160.onnx
    ├── yolov11pfm1_320.onnx
    └── yolov11pfm1_640.onnx
```

> ⚠️ **Naming mismatch to fix when copying.** The files in `Detection Model/` are named `yolo8pfm1_320.onnx` / `yolo11pfm1_320.onnx` (no `v`), but `detect.py` looks for `yolov8…` / `yolov11…` inside `yolov8/` / `yolov11/` folders. **Rename on copy** so they match, e.g.:

```bash
# from your laptop, using the Pi's eth0 IP from §7
ssh badboii@<eth0-ip> 'mkdir -p ~/MineModels/yolov8 ~/MineModels/yolov11'

scp "Detection Model/yolo8pfm1_320.onnx"  badboii@<eth0-ip>:~/MineModels/yolov8/yolov8pfm1_320.onnx
scp "Detection Model/yolo11pfm1_320.onnx" badboii@<eth0-ip>:~/MineModels/yolov11/yolov11pfm1_320.onnx
# repeat for 160 and 640 as needed
```

> Windows `cmd` doesn't expand `~`; `cd` into the folder and use bare filenames. On Windows PowerShell, `scp` works the same as above.

### Which variant should I run?

From the experiments (*Report §4.1*), on Pi 4 hardware:

| Resolution | Measured FPS | Verdict |
|---|---|---|
| 160×160 | 11–12.5 | Fast but **blind beyond ~1 m** — close-up only |
| **320×320** | **6.5–7** | ✅ **Sweet spot** — reliable to ~2.5 m, meets the ≥6 FPS bar |
| 640×640 | ~2 | Best confidence (YOLOv11 peaked at 0.85) but **too slow** — drops frames, misses passes |

- **Default to 320×320.** It's the configuration the project settled on.
- YOLOv8 and YOLOv11 nano perform almost identically; FPS is bottlenecked by the **Pi's CPU**, not the model — so resolution, not model choice, is the lever.
- YOLOv11-Large (enlarged-dataset weights) is the only variant that detected anything at 3.5 m, but inconsistently and at low confidence; it isn't worth the cost on this hardware. It needs its own `~/MineModels/yolov11large/` folder if you want to try it.

---

## 11 — Run the detector

Edit the config block at the top of [`RaspberryPi-Config/detect.py`](RaspberryPi-Config/detect.py) to match your camera IP and chosen variant:

```python
modelVersion = "yolov11"          # "yolov8" | "yolov11"
image_size   = 320                # 160 | 320 | 640
CAM_IP       = "192.168.50.18"    # ← your ESP32-CAM IP from §6
CONFIDENCE   = 0.30               # mark a detection at conf > 0.30 (Report §4.1.1)
```

Run it on the Pi:

```bash
source ~/detector-env/bin/activate
python3 ~/RaspberryPi-Config/detect.py
```

A `PFM-1 Detection` window opens on the Pi's HDMI/VNC display, showing boxes, the live FPS, and the object count. The reader thread keeps only the newest frame, so stale frames are dropped automatically and latency stays low. Press **`q`** to quit.

Running over SSH? The window still renders on the Pi's screen:

```bash
source ~/detector-env/bin/activate
export DISPLAY=:0
python3 ~/RaspberryPi-Config/detect.py
```

**Other run modes:**
- `detect_grayscale.py` — decodes frames as grayscale (~10% FPS gain). Note its defaults are `yolov8` @ 160 and `CONFIDENCE = 0.70`; the gain is only meaningful with a grayscale-trained model, which these are not (*Report §4.1.2*).
- `twocamdetect.py` — connects to `CAM_IPS = ["192.168.50.18", "192.168.50.11"]`, using cam 2 as failover. A minimal demonstration of the multi-camera scalability discussed in *Report §3.4*.

> **VNC vs. monitor:** viewing the feed over VNC costs ~10% FPS versus a real HDMI monitor (*Report §4.1.2 (Frame Rate)*). For best numbers, use a screen on the Pi.

---

## 12 — Tune the camera if the stream lags

Seeing `corrupt JPEG data` warnings or boxes lagging behind the target? The camera is shipping more bytes than the Pi can decode. Drop framesize/quality at runtime (no re-flash):

```bash
curl "http://<cam-ip>/control?var=framesize&val=6"   # CIF (400x296)
curl "http://<cam-ip>/control?var=quality&val=20"    # higher number = lower quality
```

Settings persist until the camera is power-cycled. To bake them in, edit `CameraWebServer.ino` and re-flash.

---

## 13 — Auto-start on boot (optional)

```bash
mkdir -p ~/.config/autostart
nano ~/.config/autostart/pfm1-detector.desktop
```

```ini
[Desktop Entry]
Type=Application
Name=PFM-1 Detector
Exec=/home/badboii/detector-env/bin/python3 /home/badboii/RaspberryPi-Config/detect.py
Terminal=true
X-GNOME-Autostart-enabled=true
```

Reboot — the detector opens once the desktop is up.

---

## What the experiments found

A condensed view of the results (full analysis in *Report §4–5*):

- **Detection vs. distance** — At 320×320 the models reliably detected the replica up to ~2.5 m camera height; 160×160 was unusable past ~1 m; beyond 3.5 m only YOLOv11-Large @ 640 caught it, weakly (conf 0.46). Detection was *better in dark than bright* conditions — bright glare off the replica hurt the models, likely a training-data/domain-shift effect.
- **Throughput** — 320×320 ≈ 6.5–7 FPS (usable); 640×640 ≈ 2 FPS (too slow to catch a moving pass); 160×160 ≈ 11–12.5 FPS. The bottleneck is the Pi's CPU, identical across model variants.
- **Network range** — ~50 m on the Pi's built-in radio; ~90–100 m with a USB Wi-Fi dongle; an FPC antenna improved *stability* (max RTT 6418 ms → 2147 ms) more than raw range. The **operator's own body** in the signal path dropped range to ~20 m. Indoors: 15–18 m.
- **Power** — ESP32-CAM ≈ 21–34 h streaming on one 18650; Pi 4 ≈ 4–6 h on the 5000 mAh bank. A solar option was explored but not realized (size/sunlight/scope).
- **False positives vs. negatives** — The models over-detect on same-colour objects. That's *acceptable* here because a human reviews the feed; a false **negative** (a missed mine) is the unacceptable, potentially fatal failure mode (*Report §2.3.3, §5.4*).

---

## Limitations & safety

This is honestly a proof of concept, and it's important to state where it stops:

- **Human-in-the-loop only.** The system supports an operator; it does not replace one. Every detection must be human-verified, and a missed mine is the dominant risk.
- **Standoff gap.** A PFM-1's ~37 g charge implies a recommended safety standoff on the order of **257 m** under standard demolition-safety calculations. The system's ~100 m reach does **not** meet that — a concrete safety gap, not just a performance one (*Report §5.4*).
- **Optical only → surface mines only.** A camera can't see buried mines. Real operational acceptance (e.g. IMAS) would need sensor fusion — GPR, metal detection, thermal — which the architecture *could* accept at the inference layer but doesn't today (*Report §5.4, §6.1*).
- **Not weatherproof.** Rain, heavy obstruction, and tall vegetation degrade or block detection (*Report §6.2*).
- **Security is baseline.** WPA2-PSK only; no device authentication beyond the passphrase. WPA3-SAE, a MAC allowlist + connection cap, and mTLS on the WebSocket are the recommended next steps (*Report §6.3*).
- **It needs a carrier.** On its own it just streams and detects; reaching the hazard at a safe distance assumes mounting on a UAV/UGV, which is out of scope here.

---

## Troubleshooting

| Symptom | Fix |
|---|---|
| `OSError: [Errno 28] No space left on device` during pip install | `/tmp` is a 1.9 GB tmpfs. Use `TMPDIR=~/pip-tmp pip install --no-cache-dir …` |
| `Killed` during pip install | Out of RAM. Add 2 GB swap: `sudo dphys-swapfile swapoff && sudo sed -i 's/^CONF_SWAPSIZE=.*/CONF_SWAPSIZE=2048/' /etc/dphys-swapfile && sudo dphys-swapfile setup && sudo dphys-swapfile swapon` |
| `Package 'libatlas-base-dev' has no installation candidate` | Trixie dropped it; OpenBLAS already covers it — remove it from the apt line |
| Firmware won't compile: `WebSocket support not enabled` | ESP32 board package < 3.0. Update it in Boards Manager |
| `curl /status` has no `"ws_connected"` field | Old MJPEG firmware on the board — re-flash `CameraWebServer.ino` |
| ESP32-CAM IP isn't `.18` | DHCP assigns anywhere in the pool. Use whatever Serial Monitor prints, set it as `CAM_IP` |
| `Illegal instruction` when YOLO runs | PyTorch 2.7+ on Pi 4's A72. Pin `torch==2.6.0` |
| `onnxruntime` / model load error | `onnxruntime` not installed — `pip install onnxruntime` (needed for the `.onnx` models) |
| `FileNotFoundError` on the model path | `~/MineModels/` layout or filename mismatch — see [§10](#10--put-the-models-on-the-pi) (it's `yolov8…`, not `yolo8…`) |
| `cv2.namedWindow: function is not implemented` | `opencv-python-headless` got pulled in. Uninstall both, reinstall `opencv-python` |
| `qt.qpa.xcb: could not connect to display` over SSH | `export DISPLAY=:0`, or run from a terminal on the Pi's desktop |
| `scp: Connection timed out` | Laptop and Pi on different subnets. Use the **eth0** IP from §7 |
| Stream laggy / `corrupt JPEG data` | Lower framesize/quality ([§12](#12--tune-the-camera-if-the-stream-lags)); the reader thread already drops stale frames |
| `badboyNet` gone after reboot | Confirm `hostapd`/`dnsmasq` are enabled (§7) and `wpa_supplicant` is masked |

---

## Credits & licensing

- **Authors:** Joakim Dorph Broager & Malthe Tranberg Ørsted (Roskilde Universitet, F2026).
- **Dataset:** PFM-1 imagery from the University of Southern California via [Roboflow Universe](https://universe.roboflow.com/university-of-southern-california-zbvtl/yolo8-detection-pfm-1).
- **Models:** [Ultralytics YOLOv8 / YOLO11](https://github.com/ultralytics/ultralytics).
- The 3D-printed PFM-1 replica used for testing contains no explosive or regulated components and was produced after formal legal clearance (*Report Appendix 1*).
```
