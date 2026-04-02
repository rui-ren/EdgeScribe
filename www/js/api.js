// EDGESCRIBE — API Client
const API = window.location.origin;

// Helper: parse JSON response, throw clear error if server is unavailable
async function jsonResponse(res) {
  const contentType = res.headers.get('content-type') || '';
  if (!res.ok || !contentType.includes('application/json')) {
    throw new Error('Server not available. Start the backend with: edgescribe serve');
  }
  return res.json();
}

export async function health() {
  const res = await fetch(`${API}/v1/health`);
  return jsonResponse(res);
}

export async function listModels() {
  const res = await fetch(`${API}/v1/models`);
  return jsonResponse(res);
}

// ── ASR ──
export async function transcribeFile(file) {
  const form = new FormData();
  form.append('audio', file);
  const res = await fetch(`${API}/v1/transcribe/file`, { method: 'POST', body: form });
  return jsonResponse(res);
}

export async function transcribeStreamStart() {
  const res = await fetch(`${API}/v1/transcribe/stream`, { method: 'POST' });
  return jsonResponse(res);
}

export async function transcribePush(pcmFloat32Buffer) {
  const res = await fetch(`${API}/v1/transcribe/push`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/octet-stream' },
    body: pcmFloat32Buffer,
  });
  return jsonResponse(res);
}

export async function transcribeFlush() {
  const res = await fetch(`${API}/v1/transcribe/flush`, { method: 'POST' });
  return jsonResponse(res);
}

// ── Chat / LLM ──
export async function chat(prompt, system = 'You are a helpful assistant.', maxLength = 2048) {
  const res = await fetch(`${API}/v1/chat`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ prompt, system, max_length: maxLength }),
  });
  return jsonResponse(res);
}

export async function chatMultiTurn(messages, maxLength = 2048) {
  const res = await fetch(`${API}/v1/chat`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ messages, max_length: maxLength }),
  });
  return jsonResponse(res);
}

// Streaming chat — calls onToken for each token, returns full text at end
export async function chatStream(messages, onToken, maxLength = 2048, sessionId = null) {
  const body = { messages, max_length: maxLength };
  if (sessionId) body.session_id = sessionId;

  const res = await fetch(`${API}/v1/chat/stream`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(body),
  });

  if (!res.ok) {
    throw new Error('Server not available. Start the backend with: edgescribe serve');
  }

  const reader = res.body.getReader();
  const decoder = new TextDecoder();
  let fullText = '';
  let buffer = '';

  while (true) {
    const { done, value } = await reader.read();
    if (done) break;

    buffer += decoder.decode(value, { stream: true });
    const lines = buffer.split('\n');
    buffer = lines.pop() || '';

    for (const line of lines) {
      if (!line.startsWith('data: ')) continue;
      try {
        const data = JSON.parse(line.slice(6));
        if (data.error) throw new Error(data.error);
        if (data.done) {
          fullText = data.text || fullText;
          sessionId = data.session_id || sessionId;
        } else if (data.token) {
          fullText += data.token;
          onToken(data.token);
        }
      } catch (e) {
        if (e.message !== 'Unexpected end of JSON input') throw e;
      }
    }
  }

  return { text: fullText, session_id: sessionId };
}

export async function generateSoap(transcript) {
  const res = await fetch(`${API}/v1/chat/soap`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ transcript }),
  });
  return jsonResponse(res);
}

export async function summarize(text) {
  const res = await fetch(`${API}/v1/chat/summarize`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ text }),
  });
  return jsonResponse(res);
}

// ── Vision ──
export async function analyzeImage(file, prompt = 'Describe this image in detail.') {
  const form = new FormData();
  form.append('image', file);
  form.append('prompt', prompt);
  const res = await fetch(`${API}/v1/vision/analyze`, { method: 'POST', body: form });
  return jsonResponse(res);
}

export async function ocrImage(file) {
  const form = new FormData();
  form.append('image', file);
  const res = await fetch(`${API}/v1/vision/ocr`, { method: 'POST', body: form });
  return jsonResponse(res);
}

// ── TTS ──
export async function synthesize(text, voice = 'af_heart', speed = 1.0) {
  const res = await fetch(`${API}/v1/tts/synthesize`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ text, voice, speed }),
  });
  if (!res.ok) throw new Error('Server not available. Start the backend with: edgescribe serve');
  return res.blob();
}

export async function listVoices() {
  const res = await fetch(`${API}/v1/tts/voices`);
  return jsonResponse(res);
}

// ── Memory / Sessions ──
export async function getSessions() {
  const res = await fetch(`${API}/v1/memory/sessions`);
  return jsonResponse(res);
}

export async function getSession(id) {
  const res = await fetch(`${API}/v1/memory/sessions/${id}`);
  return jsonResponse(res);
}

export async function searchMemory(query) {
  const res = await fetch(`${API}/v1/memory/search`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ query }),
  });
  return jsonResponse(res);
}

export async function deleteSession(id) {
  const res = await fetch(`${API}/v1/memory/sessions/${id}`, { method: 'DELETE' });
  return jsonResponse(res);
}
