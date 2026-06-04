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


@app.route('/sync_s3', methods=['POST'])
def sync_s3():
    global DATA_DIR
    if not DATA_DIR:
        return jsonify({"status": "error", "message": "No active local workspace directory selected."}), 400
        
    payload = request.json
    bucket_name = payload.get('bucket', '').strip()
    prefix = payload.get('prefix', '').strip()
    
    if not bucket_name:
        return jsonify({"status": "error", "message": "Target S3 Bucket Name is required."}), 400

    try:
        s3_client = boto3.client('s3')
        
        local_json_path = os.path.join(DATA_DIR, 'annotations.json')
        local_data = {}
        if os.path.exists(local_json_path):
            with open(local_json_path, 'r') as file:
                try:
                    local_data = json.load(file)
                except Exception:
                    pass

        s3_annotations_key = os.path.join(prefix, 'annotations.json') if prefix else 'annotations.json'
        s3_annotations_key = s3_annotations_key.replace('\\', '/')
        
        s3_data = {}
        try:
            s3_obj = s3_client.get_object(Bucket=bucket_name, Key=s3_annotations_key)
            s3_data = json.loads(s3_obj['Body'].read().decode('utf-8'))
        except ClientError as e:
            if e.response['Error']['Code'] != 'NoSuchKey':
                raise e

        merged_data = s3_data.copy()
        
        for filename, local_record in local_data.items():
            if filename in merged_data:
                try:
                    local_time = datetime.strptime(local_record.get('date', '1970-01-01 00:00:00'), "%Y-%m-%d %H:%M:%S")
                    s3_time = datetime.strptime(merged_data[filename].get('date', '1970-01-01 00:00:00'), "%Y-%m-%d %H:%M:%S")
                    
                    if local_time >= s3_time:
                        merged_data[filename] = local_record
                except Exception:
                    merged_data[filename] = local_record
            else:
                merged_data[filename] = local_record

        with open(local_json_path, 'w') as file:
            json.dump(merged_data, file, indent=4)

        extensions = ('.jpg', '.jpeg', '.png', '.bmp', '.webp', '.json')
        files_to_sync = [f for f in os.listdir(DATA_DIR) if f.lower().endswith(extensions)]
        
        for filename in files_to_sync:
            local_file_path = os.path.join(DATA_DIR, filename)
            s3_key = os.path.join(prefix, filename) if prefix else filename
            s3_key = s3_key.replace('\\', '/')
            s3_client.upload_file(local_file_path, bucket_name, s3_key)
            
        return jsonify({
            "status": "success", 
            "message": f"Merged tracking data seamlessly. Synced {len(files_to_sync)} files with S3 bucket '{bucket_name}'."
        })
        
    except NoCredentialsError:
        return jsonify({"status": "error", "message": "AWS Authentication profile credentials not located."}), 401
    except Exception as e:
        return jsonify({"status": "error", "message": str(e)}), 500


if __name__ == '__main__':
    app.run(port=5000, debug=True)