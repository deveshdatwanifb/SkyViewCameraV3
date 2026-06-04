#!.venv/bin/python3
import os
import json
import platform
import subprocess
from datetime import datetime
from flask import Flask, render_template, jsonify, request, send_from_directory
import tkinter as tk
from tkinter import filedialog

app = Flask(__name__)

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
DATA_DIR = None

def open_native_folder_browser():
    if platform.system() == "Darwin":
        try:
            cmd = [
                'osascript', 
                '-e', 
                'POSIX path of (choose folder with prompt "Select Workspace Folder:")'
            ]
            output = subprocess.check_output(cmd, stderr=subprocess.DEVNULL)
            return output.decode('utf-8').strip()
        except subprocess.CalledProcessError:
            return None
    else:
        root = tk.Tk()
        root.withdraw()
        root.attributes('-topmost', True)
        path = filedialog.askdirectory()
        root.destroy()
        return path

@app.route('/')
def home():
    return render_template('index.html')


@app.route('/select_folder', methods=['POST'])
def select_folder():
    global DATA_DIR
    target_path = open_native_folder_browser()
    if not target_path:
        return jsonify({"status": "cancelled"})
    
    DATA_DIR = target_path
    extensions = ('.jpg', '.jpeg', '.png', '.bmp', '.webp')
    image_list = [f for f in os.listdir(target_path) if f.lower().endswith(extensions)]
    
    json_store = os.path.join(DATA_DIR, 'annotations.json')
    annotations = {}
    if os.path.exists(json_store):
        try:
            with open(json_store, 'r') as file:
                annotations = json.load(file)
        except Exception:
            pass

    return jsonify({"folder": DATA_DIR, "images": image_list, "annotations": annotations})

@app.route('/image/<path:filename>')
def serve_image(filename):
    if not DATA_DIR:
        return "Workspace uninitialized", 400
    return send_from_directory(DATA_DIR, filename)

@app.route('/save', methods=['POST'])
def save():
    global DATA_DIR
    if not DATA_DIR:
        return jsonify({"status": "error", "message": "Missing active workspace target"}), 400
    
    payload = request.json
    filename = payload.get('filename')
    
    json_store = os.path.join(DATA_DIR, 'annotations.json')
    annotations = {}
    if os.path.exists(json_store):
        try:
            with open(json_store, 'r') as file:
                annotations = json.load(file)
        except Exception:
            pass
            
    timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    annotations[filename] = {
        "bboxes": payload.get('bboxes', []),
        "keypoints": payload.get('keypoints', []),
        "tags": payload.get('tags', []),
        "date": timestamp
    }
    
    with open(json_store, 'w') as file:
        json.dump(annotations, file, indent=4)
        
    return jsonify({"status": "success", "date": timestamp})

if __name__ == '__main__':
    app.run(port=5000, debug=True)