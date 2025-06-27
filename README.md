# Face Lock System with DeepFace, ESP32-CAM & Serial Control

This project is a real-time AI-based face recognition lock system using **DeepFace** for face embedding comparison, integrated with an **ESP32-CAM** stream and an **ESP32 microcontroller** via **serial communication**. It includes a Python GUI, registration and verification system, and lock/unlock status feedback through both the GUI and hardware.

---

## 🔒 Features

- Real-time face recognition using DeepFace (VGG-Face model)
- ESP32-CAM video stream (via `/stream` or local camera fallback)
- Facial registration with face embedding storage (`.pkl`)
- Secure face verification using cosine similarity
- Serial control to ESP32 for:
  - `lock_on`, `lock_off`, `unlock`, `reset`, `ping`
- Visual GUI overlays: locked/unlocked status, confidence % box
- Button-based GUI: Saved Faces, Register, Reset, Quit
- Robust error handling for serial, stream, and detection

---

## 📁 Project Structure

<pre>
face-lock-system/
├── py.py                        # Main Python face lock script
├── ino.ino                      # Arduino sketch for ESP32 lock control
├── image/
│   ├── door_locked.png
│   ├── door_unlocked.png
│   ├── lock.png
│   └── unlock.png
├── registered_faces/
│   └── saved_names/             # Auto-filled with registered face folders
│       └── Person_X/
│           ├── face_XXXX.jpg
│           └── embeddings.pkl
├── requirements.txt
└── README.md
</pre>

---

## ▶️ How to Run (Python GUI)

1. Install dependencies:
```bash
pip install -r requirements.txt
```

2. Connect ESP32-CAM to the same network and update:
   - `ESP32_IP = "192.168.4.1"` (in `py.py`)
   - `SERIAL_PORT = "COM4"` or your actual USB port

3. Run the main script:
```bash
python py.py
```

4. Use buttons:
- **Saved Faces** → shows total registered users
- **Register Face** → records and saves embeddings
- **Reset System** → clears all face data + sends `reset` to ESP32
- **Exit Program** → closes everything safely

---

## ⚙️ Arduino Functionality (`ino.ino`)

- Reads serial commands sent from Python:
  - `lock_on` → Lock state active
  - `lock_off` → Unlock state
  - `unlock` → Used immediately after registration
  - `reset` → Resets indicators
  - `ping` → Sends back `READY`
- Can control:
  - I2C LCD showing LOCKED/UNLOCKED
  - RGB LEDs per status
  - Optional relay or servo for physical locking

---

## 💡 requirements.txt

```txt
opencv-contrib-python
numpy
deepface
requests
pyserial
scikit-learn
```

---

## 📦 .gitignore

```gitignore
__pycache__/
*.pyc
*.log
.DS_Store
registered_faces/
image/*.tmp
```

---

## 📸 GUI Features

- Lock/unlock icons and face box
- Live camera stream (ESP32 or fallback to webcam)
- Live confidence % and identity status
- Clickable GUI buttons for full system control

---

