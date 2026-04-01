# EDGESCRIBE Demo Script

**Duration:** 5 minutes
**Key message:** Full AI stack — speech, vision, language, voice — running entirely on-device. No cloud. No API keys. Under 2 GB.

---

## Preparation (Night Before)

```powershell
# Ensure all models are downloaded
edgescribe pull nemotron
edgescribe pull qwen3-vl
edgescribe pull kokoro

# Add to PATH (so you don't type full path during demo)
$env:PATH += ";C:\Users\ruiren\Desktop\openscribe\build\Release"

# Create a sample transcript file
@"
Doctor: Good morning. What brings you in today?
Patient: I've been having severe headaches for about a week now. Mostly on the right side.
Doctor: How would you describe the pain?
Patient: It's throbbing. Gets worse with light and noise.
Doctor: Any nausea or vomiting?
Patient: Some nausea, yes. No vomiting.
Doctor: Any fever?
Patient: No fever.
Doctor: Have you tried any medications?
Patient: Just ibuprofen. It helps a little but doesn't go away completely.
Doctor: Any history of migraines?
Patient: My mother gets migraines.
Doctor: I see. Let me examine you.
"@ | Set-Content transcript.txt

# Have a medical document image ready (prescription, lab report, or chart)
# Save as prescription.jpg in current directory

# Test every command once to confirm it works
edgescribe chat "Hello"
edgescribe speak "Testing audio output"

# Verify server starts
edgescribe serve --port 8080
# Then Ctrl+C to stop
```

### Checklist

- [ ] All 3 models downloaded (nemotron, qwen3-vl, kokoro)
- [ ] `transcript.txt` created
- [ ] Medical document image ready (`prescription.jpg`)
- [ ] All commands tested
- [ ] Microphone working
- [ ] Speakers/audio working
- [ ] Browser open (for server demo)
- [ ] Terminal font size large enough for audience to read

---

## Demo Script (5 Minutes)

### 1. The Hook (30 seconds)

**Say:**
> "I'm going to show you a complete AI stack — speech recognition, vision OCR,
> language model, and text-to-speech — all running on this laptop. No internet
> required."

**Do:** Turn off WiFi visibly (click the WiFi icon, disconnect).

> "No cloud. No API keys. Everything you're about to see runs in a single
> 2-megabyte binary with local models."

---

### 2. Live Transcription (60 seconds)

**Say:**
> "Let's start with real-time speech-to-text."

**Run:**
```powershell
edgescribe run --live
```

**Speak into mic (clearly, at normal pace):**
> "Patient is a 45-year-old male presenting with chest pain and shortness of
> breath for two days. Pain is worse with exertion. No fever. No cough.
> History of hypertension."

**Wait for text to stream on screen.**

**Say:**
> "That's the Nemotron Parakeet model — 600 million parameters, running entirely
> on CPU. Real-time, private, HIPAA-safe."

**Press Ctrl+C to stop.**

---

### 3. SOAP Notes — One Command (60 seconds)

**Say:**
> "Now let's take a doctor-patient transcript and generate structured SOAP notes."

**Run:**
```powershell
edgescribe process --soap transcript.txt
```

**Wait for SOAP output to stream (Subjective, Objective, Assessment, Plan).**

**Say:**
> "A 2-billion parameter model just did medical scribe work. On a laptop.
> No Dragon Medical, no cloud subscription. One command."

---

### 4. Vision OCR (45 seconds)

**Say:**
> "The same model also does vision. Let me show you OCR on a medical document."

**Run:**
```powershell
edgescribe vision prescription.jpg --ocr
```

**Wait for extracted text to appear.**

**Say:**
> "It read a prescription photo and extracted the text. Same model handles
> images and text — Qwen3 Vision-Language model running locally."

---

### 5. Text-to-Speech Readback (30 seconds)

**Say:**
> "Now let's close the loop — AI reads the notes back to you."

**Run:**
```powershell
edgescribe speak "Patient presents with acute bronchitis. Prescribing amoxicillin 500 milligrams three times daily for 10 days. Follow up in one week."
```

**Audio plays through speakers.**

**Say:**
> "Voice in, AI processing, voice out. Full cycle, all on-device."

---

### 6. Server + Web UI (45 seconds)

**Say:**
> "For integration, EDGESCRIBE runs as a REST API server."

**Run:**
```powershell
edgescribe serve --port 8080
```

**Open browser to `http://localhost:8080`**

**Show the web UI. Type a question in the chat:**
> "What are the common side effects of metformin?"

**Wait for response to stream.**

**Say:**
> "Any web or mobile frontend can connect to this API. Same models, same
> privacy, accessible from any app."

---

### 7. The Closer (30 seconds)

**Say:**
> "Let me recap what you just saw:
>
> - Real-time speech recognition
> - Automated SOAP note generation
> - Medical document OCR
> - Natural text-to-speech
> - REST API for any frontend
>
> Total package: under 2 gigabytes. Runs on any modern laptop.
> No internet needed. HIPAA-safe by design — patient data never leaves
> the device.
>
> It's open source, MIT licensed, and available on GitHub today."

---

## Backup Commands (If Something Fails)

```powershell
# If live mic doesn't work, use a pre-recorded WAV file
edgescribe run recording.wav

# If server port is busy
edgescribe serve --port 3000

# If OCR image isn't available, use chat instead
edgescribe chat "What are the symptoms of Type 2 diabetes?"

# If TTS sounds garbled, show text output instead
edgescribe process --summarize transcript.txt
```

---

## Impressive Stats to Mention

| Stat | Value |
|------|-------|
| Binary size | ~1 MB (+20 MB runtime DLLs) |
| Total model download | ~2 GB (all 3 models) |
| RAM usage (idle server) | ~50 MB |
| RAM usage (active chat) | ~1.5-3 GB |
| Supported platforms | Windows, macOS (Intel + Apple Silicon), Linux |
| GPU acceleration | Vulkan (NVIDIA, AMD, Intel) — auto-fallback to CPU |
| Languages (ASR) | English (more with fine-tuning) |
| License | MIT (fully open source) |

---

## Q&A Prep — Likely Questions

**"How does this compare to ChatGPT?"**
> General knowledge: ChatGPT wins. Medical tasks with fine-tuned model: comparable.
> Key difference: this is private, offline, and free.

**"Can it handle accents / noisy audio?"**
> The ASR model handles standard accents well. We can fine-tune for specific
> accents using our training pipeline.

**"How accurate are the SOAP notes?"**
> With the base model: good. With our fine-tuned model: excellent — distilled
> from GPT-4 quality outputs on medical data.

**"What about HIPAA compliance?"**
> Data never leaves the device. No BAA needed because there's no cloud service
> to sign one with. Strongest privacy posture possible.

**"Can it run on a phone / tablet?"**
> Not yet, but the 2B model fits in 3 GB RAM. Mobile deployment (iOS/Android)
> is on the roadmap.

**"How do you make money if it's open source?"**
> Free base models. Premium fine-tuned specialty models (one-time purchase).
> Cloud SaaS tier with agent workflows for clinics that want maximum accuracy.
