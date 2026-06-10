#!.venv/bin/python3

import os
import json
import platform
import subprocess
import hashlib
from datetime import datetime
from flask import Flask, render_template, jsonify, request, send_from_directory
import tkinter as tk
from tkinter import filedialog
import boto3
from botocore.exceptions import NoCredentialsError, ClientError
from PIL import Image

app = Flask(__name__, static_folder='.', static_url_path='')

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
    app_store = {}
    img_map = {img['id']: img for img in coco_data.get('images', [])}
    for img in coco_data.get('images', []):
        app_store[img['file_name']] = {
            "bboxes": [], "keypoints": [], "tags": img.get('tags', []), "date": img.get('date_annotated', 'Not annotated yet')
        }
    for ann in coco_data.get('annotations', []):
        img_meta = img_map.get(ann['image_id'])
        if not img_meta: continue
        filename = img_meta['file_name']
        x1, y1 = ann['bbox'][0], ann['bbox'][1]
        x2, y2 = x1 + ann['bbox'][2], y1 + ann['bbox'][3]
        app_store[filename]["bboxes"].append({"x1": x1, "y1": y1, "x2": x2, "y2": y2, "label": "airplane"})
        coco_kps = ann.get('keypoints', [])
        for i in range(len(COCO_KP_ORDER)):
            if (i * 3 + 2) < len(coco_kps):
                x, y, v = coco_kps[i * 3], coco_kps[i * 3 + 1], coco_kps[i * 3 + 2]
                if v > 0: app_store[filename]["keypoints"].append({"x": x, "y": y, "label": COCO_KP_ORDER[i], "visibility": v})
    return app_store

def calculate_local_file_md5(file_path):
    hasher = hashlib.md5()
    with open(file_path, 'rb') as f:
        for chunk in iter(lambda: f.read(4096), b''): hasher.update(chunk)
    return f'"{hasher.hexdigest()}"'

def commit_save_to_json(workspace, filename, bboxes, keypoints, tags, img_w, img_h):
    timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    json_store = os.path.join(workspace, 'annotations.json')
    coco_dataset = build_empty_coco_skeleton()
    
    if os.path.exists(json_store):
        try:
            with open(json_store, 'r') as file:
                loaded = json.load(file)
                if "images" in loaded: coco_dataset = loaded
        except Exception: pass

    coco_dataset["images"] = [img for img in coco_dataset["images"] if img["file_name"] != filename]
    new_img_id = max([img["id"] for img in coco_dataset["images"]], default=0) + 1
    
    coco_dataset["images"].append({
        "id": new_img_id, "file_name": filename, "width": int(img_w), "height": int(img_h),
        "tags": tags, "date_annotated": timestamp
    })
    
    if coco_dataset["annotations"]:
        coco_dataset["annotations"] = [ann for ann in coco_dataset["annotations"] if ann["image_id"] != new_img_id]
    base_ann_id = max([ann["id"] for ann in coco_dataset["annotations"]], default=0) + 1
    
    for b_idx, box in enumerate(bboxes):
        x = round(max(0, box['x1']))
        y = round(max(0, box['y1']))
        w = round(box['x2'] - box['x1'])
        h = round(box['y2'] - box['y1'])
        kp_array = [0] * (len(COCO_KP_ORDER) * 3)
        kp_count = 0
        
        for kp in keypoints:
            if kp['x'] >= box['x1'] and kp['x'] <= box['x2'] and kp['y'] >= box['y1'] and kp['y'] <= box['y2']:
                if kp['label'] in COCO_KP_ORDER:
                    k_idx = COCO_KP_ORDER.index(kp['label'])
                    kp_array[k_idx * 3] = round(kp['x'])
                    kp_array[k_idx * 3 + 1] = round(kp['y'])
                    kp_array[k_idx * 3 + 2] = kp['visibility']
                    kp_count += 1
                    
        coco_dataset["annotations"].append({
            "id": base_ann_id + b_idx, "image_id": new_img_id, "category_id": 1,
            "bbox": [x, y, w, h], "area": w * h, "keypoints": kp_array, "num_keypoints": kp_count, "iscrowd": 0
        })
        
    with open(json_store, 'w') as file:
        json.dump(coco_dataset, file, indent=4)
    return timestamp

@app.route('/')
def home():
    return render_template('index.html')

@app.route('/select_folder', methods=['POST'])
def select_folder():
    target_path = open_native_folder_browser()
    if not target_path: return jsonify({"status": "cancelled"})
    image_list = [f for f in os.listdir(target_path) if f.lower().endswith(('.jpg', '.jpeg', '.png', '.bmp', '.webp'))]
    json_store = os.path.join(target_path, 'annotations.json')
    app_annotations = {}
    if os.path.exists(json_store):
        try:
            with open(json_store, 'r') as file: app_annotations = convert_coco_to_app_format(json.load(file))
        except Exception: pass
    return jsonify({"folder": target_path, "images": image_list, "annotations": app_annotations})

@app.route('/image/<path:filename>')
def serve_image(filename):
    workspace = request.args.get('workspace')
    if not workspace or not os.path.exists(workspace): return "Missing space parameter query profile", 400
    return send_from_directory(workspace, filename)

@app.route('/save', methods=['POST'])
def save():
    payload = request.json
    workspace = payload.get('workspace')
    if not workspace: return jsonify({"status": "error", "message": "Missing active workspace token."}), 400
    try:
        timestamp = commit_save_to_json(
            workspace, payload.get('filename'), payload.get('bboxes', []), 
            payload.get('keypoints', []), payload.get('tags', []),
            payload.get('image_width', 1920), payload.get('image_height', 1080)
        )
        return jsonify({"status": "success", "date": timestamp})
    except Exception as e: return jsonify({"status": "error", "message": str(e)}), 500

@app.route('/delete_image', methods=['POST'])
def delete_image():
    payload = request.json
    workspace = payload.get('workspace')
    filename = payload.get('filename')
    target = os.path.join(workspace, filename)
    if os.path.exists(target): os.remove(target)
    return jsonify({"status": "success"})

@app.route('/transform_image', methods=['POST'])
def transform_image():
    payload = request.json
    workspace = payload.get('workspace')
    filename = payload.get('filename')
    action = payload.get('action')
    tags = payload.get('tags', [])
    incoming_bboxes = payload.get('bboxes', [])
    incoming_keypoints = payload.get('keypoints', [])
    
    img_path = os.path.join(workspace, filename)
    try:
        with Image.open(img_path) as img:
            orig_w, orig_h = img.size
            if action == 'crop':
                coords = payload.get('coords')
                x1, y1 = max(0, int(coords[0])), max(0, int(coords[1]))
                x2, y2 = min(orig_w, int(coords[2])), min(orig_h, int(coords[3]))
                crop_w, crop_h = x2 - x1, y2 - y1
                
                for box in incoming_bboxes:
                    bx1, by1, bx2, by2 = box['x1'], box['y1'], box['x2'], box['y2']
                    if not (bx2 <= x1 or bx1 >= x2 or by2 <= y1 or by1 >= y2) and not (bx1 >= x1 and bx2 <= x2 and by1 >= y1 and by2 <= y2):
                        return jsonify({"status": "error", "message": f"Crop Rejected: This custom selection slice splits through the instance box labeled '{box['label']}'."}), 400
                
                img.crop((x1, y1, x2, y2)).save(img_path)
                transformed_bboxes = [{"x1": max(0, b['x1'] - x1), "y1": max(0, b['y1'] - y1), "x2": min(crop_w, b['x2'] - x1), "y2": min(crop_h, b['y2'] - y1), "label": b['label']} for b in incoming_bboxes if min(crop_w, b['x2'] - x1) > max(0, b['x1'] - x1)]
                transformed_keypoints = [{"x": k['x'] - x1, "y": k['y'] - y1, "label": k['label'], "visibility": k['visibility']} for k in incoming_keypoints if 0 <= (k['x'] - x1) <= crop_w and 0 <= (k['y'] - y1) <= crop_h]
                
                timestamp = commit_save_to_json(workspace, filename, transformed_bboxes, transformed_keypoints, tags, crop_w, crop_h)
                return jsonify({"status": "success", "bboxes": transformed_bboxes, "keypoints": transformed_keypoints, "date": timestamp})
                
            elif action == 'resize':
                target_w = int(payload.get('width'))
                ratio = target_w / orig_w
                target_h = int(orig_h * ratio)
                img.resize((target_w, target_h), Image.Resampling.LANCZOS).save(img_path)
                transformed_bboxes = [{"x1": b['x1']*ratio, "y1": b['y1']*ratio, "x2": b['x2']*ratio, "y2": b['y2']*ratio, "label": b['label']} for b in incoming_bboxes]
                transformed_keypoints = [{"x": k['x']*ratio, "y": k['y']*ratio, "label": k['label'], "visibility": k['visibility']} for k in incoming_keypoints]
                
                timestamp = commit_save_to_json(workspace, filename, transformed_bboxes, transformed_keypoints, tags, target_w, target_h)
                return jsonify({"status": "success", "bboxes": transformed_bboxes, "keypoints": transformed_keypoints, "date": timestamp})
    except Exception as e: return jsonify({"status": "error", "message": str(e)}), 500

@app.route('/sync_s3', methods=['POST'])
def sync_s3():
    payload = request.json
    workspace = payload.get('workspace')
    s3_uri = payload.get('s3_uri', '').strip()
    
    if not s3_uri:
        return jsonify({"status": "error", "message": "Missing S3 URI parameter field."}), 400
        
    if s3_uri.lower().startswith('s3://'):
        s3_uri = s3_uri[5:]
        
    uri_parts = s3_uri.split('/', 1)
    bucket_name = uri_parts[0]
    prefix = uri_parts[1].strip() if len(uri_parts) > 1 else ''
    
    if prefix.endswith('/'):
        prefix = prefix[:-1]
        
    try:
        s3_client = boto3.client('s3')
        local_json_path = os.path.join(workspace, 'annotations.json')
        s3_key = os.path.join(prefix, 'annotations.json') if prefix else 'annotations.json'
        s3_key = s3_key.replace('\\', '/')
        
        s3_dataset = build_empty_coco_skeleton()
        try:
            s3_dataset = json.loads(s3_client.get_object(Bucket=bucket_name, Key=s3_key)['Body'].read().decode('utf-8'))
        except ClientError as e:
            if e.response['Error']['Code'] != 'NoSuchKey': raise e
            
        local_dataset = build_empty_coco_skeleton()
        if os.path.exists(local_json_path):
            with open(local_json_path, 'r') as file:
                try: local_dataset = json.load(file)
                except Exception: pass
                
        s3_images = {img["file_name"]: img for img in s3_dataset.get("images", [])}
        s3_annotations = {}
        for ann in s3_dataset.get("annotations", []): s3_annotations.setdefault(ann["image_id"], []).append(ann)
        
        final_images, final_annotations = [], []
        next_img_id, next_ann_id = 1, 1
        local_images = {img["file_name"]: img for img in local_dataset.get("images", [])}
        local_annotations = {}
        img_id_to_filename = {img["id"]: img["file_name"] for img in local_dataset.get("images", [])}
        
        for ann in local_dataset.get("annotations", []):
            fname = img_id_to_filename.get(ann["image_id"])
            if fname: local_annotations.setdefault(fname, []).append(ann)
            
        for fname in set(list(s3_images.keys()) + list(local_images.keys())):
            if fname in local_images:
                img_entry, ann_entries = local_images[fname].copy(), local_annotations.get(fname, [])
            else:
                img_entry = s3_images[fname].copy()
                ann_entries = s3_annotations.get(img_entry["id"], [])
                
            current_img_id = next_img_id
            img_entry["id"] = current_img_id
            final_images.append(img_entry)
            next_img_id += 1
            for ann in ann_entries:
                new_ann = ann.copy()
                new_ann["id"], new_ann["image_id"] = next_ann_id, current_img_id
                final_annotations.append(new_ann)
                next_ann_id += 1
                
        compiled_coco = build_empty_coco_skeleton()
        compiled_coco["images"], compiled_coco["annotations"] = final_images, final_annotations
        with open(local_json_path, 'w') as file: json.dump(compiled_coco, file, indent=4)
        
        local_files = [f for f in os.listdir(workspace) if f.lower().endswith(('.jpg', '.jpeg', '.png', '.bmp', '.webp'))]
        uploaded, skipped = 0, 0
        
        for name in local_files:
            local_path = os.path.join(workspace, name)
            target_key = os.path.join(prefix, name).replace('\\', '/') if prefix else name
            try:
                if s3_client.head_object(Bucket=bucket_name, Key=target_key).get('ETag') == calculate_local_file_md5(local_path):
                    skipped += 1; continue
            except ClientError: pass
            s3_client.upload_file(local_path, bucket_name, target_key)
            uploaded += 1
            
        s3_client.upload_file(local_json_path, bucket_name, s3_key)
        return jsonify({"status": "success", "message": f"Successfully integrated database indices.\n\nUploaded {uploaded} modified assets.\nSkipped {skipped} unchanged image files."})
    except Exception as e: return jsonify({"status": "error", "message": str(e)}), 500

if __name__ == '__main__':
    app.run(port=5000, debug=True)
