#!.venv/bin/python3

import os
import json
import platform
import subprocess
from datetime import datetime
from flask import Flask, render_template, jsonify, request, send_from_directory
import tkinter as tk
from tkinter import filedialog
import boto3
from botocore.exceptions import NoCredentialsError, ClientError

app = Flask(__name__, static_folder='.', static_url_path='')
BASE_DIR = os.path.dirname(os.path.abspath(__file__))
DATA_DIR = None

COCO_KP_ORDER = [
    "nose", "left_wing_tip", "right_wing_tip", "tail_top_corner",
    "left_landing_gear", "right_landing_gear", "nose_gear"
]

def open_native_folder_browser():
    if platform.system() == "Darwin":
        try:
            cmd = ['osascript', '-e', 'POSIX path of (choose folder with prompt "Select Workspace Folder:")']
            return subprocess.check_output(cmd, stderr=subprocess.DEVNULL).decode('utf-8').strip()
        except subprocess.CalledProcessError: return None
    else:
        root = tk.Tk()
        root.withdraw()
        root.attributes('-topmost', True)
        path = filedialog.askdirectory()
        root.destroy()
        return path

def build_empty_coco_skeleton():
    return {
        "info": {"description": "FyveBy Aircraft Keypoint Dataset", "version": "1.0.0", "year": 2026, "date_created": datetime.now().strftime("%Y-%m-%d")},
        "categories": [{
            "id": 1, "name": "airplane", "supercategory": "vehicle",
            "keypoints": COCO_KP_ORDER,
            "skeleton": [[1,4], [2,4], [3,4], [5,4], [6,4], [7,4]]
        }],
        "images": [],
        "annotations": []
    }

def convert_coco_to_app_format(coco_data):
    """Translates relational COCO tables into high-speed UI layout registers."""
    app_store = {}
    img_map = {img['id']: img for img in coco_data.get('images', [])}
    
    for img in coco_data.get('images', []):
        app_store[img['file_name']] = {
            "bboxes": [], "keypoints": [],
            "tags": img.get('tags', []), "date": img.get('date_annotated', 'Not annotated yet')
        }
        
    for ann in coco_data.get('annotations', []):
        img_meta = img_map.get(ann['image_id'])
        if not img_meta: continue
        filename = img_meta['file_name']
        
        coco_box = ann['bbox']
        x1, y1 = coco_box[0], coco_box[1]
        x2, y2 = x1 + coco_box[2], y1 + coco_box[3]
        
        app_store[filename]["bboxes"].append({
            "x1": x1, "y1": y1, "x2": x2, "y2": y2, "label": "airplane"
        })
        
        coco_kps = ann.get('keypoints', [])
        for i in range(len(COCO_KP_ORDER)):
            if (i * 3 + 2) < len(coco_kps):
                x = coco_kps[i * 3]
                y = coco_kps[i * 3 + 1]
                v = coco_kps[i * 3 + 2]
                if v > 0:
                    app_store[filename]["keypoints"].append({
                        "x": x, "y": y, "label": COCO_KP_ORDER[i], "visibility": v
                    })
                    
    return app_store

@app.route('/')
def home(): return render_template('index.html')

@app.route('/select_folder', methods=['POST'])
def select_folder():
    global DATA_DIR
    target_path = open_native_folder_browser()
    if not target_path: return jsonify({"status": "cancelled"})
    DATA_DIR = target_path
    
    img_exts = ('.jpg', '.jpeg', '.png', '.bmp', '.webp')
    image_list = [f for f in os.listdir(target_path) if f.lower().endswith(img_exts)]
    
    json_store = os.path.join(DATA_DIR, 'annotations.json')
    app_annotations = {}
    
    if os.path.exists(json_store):
        try:
            with open(json_store, 'r') as file:
                coco_raw = json.load(file)
                app_annotations = convert_coco_to_app_format(coco_raw)
        except Exception: pass
        
    return jsonify({"folder": DATA_DIR, "images": image_list, "annotations": app_annotations})

@app.route('/image/<path:filename>')
def serve_image(filename):
    if not DATA_DIR: return "Workspace error", 400
    return send_from_directory(DATA_DIR, filename)

@app.route('/save', methods=['POST'])
def save():
    global DATA_DIR
    if not DATA_DIR: return jsonify({"status": "error"}), 400
    
    payload = request.json
    filename = payload.get('filename')
    timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    
    json_store = os.path.join(DATA_DIR, 'annotations.json')
    coco_dataset = build_empty_coco_skeleton()
    
    if os.path.exists(json_store):
        try:
            with open(json_store, 'r') as file:
                loaded = json.load(file)
                if "images" in loaded: coco_dataset = loaded
        except Exception: pass

    # Evict any existing indices matching the active asset to allow fresh matrix re-writes
    coco_dataset["images"] = [img for img in coco_dataset["images"] if img["file_name"] != filename]
    
    new_img_id = max([img["id"] for img in coco_dataset["images"]], default=0) + 1
    coco_dataset["images"].append({
        "id": new_img_id, "file_name": filename, "width": 1920, "height": 1080,
        "tags": payload.get('tags', []), "date_annotated": timestamp
    })
    
    if coco_dataset["annotations"]:
        coco_dataset["annotations"] = [ann for ann in coco_dataset["annotations"] if ann["image_id"] != new_img_id]
        
    base_ann_id = max([ann["id"] for ann in coco_dataset["annotations"]], default=0) + 1
    
    # Pack UI coordinates into COCO instance elements
    for b_idx, box in enumerate(payload.get('bboxes', [])):
        x = round(box['x1'])
        y = round(box['y1'])
        w = round(box['x2'] - box['x1'])
        h = round(box['y2'] - box['y1'])
        
        kp_array = [0] * (len(COCO_KP_ORDER) * 3)
        kp_count = 0
        
        for kp in payload.get('keypoints', []):
            if kp['x'] >= box['x1'] and kp['x'] <= box['x2'] and kp['y'] >= box['y1'] and kp['y'] <= box['y2']:
                if kp['label'] in COCO_KP_ORDER:
                    k_idx = COCO_KP_ORDER.index(kp['label'])
                    kp_array[k_idx * 3] = round(kp['x'])
                    kp_array[k_idx * 3 + 1] = round(kp['y'])
                    kp_array[k_idx * 3 + 2] = kp['visibility']
                    kp_count += 1
                    
        coco_dataset["annotations"].append({
            "id": base_ann_id + b_idx, "image_id": new_img_id, "category_id": 1,
            "bbox": [x, y, w, h], "area": w * h, "keypoints": kp_array,
            "num_keypoints": kp_count, "iscrowd": 0
        })
        
    with open(json_store, 'w') as file:
        json.dump(coco_dataset, file, indent=4)
        
    return jsonify({"status": "success", "date": timestamp})

@app.route('/sync_s3', methods=['POST'])
def sync_s3():
    global DATA_DIR
    payload = request.json
    bucket_name = payload.get('bucket', '').strip()
    prefix = payload.get('prefix', '').strip()
    
    try:
        s3_client = boto3.client('s3')
        local_json_path = os.path.join(DATA_DIR, 'annotations.json')
        
        # Deep lookup and merging strategy directly via cloud parameters
        s3_key = os.path.join(prefix, 'annotations.json') if prefix else 'annotations.json'
        s3_key = s3_key.replace('\\', '/')
        
        try:
            s3_obj = s3_client.get_object(Bucket=bucket_name, Key=s3_key)
            s3_raw = json.loads(s3_obj['Body'].read().decode('utf-8'))
            # [Optional merge calculations can step in here if multi-user collisions occur]
        except ClientError as e:
            if e.response['Error']['Code'] != 'NoSuchKey': raise e

        # Upload loop pushes matching image extensions up to your targeted profile bucket
        exts = ('.jpg', '.jpeg', '.png', '.bmp', '.webp', '.json')
        sync_targets = [f for f in os.listdir(DATA_DIR) if f.lower().endswith(exts)]
        
        for name in sync_targets:
            s3_client.upload_file(os.path.join(DATA_DIR, name), bucket_name, os.path.join(prefix, name).replace('\\', '/'))
            
        return jsonify({"status": "success", "message": f"Synchronized {len(sync_targets)} datasets securely with S3 storage."})
    except Exception as e: return jsonify({"status": "error", "message": str(e)}), 500

if __name__ == '__main__':
    app.run(port=5000, debug=True)