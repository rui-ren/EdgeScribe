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
let chatSystemPrompt = 'You are a helpful assistant.';
let analyserNode = null;
let waveformAnimId = null;
let currentSessionId = null;

// Configure markdown renderer (loaded via script tag in index.html)
const marked = window.marked?.marked || { parse: (t) => t, setOptions: () => {} };
marked.setOptions({ breaks: true, gfm: true });
let chatHistory = [{ role: 'system', content: 'You are a helpful assistant.' }];

// ── Toast notifications ──
function showToast(message, type = 'info') {
  const container = document.getElementById('toast-container');
  if (!container) return;
  const toast = document.createElement('div');
  toast.className = `toast ${type}`;
  toast.textContent = message;
  container.appendChild(toast);
  setTimeout(() => toast.remove(), 3200);
}

// ── Theme ──
function applyTheme(theme) {
  document.documentElement.setAttribute('data-theme', theme);
  const btn = document.getElementById('theme-toggle');
  if (btn) btn.textContent = theme === 'dark' ? '☀️' : '🌙';
}

// ── Router ──
export function navigate(page) {
  document.querySelectorAll('.page').forEach(p => p.classList.remove('active'));
  document.querySelectorAll('.nav-item').forEach(n => n.classList.remove('active'));

  const pageEl = document.getElementById(`page-${page}`);
  const navEl = document.querySelector(`.nav-item[data-page="${page}"]`);

  if (pageEl) pageEl.classList.add('active');
  if (navEl) navEl.classList.add('active');

  currentPage = page;

  // Close mobile sidebar on navigate
  closeMobileSidebar();
}

// ── Mobile sidebar ──
function closeMobileSidebar() {
  document.getElementById('sidebar')?.classList.remove('open');
  document.getElementById('sidebar-overlay')?.classList.remove('open');
}

// ── Init ──
export async function init() {
  // Apply saved theme
  applyTheme(localStorage.getItem('edgescribe-theme') || 'dark');

  // Theme toggle
  const themeToggle = document.getElementById('theme-toggle');
  if (themeToggle) {
    themeToggle.addEventListener('click', () => {
      const current = document.documentElement.getAttribute('data-theme') || 'dark';
      const next = current === 'dark' ? 'light' : 'dark';
      applyTheme(next);
      localStorage.setItem('edgescribe-theme', next);
    });
  }

  // Mobile sidebar toggle
  const sidebarToggle = document.getElementById('sidebar-toggle');
  const sidebar = document.getElementById('sidebar');
  const overlay = document.getElementById('sidebar-overlay');

  if (sidebarToggle && sidebar) {
    sidebarToggle.addEventListener('click', () => {
      sidebar.classList.toggle('open');
      overlay?.classList.toggle('open');
    });
  }
  if (overlay) {
    overlay.addEventListener('click', closeMobileSidebar);
  }

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
  setupKeyboardShortcuts();
  setupHistory();
  setupOnboarding();

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
    const card = document.getElementById(`card-${key}`);
    if (card) {
      const statusEl = card.querySelector('.status');
      if (statusEl) {
        statusEl.textContent = engines[key] ? '✓ Ready to use' : '○ Not available';
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
  document.querySelectorAll('.quick-action[data-page]').forEach(btn => {
    btn.addEventListener('click', () => navigate(btn.dataset.page));
  });
}

// ── Live Transcription ──
function setupLiveTranscription() {
  const recordBtn = document.getElementById('record-btn');
  const recordHero = document.getElementById('record-hero');
  const statusText = document.getElementById('live-status-text');
  const hintText = document.getElementById('live-hint');
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
      recordHero?.classList.add('is-recording');
      if (statusText) statusText.textContent = '● Recording...';
      if (hintText) hintText.textContent = 'Tap the button again to stop';
    } else {
      await stopRecording(transcriptEl);
      recordBtn.classList.remove('recording');
      recordHero?.classList.remove('is-recording');
      if (statusText) statusText.textContent = 'Recording complete';
      if (hintText) hintText.textContent = 'Tap to record again, or use the buttons below';
    }
  });

  copyBtn?.addEventListener('click', () => {
    const text = transcriptEl.textContent.replace('▊', '').trim();
    navigator.clipboard.writeText(text);
    showToast('Copied to clipboard!', 'success');
  });

  saveBtn?.addEventListener('click', () => {
    const text = transcriptEl.textContent.replace('▊', '').trim();
    downloadText(text, 'transcript.txt');
    showToast('Transcript saved!', 'success');
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
      showToast('Error generating SOAP notes: ' + e.message, 'error');
    }
    soapBtn.textContent = '📝 Generate SOAP Notes';
    soapBtn.disabled = false;
  });

  clearBtn?.addEventListener('click', () => {
    transcriptEl.innerHTML = '<span class="placeholder">Your transcript will appear here once you start recording.</span>';
    if (statusText) statusText.textContent = 'Tap to start recording';
    if (hintText) hintText.textContent = 'Your words will appear above as you speak';
  });
}

async function startRecording(transcriptEl, durationEl, chunksEl) {
  recording = true;
  chunkCount = 0;
  recordingStartTime = Date.now();
  transcriptEl.innerHTML = '<span class="cursor"></span>';

  await api.transcribeStreamStart();

  try {
    mediaStream = await navigator.mediaDevices.getUserMedia({
      audio: { sampleRate: 16000, channelCount: 1, echoCancellation: true }
    });
  } catch (e) {
    showToast('Microphone access is needed for recording. Please allow it in your browser.', 'error');
    recording = false;
    return;
  }

  audioContext = new AudioContext({ sampleRate: 16000 });
  const source = audioContext.createMediaStreamSource(mediaStream);
  processor = audioContext.createScriptProcessor(4096, 1, 1);

  // Waveform visualizer
  analyserNode = audioContext.createAnalyser();
  analyserNode.fftSize = 256;
  source.connect(analyserNode);
  startWaveformDraw();

  processor.onaudioprocess = async (e) => {
    if (!recording) return;
    const pcm = e.inputBuffer.getChannelData(0);
    chunkCount++;

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
  stopWaveformDraw();

  processor?.disconnect();
  analyserNode?.disconnect();
  mediaStream?.getTracks().forEach(t => t.stop());
  audioContext?.close();
  processor = null;
  analyserNode = null;
  mediaStream = null;
  audioContext = null;

  try {
    const result = await api.transcribeFlush();
    if (result.transcript) {
      transcriptEl.textContent = result.transcript;
      saveTranscript(result.transcript, 'live');
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
    showToast('Copied to clipboard!', 'success');
  });

  saveBtn?.addEventListener('click', () => {
    downloadText(resultEl.textContent, 'transcript.txt');
    showToast('Transcript saved!', 'success');
  });
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
    saveTranscript(result.text, 'file');
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
  const clearBtn = document.getElementById('chat-clear');
  const copyBtn = document.getElementById('chat-copy');
  const exportBtn = document.getElementById('chat-export');
  const systemToggle = document.getElementById('system-prompt-toggle');
  const systemArea = document.getElementById('system-prompt-area');
  const systemInput = document.getElementById('system-prompt-input');

  if (!inputEl) return;

  // Session sidebar controls
  const newChatBtn = document.getElementById('session-new');
  const searchInput = document.getElementById('session-search');

  newChatBtn?.addEventListener('click', () => {
    startNewChat();
    inputEl.focus();
  });

  let searchTimeout = null;
  searchInput?.addEventListener('input', () => {
    clearTimeout(searchTimeout);
    searchTimeout = setTimeout(() => searchSessions(searchInput.value), 400);
  });

  // Load session list from backend
  loadSessionList();

  // System prompt toggle
  if (systemToggle && systemArea) {
    systemToggle.addEventListener('click', () => {
      const isOpen = systemArea.style.display !== 'none';
      systemArea.style.display = isOpen ? 'none' : 'block';
      systemToggle.textContent = isOpen ? '⚙️ System Prompt ▼' : '⚙️ System Prompt ▲';
    });
  }

  if (systemInput) {
    systemInput.addEventListener('input', () => {
      chatSystemPrompt = systemInput.value.trim() || 'You are a helpful assistant.';
      if (chatHistory.length > 0 && chatHistory[0].role === 'system') {
        chatHistory[0].content = chatSystemPrompt;
      }
    });
  }

  let chatImageFile = null;

  const sendMessage = async () => {
    const text = inputEl.value.trim();
    if (!text && !chatImageFile) return;
    inputEl.value = '';
    inputEl.style.height = 'auto';

    if (chatImageFile) {
      // Vision mode — send image + prompt
      const prompt = text || 'Describe this image in detail.';
      const userDiv = document.createElement('div');
      userDiv.className = 'message';
      const imgUrl = URL.createObjectURL(chatImageFile);
      userDiv.innerHTML = `
        <div class="message-avatar user">👤</div>
        <div class="message-content">
          <img src="${imgUrl}" class="chat-inline-image" alt="Uploaded image">
          <p>${escapeHtml(prompt)}</p>
        </div>
      `;
      messagesEl.appendChild(userDiv);
      messagesEl.scrollTop = messagesEl.scrollHeight;

      const thinkingEl = appendMessage(messagesEl, 'assistant', '');
      const contentEl = thinkingEl.querySelector('.message-content');
      contentEl.classList.add('streaming');

      try {
        const result = await api.analyzeImage(chatImageFile, prompt);
        contentEl.classList.remove('streaming');
        contentEl.innerHTML = marked.parse(result.text);
      } catch (e) {
        contentEl.classList.remove('streaming');
        contentEl.textContent = '❌ Error: ' + e.message;
      }

      chatImageFile = null;
      const preview = document.getElementById('chat-image-preview');
      if (preview) preview.style.display = 'none';
      inputEl.placeholder = 'Type a message and press Enter to send...';
    } else {
      // Text-only chat mode
      appendMessage(messagesEl, 'user', text);
      chatHistory.push({ role: 'user', content: text });
      updateContextIndicator();

      const thinkingEl = appendMessage(messagesEl, 'assistant', '');
      const contentEl = thinkingEl.querySelector('.message-content');
      contentEl.classList.add('streaming');
      contentEl.textContent = '';

      try {
        const result = await api.chatMultiTurn(chatHistory);
        contentEl.classList.remove('streaming');
        contentEl.innerHTML = marked.parse(result.text);
        chatHistory.push({ role: 'assistant', content: result.text });
        updateContextIndicator();
      } catch (e) {
        contentEl.classList.remove('streaming');
        contentEl.textContent = '❌ Error: ' + e.message;
      }
    }
  };

  sendBtn?.addEventListener('click', sendMessage);
  inputEl.addEventListener('keydown', (e) => {
    if (e.key === 'Enter' && !e.shiftKey) { e.preventDefault(); sendMessage(); }
  });

  // Auto-grow textarea
  inputEl.addEventListener('input', () => {
    inputEl.style.height = 'auto';
    inputEl.style.height = Math.min(inputEl.scrollHeight, 120) + 'px';
  });

  // Drag-and-drop images into chat
  const chatArea = document.querySelector('.chat-container');

  if (chatArea) {
    chatArea.addEventListener('dragover', (e) => {
      e.preventDefault();
      chatArea.classList.add('chat-dragover');
    });
    chatArea.addEventListener('dragleave', () => {
      chatArea.classList.remove('chat-dragover');
    });
    chatArea.addEventListener('drop', (e) => {
      e.preventDefault();
      chatArea.classList.remove('chat-dragover');
      const file = e.dataTransfer.files[0];
      if (file && file.type.startsWith('image/')) {
        chatImageFile = file;
        showChatImagePreview(file);
        inputEl.placeholder = 'Ask a question about this image...';
        inputEl.focus();
      }
    });
  }

  function showChatImagePreview(file) {
    let preview = document.getElementById('chat-image-preview');
    if (!preview) {
      preview = document.createElement('div');
      preview.id = 'chat-image-preview';
      preview.className = 'chat-image-preview';
      const inputArea = document.querySelector('.chat-input-area');
      inputArea?.insertBefore(preview, inputArea.firstChild);
    }
    const url = URL.createObjectURL(file);
    preview.innerHTML = `
      <img src="${url}" alt="Dropped image">
      <span class="chat-image-name">📎 ${file.name}</span>
      <button class="chat-image-remove" title="Remove image">✕</button>
    `;
    preview.style.display = 'flex';
    preview.querySelector('.chat-image-remove').addEventListener('click', () => {
      chatImageFile = null;
      preview.style.display = 'none';
      inputEl.placeholder = 'Type a message and press Enter to send...';
    });
  }

  // Chat export
  copyBtn?.addEventListener('click', () => {
    const text = buildChatText(messagesEl);
    if (!text) return;
    navigator.clipboard.writeText(text);
    showToast('Chat copied to clipboard!', 'success');
  });

  exportBtn?.addEventListener('click', () => {
    const md = buildChatMarkdown(messagesEl);
    if (!md) return;
    downloadText(md, 'chat-export.md');
    showToast('Chat saved as Markdown!', 'success');
  });

  // PDF export via print window
  const pdfBtn = document.getElementById('chat-export-pdf');
  pdfBtn?.addEventListener('click', () => {
    const messages = messagesEl.querySelectorAll('.message');
    if (!messages.length) return;
    let html = '<html><head><meta charset="UTF-8"><title>EDGESCRIBE Chat Export</title>';
    html += '<style>';
    html += 'body{font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,sans-serif;max-width:700px;margin:40px auto;padding:0 20px;color:#1a1a2e;font-size:14px;line-height:1.7;}';
    html += 'h1{font-size:20px;border-bottom:1px solid #ddd;padding-bottom:8px;margin-bottom:24px;}';
    html += '.msg{margin-bottom:20px;} .role{font-weight:600;margin-bottom:4px;} .role.user{color:#4f46e5;} .role.bot{color:#059669;}';
    html += 'pre{background:#f3f4f6;padding:12px;border-radius:6px;overflow-x:auto;font-size:13px;}';
    html += 'code{background:#f3f4f6;padding:2px 4px;border-radius:3px;font-size:0.9em;}';
    html += 'pre code{background:none;padding:0;}';
    html += '</style></head><body>';
    html += '<h1>🩺 EDGESCRIBE Chat Export</h1>';
    html += '<p style="color:#666;font-size:12px;">Exported on ' + new Date().toLocaleString() + '</p>';
    messages.forEach(msg => {
      const isUser = !!msg.querySelector('.user');
      const content = msg.querySelector('.message-content')?.innerHTML || '';
      html += '<div class="msg"><div class="role ' + (isUser ? 'user' : 'bot') + '">' + (isUser ? '👤 You' : '🤖 Assistant') + '</div>';
      html += '<div>' + content + '</div></div>';
    });
    html += '</body></html>';
    const win = window.open('', '_blank');
    win.document.write(html);
    win.document.close();
    win.print();
  });

  // Clear chat — start fresh
  clearBtn?.addEventListener('click', () => {
    startNewChat();
    loadSessionList();
  });
}

function appendMessage(container, role, text) {
  const div = document.createElement('div');
  div.className = 'message';
  const contentHtml = role === 'assistant' && text
    ? marked.parse(text)
    : escapeHtml(text);
  div.innerHTML = `
    <div class="message-avatar ${role}">${role === 'user' ? '👤' : '🤖'}</div>
    <div class="message-content ${role === 'assistant' ? 'markdown-body' : ''}">${contentHtml}</div>
  `;
  container.appendChild(div);
  container.scrollTop = container.scrollHeight;
  return div;
}

function buildChatText(container) {
  const messages = container.querySelectorAll('.message');
  if (!messages.length) return '';
  let text = '';
  messages.forEach(msg => {
    const role = msg.querySelector('.user') ? 'You' : 'Assistant';
    const content = msg.querySelector('.message-content').textContent.trim();
    text += `${role}: ${content}\n\n`;
  });
  return text.trim();
}

function buildChatMarkdown(container) {
  const messages = container.querySelectorAll('.message');
  if (!messages.length) return '';
  let md = '# EDGESCRIBE Chat Export\n\n';
  md += `_Exported on ${new Date().toLocaleString()}_\n\n---\n\n`;
  messages.forEach(msg => {
    const role = msg.querySelector('.user') ? '**You**' : '**Assistant**';
    const content = msg.querySelector('.message-content').textContent.trim();
    md += `${role}:\n${content}\n\n`;
  });
  return md.trim();
}

// ── Session Management ──
const WELCOME_MSG = 'Hi! I\'m your private AI assistant. How can I help?\n\n' +
  '• Answer questions or look up information\n' +
  '• Create SOAP notes from transcripts\n' +
  '• Summarize documents\n' +
  '• Help with writing and editing\n\n' +
  'Everything stays private — nothing leaves your device.';

function startNewChat() {
  currentSessionId = null;
  chatHistory = [{ role: 'system', content: chatSystemPrompt }];
  const messagesEl = document.getElementById('chat-messages');
  if (messagesEl) {
    messagesEl.innerHTML = '';
    appendMessage(messagesEl, 'assistant', WELCOME_MSG);
  }
  updateContextIndicator();
  highlightActiveSession(null);
}

async function loadSessionList() {
  const listEl = document.getElementById('session-list');
  if (!listEl) return;

  try {
    const data = await api.getSessions();
    const sessions = data.sessions || [];
    if (!sessions.length) {
      listEl.innerHTML = '<div class="session-list-empty">No past conversations</div>';
      return;
    }

    // Group by date
    const groups = {};
    sessions.forEach(s => {
      const date = new Date(s.started_at);
      const today = new Date();
      const yesterday = new Date(today);
      yesterday.setDate(yesterday.getDate() - 1);

      let label;
      if (date.toDateString() === today.toDateString()) label = 'Today';
      else if (date.toDateString() === yesterday.toDateString()) label = 'Yesterday';
      else label = date.toLocaleDateString();

      if (!groups[label]) groups[label] = [];
      groups[label].push(s);
    });

    let html = '';
    for (const [label, items] of Object.entries(groups)) {
      html += `<div class="session-date-label">${escapeHtml(label)}</div>`;
      items.forEach(s => {
        const time = new Date(s.started_at).toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' });
        const title = s.type === 'chat' ? 'Chat' : s.type;
        const msgs = s.message_count || 0;
        const isActive = s.id === currentSessionId;
        html += `<div class="session-item ${isActive ? 'active' : ''}" data-id="${escapeHtml(s.id)}">
          <div class="session-item-body">
            <div class="session-item-title">💬 ${escapeHtml(title)} · ${time}</div>
            <div class="session-item-meta">${msgs} messages${s.model ? ' · ' + escapeHtml(s.model) : ''}</div>
          </div>
          <button class="session-item-delete" data-delete-id="${escapeHtml(s.id)}" title="Delete">🗑</button>
        </div>`;
      });
    }
    listEl.innerHTML = html;

    // Click to load session
    listEl.querySelectorAll('.session-item').forEach(el => {
      el.addEventListener('click', (e) => {
        if (e.target.closest('.session-item-delete')) return;
        loadSession(el.dataset.id);
      });
    });

    // Delete buttons
    listEl.querySelectorAll('.session-item-delete').forEach(btn => {
      btn.addEventListener('click', async (e) => {
        e.stopPropagation();
        const id = btn.dataset.deleteId;
        try {
          await api.deleteSession(id);
          if (currentSessionId === id) startNewChat();
          loadSessionList();
          showToast('Conversation deleted', 'info');
        } catch (err) {
          showToast('Failed to delete: ' + err.message, 'error');
        }
      });
    });
  } catch (e) {
    // Server not available — session list stays empty
    console.error('Could not load sessions:', e);
  }
}

async function loadSession(sessionId) {
  const messagesEl = document.getElementById('chat-messages');
  if (!messagesEl) return;

  try {
    const data = await api.getSession(sessionId);
    currentSessionId = sessionId;
    chatHistory = [{ role: 'system', content: chatSystemPrompt }];
    messagesEl.innerHTML = '';

    const messages = data.messages || [];
    messages.forEach(m => {
      if (m.role === 'system') return;
      appendMessage(messagesEl, m.role, m.content);
      chatHistory.push({ role: m.role, content: m.content });
    });

    updateContextIndicator();
    highlightActiveSession(sessionId);
    showToast('Conversation loaded', 'success');
  } catch (e) {
    showToast('Failed to load conversation: ' + e.message, 'error');
  }
}

function highlightActiveSession(sessionId) {
  document.querySelectorAll('.session-item').forEach(el => {
    el.classList.toggle('active', el.dataset.id === sessionId);
  });
}

async function searchSessions(query) {
  const listEl = document.getElementById('session-list');
  if (!listEl || !query.trim()) {
    loadSessionList();
    return;
  }

  try {
    const data = await api.searchMemory(query);
    const results = data.results || [];
    if (!results.length) {
      listEl.innerHTML = '<div class="session-list-empty">No results found</div>';
      return;
    }

    let html = '<div class="session-date-label">Search Results</div>';
    results.forEach(r => {
      const preview = r.content.substring(0, 80).replace(/\n/g, ' ');
      html += `<div class="session-item" data-id="${escapeHtml(r.session_id)}">
        <div class="session-item-body">
          <div class="session-item-title">${r.role === 'user' ? '👤' : '🤖'} ${escapeHtml(preview)}...</div>
          <div class="session-item-meta">${escapeHtml(r.created_at || '')}</div>
        </div>
      </div>`;
    });
    listEl.innerHTML = html;

    listEl.querySelectorAll('.session-item').forEach(el => {
      el.addEventListener('click', () => loadSession(el.dataset.id));
    });
  } catch (e) {
    console.error('Search failed:', e);
  }
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
    if (!imageInput.files.length) { showToast('Please upload an image first.', 'info'); return; }
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
    if (!imageInput.files.length) { showToast('Please upload an image first.', 'info'); return; }
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
  const voiceSelect = document.getElementById('tts-voice');
  const speedSlider = document.getElementById('tts-speed');
  const speedLabel = document.getElementById('tts-speed-label');

  if (!textInput) return;

  speedSlider?.addEventListener('input', () => {
    if (speedLabel) speedLabel.textContent = parseFloat(speedSlider.value).toFixed(1) + '×';
  });

  const getVoice = () => voiceSelect?.value || 'af_heart';
  const getSpeed = () => parseFloat(speedSlider?.value || '1.0');

  playBtn?.addEventListener('click', async () => {
    const text = textInput.value.trim();
    if (!text) return;
    playBtn.textContent = '⏳ Synthesizing...';
    playBtn.disabled = true;
    try {
      const blob = await api.synthesize(text, getVoice(), getSpeed());
      const url = URL.createObjectURL(blob);
      audioPlayer.src = url;
      audioPlayer.style.display = 'block';
      audioPlayer.play();
    } catch (e) {
      showToast('Error: ' + e.message, 'error');
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
      const blob = await api.synthesize(text, getVoice(), getSpeed());
      const url = URL.createObjectURL(blob);
      const a = document.createElement('a');
      a.href = url;
      a.download = 'speech.wav';
      a.click();
      showToast('Audio file saved!', 'success');
    } catch (e) {
      showToast('Error: ' + e.message, 'error');
    }
    saveBtn.textContent = '💾 Save as Audio File';
    saveBtn.disabled = false;
  });
}

// ── Keyboard Shortcuts ──
function setupKeyboardShortcuts() {
  document.addEventListener('keydown', (e) => {
    // Ctrl+Shift+R — toggle recording
    if (e.ctrlKey && e.shiftKey && e.key === 'R') {
      e.preventDefault();
      navigate('live');
      document.getElementById('record-btn')?.click();
    }
    // Ctrl+Shift+N — focus chat input (new message)
    if (e.ctrlKey && e.shiftKey && e.key === 'N') {
      e.preventDefault();
      navigate('chat');
      document.getElementById('chat-input')?.focus();
    }
    // Ctrl+Shift+C — copy last assistant response
    if (e.ctrlKey && e.shiftKey && e.key === 'C') {
      e.preventDefault();
      const messages = document.querySelectorAll('#chat-messages .message');
      const last = [...messages].reverse().find(m => m.querySelector('.assistant'));
      if (last) {
        const text = last.querySelector('.message-content').textContent.trim();
        navigator.clipboard.writeText(text);
        showToast('Last response copied!', 'success');
      }
    }
  });
}

// ── Context Window Indicator ──
function updateContextIndicator() {
  const fill = document.getElementById('context-fill');
  const label = document.getElementById('context-label');
  if (!fill || !label) return;

  // Rough estimate: ~4 chars per token for English
  const totalChars = chatHistory.reduce((sum, m) => sum + m.content.length, 0);
  const estimatedTokens = Math.round(totalChars / 4);
  const maxTokens = 16384;
  const pct = Math.min((estimatedTokens / maxTokens) * 100, 100);

  fill.style.width = pct + '%';
  fill.classList.remove('warning', 'danger');
  if (pct > 80) fill.classList.add('danger');
  else if (pct > 60) fill.classList.add('warning');

  const kTokens = (estimatedTokens / 1000).toFixed(1);
  label.textContent = `~${kTokens}K / 16K tokens`;
}

// ── Onboarding Wizard ──
function setupOnboarding() {
  if (localStorage.getItem('edgescribe-onboarded')) return;
  const overlay = document.getElementById('onboarding-overlay');
  if (!overlay) return;
  overlay.style.display = 'flex';

  const dots = overlay.querySelectorAll('.onboarding-dot');
  function goToStep(n) {
    overlay.querySelectorAll('.onboarding-step').forEach(s => s.style.display = 'none');
    document.getElementById('onboarding-step-' + n).style.display = 'block';
    dots.forEach(d => d.classList.toggle('active', parseInt(d.dataset.step) === n));
  }

  document.getElementById('onboarding-next-1')?.addEventListener('click', () => goToStep(2));
  document.getElementById('onboarding-next-2')?.addEventListener('click', () => goToStep(3));
  document.getElementById('onboarding-finish')?.addEventListener('click', () => {
    overlay.style.display = 'none';
    localStorage.setItem('edgescribe-onboarded', '1');
  });
}

// ── Transcript History (from backend SQLite) ──
function saveTranscript(text, source = 'live') {
  // Backend auto-saves transcripts via the memory system.
  // Just refresh the dashboard history list.
  renderHistory();
}

async function renderHistory() {
  const container = document.getElementById('history-list');
  if (!container) return;

  try {
    const data = await api.getSessions();
    const sessions = data.sessions || [];
    if (!sessions.length) {
      container.innerHTML = '<span class="placeholder" style="font-size: 13px;">No sessions yet. Record audio or start a chat to get started.</span>';
      return;
    }

    container.innerHTML = sessions.slice(0, 8).map(item => {
      const icon = item.type === 'chat' ? '💬' : item.type === 'transcribe' ? '🎤' : '📁';
      const date = new Date(item.started_at);
      const timeStr = date.toLocaleDateString() + ' ' + date.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' });
      const label = item.type === 'chat' ? 'Chat' : item.type === 'transcribe' ? 'Transcription' : item.type;
      const msgs = item.message_count || 0;
      return `<div class="history-item" data-id="${escapeHtml(item.id)}">
        <span class="history-item-icon">${icon}</span>
        <div class="history-item-body">
          <div class="history-item-preview">${escapeHtml(label)}${item.model ? ' · ' + escapeHtml(item.model) : ''}</div>
          <div class="history-item-meta">${timeStr} · ${msgs} messages</div>
        </div>
      </div>`;
    }).join('');

    container.querySelectorAll('.history-item').forEach(el => {
      el.addEventListener('click', async () => {
        const id = el.dataset.id;
        try {
          const session = await api.getSession(id);
          const messages = session.messages || [];
          const text = messages
            .filter(m => m.role !== 'system')
            .map(m => `${m.role === 'user' ? 'You' : 'Assistant'}: ${m.content}`)
            .join('\n\n');
          if (text) {
            navigator.clipboard.writeText(text);
            showToast('Session copied to clipboard!', 'success');
          }
        } catch (e) {
          showToast('Could not load session', 'error');
        }
      });
    });
  } catch (e) {
    // Server not reachable — show empty state
    container.innerHTML = '<span class="placeholder" style="font-size: 13px;">Connect to the server to see your history.</span>';
  }
}

function setupHistory() {
  renderHistory();
  const clearBtn = document.getElementById('history-clear');
  clearBtn?.addEventListener('click', async () => {
    // Note: bulk delete not available in API yet. Clear just refreshes.
    showToast('Use the chat sidebar to delete individual sessions', 'info');
  });
}

// ── Waveform Visualizer ──
function startWaveformDraw() {
  const canvas = document.getElementById('waveform-canvas');
  if (!canvas || !analyserNode) return;
  const ctx = canvas.getContext('2d');
  const bufLen = analyserNode.frequencyBinCount;
  const dataArray = new Uint8Array(bufLen);

  function draw() {
    waveformAnimId = requestAnimationFrame(draw);
    analyserNode.getByteTimeDomainData(dataArray);

    const w = canvas.width;
    const h = canvas.height;
    ctx.clearRect(0, 0, w, h);

    ctx.lineWidth = 2;
    const isDark = document.documentElement.getAttribute('data-theme') !== 'light';
    ctx.strokeStyle = isDark ? 'rgba(239,68,68,0.7)' : 'rgba(220,38,38,0.7)';
    ctx.beginPath();

    const sliceWidth = w / bufLen;
    let x = 0;
    for (let i = 0; i < bufLen; i++) {
      const v = dataArray[i] / 128.0;
      const y = (v * h) / 2;
      if (i === 0) ctx.moveTo(x, y);
      else ctx.lineTo(x, y);
      x += sliceWidth;
    }
    ctx.lineTo(w, h / 2);
    ctx.stroke();
  }
  draw();
}

function stopWaveformDraw() {
  if (waveformAnimId) {
    cancelAnimationFrame(waveformAnimId);
    waveformAnimId = null;
  }
  const canvas = document.getElementById('waveform-canvas');
  if (canvas) {
    const ctx = canvas.getContext('2d');
    ctx.clearRect(0, 0, canvas.width, canvas.height);
  }
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
if (document.readyState === 'loading') {
  document.addEventListener('DOMContentLoaded', init);
} else {
  init();
}
