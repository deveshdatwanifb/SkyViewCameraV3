import json
import os

def convert_coco_to_yolo(json_path, output_dir, image_dir):
    with open(json_path) as f:
        data = json.load(f)
    images = {img['id']: img for img in data['images']}
    anns_by_image = {img_id: [] for img_id in images}
    for ann in data['annotations']:
        if ann['image_id'] in anns_by_image:
            anns_by_image[ann['image_id']].append(ann)
    os.makedirs(output_dir, exist_ok=True)
    for img_id, anns in anns_by_image.items():
        img = images[img_id]
        if not os.path.exists(os.path.join(image_dir, img['file_name'])):
            continue
        file_name = os.path.splitext(img['file_name'])[0] + '.txt'
        txt_path = os.path.join(output_dir, file_name)
        with open(txt_path, 'w') as text_file:
            for ann in anns:
                img_w, img_h = img['width'], img['height']
                x_min, y_min, bbox_w, bbox_h = ann['bbox']
                x_center = (x_min + (bbox_w / 2.0)) / img_w
                y_center = (y_min + (bbox_h / 2.0)) / img_h
                norm_w = bbox_w / img_w
                norm_h = bbox_h / img_h
                text_file.write(f"0 {x_center:.6f} {y_center:.6f} {norm_w:.6f} {norm_h:.6f}\n")

if __name__ == '__main__':
    target_dir = '/Users/devesh-fyveby/Downloads/aircraft-kp-od-misc-od-2026-06'
    convert_coco_to_yolo('/Users/devesh-fyveby/Downloads/aircraft-kp-od-misc-od-2026-06/annotations.json', target_dir, target_dir)