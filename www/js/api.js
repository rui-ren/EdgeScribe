// EDGESCRIBE — API Client
const API = window.location.origin;

export async function health() {
  const res = await fetch(`${API}/v1/health`);
  return res.json();
}

export async function listModels() {
  const res = await fetch(`${API}/v1/models`);
  return res.json();
}

// ── ASR ──
export async function transcribeFile(file) {
  const form = new FormData();
  form.append('audio', file);
  const res = await fetch(`${API}/v1/transcribe/file`, { method: 'POST', body: form });
  return res.json();
}

export async function transcribeStreamStart() {
  const res = await fetch(`${API}/v1/transcribe/stream`, { method: 'POST' });
  return res.json();
}

export async function transcribePush(pcmFloat32Buffer) {
  const res = await fetch(`${API}/v1/transcribe/push`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/octet-stream' },
    body: pcmFloat32Buffer,
  });
  return res.json();
}

export async function transcribeFlush() {
  const res = await fetch(`${API}/v1/transcribe/flush`, { method: 'POST' });
  return res.json();
}

// ── Chat / LLM ──
export async function chat(prompt, system = 'You are a helpful assistant.', maxLength = 2048) {
  const res = await fetch(`${API}/v1/chat`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ prompt, system, max_length: maxLength }),
  });
  return res.json();
}

export async function chatMultiTurn(messages, maxLength = 2048) {
  const res = await fetch(`${API}/v1/chat`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ messages, max_length: maxLength }),
  });
  return res.json();
}

export async function generateSoap(transcript) {
  const res = await fetch(`${API}/v1/chat/soap`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ transcript }),
  });
  return res.json();
}

export async function summarize(text) {
  const res = await fetch(`${API}/v1/chat/summarize`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ text }),
  });
  return res.json();
}

// ── Vision ──
export async function analyzeImage(file, prompt = 'Describe this image in detail.') {
  const form = new FormData();
  form.append('image', file);
  form.append('prompt', prompt);
  const res = await fetch(`${API}/v1/vision/analyze`, { method: 'POST', body: form });
  return res.json();
}

export async function ocrImage(file) {
  const form = new FormData();
  form.append('image', file);
  const res = await fetch(`${API}/v1/vision/ocr`, { method: 'POST', body: form });
  return res.json();
}

// ── TTS ──
export async function synthesize(text, voice = 'af_heart', speed = 1.0) {
  const res = await fetch(`${API}/v1/tts/synthesize`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ text, voice, speed }),
  });
  return res.blob();
}

export async function listVoices() {
  const res = await fetch(`${API}/v1/tts/voices`);
  return res.json();
}

// ── Memory / Sessions ──
export async function getSessions() {
  const res = await fetch(`${API}/v1/memory/sessions`);
  return res.json();
}

export async function getSession(id) {
  const res = await fetch(`${API}/v1/memory/sessions/${id}`);
  return res.json();
}

export async function searchMemory(query) {
  const res = await fetch(`${API}/v1/memory/search`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ query }),
  });
  return res.json();
}

export async function deleteSession(id) {
  const res = await fetch(`${API}/v1/memory/sessions/${id}`, { method: 'DELETE' });
  return res.json();
}
