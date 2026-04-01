# EDGESCRIBE Monetization — Strategy Document

## Why SaaS Doesn't Work for EDGESCRIBE

EDGESCRIBE runs on-device with small quantized models (2B parameters). It **cannot compete on output quality** with cloud LLMs (GPT-4, Claude, Gemini) that have 100-1000× more parameters. A monthly subscription for inferior AI output won't retain users.

The value proposition is **privacy + offline + zero latency + zero recurring cost** — the opposite of SaaS. Monetization must align with this positioning.

---

## Recommended Revenue Models

### 1. One-Time App Purchase (Primary)

Sell EDGESCRIBE as a paid desktop application. Open-source the core engine (for trust and auditability), but sell a polished installer with auto-updates, model management, and the web dashboard.

```
EDGESCRIBE Free (open-source)
├── CLI tool (full functionality)
├── All models (pull from HuggingFace)
├── Community support (GitHub issues)
└── Build from source

EDGESCRIBE Pro — $49 one-time (or $79 with lifetime updates)
├── Pre-built installer (Windows + macOS)
├── Auto-updates
├── Web dashboard UI
├── One-click model setup (no CLI needed)
├── Priority bug fixes
└── Email support
```

**Why this works:**
- Users pay once, feel good about it (no recurring drain)
- Open-source core builds trust (clinics can audit)
- The paid product is the **convenience** — not the AI itself
- Similar to how Sublime Text, Obsidian, and Raycast monetize

---

### 2. Paid Feature Tiers (Freemium)

Keep the core free, charge for advanced workflow features that save professionals time.

```
Free Tier:
├── Live transcription
├── File transcription
├── Basic chat
├── Vision / OCR
├── Text-to-speech
└── REST API

Pro Tier — $39 one-time (or $5/month)
├── Everything in Free
├── Persistent memory (session history, search)
├── RAG knowledge base (ingest documents)
├── SOAP note templates (customizable)
├── Chat export (Markdown, PDF, TXT)
├── Conversation history
└── System prompt presets

Clinic Tier — $149 one-time per seat
├── Everything in Pro
├── EMR export formats (HL7 FHIR, CDA)
├── Multi-user model management
├── Custom SOAP templates per specialty
├── Bulk file transcription
└── Priority support
```

---

### 3. Managed Appliance (Highest Margin)

Sell a **pre-configured hardware device** with EDGESCRIBE pre-installed, models pre-loaded, ready to plug in and use. Zero setup for the end user.

**What this looks like:**

```
┌─────────────────────────────────────────────────┐
│  EDGESCRIBE Medical Transcription Appliance     │
│                                                  │
│  Hardware: Mini PC (AMD Ryzen 5 or Intel i5)     │
│  RAM: 16 GB                                      │
│  Storage: 500 GB NVMe SSD                        │
│  OS: Windows 11 Pro (familiar to clinics)         │
│  Pre-installed: EDGESCRIBE + all models          │
│  Auto-starts: Web dashboard on boot              │
│                                                  │
│  User experience:                                │
│  1. Plug into power                              │
│  2. Open browser → http://localhost:8080         │
│  3. Start transcribing                           │
│                                                  │
│  Hardware cost: ~$250-300                         │
│  Sell price: $699-999                             │
│  Margin: ~$400-700 per unit                       │
└─────────────────────────────────────────────────┘
```

**Target buyers:**
- Small clinics (1-5 doctors) that don't have IT staff
- Solo practitioners who want "plug and play"
- Telehealth setups that need local transcription
- Legal offices, therapy practices, any profession needing private transcription

#### Recommended Hardware

| Device | CPU | RAM | Storage | Price | Where to Buy | Notes |
|---|---|---|---|---|---|---|
| **Beelink SER5** (Recommended) | Ryzen 5 5560U (6C) | 16 GB | 500 GB NVMe | ~$260 | Amazon | Best value. Windows 11 pre-installed. Silent. |
| **Beelink EQ12** | Intel N100 (4C) | 16 GB | 500 GB NVMe | ~$180 | Amazon, AliExpress | Budget option. Lower performance but adequate. |
| **MinisForum UM560** | Ryzen 5 5625U (6C) | 16 GB | 512 GB NVMe | ~$290 | minisforum.com, Amazon | Good build quality. |
| **Beelink EQR6** | Ryzen 5 6600H (6C) | 16 GB | 500 GB NVMe | ~$320 | Amazon | Best CPU perf in this range. |
| **HP Pro Mini 400 G9** | Intel i5-12500T (6C) | 16 GB | 256 GB NVMe | ~$300 | Amazon, refurbished | Enterprise-grade, clinic-friendly brand. |
| **Mac Mini M2** | Apple M2 (8C) | 16 GB | 256 GB SSD | ~$500 | Apple, Amazon | Premium. Best CPU inference speed. Sell for $999. |

**Primary recommendation: Beelink SER5 (~$260)**
- Ryzen 5 6-core handles llama.cpp inference well (~15-20 tok/s for Qwen3-VL-2B Q4)
- 16 GB DDR4 fits all EDGESCRIBE models (total ~2 GB) with plenty of room
- Windows 11 Pro pre-installed — doctors see a familiar OS
- Silent fan, tiny form factor (fits on a desk or behind a monitor)
- WiFi + Bluetooth + 2x HDMI + USB-C
- Cost ~$260 → Sell for $699 → **~$440 margin per unit**

#### Why NOT Jetson Nano / Orin

| Issue | Impact |
|---|---|
| **ARM Linux only** | EDGESCRIBE v1 targets Windows + macOS. Doctors won't use Linux. |
| **4-8 GB RAM** | Not enough for Qwen3-VL (~1.5 GB) + KV cache (~460 MB) + OS + browser |
| **CUDA benefit wasted** | EDGESCRIBE runs on CPU (llama.cpp, ONNX Runtime). CUDA doesn't help current stack. |
| **No familiar OS** | Clinics expect Windows. IT departments expect Windows. |
| **Developer-oriented** | Requires flashing OS, apt-get, command line. Not "plug and play". |

Jetson is excellent for robotics, computer vision at the edge, and custom AI pipelines — but it's wrong for a **clinical desktop product** where the user expects to plug in and open a browser.

#### Appliance Setup Checklist

```
1. Buy Mini PC (Beelink SER5 or similar)
2. Boot into Windows 11 Pro
3. Install EDGESCRIBE binary to C:\Program Files\EDGESCRIBE\
4. Run: edgescribe pull nemotron && edgescribe pull qwen3-vl && edgescribe pull kokoro
5. Create startup script: edgescribe serve --port 8080
6. Add to Windows Task Scheduler (run on login, auto-restart)
7. Set auto-login for the Windows user account
8. Pin browser shortcut to desktop: http://localhost:8080
9. Optional: Set computer name to EDGESCRIBE, configure network discovery
10. Package and ship
```

**The user experience:**
```
Doctor receives box → plugs in power + monitor (or uses existing monitor)
→ Windows boots → EDGESCRIBE server auto-starts
→ Doctor opens Chrome/Edge → types localhost:8080
→ Web dashboard loads → click 🎤 → start transcribing
→ No CLI, no terminal, no installation, no model downloads
```

#### Revenue Model for Appliance

```
Hardware cost:           ~$260 (Beelink SER5)
Software + setup labor:  ~$50 (1 hour to configure)
Shipping:                ~$20
Total cost:              ~$330

Sell price:              $699 (standard) / $999 (with Mac Mini M2)
Margin per unit:         ~$370-$670

Optional add-ons:
  - Annual support contract: $99/year (remote updates, email support)
  - Extended warranty: $49/year
  - Custom SOAP templates: $149 one-time per specialty
```

**Why this works:**
- Clinics don't want to install software — they want a device that works
- Hardware has high perceived value ($599 for a device feels fair; $599 for software feels expensive)
- Recurring revenue from optional support/update contracts ($99/year)
- HIPAA story is bulletproof: "The data never leaves this box"

**This is NOT selling laptops.** It's selling a **medical device** — a dedicated transcription appliance. The software is the value; the hardware is the delivery mechanism.

---

### 4. Enterprise / Clinic Licensing

For larger healthcare organizations (hospitals, clinic chains) that want to deploy EDGESCRIBE across multiple workstations.

```
Enterprise License — $99/seat/month (or $999/seat/year)
├── Volume deployment tools (MSI installer, MDM support)
├── Centralized model management
├── HIPAA compliance documentation + BAA
├── Custom model fine-tuning (per specialty)
├── EMR integration support
├── Dedicated support + SLA
├── On-site deployment assistance
└── Training materials for staff
```

**Why this works:**
- Hospitals already pay $200-500/month/provider for cloud transcription (Nuance/DAX, DeepScribe)
- EDGESCRIBE at $99/seat/month with ZERO cloud is a massive cost saving
- The privacy story sells itself in healthcare

---

## Models That DON'T Work

| Model | Why It Fails |
|---|---|
| **SaaS subscription for AI output** | Edge models can't compete with cloud quality. Users won't pay monthly for worse output. |
| **Selling training data** | You don't have proprietary data. Users own their data. |
| **Selling fine-tuned models** | Requires domain expertise + labeled data you don't have. Maybe later with clinic partnerships. |
| **Ads** | Incompatible with privacy-first, medical-grade positioning. |
| **API-as-a-service (cloud hosted)** | Contradicts "on-device" value proposition. Competing with OpenAI on their turf. |

---

## Recommended Strategy

### Phase 1: Launch (v1.0)

```
- Open-source everything (build trust, get users)
- Free download, free models
- GitHub stars → awareness → early adopters
- Collect feedback on what features users will pay for
```

### Phase 2: Monetize (v1.2+)

```
- EDGESCRIBE Pro: $49 one-time (installer + dashboard + auto-updates)
- Pro features: memory system, RAG, chat export, SOAP templates
- Start selling on website with Stripe/Gumroad
```

### Phase 3: Scale (v2.0+)

```
- Managed appliance ($599-999) for clinics
- Enterprise licensing for hospitals
- Optional: specialty fine-tuned models (radiology, dermatology, etc.)
- Optional: annual support contracts ($99/year)
```

---

## Competitive Pricing Context

| Competitor | Price | Cloud/Local | Quality |
|---|---|---|---|
| **Nuance DAX** | ~$200-500/month/provider | Cloud | ✅ Best (large models) |
| **DeepScribe** | ~$300/month/provider | Cloud | ✅ Good |
| **Whisper API (OpenAI)** | $0.006/min | Cloud | ✅ Good |
| **Ollama** | Free | Local | N/A (no transcription) |
| **LM Studio** | Free / Pro sub | Local | N/A (no transcription) |
| **EDGESCRIBE Pro** | $49 one-time | Local | ⚠️ Good enough for most use cases |
| **EDGESCRIBE Appliance** | $599-999 one-time | Local | ⚠️ Good enough + zero recurring cost |

**The pitch:** "Pay $49 once instead of $300/month. Your data stays on your device. Works offline."

---

## Summary

```
Best fit for EDGESCRIBE:
  1. One-time app purchase ($49 Pro)     — primary revenue
  2. Paid feature tiers (memory, RAG)    — upsell
  3. Managed appliance ($599-999)        — highest margin, clinic market
  4. Enterprise licensing ($99/seat/mo)  — scale play

NOT a fit:
  - SaaS subscription for AI output
  - Selling data or fine-tuned models
  - Cloud API service
  - Ads
```
