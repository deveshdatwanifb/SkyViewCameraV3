import os
import random
import torch
import yaml
from ultralytics import YOLO

def create_splits_and_yaml(image_dir, split_ratio=0.8):
    images = [os.path.join(image_dir, f) for f in os.listdir(image_dir) if f.endswith(('.jpg', '.png', '.jpeg'))]
    random.shuffle(images)
    split_idx = int(len(images) * split_ratio)
    train_imgs = images[:split_idx]
    val_imgs = images[split_idx:]
    with open('train.txt', 'w') as f:
        f.write('\n'.join(train_imgs))
    with open('val.txt', 'w') as f:
        f.write('\n'.join(val_imgs))
    yaml_content = {
        'train': os.path.abspath('train.txt'),
        'val': os.path.abspath('val.txt'),
        'nc': 1,
        'names': ['airplane']
    }
    with open('data.yaml', 'w') as f:
        yaml.dump(yaml_content, f)
    return 'data.yaml'

def main():
    device = 'cuda' if torch.cuda.is_available() else 'cpu'
    yaml_path = create_splits_and_yaml('/Users/devesh-fyveby/Downloads/aircraft-kp-od-misc-od-2026-06')
    model = YOLO('/Users/devesh-fyveby/Downloads/skyview_v2.pt')
    model.train(data=yaml_path, 
                epochs=100, 
                imgsz=640, batch=16, 
                device=device, project='/Users/devesh-fyveby/Downloads/finetune_project', 
                name='yolov26_aircraft_od_v1', save_period=1,
                box=10.0, 
                dfl=2.0,  
                scale=0.2,
                lr0=0.001,
                mosaic=0.5, resume=True)

if __name__ == '__main__':
    main()