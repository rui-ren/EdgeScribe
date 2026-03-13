// EDGESCRIBE — Main App Controller
import * as api from './api.js';

// ── State ──
let currentPage = 'dashboard';
let engines = { asr: false, llm: false, vision: false, tts: false };
let recording = false;
let audioContext = null;
let mediaStream = null;
let processor = null;
let recordingStartTime = null;
let chunkCount = 0;

// ── Router ──
export function navigate(page) {
  document.querySelectorAll('.page').forEach(p => p.classList.remove('active'));
  document.querySelectorAll('.nav-item').forEach(n => n.classList.remove('active'));

  const pageEl = document.getElementById(`page-${page}`);
  const navEl = document.querySelector(`.nav-item[data-page="${page}"]`);

  if (pageEl) pageEl.classList.add('active');
  if (navEl) navEl.classList.add('active');

  currentPage = page;
}

// ── Init ──
export async function init() {
  // Set up navigation
  document.querySelectorAll('.nav-item[data-page]').forEach(el => {
    el.addEventListener('click', (e) => {
      e.preventDefault();
      navigate(el.dataset.page);
    });
  });

  // Check server health
  await refreshHealth();

  // Set up page-specific handlers
  setupDashboard();
  setupLiveTranscription();
  setupFileTranscription();
  setupChat();
  setupVision();
  setupTTS();

  // Navigate to dashboard
  navigate('dashboard');
}

// ── Health check ──
async function refreshHealth() {
  try {
    const data = await api.health();
    engines = data.engines;
    updateEngineStatus();
  } catch (e) {
    console.error('Server not reachable:', e);
  }
}

function updateEngineStatus() {
  ['asr', 'llm', 'vision', 'tts'].forEach(key => {
    const dot = document.getElementById(`status-${key}`);
    if (dot) {
      dot.classList.toggle('active', engines[key]);
    }
    // Update dashboard cards
    const card = document.getElementById(`card-${key}`);
    if (card) {
      const statusEl = card.querySelector('.status');
      if (statusEl) {
        statusEl.textContent = engines[key] ? '● Ready' : '○ Not loaded';
        statusEl.classList.toggle('ready', engines[key]);
      }
    }
  });
}

// ── Dashboard ──
function setupDashboard() {
  document.querySelectorAll('.stat-card[data-page]').forEach(card => {
    card.addEventListener('click', () => navigate(card.dataset.page));
  });
}

// ── Live Transcription ──
function setupLiveTranscription() {
  const recordBtn = document.getElementById('record-btn');
  const transcriptEl = document.getElementById('live-transcript');
  const durationEl = document.getElementById('live-duration');
  const chunksEl = document.getElementById('live-chunks');
  const copyBtn = document.getElementById('live-copy');
  const saveBtn = document.getElementById('live-save');
  const soapBtn = document.getElementById('live-soap');
  const clearBtn = document.getElementById('live-clear');

  if (!recordBtn) return;

  recordBtn.addEventListener('click', async () => {
    if (!recording) {
      await startRecording(transcriptEl, durationEl, chunksEl);
      recordBtn.classList.add('recording');
    } else {
      await stopRecording(transcriptEl);
      recordBtn.classList.remove('recording');
    }
  });

  copyBtn?.addEventListener('click', () => {
    const text = transcriptEl.textContent.replace('▊', '').trim();
    navigator.clipboard.writeText(text);
    copyBtn.textContent = '✓ Copied';
    setTimeout(() => copyBtn.textContent = '📋 Copy', 1500);
  });

  saveBtn?.addEventListener('click', () => {
    const text = transcriptEl.textContent.replace('▊', '').trim();
    downloadText(text, 'transcript.txt');
  });

  soapBtn?.addEventListener('click', async () => {
    const text = transcriptEl.textContent.replace('▊', '').trim();
    if (!text) return;
    soapBtn.textContent = '⏳ Generating...';
    soapBtn.disabled = true;
    try {
      const result = await api.generateSoap(text);
      transcriptEl.textContent = result.text;
    } catch (e) {
      alert('Error: ' + e.message);
    }
    soapBtn.textContent = '📝 SOAP Notes';
    soapBtn.disabled = false;
  });

  clearBtn?.addEventListener('click', () => {
    transcriptEl.innerHTML = '<span class="placeholder">Click the red button to start recording...</span>';
  });
}

async function startRecording(transcriptEl, durationEl, chunksEl) {
  recording = true;
  chunkCount = 0;
  recordingStartTime = Date.now();
  transcriptEl.innerHTML = '<span class="cursor"></span>';

  // Start server-side stream
  await api.transcribeStreamStart();

  // Start mic
  try {
    mediaStream = await navigator.mediaDevices.getUserMedia({
      audio: { sampleRate: 16000, channelCount: 1, echoCancellation: true }
    });
  } catch (e) {
    alert('Microphone access denied. Please allow microphone access.');
    recording = false;
    return;
  }

  audioContext = new AudioContext({ sampleRate: 16000 });
  const source = audioContext.createMediaStreamSource(mediaStream);
  processor = audioContext.createScriptProcessor(4096, 1, 1);

  processor.onaudioprocess = async (e) => {
    if (!recording) return;
    const pcm = e.inputBuffer.getChannelData(0);
    chunkCount++;

    // Update stats
    const elapsed = ((Date.now() - recordingStartTime) / 1000).toFixed(0);
    if (durationEl) durationEl.textContent = formatDuration(parseInt(elapsed));
    if (chunksEl) chunksEl.textContent = chunkCount;

    try {
      const result = await api.transcribePush(pcm.buffer);
      if (result.transcript) {
        transcriptEl.innerHTML = result.transcript + '<span class="cursor"></span>';
        transcriptEl.scrollTop = transcriptEl.scrollHeight;
      }
    } catch (e) {
      // Server may be busy, skip this chunk
    }
  };

  source.connect(processor);
  processor.connect(audioContext.destination);
}

async function stopRecording(transcriptEl) {
  recording = false;

  processor?.disconnect();
  mediaStream?.getTracks().forEach(t => t.stop());
  audioContext?.close();
  processor = null;
  mediaStream = null;
  audioContext = null;

  // Flush
  try {
    const result = await api.transcribeFlush();
    if (result.transcript) {
      transcriptEl.textContent = result.transcript;
    }
  } catch (e) {
    console.error('Flush error:', e);
  }
}

// ── File Transcription ──
function setupFileTranscription() {
  const dropZone = document.getElementById('file-drop-zone');
  const fileInput = document.getElementById('file-input');
  const progressBar = document.getElementById('file-progress');
  const resultEl = document.getElementById('file-result');
  const copyBtn = document.getElementById('file-copy');
  const saveBtn = document.getElementById('file-save');

  if (!dropZone) return;

  dropZone.addEventListener('click', () => fileInput.click());
  dropZone.addEventListener('dragover', (e) => { e.preventDefault(); dropZone.classList.add('dragover'); });
  dropZone.addEventListener('dragleave', () => dropZone.classList.remove('dragover'));
  dropZone.addEventListener('drop', (e) => {
    e.preventDefault();
    dropZone.classList.remove('dragover');
    if (e.dataTransfer.files.length) handleFileUpload(e.dataTransfer.files[0], progressBar, resultEl);
  });

  fileInput.addEventListener('change', () => {
    if (fileInput.files.length) handleFileUpload(fileInput.files[0], progressBar, resultEl);
  });

  copyBtn?.addEventListener('click', () => {
    navigator.clipboard.writeText(resultEl.textContent);
    copyBtn.textContent = '✓ Copied';
    setTimeout(() => copyBtn.textContent = '📋 Copy', 1500);
  });

  saveBtn?.addEventListener('click', () => downloadText(resultEl.textContent, 'transcript.txt'));
}

async function handleFileUpload(file, progressBar, resultEl) {
  resultEl.innerHTML = '<span class="placeholder">Processing...</span>';
  if (progressBar) {
    progressBar.style.display = 'block';
    progressBar.querySelector('.fill').style.width = '30%';
  }

  try {
    const result = await api.transcribeFile(file);
    if (progressBar) progressBar.querySelector('.fill').style.width = '100%';
    resultEl.textContent = result.text;
    setTimeout(() => { if (progressBar) progressBar.style.display = 'none'; }, 1000);
  } catch (e) {
    resultEl.textContent = 'Error: ' + e.message;
    if (progressBar) progressBar.style.display = 'none';
  }
}

// ── Chat ──
function setupChat() {
  const messagesEl = document.getElementById('chat-messages');
  const inputEl = document.getElementById('chat-input');
  const sendBtn = document.getElementById('chat-send');

  if (!inputEl) return;

  const sendMessage = async () => {
    const text = inputEl.value.trim();
    if (!text) return;
    inputEl.value = '';

    appendMessage(messagesEl, 'user', text);

    const thinkingEl = appendMessage(messagesEl, 'assistant', '⏳ Thinking...');

    try {
      const result = await api.chat(text);
      thinkingEl.querySelector('.message-content').textContent = result.text;
    } catch (e) {
      thinkingEl.querySelector('.message-content').textContent = '❌ Error: ' + e.message;
    }
  };

  sendBtn?.addEventListener('click', sendMessage);
  inputEl.addEventListener('keydown', (e) => {
    if (e.key === 'Enter' && !e.shiftKey) { e.preventDefault(); sendMessage(); }
  });
}

function appendMessage(container, role, text) {
  const div = document.createElement('div');
  div.className = 'message';
  div.innerHTML = `
    <div class="message-avatar ${role}">${role === 'user' ? '👤' : '🤖'}</div>
    <div class="message-content">${escapeHtml(text)}</div>
  `;
  container.appendChild(div);
  container.scrollTop = container.scrollHeight;
  return div;
}

// ── Vision ──
function setupVision() {
  const imageInput = document.getElementById('vision-image-input');
  const imagePreview = document.getElementById('vision-image-preview');
  const promptInput = document.getElementById('vision-prompt');
  const analyzeBtn = document.getElementById('vision-analyze');
  const ocrBtn = document.getElementById('vision-ocr');
  const resultEl = document.getElementById('vision-result');
  const uploadBtn = document.getElementById('vision-upload-btn');

  if (!imageInput) return;

  uploadBtn?.addEventListener('click', () => imageInput.click());
  imageInput.addEventListener('change', () => {
    if (imageInput.files.length) {
      const url = URL.createObjectURL(imageInput.files[0]);
      imagePreview.src = url;
      imagePreview.style.display = 'block';
    }
  });

  analyzeBtn?.addEventListener('click', async () => {
    if (!imageInput.files.length) { alert('Please upload an image first.'); return; }
    const prompt = promptInput?.value || 'Describe this image in detail.';
    resultEl.innerHTML = '<span class="placeholder">Analyzing...</span>';
    analyzeBtn.disabled = true;
    try {
      const result = await api.analyzeImage(imageInput.files[0], prompt);
      resultEl.textContent = result.text;
    } catch (e) {
      resultEl.textContent = '❌ Error: ' + e.message;
    }
    analyzeBtn.disabled = false;
  });

  ocrBtn?.addEventListener('click', async () => {
    if (!imageInput.files.length) { alert('Please upload an image first.'); return; }
    resultEl.innerHTML = '<span class="placeholder">Extracting text...</span>';
    ocrBtn.disabled = true;
    try {
      const result = await api.ocrImage(imageInput.files[0]);
      resultEl.textContent = result.text;
    } catch (e) {
      resultEl.textContent = '❌ Error: ' + e.message;
    }
    ocrBtn.disabled = false;
  });
}

// ── TTS ──
function setupTTS() {
  const textInput = document.getElementById('tts-text');
  const playBtn = document.getElementById('tts-play');
  const saveBtn = document.getElementById('tts-save');
  const audioPlayer = document.getElementById('tts-audio');

  if (!textInput) return;

  playBtn?.addEventListener('click', async () => {
    const text = textInput.value.trim();
    if (!text) return;
    playBtn.textContent = '⏳ Synthesizing...';
    playBtn.disabled = true;
    try {
      const blob = await api.synthesize(text);
      const url = URL.createObjectURL(blob);
      audioPlayer.src = url;
      audioPlayer.style.display = 'block';
      audioPlayer.play();
    } catch (e) {
      alert('Error: ' + e.message);
    }
    playBtn.textContent = '▶ Play';
    playBtn.disabled = false;
  });

  saveBtn?.addEventListener('click', async () => {
    const text = textInput.value.trim();
    if (!text) return;
    saveBtn.textContent = '⏳ Saving...';
    saveBtn.disabled = true;
    try {
      const blob = await api.synthesize(text);
      const url = URL.createObjectURL(blob);
      const a = document.createElement('a');
      a.href = url;
      a.download = 'speech.wav';
      a.click();
    } catch (e) {
      alert('Error: ' + e.message);
    }
    saveBtn.textContent = '💾 Save WAV';
    saveBtn.disabled = false;
  });
}

// ── Helpers ──
function formatDuration(seconds) {
  const m = Math.floor(seconds / 60);
  const s = seconds % 60;
  return `${m}:${s.toString().padStart(2, '0')}`;
}

function escapeHtml(text) {
  const div = document.createElement('div');
  div.textContent = text;
  return div.innerHTML;
}

function downloadText(text, filename) {
  const blob = new Blob([text], { type: 'text/plain' });
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = filename;
  a.click();
  URL.revokeObjectURL(url);
}

// ── Start ──
document.addEventListener('DOMContentLoaded', init);
