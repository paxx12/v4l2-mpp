#!/usr/bin/env python3

import sys
import json
import argparse
import numpy as np
import cv2
import time
import socket
import os
from pathlib import Path
from glob import glob

try:
    from rknnlite.api import RKNNLite
except ImportError:
    print("Error: rknnlite not installed. Run: pip install rknnlite2")
    sys.exit(1)

debug_mode = False

def log(msg):
    print(f"{msg}", flush=True, file=sys.stderr)

def debug(msg):
    if debug_mode:
        print(f"[DEBUG] {msg}", flush=True, file=sys.stderr)

def load_labels(labels_path):
    with open(labels_path, 'r') as f:
        return [line.strip() for line in f.readlines() if line.strip()]

def load_image(image_path):
    img = cv2.imread(image_path)
    if img is None:
        raise ValueError(f"Failed to load image: {image_path}")

    original_h, original_w = img.shape[:2]
    debug(f"Original image size: {original_w}x{original_h}")

    return img

def resize_image(img, target_size):
    original_h, original_w = img.shape[:2]
    target_w, target_h = target_size

    scale = min(target_w / original_w, target_h / original_h)
    new_w = int(original_w * scale)
    new_h = int(original_h * scale)

    debug(f"Letterbox resize: scale={scale:.6f}, new_size={new_w}x{new_h}")

    img_scaled = cv2.resize(img, (new_w, new_h), interpolation=cv2.INTER_LINEAR)

    canvas = np.full((target_h, target_w, 3), 0x72, dtype=np.uint8)

    offset_x = (target_w - new_w) // 2
    offset_y = (target_h - new_h) // 2

    debug(f"Letterbox padding: offset=({offset_x}, {offset_y}), color=0x72")

    canvas[offset_y:offset_y+new_h, offset_x:offset_x+new_w] = img_scaled

    img_rgb = cv2.cvtColor(canvas, cv2.COLOR_BGR2RGB)

    return np.expand_dims(img_rgb, axis=0), scale, (offset_x, offset_y)

def filter_boxes(boxes, box_confidences, box_class_probs, obj_thresh):
    box_confidences = box_confidences.reshape(-1)

    class_max_score = np.max(box_class_probs, axis=-1)
    classes = np.argmax(box_class_probs, axis=-1)

    class_pos = np.where(class_max_score * box_confidences >= obj_thresh)
    scores = (class_max_score * box_confidences)[class_pos]

    boxes = boxes[class_pos]
    classes = classes[class_pos]

    return boxes, classes, scores

def nms_boxes(boxes, scores, nms_thresh):
    x = boxes[:, 0]
    y = boxes[:, 1]
    w = boxes[:, 2] - boxes[:, 0]
    h = boxes[:, 3] - boxes[:, 1]

    areas = w * h
    order = scores.argsort()[::-1]

    keep = []
    while order.size > 0:
        i = order[0]
        keep.append(i)

        xx1 = np.maximum(x[i], x[order[1:]])
        yy1 = np.maximum(y[i], y[order[1:]])
        xx2 = np.minimum(x[i] + w[i], x[order[1:]] + w[order[1:]])
        yy2 = np.minimum(y[i] + h[i], y[order[1:]] + h[order[1:]])

        w1 = np.maximum(0.0, xx2 - xx1 + 0.00001)
        h1 = np.maximum(0.0, yy2 - yy1 + 0.00001)
        inter = w1 * h1

        ovr = inter / (areas[i] + areas[order[1:]] - inter)
        inds = np.where(ovr <= nms_thresh)[0]
        order = order[inds + 1]
    keep = np.array(keep)
    return keep

def dfl(position):
    n, c, h, w = position.shape
    p_num = 4
    mc = c // p_num
    y = position.reshape(n, p_num, mc, h, w)
    exp_y = np.exp(y - np.max(y, axis=2, keepdims=True))
    y = exp_y / np.sum(exp_y, axis=2, keepdims=True)
    acc_matrix = np.arange(mc, dtype=np.float32).reshape(1, 1, mc, 1, 1)
    y = np.sum(y * acc_matrix, axis=2)
    return y

def box_process(position, img_size):
    grid_h, grid_w = position.shape[2:4]
    col, row = np.meshgrid(np.arange(0, grid_w), np.arange(0, grid_h))
    col = col.reshape(1, 1, grid_h, grid_w)
    row = row.reshape(1, 1, grid_h, grid_w)
    grid = np.concatenate((col, row), axis=1)
    stride = np.array([img_size[1] // grid_h, img_size[0] // grid_w]).reshape(1, 2, 1, 1)

    position = dfl(position)
    box_xy = grid + 0.5 - position[:, 0:2, :, :]
    box_xy2 = grid + 0.5 + position[:, 2:4, :, :]
    xyxy = np.concatenate((box_xy * stride, box_xy2 * stride), axis=1)

    return xyxy

def post_process(input_data, input_size, obj_thresh, nms_thresh):
    boxes, scores, classes_conf = [], [], []
    num_branches = 3
    pair_per_branch = len(input_data) // num_branches

    for i in range(num_branches):
        boxes.append(box_process(input_data[pair_per_branch * i], input_size))
        classes_conf.append(input_data[pair_per_branch * i + 1])
        scores.append(np.ones_like(input_data[pair_per_branch * i + 1][:, :1, :, :], dtype=np.float32))

    def sp_flatten(arr):
        ch = arr.shape[1]
        arr = arr.transpose(0, 2, 3, 1)
        return arr.reshape(-1, ch)

    boxes = [sp_flatten(v) for v in boxes]
    classes_conf = [sp_flatten(v) for v in classes_conf]
    scores = [sp_flatten(v) for v in scores]

    boxes = np.concatenate(boxes)
    classes_conf = np.concatenate(classes_conf)
    scores = np.concatenate(scores)

    boxes, classes, scores = filter_boxes(boxes, scores, classes_conf, obj_thresh)

    nboxes, nclasses, nscores = [], [], []
    for class_id in set(classes):
        inds = np.where(classes == class_id)
        class_boxes = boxes[inds]
        class_classes = classes[inds]
        class_scores = scores[inds]
        keep = nms_boxes(class_boxes, class_scores, nms_thresh)

        if len(keep) != 0:
            nboxes.append(class_boxes[keep])
            nclasses.append(class_classes[keep])
            nscores.append(class_scores[keep])

    if not nclasses and not nscores:
        return None, None, None

    boxes = np.concatenate(nboxes)
    classes = np.concatenate(nclasses)
    scores = np.concatenate(nscores)

    return boxes, classes, scores

def scale_boxes(boxes, output_size, scale, offset):
    offset_x, offset_y = offset
    output_w, output_h = output_size
    scaled_boxes = []
    for i in range(len(boxes)):
        x1, y1, x2, y2 = boxes[i]
        x1 = int(max(0, min(output_w, (x1 - offset_x) / scale)))
        y1 = int(max(0, min(output_h, (y1 - offset_y) / scale)))
        x2 = int(max(0, min(output_w, (x2 - offset_x) / scale)))
        y2 = int(max(0, min(output_h, (y2 - offset_y) / scale)))
        scaled_boxes.append([x1, y1, x2, y2])
    return np.array(scaled_boxes)

def create_detections_and_stats(boxes, classes, scores, labels, img_width, img_height):
    total_pixels = img_width * img_height

    per_class_detections = {}
    for label in labels:
        per_class_detections[label] = []

    detections = []
    if boxes is not None and len(boxes) > 0:
        for i in range(len(boxes)):
            x1, y1, x2, y2 = boxes[i]
            class_id = int(classes[i])
            confidence = float(scores[i])
            class_name = labels[class_id] if class_id < len(labels) else f'class_{class_id}'

            width = x2 - x1
            height = y2 - y1
            area = (width * height) / total_pixels

            box_info = {
                'x': int(x1),
                'y': int(y1),
                'w': int(width),
                'h': int(height),
                'confidence': confidence,
                'area': area
            }

            detections.append({
                'bbox': {
                    'x': int(x1),
                    'y': int(y1),
                    'w': int(width),
                    'h': int(height)
                },
                'confidence': confidence,
                'class_id': class_id,
                'class_name': class_name
            })

            if class_name in per_class_detections:
                per_class_detections[class_name].append(box_info)

    return detections, per_class_detections

def detect_objects(rknn, image_path, input_size, labels, obj_threshold, nms_threshold):
    stats = {}

    t_start = time.time()
    original_img = load_image(image_path)
    t_load = time.time()
    stats['decode_image_ms'] = (t_load - t_start) * 1000

    t_resize_start = time.time()
    input_tensor, scale, (offset_x, offset_y) = resize_image(original_img, input_size)
    t_resize_end = time.time()
    stats['resize_image_ms'] = (t_resize_end - t_resize_start) * 1000

    debug(f"Running inference on {image_path}")
    t_inf_start = time.time()
    outputs = rknn.inference(inputs=[input_tensor])
    t_inf_end = time.time()
    stats['inference_ms'] = (t_inf_end - t_inf_start) * 1000
    debug(f"Inference complete. Output contains {len(outputs)} tensors.")

    t_post_start = time.time()
    boxes, classes, scores = post_process(outputs, input_size, obj_threshold, nms_threshold)

    if boxes is not None and len(boxes) > 0:
        output_size = (original_img.shape[1], original_img.shape[0])
        boxes = scale_boxes(boxes, output_size, scale, (offset_x, offset_y))

    img_height, img_width = original_img.shape[:2]
    detections, per_class_detections = create_detections_and_stats(boxes, classes, scores, labels, img_width, img_height)
    t_post_end = time.time()
    stats['post_process_ms'] = (t_post_end - t_post_start) * 1000

    t_total_end = time.time()
    stats['total_ms'] = (t_total_end - t_start) * 1000

    debug(f"Found {len(detections)} detections")

    result = {
        "detections": per_class_detections,
        "stats": stats
    }

    return result, detections, original_img

def run_socket_server(sock_path, rknn, input_size, labels, obj_threshold, nms_threshold):
    if os.path.exists(sock_path):
        os.remove(sock_path)

    server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    server.bind(sock_path)
    server.listen(1)

    debug(f"Socket server listening on {sock_path}")

    try:
        while True:
            conn, _ = server.accept()
            try:
                data = b""
                while True:
                    chunk = conn.recv(4096)
                    if not chunk:
                        break
                    data += chunk

                message = json.loads(data.decode('utf-8'))
                image_path = message.get('image')

                if not image_path or not Path(image_path).exists():
                    response = {"error": "Invalid or missing image path"}
                else:
                    result, _, _ = detect_objects(
                        rknn, image_path, input_size, labels, obj_threshold, nms_threshold
                    )
                    response = result

                # Log request and response result
                log(f"Processed: {message}. Result: {response}")

                conn.sendall(json.dumps(response).encode('utf-8'))

            except Exception as e:
                error_response = {"error": str(e)}
                log(f"Error processing message: {message}. Error: {error_response}")
                conn.sendall(json.dumps(error_response).encode('utf-8'))
            finally:
                conn.close()

    except KeyboardInterrupt:
        debug("Shutting down socket server")
    finally:
        server.close()
        if os.path.exists(sock_path):
            os.remove(sock_path)

def draw_detections(img, detections, max_detections=10):
    output_img = img.copy()
    colors = [
        (0, 255, 0),
        (255, 0, 0),
        (0, 0, 255),
        (255, 255, 0),
        (255, 0, 255),
        (0, 255, 255),
    ]

    for i, det in enumerate(detections[:max_detections]):
        bbox = det['bbox']
        x, y, w, h = bbox['x'], bbox['y'], bbox['w'], bbox['h']
        color = colors[i % len(colors)]
        cv2.rectangle(output_img, (x, y), (x + w, y + h), color, 2)
        label = f"{i+1}. {det['class_name']}: {det['confidence']:.3f}"
        cv2.putText(output_img, label, (x, y - 5),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.5, color, 2)

    return output_img

def run_image_mode(image_path, rknn, input_size, labels, obj_threshold, nms_threshold, output_image_path, only_matches=False):
    image_files = sorted(glob(image_path))

    for img_path in image_files:
        result, detections, original_img = detect_objects(
            rknn, img_path, input_size, labels, obj_threshold, nms_threshold
        )

        if only_matches and not detections:
            continue

        output_result = result.copy()
        output_result['image_path'] = img_path

        if output_image_path and detections:
            debug(f"Drawing detections on image...")
            output_img = draw_detections(original_img, detections)
            base_name = Path(img_path).stem
            ext = Path(output_image_path).suffix
            out_dir = Path(output_image_path).parent
            out_name = f"{base_name}_detected{ext}"
            final_output_path = out_dir / out_name
            cv2.imwrite(str(final_output_path), output_img)
            output_result['output_image'] = str(final_output_path)
            debug(f"Saved output image to: {final_output_path}")

        print(json.dumps(output_result, indent=2))

def main():
    parser = argparse.ArgumentParser(description='YOLO11 RKNN Object Detection')
    parser.add_argument('--model-path', required=True, help='Path to RKNN model file')

    labels_group = parser.add_mutually_exclusive_group(required=True)
    labels_group.add_argument('--labels-path', help='Path to labels file')
    labels_group.add_argument('--labels', help='Comma-separated list of labels')

    input_group = parser.add_mutually_exclusive_group(required=True)
    input_group.add_argument('--image', help='Input JPEG image or wildcard pattern (e.g., /path/*.jpg)')
    input_group.add_argument('--sock', help='Unix socket path for server mode')

    parser.add_argument('--input-size', default='800x480', help='Model input size WxH (default: 800x480)')
    parser.add_argument('--threshold', type=float, default=0.1, help='Box confidence threshold (default: 0.1)')
    parser.add_argument('--nms-threshold', type=float, default=0.5, help='NMS IoU threshold (default: 0.5)')
    parser.add_argument('--output-image', help='Output image path (optional, only for --image mode)')
    parser.add_argument('--only-matches', default=True, action='store_true', help='Only output results for images with detections')
    parser.add_argument('--debug', action='store_true', help='Enable debug output to stderr')
    args = parser.parse_args()

    global debug_mode
    debug_mode = args.debug

    if not Path(args.model_path).exists():
        log(f"Error: Model not found: {args.model_path}")
        sys.exit(1)

    if args.labels_path:
        if not Path(args.labels_path).exists():
            log(f"Error: Labels not found: {args.labels_path}")
            sys.exit(1)
        labels = load_labels(args.labels_path)
    else:
        labels = [label.strip() for label in args.labels.split(',')]

    try:
        width, height = map(int, args.input_size.split('x'))
        input_size = (width, height)
    except ValueError:
        log(f"Error: Invalid input size: {args.input_size}. Use WxH format")
        sys.exit(1)
    obj_threshold = args.threshold
    nms_threshold = args.nms_threshold

    debug(f"Loaded {len(labels)} labels")
    debug(f"Object threshold: {obj_threshold}, NMS threshold: {nms_threshold}")

    debug(f"Loading RKNN model: {args.model_path}")
    rknn = RKNNLite()
    ret = rknn.load_rknn(args.model_path)
    if ret != 0:
        log(f"Failed to load model: {ret}")
        sys.exit(1)

    ret = rknn.init_runtime()
    if ret != 0:
        log(f"Failed to init runtime: {ret}")
        sys.exit(1)
    debug("Model initialized successfully")

    if args.sock:
        run_socket_server(
            args.sock, rknn, input_size, labels, obj_threshold, nms_threshold
        )
    else:
        run_image_mode(
            args.image, rknn, input_size, labels, obj_threshold, nms_threshold, args.output_image, args.only_matches
        )

    rknn.release()

if __name__ == '__main__':
    main()
