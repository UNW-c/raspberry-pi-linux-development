// PRTS detector page: play the WebRTC stream and draw detection boxes on top.
//
// Step 1 (this file): the overlay is driven by a placeholder box, so the only
// thing being verified is that boxes land exactly on the picture and follow it
// when the window is resized. Inference comes in step 2 and only replaces the
// `detections` array.

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
let detections = [];
let sourceSize = { width: 0, height: 0 };

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

// A placeholder that moves on its own: if it stays glued to the picture while
// the window resizes, the coordinate mapping is right.
function placeholder(now) {
  const width = sourceSize.width;
  const height = sourceSize.height;
  if (!width) {
    return [];
  }
  const span = width * 0.5;
  const x = (Math.sin(now / 1600) * 0.5 + 0.5) * (width - span);
  const y = (Math.sin(now / 2300) * 0.5 + 0.5) * (height * 0.5);
  return [{ label: 'placeholder', score: 0.99, box: [x, y, span, height * 0.3] }];
}

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

  // Wait for the ICE gathering to settle so the offer already carries the
  // candidates; mediamtx accepts a non-trickle WHEP offer.
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

  const answer = await response.text();
  await peer.setRemoteDescription({ type: 'answer', sdp: answer });
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

async function waitForStream(timeout = 8000) {
  const deadline = performance.now() + timeout;
  while (performance.now() < deadline) {
    if (video.srcObject && video.srcObject.getVideoTracks().length > 0) {
      return video.srcObject;
    }
    await new Promise((resolve) => setTimeout(resolve, 100));
  }
  throw new Error('no video track arrived');
}

// Newest decoded frame straight from the WebRTC pipeline. Chromium only; the
// fallback uses requestVideoFrameCallback, which still counts every frame.
async function watchFrames() {
  const stream = await waitForStream();

  if (!('MediaStreamTrackProcessor' in window)) {
    status.note.textContent = 'MediaStreamTrackProcessor missing: using the fallback';
    const tick = () => {
      countFrame();
      video.requestVideoFrameCallback(tick);
    };
    video.requestVideoFrameCallback(tick);
    return;
  }

  const track = stream.getVideoTracks()[0];
  const processor = new MediaStreamTrackProcessor({ track });
  const reader = processor.readable.getReader();
  for (;;) {
    const { value: frame, done } = await reader.read();
    if (done) {
      break;
    }
    countFrame();
    frame.close();
  }
}

// ------------------------------------------------------------------ boot ----
video.addEventListener('loadedmetadata', () => {
  sourceSize = { width: video.videoWidth, height: video.videoHeight };
  status.size.textContent = `${sourceSize.width}x${sourceSize.height}`;
  layout();
});

new ResizeObserver(() => {
  layout();
  draw();
}).observe(stage);

// Placeholder loop: replace `placeholder(now)` with the worker results later.
function tick(now) {
  detections = placeholder(now);
  status.detect.textContent = 'placeholder';
  draw();
  requestAnimationFrame(tick);
}
requestAnimationFrame(tick);

connect()
  .then(() => watchFrames())
  .catch((error) => {
    status.whep.textContent = 'failed';
    status.note.textContent = `${error.message} - is start-stream running?`;
  });
