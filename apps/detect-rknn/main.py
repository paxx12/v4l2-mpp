#!/tmp/rknn-env/bin/python3

###!/usr/bin/env python3

import os
import sys
import json
import socket
import argparse
import time
import signal
import threading
from pathlib import Path

try:
    from rknnlite.api import RKNNLite
except ImportError:
    print("Error: rknnlite not installed. Run: pip install rknnlite2")
    sys.exit(1)

import numpy as np
import cv2

class RKNNDetector:
    def __init__(self, model_path, config_path, labels_path):
        self.model_path = model_path
        self.load_config(config_path)
        self.labels = self.load_labels(labels_path)
        self.rknn = RKNNLite()
        self.running = False

    def load_config(self, config_path):
        with open(config_path, 'r') as f:
            config = json.load(f)
        self.nms_threshold = config.get('nms_threshold', 0.5)
        self.box_conf_threshold = config.get('box_conf_threshold', 0.7)
        self.max_detections = config.get('max_detections', 5)
        self.max_detect_pic_cnt = config.get('max_detect_pic_cnt', 30)
        self.save_original_pic = config.get('save_orignal_pic', 1)
        log(f"Config loaded: nms={self.nms_threshold}, conf={self.box_conf_threshold}, max={self.max_detections}")

    def load_labels(self, labels_path):
        with open(labels_path, 'r') as f:
            return [line.strip() for line in f.readlines() if line.strip()]

    def init_model(self):
        log(f"Loading RKNN model: {self.model_path}")
        ret = self.rknn.load_rknn(self.model_path)
        if ret != 0:
            raise RuntimeError(f"Failed to load RKNN model: {ret}")
        ret = self.rknn.init_runtime()
        if ret != 0:
            raise RuntimeError(f"Failed to init RKNN runtime: {ret}")
        log("RKNN model initialized successfully")

    def preprocess_image(self, image_data):
        img = cv2.imdecode(np.frombuffer(image_data, np.uint8), cv2.IMREAD_COLOR)
        if img is None:
            raise ValueError("Failed to decode image")
        img_rgb = cv2.cvtColor(img, cv2.COLOR_BGR2RGB)
        img_rgb_batch = np.expand_dims(img_rgb, axis=0)
        return img, img_rgb_batch

    def postprocess_detections(self, outputs, img_shape):
        detections = []
        for i, output in enumerate(outputs):
            if output is None:
                continue
            if output.ndim == 4 and output.shape[1] > 10:
                boxes = self.decode_yolo_output(output, img_shape)
                detections.extend(boxes)
        detections = self.apply_nms(detections)
        return detections

    def sigmoid(self, x):
        return 1.0 / (1.0 + np.exp(-np.clip(x, -50, 50)))

    def decode_yolo_output(self, output, img_shape):
        boxes = []
        h, w = img_shape[:2]
        batch, channels, grid_h, grid_w = output.shape
        num_classes = channels - 5
        output = output.reshape(batch, channels, -1).transpose(0, 2, 1)
        for detection in output[0]:
            obj_conf = self.sigmoid(detection[4])
            if obj_conf < self.box_conf_threshold:
                continue
            class_scores = self.sigmoid(detection[5:])
            class_id = int(np.argmax(class_scores))
            class_conf = class_scores[class_id]
            confidence = obj_conf * class_conf
            if confidence < self.box_conf_threshold:
                continue
            x_center = self.sigmoid(detection[0])
            y_center = self.sigmoid(detection[1])
            box_w = self.sigmoid(detection[2])
            box_h = self.sigmoid(detection[3])
            x1 = int((x_center - box_w / 2) * w)
            y1 = int((y_center - box_h / 2) * h)
            x2 = int((x_center + box_w / 2) * w)
            y2 = int((y_center + box_h / 2) * h)
            boxes.append({
                'bbox': [x1, y1, x2, y2],
                'confidence': float(confidence),
                'class_id': class_id,
                'class_name': self.labels[class_id] if class_id < len(self.labels) else f'class_{class_id}'
            })
        return boxes

    def apply_nms(self, detections):
        if not detections:
            return []
        detections_sorted = sorted(detections, key=lambda x: x['confidence'], reverse=True)
        detections_sorted = detections_sorted[:self.max_detections]
        boxes = np.array([d['bbox'] for d in detections_sorted])
        scores = np.array([d['confidence'] for d in detections_sorted])
        indices = cv2.dnn.NMSBoxes(
            boxes.tolist(),
            scores.tolist(),
            self.box_conf_threshold,
            self.nms_threshold
        )
        if len(indices) > 0:
            indices = indices.flatten()
            return [detections_sorted[i] for i in indices]
        return []

    def detect(self, image_data):
        t0 = time.time()
        img, img_rgb_batch = self.preprocess_image(image_data)
        t1 = time.time()
        outputs = self.rknn.inference(inputs=[img_rgb_batch])
        t2 = time.time()
        detections = self.postprocess_detections(outputs, img.shape)
        t3 = time.time()
        decode_time = (t1 - t0) * 1000
        inference_time = (t2 - t1) * 1000
        postprocess_time = (t3 - t2) * 1000
        log(f"Timing: decode={decode_time:.1f}ms, inference={inference_time:.1f}ms, postprocess={postprocess_time:.1f}ms")
        return detections, img

    def release(self):
        if self.rknn:
            self.rknn.release()
            log("RKNN model released")

def log(msg):
    ts = time.strftime('%H:%M:%S')
    print(f"[{ts}] {msg}", flush=True)

def read_socket_image(sock_path, timeout=5.0):
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    sock.settimeout(timeout)
    try:
        sock.connect(sock_path)
        chunks = []
        while True:
            chunk = sock.recv(65536)
            if not chunk:
                break
            chunks.append(chunk)
        return b''.join(chunks)
    finally:
        sock.close()

def save_detection_result(img, detections, output_path, save_count):
    img_with_boxes = img.copy()
    for det in detections:
        x1, y1, x2, y2 = det['bbox']
        conf = det['confidence']
        label = det['class_name']
        cv2.rectangle(img_with_boxes, (x1, y1), (x2, y2), (0, 255, 0), 2)
        text = f"{label}: {conf:.2f}"
        cv2.putText(img_with_boxes, text, (x1, y1 - 10),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.5, (0, 255, 0), 2)
    output_file = output_path / f"detection_{save_count:06d}.jpg"
    cv2.imwrite(str(output_file), img_with_boxes)
    log(f"Saved detection result: {output_file}")

def detection_loop(detector, sock_path, interval, output_path, save_results):
    log(f"Starting detection loop: socket={sock_path}, interval={interval}s")
    save_count = 0
    while detector.running:
        try:
            start_time = time.time()
            image_data = read_socket_image(sock_path)
            if not image_data:
                log("No image data received")
                time.sleep(interval)
                continue
            detections, img = detector.detect(image_data)
            elapsed = time.time() - start_time
            if detections:
                log(f"Detected {len(detections)} objects (total={elapsed*1000:.1f}ms):")
                for det in detections:
                    log(f"  - {det['class_name']}: {det['confidence']:.3f} at {det['bbox']}")
                if save_results and save_count < detector.max_detect_pic_cnt:
                    save_detection_result(img, detections, output_path, save_count)
                    save_count += 1
            else:
                log(f"No detections (total={elapsed*1000:.1f}ms)")
            time.sleep(max(0, interval - elapsed))
        except socket.timeout:
            log("Socket timeout, retrying...")
        except Exception as e:
            log(f"Detection error: {e}")
            time.sleep(interval)

def main():
    parser = argparse.ArgumentParser(description='V4L2-MPP RKNN Object Detection')
    parser.add_argument('--model-path', required=True, help='Path to RKNN model file')
    parser.add_argument('--config-path', required=True, help='Path to config.json')
    parser.add_argument('--labels-path', required=True, help='Path to labels file')
    parser.add_argument('--jpeg-sock', required=True, help='JPEG snapshot socket')
    parser.add_argument('--interval', type=float, default=1.0, help='Detection interval in seconds')
    parser.add_argument('--output-dir', default='./detections', help='Output directory for detection results')
    parser.add_argument('--save-results', action='store_true', help='Save detection results with bounding boxes')
    args = parser.parse_args()

    if not Path(args.model_path).exists():
        log(f"Error: Model file not found: {args.model_path}")
        sys.exit(1)

    if not Path(args.config_path).exists():
        log(f"Error: Config file not found: {args.config_path}")
        sys.exit(1)

    if not Path(args.labels_path).exists():
        log(f"Error: Labels file not found: {args.labels_path}")
        sys.exit(1)

    output_path = Path(args.output_dir)
    if args.save_results:
        output_path.mkdir(parents=True, exist_ok=True)
        log(f"Detection results will be saved to: {output_path}")

    detector = RKNNDetector(args.model_path, args.config_path, args.labels_path)
    try:
        detector.init_model()
        detector.running = True

        def shutdown(signum, frame):
            log("Shutting down...")
            detector.running = False
            time.sleep(0.5)
            detector.release()
            sys.exit(0)

        signal.signal(signal.SIGINT, shutdown)
        signal.signal(signal.SIGTERM, shutdown)

        detection_loop(detector, args.jpeg_sock, args.interval, output_path, args.save_results)

    except Exception as e:
        log(f"Fatal error: {e}")
        detector.release()
        sys.exit(1)

if __name__ == '__main__':
    main()
