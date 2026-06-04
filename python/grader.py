import requests
import time
import json
import os
import socket
import threading
import google.generativeai as genai
from flask import Flask, jsonify
from PIL import Image
from io import BytesIO
from requests.adapters import HTTPAdapter
from urllib3.util.retry import Retry

# ================= CONFIGURATION =================
CAM_HOST = "http://esp32-cam.local"
SCREEN_HOST = "http://esp32-screen.local"
GOOGLE_API_KEY = os.environ.get('GOOGLE_API_KEY')

app = Flask(__name__)

# GEMINI 2.5 FLASH
genai.configure(api_key=GOOGLE_API_KEY)
model = genai.GenerativeModel('models/gemini-2.5-flash') 

# ================= GLOBAL STATE =================
local_ip = "0.0.0.0"
is_scanning = False 

# ================= ROBUST SESSION =================
def get_session():
    """Creates a request session with retries"""
    session = requests.Session()
    retry = Retry(connect=3, backoff_factor=0.5)
    adapter = HTTPAdapter(max_retries=retry)
    session.mount('http://', adapter)
    return session

# ================= HELPERS =================
def get_local_ip():
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        ip = s.getsockname()[0]
        s.close()
        return ip
    except:
        return "127.0.0.1"

def check_device(url):
    """Pings a device to see if it is online"""
    try:
        requests.get(f"{url}/", timeout=2)
        return True
    except:
        return False

def update_screen(data):
    """Sends command to Screen using Hostname"""
    try:
        headers = {'Content-Type': 'text/plain'}
        requests.post(f"{SCREEN_HOST}/update", data=json.dumps(data), headers=headers, timeout=5)
    except Exception as e:
        print(f"⚠️ Screen Update Skipped: {e}")

# ================= BACKGROUND THREAD =================
def dashboard_loop():
    """Updates status only when NOT scanning"""
    print("🔹 Dashboard Thread Started")
    update_screen({"screen": "splash"})
    time.sleep(3)

    while True:
        if is_scanning:
            time.sleep(1)
            continue

        try:
            # Check Status
            cam_online = check_device(CAM_HOST)
            screen_online = check_device(SCREEN_HOST)
            
            payload = {
                "screen": "dashboard",
                "rec_ip": "Online" if screen_online else "Offline",
                "cam_ip": "Online" if cam_online else "Offline",
                "server_ip": local_ip 
            }
            
            if screen_online:
                update_screen(payload)
                time.sleep(5) 
            else:
                time.sleep(2) 

        except Exception as e:
            time.sleep(5)

# ================= FLASK ROUTES =================
@app.route('/start_scan', methods=['GET'])
def start_scan():
    global is_scanning
    if is_scanning: return jsonify({"status": "busy"})

    is_scanning = True 
    print("\n🚀 TRIGGER RECEIVED! Starting Scan...")
    
    session = get_session()
    
    try:
        # 1. Show Loading on Screen
        update_screen({"screen": "loading", "msg": "Capturing...", "sub": "Please Wait"})
        print("📸 Connecting to Camera...")

        url = f"{CAM_HOST}/capture"
        
        # --- ROBUST DOWNLOAD LOOP ---
        MAX_RETRIES = 2
        img = None
        
        for attempt in range(1, MAX_RETRIES + 1):
            try:
                # 120s timeout for safety
                r = session.get(url, stream=True, timeout=(10, 120))
                if r.status_code != 200: raise RuntimeError(f"Camera status {r.status_code}")
                
                print("✅ Connection Established. Downloading...")
                img_data = BytesIO()
                for chunk in r.iter_content(chunk_size=1024):
                    if chunk: img_data.write(chunk)
                
                # Validation
                expected = int(r.headers.get("Content-Length", 0))
                received = img_data.getbuffer().nbytes
                
                if expected and received != expected:
                    raise RuntimeError("Image truncated")
                    
                img_data.seek(0)
                img = Image.open(img_data)
                print("✅ Image download complete")
                break 
                
            except Exception as e:
                print(f"⚠️ Attempt {attempt} failed: {e}")
                if attempt == MAX_RETRIES: raise
                time.sleep(1)

        # --- SAVE IMAGE ---
        if not os.path.exists("scans"): os.makedirs("scans")
        filename = f"scans/scan_{int(time.time())}.jpg"
        img.save(filename)

        # --- AI PROCESS ---
        update_screen({"screen": "loading", "msg": "AI Processing", "sub": "Gemini 2.5..."})
        print("🧠 Sending to Gemini...")
        
        prompt = """
        Analyze this image carefully. You are an AI quiz grader who marks quizzes based on questions and their selected answers. Read each question one by one and look at the 
        provided options and marked answers. Then check the marked answer against your knowledge base and mark correct if the marked answer is correct otherwise mark it as wrong. 
        If multiple answers are selected mark that as wrong. Your job is to check these images based on the criteria mentioned. 

        3. **OUTPUT FORMAT (Strict JSON):**
        {
          "valid": true,
          "total_score": "X/Y",
          "questions": [
            {"q": "1", "opt": "C", "correct": true}, 
            {"q": "2", "opt": "A", "correct": false}
          ]
        }
        
        If the image is blurry, rotated, or not a quiz, return {"valid": false, "reason": "unreadable"}.
        """
        
        response = model.generate_content([prompt, img])
        text = response.text.replace("```json", "").replace("```", "").strip()
        
        try:
            result = json.loads(text)
            
            if not result.get("valid"):
                reason = result.get("reason", "invalid")
                msg = "Invalid Image!" if reason == "invalid" else "Unreadable!"
                update_screen({"screen": "error", "msg": msg, "sub": "Capture Again"})
            else:
                # PAGINATION LOGIC
                all_questions = result["questions"]
                chunk_size = 10 
                
                for i in range(0, len(all_questions), chunk_size):
                    batch = all_questions[i:i + chunk_size]
                    
                    payload = {
                        "screen": "result",
                        "total": result["total_score"],
                        "questions": batch,
                        "page": (i // chunk_size) + 1,
                        "has_more": (i + chunk_size < len(all_questions))
                    }
                    update_screen(payload)
                    
                    if payload["has_more"]: time.sleep(5) 
                        
        except Exception as e:
            print(f"JSON Error: {e}")
            update_screen({"screen": "error", "msg": "AI Error", "sub": "Try Again"})

    except Exception as e:
        print(f"❌ SCAN FAILED: {e}")
        update_screen({"screen": "error", "msg": "Timeout/Error", "sub": "Check WiFi"})
    
    finally:
        is_scanning = False 
        
    return jsonify({"status": "ok"})

# ================= MAIN =================
if __name__ == "__main__":
    local_ip = get_local_ip()
    print(f"--- SYSTEM ONLINE ---")
    print(f"💻 Python Server IP: {local_ip}")
    print(f"🤖 Port: 5000")
    
    t = threading.Thread(target=dashboard_loop)
    t.daemon = True
    t.start()
    
    app.run(host='0.0.0.0', port=5000, debug=False)
