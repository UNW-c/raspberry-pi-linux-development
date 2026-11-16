"""Live 奶龙 detection on the desktop.

The node serves the camera as an RTSP stream (V4L2 -> FFmpeg -> mediamtx); this
script pulls the newest frame from it and runs YOLOX (the model from
nkxingxh/NailongDetection) with OpenCV's DNN module, drawing the boxes.
Inference runs here on purpose: the Pi 3B has about 900 MB of RAM and no vector
unit worth the name, so a detector on the node would starve the camera pipeline
it is supposed to watch.

    python detect_nailong.py                       # window with boxes
    python detect_nailong.py --no-window           # console only
    python detect_nailong.py --http 18081          # annotated stream for the browser
    python detect_nailong.py --source snapshot     # old camera page (no stream running)
"""

from __future__ import annotations

import argparse
import json
import os
import sys
import threading
import time
import urllib.error
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlparse

import cv2
import numpy as np


def letterbox(image: np.ndarray, size: int, pad: int = 114):
    """Resize with the aspect ratio kept, padding the rest like YOLOX training."""
    height, width = image.shape[:2]
    ratio = min(size / height, size / width)
    new_w, new_h = int(round(width * ratio)), int(round(height * ratio))
    canvas = np.full((size, size, 3), pad, dtype=np.uint8)
    dx, dy = (size - new_w) // 2, (size - new_h) // 2
    canvas[dy : dy + new_h, dx : dx + new_w] = cv2.resize(image, (new_w, new_h))
    return canvas, ratio, dx, dy


def decode_yolox(output: np.ndarray, size: int) -> np.ndarray:
    """Turn the raw YOLOX head into (x, y, w, h) boxes in network coordinates."""
    strides = [8, 16, 32]
    grids, expanded = [], []
    for stride in strides:
        hsize = wsize = size // stride
        xv, yv = np.meshgrid(np.arange(wsize), np.arange(hsize))
        grid = np.stack((xv, yv), 2).reshape(1, -1, 2).astype(np.float32)
        grids.append(grid)
        expanded.append(np.full((1, grid.shape[1], 1), stride, dtype=np.float32))
    output = output.copy()
    output[..., :2] = (output[..., :2] + np.concatenate(grids, 1)) * np.concatenate(expanded, 1)
    output[..., 2:4] = np.exp(output[..., 2:4]) * np.concatenate(expanded, 1)
    return output


def nms(boxes: np.ndarray, scores: np.ndarray, threshold: float) -> list[int]:
    """Plain greedy NMS; boxes are x, y, w, h in pixels."""
    x1, y1 = boxes[:, 0], boxes[:, 1]
    x2, y2 = boxes[:, 0] + boxes[:, 2], boxes[:, 1] + boxes[:, 3]
    areas = np.maximum(0, x2 - x1) * np.maximum(0, y2 - y1)
    order = scores.argsort()[::-1]
    keep = []
    while order.size > 0:
        best = order[0]
        keep.append(int(best))
        rest = order[1:]
        xx1 = np.maximum(x1[best], x1[rest])
        yy1 = np.maximum(y1[best], y1[rest])
        xx2 = np.minimum(x2[best], x2[rest])
        yy2 = np.minimum(y2[best], y2[rest])
        inter = np.maximum(0, xx2 - xx1) * np.maximum(0, yy2 - yy1)
        iou = inter / np.maximum(1e-6, areas[best] + areas[rest] - inter)
        order = rest[iou <= threshold]
    return keep


class Detector:
    def __init__(self, model_path: str, labels: list[str], size: int, score: float,
                 nms_threshold: float, threads: int, backend: str = "auto") -> None:
        self.size = size
        self.labels = labels
        self.score = score
        self.nms_threshold = nms_threshold
        self.session = None
        self.net = None
        self.backend = backend

        if backend in ("auto", "onnxruntime"):
            try:
                import onnxruntime as ort

                options = ort.SessionOptions()
                options.intra_op_num_threads = threads
                options.inter_op_num_threads = 1
                options.graph_optimization_level = ort.GraphOptimizationLevel.ORT_ENABLE_ALL
                self.session = ort.InferenceSession(
                    model_path, options, providers=["CPUExecutionProvider"]
                )
                self.input_name = self.session.get_inputs()[0].name
                self.backend = "onnxruntime"
            except ImportError:
                if backend == "onnxruntime":
                    raise

        if self.session is None:
            # OpenCV already ships an ONNX importer, so the detector runs with
            # nothing but cv2 + numpy installed.
            cv2.setNumThreads(max(1, threads))
            self.net = cv2.dnn.readNetFromONNX(model_path)
            self.backend = "opencv-dnn"

    def infer(self, tensor: np.ndarray) -> np.ndarray:
        if self.session is not None:
            return self.session.run(None, {self.input_name: tensor})[0]
        self.net.setInput(tensor)
        return self.net.forward()

    def detect(self, frame: np.ndarray):
        padded, ratio, dx, dy = letterbox(frame, self.size)
        # YOLOX expects raw 0-255 values in RGB order; dividing by 255 here
        # silences the model completely (measured: objectness 0.99 -> 0.14).
        tensor = cv2.dnn.blobFromImage(padded, 1.0, (self.size, self.size), swapRB=True)

        output = self.infer(tensor)
        if output.ndim == 3:
            output = decode_yolox(output, self.size)[0]

        boxes = output[:, :4]
        objectness = output[:, 4]
        class_scores = output[:, 5:] * objectness[:, None]
        classes = class_scores.argmax(axis=1)
        scores = class_scores.max(axis=1)

        # Boxes come out as centre, size: convert once, then filter.
        boxes = np.stack(
            [
                boxes[:, 0] - boxes[:, 2] / 2,
                boxes[:, 1] - boxes[:, 3] / 2,
                boxes[:, 2],
                boxes[:, 3],
            ],
            axis=1,
        )
        keep_mask = scores > self.score
        boxes, scores, classes = boxes[keep_mask], scores[keep_mask], classes[keep_mask]

        results = []
        for label in np.unique(classes):
            picked = np.where(classes == label)[0]
            for index in nms(boxes[picked], scores[picked], self.nms_threshold):
                x, y, w, h = boxes[picked[index]]
                results.append(
                    {
                        "label": self.labels[int(label)] if int(label) < len(self.labels) else str(int(label)),
                        "score": float(scores[picked[index]]),
                        "box": [
                            float((x - dx) / ratio),
                            float((y - dy) / ratio),
                            float(w / ratio),
                            float(h / ratio),
                        ],
                    }
                )
        results.sort(key=lambda item: item["score"], reverse=True)
        return results


class FrameSource:
    """Polls /snapshot so the viewer always gets the newest frame, never a backlog."""

    def __init__(self, base_url: str) -> None:
        self.url = base_url.rstrip("/") + "/snapshot"
        self.last: bytes | None = None
        # Windows hands urllib the system proxy, which breaks 127.0.0.1.
        self.opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
        self.misses = 0

    def read(self) -> np.ndarray | None:
        try:
            with self.opener.open(self.url, timeout=2) as response:
                payload = response.read()
        except (urllib.error.URLError, OSError, TimeoutError) as error:
            self.misses += 1
            if self.misses in (40, 200):
                print(f"no frames from {self.url} ({error}) - is the tunnel up?", flush=True)
            return None
        self.misses = 0
        if payload == self.last:
            return None
        self.last = payload
        image = cv2.imdecode(np.frombuffer(payload, dtype=np.uint8), cv2.IMREAD_COLOR)
        return image


class RtspSource:
    """Blocking reader for the RTSP stream, with reconnect on a dead tunnel."""

    def __init__(self, url: str, transport: str = "udp") -> None:
        self.url = url
        self.transport = transport
        # rtsp_transport is a ffmpeg demuxer option and only reachable through
        # this variable; it has to be set before the capture is created.
        #   udp  - media over RTP/UDP, the same transport the browser uses
        #          through WebRTC, so both pictures are equally close to live
        #   tcp  - needed when the stream comes through an SSH tunnel
        os.environ["OPENCV_FFMPEG_CAPTURE_OPTIONS"] = (
            f"rtsp_transport;{transport}|fflags;nobuffer|stimeout;5000000"
        )
        print(f"connecting to {url} over rtsp/{transport}"
              + (" (the first open can take ~10 s)" if transport == "tcp" else ""),
              flush=True)
        self.capture = cv2.VideoCapture(url, cv2.CAP_FFMPEG)
        if not self.capture.isOpened():
            raise RuntimeError(f"cannot open {url}")
        self.capture.set(cv2.CAP_PROP_BUFFERSIZE, 1)
        self.lastReconnect = 0.0
        self.misses = 0

    def _reconnect(self) -> None:
        """Reopens the stream after the tunnel or the publisher went away."""
        if time.monotonic() - self.lastReconnect < 3.0:
            return
        self.lastReconnect = time.monotonic()
        print(f"stream lost, reconnecting to {self.url} ...", flush=True)
        try:
            self.capture.release()
        except cv2.error:
            pass
        try:
            self.capture = cv2.VideoCapture(self.url, cv2.CAP_FFMPEG)
            self.capture.set(cv2.CAP_PROP_BUFFERSIZE, 1)
        except cv2.error:
            self.capture = cv2.VideoCapture()
    def read(self) -> np.ndarray | None:
        # Never let an exception escape: a dead tunnel makes OpenCV throw, and
        # that would end the reader thread and silently freeze the picture.
        try:
            ok, frame = self.capture.read()
        except cv2.error:
            ok, frame = False, None
        if not ok or frame is None:
            self.misses += 1
            if self.misses in (30, 200):
                print(
                    f"no frames from {self.url} - start the stream first "
                    f"(stream\\start-stream.bat)",
                    flush=True,
                )
            time.sleep(0.2)
            self._reconnect()
            return None
        self.misses = 0
        return frame

    def close(self) -> None:
        try:
            self.capture.release()
        except cv2.error:
            pass


class LatestFrame:
    """Keeps the newest frame from any source: one reader, many consumers.

    Display and detection both need "the picture as of now", at their own pace.
    Without this, whoever reads first steals the frame from the other, and the
    display ends up as slow as the detector.
    """

    def __init__(self, source) -> None:
        self.source = source
        self.latest = None
        self.seen = 0          # frames pulled from the source
        self.dropped = 0       # frames replaced before any consumer took them
        self.running = True
        self.thread = threading.Thread(target=self._pump, daemon=True)
        self.thread.start()

    def _pump(self) -> None:
        while self.running:
            frame = self.source.read()
            if frame is None:
                time.sleep(0.005)
                continue
            if self.latest is not None:
                self.dropped += 1
            self.latest = frame
            self.seen += 1

    def get(self):
        """The current picture; never blocks and never removes it."""
        return self.latest

    def close(self) -> None:
        # Stop the reader before releasing the capture: releasing while a decode
        # is in flight trips an FFmpeg frame-threading assertion on exit.
        self.running = False
        self.thread.join(timeout=2.0)
        self.source.close()


class DetectionWorker(threading.Thread):
    """Runs the detector on the newest frame, in parallel with the display."""

    def __init__(self, detector, frames: LatestFrame) -> None:
        super().__init__(daemon=True)
        self.detector = detector
        self.frames = frames
        self.lock = threading.Lock()
        self.results: list[dict] = []
        self.shape = None
        self.processed = 0
        self.hits = 0
        self.fps = 0.0
        self.running = True
        self._lastSeen = -1
        # Never name a Thread attribute _started or _ident: those belong to
        # threading.Thread itself and shadowing them breaks start().
        self._startedAt = time.monotonic()

    def run(self) -> None:
        while self.running:
            frame = self.frames.get()
            if frame is None or self.frames.seen == self._lastSeen:
                time.sleep(0.005)
                continue
            self._lastSeen = self.frames.seen
            detections = self.detector.detect(frame)
            elapsed = max(1e-6, time.monotonic() - self._startedAt)
            with self.lock:
                self.results = detections
                self.shape = frame.shape
                self.processed += 1
                self.hits += 1 if detections else 0
                self.fps = self.processed / elapsed

    def snapshot(self):
        with self.lock:
            return list(self.results), self.shape, self.processed, self.hits, self.fps


def resolve_transport(url: str, requested: str) -> str:
    """UDP for a direct connection, TCP when the stream comes through a tunnel."""
    if requested in ("udp", "tcp"):
        return requested
    host = (urlparse(url).hostname or "").lower()
    return "tcp" if host in ("127.0.0.1", "localhost", "::1") else "udp"


def open_source(args):
    """Picks the frame source: RTSP by default, the old snapshot as a fallback."""
    if args.source == "snapshot":
        return FrameSource(args.url), f"snapshot {args.url.rstrip('/')}/snapshot"

    transport = resolve_transport(args.rtsp, args.transport)
    label = f"rtsp {args.rtsp} ({transport})"

    if args.source == "rtsp":
        return RtspSource(args.rtsp, transport), label

    try:
        return RtspSource(args.rtsp, transport), label
    except RuntimeError as error:
        print(f"rtsp unavailable ({error}), falling back to the snapshot path", flush=True)
        return FrameSource(args.url), f"snapshot {args.url.rstrip('/')}/snapshot"


def resolve_rtsp_url(explicit: str) -> str:
    """Finds the stream address: the flag, then what start-stream.ps1 recorded.

    start-stream.ps1 writes the node's direct address to .tmp-ops/stream-rtsp.txt.
    Using that instead of the tunnel puts the detector on the same path as the
    browser (no SSH hop, no extra buffering), so both pictures stay in step.
    """
    if explicit:
        return explicit
    state = Path(__file__).resolve().parents[2] / ".tmp-ops" / "stream-rtsp.txt"
    try:
        url = state.read_text(encoding="utf-8").strip()
        if url:
            return url
    except OSError:
        pass
    return "rtsp://127.0.0.1:8554/prts"


class AnnotatedStream:
    """Serves the annotated picture as MJPEG so the browser can watch it."""

    def __init__(self, port: int) -> None:
        self.port = port
        self.jpeg: bytes = b""
        self.lock = threading.Lock()
        self.server: ThreadingHTTPServer | None = None

    def update(self, image: np.ndarray) -> None:
        ok, buffer = cv2.imencode(".jpg", image, [int(cv2.IMWRITE_JPEG_QUALITY), 80])
        if ok:
            with self.lock:
                self.jpeg = buffer.tobytes()

    def start(self) -> None:
        owner = self

        class Handler(BaseHTTPRequestHandler):
            def do_GET(self) -> None:  # noqa: N802 - http.server naming
                if self.path not in ("/", "/stream", "/snapshot"):
                    self.send_error(404)
                    return
                if self.path == "/":
                    page = (
                        b"<!doctype html><meta charset=utf-8><title>PRTS / DETECT</title>"
                        b"<body style='margin:0;background:#000'>"
                        b"<img src='/stream' style='width:100%;max-width:960px;display:block;margin:0 auto'>"
                    )
                    self.send_response(200)
                    self.send_header("Content-Type", "text/html; charset=utf-8")
                    self.send_header("Content-Length", str(len(page)))
                    self.end_headers()
                    self.wfile.write(page)
                    return
                if self.path == "/snapshot":
                    with owner.lock:
                        payload = owner.jpeg
                    self.send_response(200 if payload else 503)
                    self.send_header("Content-Type", "image/jpeg")
                    self.send_header("Content-Length", str(len(payload)))
                    self.end_headers()
                    if payload:
                        self.wfile.write(payload)
                    return
                self.send_response(200)
                self.send_header("Content-Type", "multipart/x-mixed-replace; boundary=frame")
                self.end_headers()
                try:
                    while True:
                        with owner.lock:
                            payload = owner.jpeg
                        if payload:
                            self.wfile.write(b"--frame\r\nContent-Type: image/jpeg\r\nContent-Length: ")
                            self.wfile.write(str(len(payload)).encode())
                            self.wfile.write(b"\r\n\r\n" + payload + b"\r\n")
                        time.sleep(0.05)
                except (BrokenPipeError, ConnectionResetError):
                    return

            def log_message(self, *args) -> None:  # keep the console clean
                return

        self.server = ThreadingHTTPServer(("127.0.0.1", self.port), Handler)
        self.server.daemon_threads = True
        threading.Thread(target=self.server.serve_forever, daemon=True).start()


def load_labels(path: str) -> list[str]:
    with open(path, "r", encoding="utf-8") as handle:
        return [line.strip() for line in handle if line.strip()]


def draw(image: np.ndarray, detections: list[dict], fps: float, info: str,
         width: int = 0) -> np.ndarray:
    """Draws the boxes; boxes are in source pixels, the canvas may be resized."""
    scale = 1.0
    if width and width > 0:
        height = int(round(image.shape[0] * width / image.shape[1]))
        canvas = cv2.resize(image, (width, height), interpolation=cv2.INTER_LINEAR)
        scale = width / image.shape[1]
    else:
        canvas = image.copy()

    unit = max(1, canvas.shape[1] // 320)
    for item in detections:
        x, y, w, h = (int(round(value * scale)) for value in item["box"])
        cv2.rectangle(canvas, (x, y), (x + w, y + h), (160, 220, 255), unit)
        caption = f"{item['label']} {item['score']:.2f}"
        cv2.putText(canvas, caption, (x, max(12, y - 4)), cv2.FONT_HERSHEY_SIMPLEX,
                    0.4 * unit, (160, 220, 255), max(1, unit - 1), cv2.LINE_AA)
    cv2.putText(canvas, f"{info}  {fps:4.1f} fps", (6, canvas.shape[0] - 6),
                cv2.FONT_HERSHEY_SIMPLEX, 0.4 * unit, (200, 200, 200), max(1, unit - 1),
                cv2.LINE_AA)
    return canvas


def main() -> int:
    parser = argparse.ArgumentParser(description="奶龙 detection on desktop frames")
    parser.add_argument("--source", choices=["rtsp", "snapshot", "auto"], default="rtsp",
                        help="rtsp = the ffmpeg stream (default), snapshot = the old camera "
                             "page, auto = rtsp with a snapshot fallback")
    parser.add_argument("--rtsp", default="",
                        help="RTSP url; default is what start-stream.ps1 recorded, "
                             "else rtsp://127.0.0.1:8554/prts (the tunnel)")
    parser.add_argument("--transport", choices=["auto", "udp", "tcp"], default="auto",
                        help="RTP transport; auto = udp when connecting directly, tcp "
                             "through a tunnel")
    parser.add_argument("--url", default="http://127.0.0.1:18080",
                        help="camera web base url used by --source snapshot")
    parser.add_argument("--model", default="models/nailong_v2.6_tiny.onnx")
    parser.add_argument("--labels", default="models/labels.txt")
    parser.add_argument("--size", type=int, default=416)
    parser.add_argument("--score", type=float, default=0.35)
    parser.add_argument("--nms", type=float, default=0.45)
    parser.add_argument("--threads", type=int, default=0, help="0 = one per core")
    parser.add_argument("--http", type=int, default=0, help="also serve annotated MJPEG on this port")
    parser.add_argument("--no-window", action="store_true")
    parser.add_argument("--display-width", type=int, default=960,
                        help="window width in pixels; 0 = native camera size")
    parser.add_argument("--fps", type=int, default=30,
                        help="window refresh rate, independent of the detector")
    parser.add_argument("--json", action="store_true", help="print each result as json")
    parser.add_argument("--backend", choices=["auto", "onnxruntime", "opencv"], default="auto",
                        help="inference backend, auto prefers onnxruntime when installed")
    parser.add_argument("--count", type=int, default=0, help="stop after N frames (0 = forever)")
    parser.add_argument("--idle-timeout", type=float, default=20.0,
                        help="give up after this many seconds without a frame")
    args = parser.parse_args()

    threads = args.threads or max(1, (os.cpu_count() or 4))
    args.rtsp = resolve_rtsp_url(args.rtsp)
    labels = load_labels(args.labels)
    detector = Detector(args.model, labels, args.size, args.score, args.nms, threads, args.backend)
    try:
        source, source_label = open_source(args)
    except RuntimeError as error:
        print(f"no frame source: {error}", flush=True)
        return 4
    stream = AnnotatedStream(args.http) if args.http else None
    if stream is not None:
        stream.start()
        print(f"annotated stream: http://127.0.0.1:{args.http}/", flush=True)
    print(
        f"model {args.model}  backend {detector.backend}  source {source_label}  "
        f"classes {', '.join(labels)}  threads {threads}",
        flush=True,
    )

    frames = LatestFrame(source)
    started = time.monotonic()
    last_report = started

    def report(processed: int, fps: float, detections: list[dict]) -> None:
        nonlocal last_report
        if time.monotonic() - last_report < 2.0:
            return
        last_report = time.monotonic()
        summary = ", ".join(f"{d['label']} {d['score']:.2f}" for d in detections) or "clear"
        print(f"{processed:5d} frames  {fps:5.1f} fps  {summary}", flush=True)

    def idle_gave_up(processed: int) -> bool:
        if processed > 0 or time.monotonic() - started <= args.idle_timeout:
            return False
        print(
            f"no frame within {args.idle_timeout:.0f}s, giving up - source was {source_label}",
            flush=True,
        )
        return True

    if args.no_window:
        # No display: detection runs in this thread, one frame at a time.
        processed = 0
        lastSeen = -1
        while True:
            frame = frames.get()
            if frame is None or frames.seen == lastSeen:
                if idle_gave_up(processed):
                    frames.close()
                    return 5
                time.sleep(0.005)
                continue
            lastSeen = frames.seen
            detections = detector.detect(frame)
            processed += 1
            fps = processed / max(1e-6, time.monotonic() - started)
            if args.json:
                print(json.dumps({"fps": round(fps, 2), "detections": detections}), flush=True)
            if stream is not None:
                info = f"{frame.shape[1]}x{frame.shape[0]}  det {processed}"
                stream.update(draw(frame, detections, fps, info, args.display_width))
            report(processed, fps, detections)
            if args.count and processed >= args.count:
                break
    else:
        # Display at its own pace from the newest frame; detection runs beside it
        # in a worker, so the picture stays smooth and the boxes trail slightly.
        worker = DetectionWorker(detector, frames)
        worker.start()
        interval = 1.0 / max(1, args.fps)
        width = args.display_width
        processed = 0
        while True:
            tick = time.monotonic()
            frame = frames.get()
            if frame is None:
                if idle_gave_up(0):
                    break
                time.sleep(0.005)
                continue

            detections, shape, processed, hits, detectFps = worker.snapshot()
            info = (f"{frame.shape[1]}x{frame.shape[0]}  det {processed}  "
                    f"target {hits}")
            annotated = draw(frame, detections, detectFps, info, width)
            if stream is not None:
                stream.update(annotated)
            cv2.imshow("PRTS / DETECT", annotated)
            report(processed, detectFps, detections)

            key = cv2.waitKey(1) & 0xFF
            if key in (27, ord("q")) or (args.count and processed >= args.count):
                break
            spent = time.monotonic() - tick
            if spent < interval:
                time.sleep(interval - spent)

        worker.running = False
        cv2.destroyAllWindows()

    frames.close()
    if stream is not None and stream.server is not None:
        stream.server.shutdown()
    print(f"stopped after {processed} frames", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
