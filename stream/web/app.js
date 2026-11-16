// PRTS detector page: play the WebRTC stream and draw detection boxes on top.
//
// The picture is the browser's own decode - the same frames the plain viewer
// would show. Frames are handed to a worker that runs YOLOX, and only the boxes
// come back, so inference never slows the picture down.

const params = new URLSearchParams(location.search);
// Same origin on purpose: serve.py forwards it to mediamtx, which avoids the
// CORS preflight mediamtx would not answer.
const WHEP_URL = params.get('whep') || '/whep';

const video = document.getElementById('video');
const overlay = document.getElementById('overlay');
const stage = document.getElementById('stage');
const status = {
  whep: document.getElementById('whep'),
  size: document.getElementById('size'),
  frames: document.getElementById('frames'),
  fps: document.getElementById('fps'),
  detect: document.getElementById('detect'),
  note: document.getElementById('note'),
};

const context = overlay.getContext('2d');
let detections = [];
let sourceSize = { width: 0, height: 0 };

// ---------------------------------------------------------------- layout ----
// The canvas is sized to the video's *content* box (the visible picture, not the
// element), in CSS pixels scaled by devicePixelRatio for sharp lines.
function layout() {
  const width = video.videoWidth;
  const height = video.videoHeight;
  if (!width || !height) {
    return;
  }

  const rect = stage.getBoundingClientRect();
  const pictureAspect = width / height;
  const boxAspect = rect.width / rect.height;

  let drawWidth = rect.width;
  let drawHeight = rect.height;
  if (boxAspect > pictureAspect) {
    drawWidth = rect.height * pictureAspect;
  } else {
    drawHeight = rect.width / pictureAspect;
  }

  overlay.style.left = `${(rect.width - drawWidth) / 2}px`;
  overlay.style.top = `${(rect.height - drawHeight) / 2}px`;
  overlay.style.width = `${drawWidth}px`;
  overlay.style.height = `${drawHeight}px`;

  const ratio = window.devicePixelRatio || 1;
  overlay.width = Math.round(drawWidth * ratio);
  overlay.height = Math.round(drawHeight * ratio);
}

// --------------------------------------------------------------- overlay ----
// `detections` uses source pixels, the same coordinate space as the model.
function draw() {
  const ratio = window.devicePixelRatio || 1;
  context.setTransform(ratio, 0, 0, ratio, 0, 0);
  context.clearRect(0, 0, overlay.width / ratio, overlay.height / ratio);

  if (!sourceSize.width || !sourceSize.height || overlay.width === 0) {
    return;
  }

  const scale = overlay.width / ratio / sourceSize.width;
  context.lineWidth = Math.max(1, Math.round(2 * scale));
  context.strokeStyle = '#a0dcff';
  context.fillStyle = '#a0dcff';
  context.font = `${Math.max(11, Math.round(14 * scale))}px ui-monospace, monospace`;

  for (const item of detections) {
    const [x, y, w, h] = item.box;
    context.strokeRect(x * scale, y * scale, w * scale, h * scale);
    const caption = `${item.label} ${item.score.toFixed(2)}`;
    context.fillText(caption, x * scale, Math.max(12, y * scale - 4));
  }
}

// ---------------------------------------------------------------- worker ----
const worker = new Worker('./worker.js', { type: 'module' });

worker.onmessage = (event) => {
  const message = event.data;
  if (message.type === 'result') {
    detections = message.detections;
    status.detect.textContent = `${message.processed} @ ${message.fps.toFixed(1)} fps`;
    draw();
  } else if (message.type === 'status') {
    status.note.textContent = message.text;
  } else if (message.type === 'error') {
    status.note.textContent = `detector: ${message.text}`;
  }
};

// ------------------------------------------------------------------ WHEP ----
async function connect() {
  const peer = new RTCPeerConnection({ iceServers: [] });
  peer.addTransceiver('video', { direction: 'recvonly' });

  peer.ontrack = (event) => {
    const stream = event.streams[0] || new MediaStream([event.track]);
    video.srcObject = stream;
    status.whep.textContent = 'connected';
    video.play().catch(() => {
      status.note.textContent = 'press play: the browser blocked autoplay';
    });
  };

  peer.onconnectionstatechange = () => {
    status.whep.textContent = peer.connectionState;
    if (['failed', 'disconnected'].includes(peer.connectionState)) {
      status.note.textContent = 'connection lost';
    }
  };

  const offer = await peer.createOffer();
  await peer.setLocalDescription(offer);

  // Wait for ICE gathering so the offer already carries its candidates;
  // mediamtx accepts a non-trickle WHEP offer.
  await new Promise((resolve) => {
    if (peer.iceGatheringState === 'complete') {
      resolve();
      return;
    }
    const done = () => {
      if (peer.iceGatheringState === 'complete') {
        peer.removeEventListener('icegatheringstatechange', done);
        resolve();
      }
    };
    peer.addEventListener('icegatheringstatechange', done);
    setTimeout(resolve, 2000);
  });

  const response = await fetch(WHEP_URL, {
    method: 'POST',
    headers: { 'Content-Type': 'application/sdp' },
    body: peer.localDescription.sdp,
  });
  if (!response.ok) {
    throw new Error(`WHEP ${response.status} ${response.statusText}`);
  }
  await peer.setRemoteDescription({ type: 'answer', sdp: await response.text() });
  return peer;
}

// --------------------------------------------------------------- counters ----
let frameCount = 0;
let fpsWindowStart = performance.now();
let fpsFrames = 0;

function countFrame() {
  frameCount += 1;
  fpsFrames += 1;
  const now = performance.now();
  if (now - fpsWindowStart >= 1000) {
    status.fps.textContent = String(fpsFrames);
    fpsFrames = 0;
    fpsWindowStart = now;
  }
  status.frames.textContent = String(frameCount);
}

function waitForStream(timeout = 8000) {
  return new Promise((resolve, reject) => {
    const deadline = performance.now() + timeout;
    const poll = () => {
      if (video.srcObject && video.srcObject.getVideoTracks().length > 0) {
        resolve(video.srcObject);
        return;
      }
      if (performance.now() > deadline) {
        reject(new Error('no video track arrived'));
        return;
      }
      setTimeout(poll, 100);
    };
    poll();
  });
}

// Newest decoded frame straight from the WebRTC pipeline, handed to the worker
// without a copy. Chromium only; the fallback builds a VideoFrame from the
// element, which costs one extra copy per frame.
async function pumpFrames() {
  const stream = await waitForStream();

  if ('MediaStreamTrackProcessor' in window) {
    const processor = new MediaStreamTrackProcessor({ track: stream.getVideoTracks()[0] });
    const reader = processor.readable.getReader();
    for (;;) {
      const { value: frame, done } = await reader.read();
      if (done) {
        break;
      }
      countFrame();
      worker.postMessage({ type: 'frame', frame }, [frame]);
    }
    return;
  }

  status.note.textContent = 'no MediaStreamTrackProcessor: frames go through the main thread';
  const tick = () => {
    if (video.readyState >= 2) {
      countFrame();
      let frame = null;
      try {
        frame = new VideoFrame(video);
      } catch (error) {
        status.note.textContent = `VideoFrame failed: ${error.message}`;
      }
      if (frame) {
        worker.postMessage({ type: 'frame', frame }, [frame]);
      }
    }
    video.requestVideoFrameCallback(tick);
  };
  video.requestVideoFrameCallback(tick);
}

// ------------------------------------------------------------------ boot ----
video.addEventListener('loadedmetadata', () => {
  sourceSize = { width: video.videoWidth, height: video.videoHeight };
  status.size.textContent = `${sourceSize.width}x${sourceSize.height}`;
  layout();
  draw();
});

new ResizeObserver(() => {
  layout();
  draw();
}).observe(stage);

worker.postMessage({ type: 'init' });

connect()
  .then(() => pumpFrames())
  .catch((error) => {
    status.whep.textContent = 'failed';
    status.note.textContent = `${error.message} - is start-stream running?`;
  });
