// Detection worker: takes decoded video frames, runs YOLOX through ONNX Runtime
// Web and answers with boxes in source pixels.
//
// Everything here is off the main thread on purpose: the picture keeps the
// browser's smoothness no matter how long the inference takes.

import * as ort from './vendor/ort/ort.wasm.min.mjs';

const MODEL_URL = '/models/nailong_v2.6_tiny.onnx';
const LABELS_URL = '/models/labels.txt';
const INPUT_SIZE = 416;
const PAD = 114;

let session = null;
let labels = [];
let inputName = '';
let busy = false;
let processed = 0;
let startedAt = 0;

// A canvas in the worker: no DOM needed, and the picture never leaves the
// worker's own memory.
const canvas = new OffscreenCanvas(INPUT_SIZE, INPUT_SIZE);
const context = canvas.getContext('2d', { alpha: false, willReadFrequently: true });

function post(message, transfer) {
  if (transfer) {
    self.postMessage(message, transfer);
  } else {
    self.postMessage(message);
  }
}

function status(text) {
  post({ type: 'status', text });
}

async function load() {
  ort.env.wasm.wasmPaths = new URL('./vendor/ort/', self.location.href).href;
  // Worker threads need SharedArrayBuffer, which the browser only exposes on a
  // cross-origin isolated page (serve.py sends COOP/COEP for that).
  ort.env.wasm.numThreads = self.crossOriginIsolated
    ? Math.max(1, Math.min(4, navigator.hardwareConcurrency || 2))
    : 1;
  ort.env.wasm.simd = true;
  ort.env.logLevel = 'warning';

  status(`loading model (${ort.env.wasm.numThreads} threads)`);
  const [model, labelText] = await Promise.all([
    fetch(MODEL_URL).then((response) => {
      if (!response.ok) {
        throw new Error(`model ${response.status}`);
      }
      return response.arrayBuffer();
    }),
    fetch(LABELS_URL).then((response) => response.text()),
  ]);

  labels = labelText.split(/\r?\n/).map((line) => line.trim()).filter(Boolean);
  session = await ort.InferenceSession.create(model, {
    executionProviders: ['wasm'],
    graphOptimizationLevel: 'all',
  });
  inputName = session.inputNames[0];
  post({ type: 'ready', labels, threads: ort.env.wasm.numThreads });
  status(`ready, ${labels.length} classes`);
}

// Letterbox into the model's square input, keeping the aspect ratio.
function fitFrame(frame) {
  const width = frame.displayWidth;
  const height = frame.displayHeight;
  const ratio = Math.min(INPUT_SIZE / width, INPUT_SIZE / height);
  const drawWidth = Math.round(width * ratio);
  const drawHeight = Math.round(height * ratio);
  const dx = Math.floor((INPUT_SIZE - drawWidth) / 2);
  const dy = Math.floor((INPUT_SIZE - drawHeight) / 2);

  context.fillStyle = `rgb(${PAD},${PAD},${PAD})`;
  context.fillRect(0, 0, INPUT_SIZE, INPUT_SIZE);
  context.drawImage(frame, dx, dy, drawWidth, drawHeight);
  return { ratio, dx, dy };
}

// RGBA canvas -> NCHW float tensor. The model wants raw 0-255 values: dividing
// by 255 here drops every score to noise (objectness 0.99 -> 0.14).
function toTensor() {
  const { data } = context.getImageData(0, 0, INPUT_SIZE, INPUT_SIZE);
  const plane = INPUT_SIZE * INPUT_SIZE;
  const tensor = new Float32Array(plane * 3);
  for (let pixel = 0; pixel < plane; pixel += 1) {
    const source = pixel * 4;
    tensor[pixel] = data[source];
    tensor[plane + pixel] = data[source + 1];
    tensor[plane * 2 + pixel] = data[source + 2];
  }
  return new ort.Tensor('float32', tensor, [1, 3, INPUT_SIZE, INPUT_SIZE]);
}

// YOLOX head -> (x, y, w, h) in network pixels, with objectness and class scores.
function decode(output, dims) {
  const cols = dims[2];
  const classes = cols - 5;
  const strides = [8, 16, 32];
  const boxes = [];
  let row = 0;

  for (const stride of strides) {
    const cells = INPUT_SIZE / stride;
    for (let cy = 0; cy < cells; cy += 1) {
      for (let cx = 0; cx < cells; cx += 1) {
        const base = row * cols;
        let best = -1;
        let bestClass = 0;
        const objectness = output[base + 4];
        if (objectness > 0.01) {
          for (let c = 0; c < classes; c += 1) {
            const score = objectness * output[base + 5 + c];
            if (score > best) {
              best = score;
              bestClass = c;
            }
          }
        }
        if (best > 0.2) {
          const w = Math.exp(output[base + 2]) * stride;
          const h = Math.exp(output[base + 3]) * stride;
          boxes.push({
            x: ((output[base] + cx) * stride) - w / 2,
            y: ((output[base + 1] + cy) * stride) - h / 2,
            w,
            h,
            score: best,
            classId: bestClass,
          });
        }
        row += 1;
      }
    }
  }
  return boxes;
}

// Greedy per-class NMS, the same rule the Python side uses.
function suppress(boxes, threshold) {
  const grouped = new Map();
  for (const box of boxes) {
    if (!grouped.has(box.classId)) {
      grouped.set(box.classId, []);
    }
    grouped.get(box.classId).push(box);
  }

  const keep = [];
  for (const group of grouped.values()) {
    group.sort((a, b) => b.score - a.score);
    const taken = [];
    for (const candidate of group) {
      const overlaps = taken.some((kept) => {
        const x1 = Math.max(candidate.x, kept.x);
        const y1 = Math.max(candidate.y, kept.y);
        const x2 = Math.min(candidate.x + candidate.w, kept.x + kept.w);
        const y2 = Math.min(candidate.y + candidate.h, kept.y + kept.h);
        const intersection = Math.max(0, x2 - x1) * Math.max(0, y2 - y1);
        const union = candidate.w * candidate.h + kept.w * kept.h - intersection;
        return union > 0 && intersection / union > threshold;
      });
      if (!overlaps) {
        taken.push(candidate);
      }
    }
    keep.push(...taken);
  }
  return keep;
}

async function detect(frame, scoreThreshold) {
  const geometry = fitFrame(frame);
  const tensor = toTensor();
  const outputs = await session.run({ [inputName]: tensor });
  const result = outputs[session.outputNames[0]];

  const boxes = suppress(
    decode(result.data, result.dims).filter((box) => box.score >= scoreThreshold),
    0.45,
  );

  return boxes
    .map((box) => ({
      label: labels[box.classId] || String(box.classId),
      score: box.score,
      box: [
        (box.x - geometry.dx) / geometry.ratio,
        (box.y - geometry.dy) / geometry.ratio,
        box.w / geometry.ratio,
        box.h / geometry.ratio,
      ],
    }))
    .sort((a, b) => b.score - a.score);
}

self.onmessage = async (event) => {
  const message = event.data;

  if (message.type === 'init') {
    try {
      await load();
    } catch (error) {
      status(`load failed: ${error.message}`);
      post({ type: 'error', text: error.message });
    }
    return;
  }

  if (message.type !== 'frame') {
    return;
  }

  const frame = message.frame;
  if (!session || busy) {
    frame.close();
    return;
  }

  busy = true;
  startedAt = startedAt || performance.now();
  try {
    const detections = await detect(frame, 0.35);
    processed += 1;
    const elapsed = (performance.now() - startedAt) / 1000;
    post({
      type: 'result',
      detections,
      processed,
      fps: processed / Math.max(0.001, elapsed),
      width: frame.displayWidth,
      height: frame.displayHeight,
    });
  } catch (error) {
    status(`inference failed: ${error.message}`);
  } finally {
    frame.close();
    busy = false;
  }
};
