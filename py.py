# DeepFace-based Face Lock System with ESP32-CAM Integration

import cv2
import numpy as np
import os
import time
import uuid
import shutil
import serial
import pickle
import threading
import requests
from deepface import DeepFace
from sklearn.metrics.pairwise import cosine_similarity

# --- CONFIG ---
DB_PATH = "./registered_faces"
SAVED_NAMES_PATH = os.path.join(DB_PATH, "saved_names")
IMG_PATH = "./image"
WINDOW_WIDTH, WINDOW_HEIGHT = 1200, 600
FONT = cv2.FONT_HERSHEY_SIMPLEX
UPDATE_INTERVAL = 1
REGISTRATION_DURATION = 7
CONFIDENCE_THRESHOLD = 0.4
MIN_FACE_SIZE = (80, 80)
MAX_REGISTRATION_IMAGES = 10
MIN_REGISTRATION_FACES = 3
REGISTRATION_FEEDBACK_DELAY = 2
SERIAL_PORT = "COM4"
BAUD_RATE = 115200
ESP32_IP = "192.168.4.1"
ESP32_STREAM_URL = f"http://{ESP32_IP}/stream"
ESP32_CAPTURE_URL = f"http://{ESP32_IP}/capture"
ESP32_TIMEOUT = 5

# Load images with error handling
def load_image_safe(path, default_size=(150, 150)):
    try:
        img = cv2.imread(path, cv2.IMREAD_UNCHANGED)
        if img is not None:
            return img
        else:
            # Create a default placeholder image if file doesn't exist
            placeholder = np.ones((*default_size, 3), dtype=np.uint8) * 128
            cv2.putText(placeholder, "IMG", (20, 80), FONT, 1, (255, 255, 255), 2)
            return placeholder
    except Exception as e:
        print(f"⚠️ Error loading image {path}: {e}")
        placeholder = np.ones((*default_size, 3), dtype=np.uint8) * 128
        cv2.putText(placeholder, "IMG", (20, 80), FONT, 1, (255, 255, 255), 2)
        return placeholder

# Load images
img_locked_top = load_image_safe(os.path.join(IMG_PATH, "door_locked.png"))
img_unlocked_top = load_image_safe(os.path.join(IMG_PATH, "door_unlocked.png"))
img_locked_bottom = load_image_safe(os.path.join(IMG_PATH, "lock.png"))
img_unlocked_bottom = load_image_safe(os.path.join(IMG_PATH, "unlock.png"))

os.makedirs(DB_PATH, exist_ok=True)
os.makedirs(SAVED_NAMES_PATH, exist_ok=True)

# Initialize face cascade
face_cascade = cv2.CascadeClassifier(cv2.data.haarcascades + 'haarcascade_frontalface_default.xml')
if face_cascade.empty():
    print("❌ Failed to load Haar cascade")
    exit()

# Global variables
arduino = None
status_text = "System Ready"
is_unlocked = False
person_counter = 1
name_map = {}
button_clicked = None
last_lock_state = None
vgg_model = None
esp32_connected = False
registration_in_progress = False

def init_serial():
    """Initialize serial connection to ESP32"""
    global arduino
    try:
        arduino = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)
        time.sleep(2)
        # Send ping to check connection
        arduino.write(b"ping\n")
        time.sleep(0.5)
        response = arduino.readline().decode().strip()
        if "READY" in response:
            print("✅ ESP32 Serial connection established")
            return True
        else:
            print("⚠️ ESP32 not responding properly")
            return False
    except Exception as e:
        print(f"⚠️ Serial connection failed: {e}")
        arduino = None
        return False

def check_esp32_connection():
    """Check if ESP32-CAM is accessible via HTTP"""
    global esp32_connected
    try:
        response = requests.get(f"http://{ESP32_IP}/capture", timeout=ESP32_TIMEOUT)
        esp32_connected = response.status_code == 200
        return esp32_connected
    except:
        esp32_connected = False
        return False

def load_vggface_model():
    """Load VGG-Face model with error handling"""
    global vgg_model
    try:
        print("🔄 Loading VGG-Face model...")
        # Test with a dummy image to ensure model loads properly
        dummy_img = np.ones((224, 224, 3), dtype=np.uint8) * 128
        result = DeepFace.represent(dummy_img, model_name="VGG-Face", enforce_detection=False)
        vgg_model = True
        print("✅ VGG-Face model loaded successfully")
        return True
    except Exception as e:
        print(f"❌ Failed to load VGG-Face: {e}")
        vgg_model = False
        return False

def extract_face_embedding(img):
    """Extract face embedding using DeepFace"""
    try:
        if img is None or img.size == 0:
            return None
        
        # Ensure minimum size
        if img.shape[0] < 80 or img.shape[1] < 80:
            img = cv2.resize(img, (80, 80))
        
        result = DeepFace.represent(
            img_path=img,
            model_name="VGG-Face",
            enforce_detection=False
        )
        return result[0]["embedding"]
    except Exception as e:
        print(f"⚠️ Embedding extraction error: {e}")
        return None

class ESP32Camera:
    """Handle ESP32-CAM streaming"""
    def __init__(self):
        self.cap = None
        self.connected = False
        self.frame = None
        self.thread = None
        self.running = False
    
    def connect(self):
        """Connect to ESP32-CAM stream"""
        try:
            self.cap = cv2.VideoCapture(ESP32_STREAM_URL)
            self.cap.set(cv2.CAP_PROP_BUFFERSIZE, 1)  # Minimize buffer for real-time
            
            # Test connection
            ret, frame = self.cap.read()
            if ret and frame is not None:
                self.connected = True
                self.running = True
                self.thread = threading.Thread(target=self._stream_reader, daemon=True)
                self.thread.start()
                print("✅ ESP32-CAM stream connected")
                return True
            else:
                self.cap.release()
                self.connected = False
                return False
        except Exception as e:
            print(f"⚠️ ESP32-CAM connection failed: {e}")
            self.connected = False
            return False
    
    def _stream_reader(self):
        """Background thread to read frames"""
        while self.running and self.cap and self.cap.isOpened():
            try:
                ret, frame = self.cap.read()
                if ret:
                    self.frame = frame
                else:
                    time.sleep(0.1)
            except:
                time.sleep(0.1)
    
    def read(self):
        """Get latest frame"""
        if self.connected and self.frame is not None:
            return True, self.frame.copy()
        return False, None
    
    def release(self):
        """Release resources"""
        self.running = False
        if self.thread:
            self.thread.join(timeout=1)
        if self.cap:
            self.cap.release()
        self.connected = False

def try_camera_sources():
    """Try ESP32-CAM first, fallback to internal camera"""
    global esp32_connected
    
    # Try ESP32-CAM first
    if check_esp32_connection():
        esp32_cam = ESP32Camera()
        if esp32_cam.connect():
            print("✅ Using ESP32-CAM stream")
            return esp32_cam
    
    # Fallback to internal camera
    print("⚠️ ESP32-CAM not available, using internal camera")
    cap = cv2.VideoCapture(0)
    if cap.isOpened():
        cap.set(cv2.CAP_PROP_BUFFERSIZE, 1)
        print("✅ Using internal webcam")
        return cap
    else:
        print("❌ No camera available")
        return None

def is_valid_face(img):
    """Check if face image is valid"""
    if img is None or img.size == 0:
        return False
    
    # Check minimum size
    if img.shape[0] < 50 or img.shape[1] < 50:
        return False
    
    # Check variance to avoid blank images
    variance = np.var(img)
    return 10 < variance < 50000

def overlay_image(bg, img, x, y, max_size=(150, 150)):
    """Safely overlay an image with bounds checking"""
    try:
        if img is None or bg is None:
            return
        
        bg_h, bg_w = bg.shape[:2]
        
        # Ensure coordinates are within bounds
        if x >= bg_w or y >= bg_h or x < 0 or y < 0:
            return
        
        # Resize image to fit
        img_h, img_w = img.shape[:2]
        new_w = min(max_size[0], bg_w - x, img_w)
        new_h = min(max_size[1], bg_h - y, img_h)
        
        if new_w <= 0 or new_h <= 0:
            return
        
        img_resized = cv2.resize(img, (new_w, new_h))
        
        # Handle transparency
        if img_resized.shape[2] == 4:
            alpha = img_resized[:, :, 3] / 255.0
            for c in range(3):
                bg[y:y+new_h, x:x+new_w, c] = (
                    alpha * img_resized[:, :, c] + 
                    (1 - alpha) * bg[y:y+new_h, x:x+new_w, c]
                )
        else:
            bg[y:y+new_h, x:x+new_w] = img_resized
            
    except Exception as e:
        print(f"⚠️ Overlay error: {e}")

def draw_layout(frame, status, is_unlocked, face_box=None, confidence=None):
    """Draw the GUI layout with improved error handling"""
    try:
        gui = np.ones((WINDOW_HEIGHT, WINDOW_WIDTH, 3), dtype=np.uint8) * 240
        
        # Draw title and status
        state_text = "UNLOCKED" if is_unlocked else "LOCKED"
        color = (0, 150, 0) if is_unlocked else (0, 0, 150)
        cv2.putText(gui, state_text, (80, 40), FONT, 1.2, color, 3)
        
        # Overlay status images
        overlay_image(gui, img_unlocked_top if is_unlocked else img_locked_top, 75, 70)
        
        # Status text
        cv2.putText(gui, "Status:", (80, 260), FONT, 0.8, (0, 0, 0), 2)
        
        # Wrap status text
        status_lines = [status[i:i+35] for i in range(0, len(status), 35)]
        for i, line in enumerate(status_lines[:3]):  # Limit to 3 lines
            cv2.putText(gui, line, (30, 290 + i*25), FONT, 0.6, (0, 0, 0), 2)
        
        # Bottom status image
        overlay_image(gui, img_unlocked_bottom if is_unlocked else img_locked_bottom, 80, 380)
        
        # Camera feed
        if frame is not None:
            try:
                frame_resized = cv2.resize(frame, (480, 360))
                gui[80:440, 360:840] = frame_resized
                
                # Draw face detection box
                if face_box and confidence:
                    x, y, w, h = face_box
                    # Scale coordinates to resized frame
                    scale_x = 480 / frame.shape[1]
                    scale_y = 360 / frame.shape[0]
                    
                    scaled_x = int(x * scale_x)
                    scaled_y = int(y * scale_y)
                    scaled_w = int(w * scale_x)
                    scaled_h = int(h * scale_y)
                    
                    box_color = (0, 255, 0) if confidence > 40 else (0, 165, 255)
                    cv2.rectangle(gui, 
                                (360 + scaled_x, 80 + scaled_y), 
                                (360 + scaled_x + scaled_w, 80 + scaled_y + scaled_h), 
                                box_color, 2)
                    
                    # Confidence text
                    cv2.putText(gui, f"Confidence: {confidence:.1f}%", 
                              (450, 460), FONT, 0.7, (255, 255, 255), 2)
            except Exception as e:
                print(f"⚠️ Frame processing error: {e}")
                # Draw placeholder
                cv2.rectangle(gui, (360, 80), (840, 440), (128, 128, 128), -1)
                cv2.putText(gui, "Camera Error", (500, 260), FONT, 1, (255, 255, 255), 2)
        
        # Draw buttons
        buttons = [
            ("Saved Faces", 50, (70, 70, 70)),
            ("Register Face", 120, (0, 100, 200) if not registration_in_progress else (200, 100, 0)),
            ("Reset System", 190, (150, 50, 50)),
            ("Exit Program", 500, (100, 100, 100))
        ]
        
        for text, y, color in buttons:
            cv2.rectangle(gui, (950, y), (1150, y+50), color, -1)
            cv2.rectangle(gui, (950, y), (1150, y+50), (0, 0, 0), 2)
            cv2.putText(gui, text, (965, y+32), FONT, 0.7, (255, 255, 255), 2)
        
        # Connection status
        conn_text = "ESP32: Connected" if esp32_connected else "ESP32: Disconnected"
        conn_color = (0, 150, 0) if esp32_connected else (0, 0, 150)
        cv2.putText(gui, conn_text, (950, 20), FONT, 0.5, conn_color, 1)
        
        return gui
        
    except Exception as e:
        print(f"⚠️ GUI drawing error: {e}")
        # Return a minimal error GUI
        error_gui = np.ones((WINDOW_HEIGHT, WINDOW_WIDTH, 3), dtype=np.uint8) * 128
        cv2.putText(error_gui, "GUI Error", (400, 300), FONT, 2, (255, 255, 255), 3)
        return error_gui

def send_serial_command(command):
    """Send command to ESP32 with confirmation"""
    global arduino
    if arduino:
        try:
            arduino.write((command + "\n").encode())
            time.sleep(0.1)
            
            # Wait for confirmation
            start_time = time.time()
            while time.time() - start_time < 1:
                if arduino.in_waiting:
                    response = arduino.readline().decode().strip()
                    print(f"ESP32 Response: {response}")
                    return True
                time.sleep(0.1)
            return False
        except Exception as e:
            print(f"⚠️ Serial command error: {e}")
            return False
    return False

def get_face_count():
    """Get number of registered faces"""
    try:
        return len([d for d in os.listdir(SAVED_NAMES_PATH) 
                   if os.path.isdir(os.path.join(SAVED_NAMES_PATH, d))])
    except:
        return 0

def register_face(cap):
    """Register a new face with improved feedback"""
    global person_counter, name_map, status_text, registration_in_progress
    
    registration_in_progress = True
    faces = []
    embeddings = []
    user_id = f"Person_{person_counter}"
    user_dir = os.path.join(SAVED_NAMES_PATH, f"person_{person_counter}")
    
    try:
        os.makedirs(user_dir, exist_ok=True)
        start_time = time.time()
        last_capture = 0
        
        print(f"Starting registration for {user_id}")
        
        while (time.time() - start_time < REGISTRATION_DURATION and 
               len(faces) < MAX_REGISTRATION_IMAGES):
            
            ret, frame = cap.read()
            if not ret:
                continue
            
            # Detect faces
            faces_detected = face_cascade.detectMultiScale(
                frame, scaleFactor=1.1, minNeighbors=5, minSize=MIN_FACE_SIZE
            )
            
            # Only process if exactly one face is detected
            if len(faces_detected) == 1:
                x, y, w, h = faces_detected[0]
                face_img = frame[y:y+h, x:x+w]
                
                if is_valid_face(face_img) and time.time() - last_capture > 0.5:
                    emb = extract_face_embedding(face_img)
                    if emb is not None:
                        # Save face image
                        face_path = os.path.join(user_dir, f"{uuid.uuid4()}.jpg")
                        cv2.imwrite(face_path, face_img)
                        
                        embeddings.append(emb)
                        faces.append(face_img)
                        last_capture = time.time()
                        
                        print(f"Captured face {len(faces)}/{MAX_REGISTRATION_IMAGES}")
                        status_text = f"Capturing... {len(faces)}/{MAX_REGISTRATION_IMAGES}"
            
            # Show registration progress
            progress_frame = frame.copy()
            remaining_time = int(REGISTRATION_DURATION - (time.time() - start_time))
            
            # Draw progress info
            cv2.putText(progress_frame, f"Registering: {len(faces)}/{MAX_REGISTRATION_IMAGES}", 
                       (10, 30), FONT, 0.7, (0, 255, 0), 2)
            cv2.putText(progress_frame, f"Time left: {remaining_time}s", 
                       (10, 60), FONT, 0.7, (0, 255, 0), 2)
            
            # Draw face boxes
            for (x, y, w, h) in faces_detected:
                cv2.rectangle(progress_frame, (x, y), (x+w, y+h), (0, 255, 0), 2)
            
            cv2.imshow("Registering Face - Press ESC to cancel", progress_frame)
            
            key = cv2.waitKey(1) & 0xFF
            if key == 27:  # ESC key
                break
        
        cv2.destroyWindow("Registering Face - Press ESC to cancel")
        
        # Check if we have enough faces
        if len(faces) >= MIN_REGISTRATION_FACES:
            try:
                # Save embeddings
                with open(os.path.join(user_dir, "embeddings.pkl"), "wb") as f:
                    pickle.dump(embeddings, f)
                
                name_map[person_counter] = user_id
                person_counter += 1
                
                status_text = f"Successfully registered {user_id}"
                print(f"✅ Registration successful: {user_id}")
                send_serial_command("unlock")
                
                registration_in_progress = False
                return user_id
                
            except Exception as e:
                print(f"❌ Error saving registration: {e}")
                shutil.rmtree(user_dir, ignore_errors=True)
                status_text = "Registration failed - save error"
        else:
            shutil.rmtree(user_dir, ignore_errors=True)
            status_text = f"Registration failed - only {len(faces)} faces captured"
            print(f"❌ Registration failed: insufficient faces ({len(faces)})")
        
    except Exception as e:
        print(f"❌ Registration error: {e}")
        shutil.rmtree(user_dir, ignore_errors=True)
        status_text = "Registration failed - error occurred"
    
    finally:
        registration_in_progress = False
    
    return None

def verify_face(frame):
    """Verify face against registered faces"""
    global status_text
    
    try:
        faces = face_cascade.detectMultiScale(
            frame, scaleFactor=1.1, minNeighbors=5, minSize=MIN_FACE_SIZE
        )
        
        if len(faces) != 1:
            status_text = "Please show only one face clearly"
            return False, None, None
        
        x, y, w, h = faces[0]
        face_img = frame[y:y+h, x:x+w]
        
        if not is_valid_face(face_img):
            status_text = "Face not clear enough"
            return False, None, None
        
        test_emb = extract_face_embedding(face_img)
        if test_emb is None:
            status_text = "Could not process face"
            return False, (x, y, w, h), 0
        
        best_score = 0
        best_id = None
        
        # Compare with all registered faces
        for folder in os.listdir(SAVED_NAMES_PATH):
            folder_path = os.path.join(SAVED_NAMES_PATH, folder)
            if not os.path.isdir(folder_path):
                continue
                
            emb_path = os.path.join(folder_path, "embeddings.pkl")
            if os.path.exists(emb_path):
                try:
                    with open(emb_path, "rb") as f:
                        saved_embs = pickle.load(f)
                        
                    for emb in saved_embs:
                        score = cosine_similarity([test_emb], [emb])[0][0]
                        if score > best_score:
                            best_score = score
                            best_id = folder
                except Exception as e:
                    print(f"⚠️ Error loading embeddings for {folder}: {e}")
        
        confidence_percent = best_score * 100
        
        if best_score > CONFIDENCE_THRESHOLD:
            status_text = f"Welcome {best_id}! ({confidence_percent:.1f}%)"
            return True, (x, y, w, h), confidence_percent
        else:
            status_text = f"Access denied ({confidence_percent:.1f}%)"
            return False, (x, y, w, h), confidence_percent
            
    except Exception as e:
        print(f"⚠️ Face verification error: {e}")
        status_text = "Verification error occurred"
        return False, None, None

def is_button_clicked(x, y):
    """Check if a button was clicked"""
    if 950 <= x <= 1150:
        if 50 <= y <= 100:
            return "saved_faces"
        elif 120 <= y <= 170:
            return "register_face"
        elif 190 <= y <= 240:
            return "reset"
        elif 500 <= y <= 550:
            return "quit"
    return None

def main():
    """Main function"""
    global is_unlocked, status_text, person_counter, name_map, button_clicked, last_lock_state
    
    print("🚀 Starting Face Lock System...")
    
    # Initialize components
    if not load_vggface_model():
        print("❌ Cannot continue without VGG-Face model")
        return
    
    # Initialize serial connection
    serial_connected = init_serial()
    if serial_connected:
        send_serial_command("ping")
    
    # Initialize camera
    cap = try_camera_sources()
    if not cap:
        print("❌ No camera available")
        return
    
    def on_mouse(event, x, y, flags, param):
        global is_unlocked, status_text, person_counter, name_map, button_clicked, registration_in_progress
        
        if event == cv2.EVENT_LBUTTONDOWN and not registration_in_progress:
            button_clicked = is_button_clicked(x, y)
            
            if button_clicked == "saved_faces":
                count = get_face_count()
                status_text = f"Total registered faces: {count}"
                
            elif button_clicked == "register_face":
                threading.Thread(target=lambda: register_face(cap), daemon=True).start()
                
            elif button_clicked == "reset":
                try:
                    shutil.rmtree(SAVED_NAMES_PATH, ignore_errors=True)
                    os.makedirs(SAVED_NAMES_PATH, exist_ok=True)
                    person_counter = 1
                    name_map.clear()
                    is_unlocked = False
                    status_text = "System reset completed"
                    send_serial_command("reset")
                    print("✅ System reset completed")
                except Exception as e:
                    status_text = f"Reset failed: {e}"
                    print(f"❌ Reset failed: {e}")
                    
            elif button_clicked == "quit":
                print("👋 Shutting down...")
                send_serial_command("lock")
                if arduino:
                    arduino.close()
                if hasattr(cap, 'release'):
                    cap.release()
                cv2.destroyAllWindows()
                exit()

    cv2.namedWindow("Face Lock System", cv2.WINDOW_NORMAL)
    cv2.resizeWindow("Face Lock System", WINDOW_WIDTH, WINDOW_HEIGHT)
    cv2.setMouseCallback("Face Lock System", on_mouse)
    
    last_update_time = 0
    face_box = None
    confidence = None
    
    print("✅ Face Lock System ready!")
    
    try:
        while True:
            ret, frame = cap.read()
            if not ret:
                continue
            
            # Update face verification periodically
            current_time = time.time()
            if current_time - last_update_time > UPDATE_INTERVAL and not registration_in_progress:
                is_unlocked, face_box, confidence = verify_face(frame)
                
                # Send command to ESP32 if state changed
                if last_lock_state != is_unlocked:
                    command = "lock_off" if is_unlocked else "lock_on"
                    send_serial_command(command)
                    last_lock_state = is_unlocked
                
                last_update_time = current_time
                button_clicked = None
            
            # Draw and display GUI
            gui = draw_layout(frame, status_text, is_unlocked, face_box, confidence)
            cv2.imshow("Face Lock System", gui)
            
            # Handle key press
            key = cv2.waitKey(1) & 0xFF
            if key == 27:  # ESC key
                break
                
    except KeyboardInterrupt:
        print("\n⚠️ Interrupted by user")
    except Exception as e:
        print(f"❌ Unexpected error: {e}")
    finally:
        print("🔒 Locking system...")
        send_serial_command("lock_on")
        if arduino:
            arduino.close()
        if hasattr(cap, 'release'):
            cap.release()
        cv2.destroyAllWindows()
        print("✅ Cleanup completed")

if __name__ == "__main__":
    main()
