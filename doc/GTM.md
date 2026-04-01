# EDGESCRIBE Go-To-Market Strategy

## Business Model: Open-Core + SaaS

```
FREE (Open Source)          PAID MODELS (Edge)           SAAS (Cloud)
━━━━━━━━━━━━━━━━━           ━━━━━━━━━━━━━━━━━           ━━━━━━━━━━━━
Attracts users              Monetizes edge users         Monetizes teams
Builds trust                One-time purchase            Recurring revenue
Proves privacy story        Still 100% on-device         Best accuracy

$0                          $99–499 per model            $49–199/mo per seat
```

---

## Tier 1: Free — Open Source EDGESCRIBE

### What's Included

- EDGESCRIBE binary (Windows, macOS, Linux)
- Base models:
  - Qwen3-VL-2B (LLM + Vision + OCR)
  - Nemotron Parakeet 0.6B (ASR)
  - Kokoro / Piper (TTS)
- All CLI commands + REST API + Web UI
- 100% on-device, no account needed

### Purpose

- **User acquisition** — zero-friction trial, no signup
- **Trust building** — open source = auditable, proves privacy claims
- **Community** — developers contribute, report bugs, spread word
- **Bottom of funnel** — free users discover limitations, upgrade naturally

### Distribution

- GitHub releases (direct download)
- Windows installer (Inno Setup)
- Homebrew (macOS)
- Package managers (apt, winget — future)
- Product Hunt, Hacker News, Reddit r/LocalLLaMA

---

## Tier 2: Paid Models — Edge Deployment

### What's Included

Fine-tuned specialty models that run on-device via EDGESCRIBE. Purchased once,
owned forever. No subscription, no cloud dependency.

### Model Catalog (Examples)

| Model | Specialty | Price | Description |
|-------|-----------|:---:|-------------|
| SOAP-Pro | General medicine | $99 | Superior SOAP note generation |
| SOAP-Cardiology | Cardiology | $199 | Cardiology-specific SOAP + terminology |
| SOAP-Orthopedics | Orthopedics | $199 | Ortho-specific assessments + plans |
| SOAP-Dermatology | Dermatology | $199 | Derm-specific + lesion description |
| OCR-Medical | Medical documents | $149 | Prescription, lab report, chart OCR |
| ASR-Medical | Medical vocabulary | $149 | Better recognition of medical terms |
| Voice-Professional | TTS | $49 | Premium medical narrator voice |
| **Bundle: Full Medical Suite** | All above | **$499** | All models, best value |

### How It Works

```
1. User purchases model → receives HuggingFace access token
2. User runs: edgescribe pull soap-cardiology --token hf_xxxxx
3. Model downloads (~1 GB) to local cache
4. Done — runs entirely on-device, forever
```

### Technical Implementation

- Models hosted as **gated repos** on HuggingFace
- User gets HF token after purchase (via your website/Gumroad/LemonSqueezy)
- `--token` flag already built into EDGESCRIBE
- Same GGUF format, same llama.cpp runtime — zero code changes
- Models are fine-tuned Qwen3-VL-2B with LoRA (see edgescribe-trainer)

### Pricing Strategy

- **One-time purchase** (not subscription) — key differentiator vs cloud
- "Buy once, own forever" resonates with privacy-conscious buyers
- Bundle discount encourages full suite purchase
- Price anchored against cloud alternatives:
  - Cloud medical scribe: $200-500/month ongoing
  - EDGESCRIBE model: $99-499 one-time, runs forever

### Margin

- Cost to create: ~$50-150 (synthetic data + GPU training)
- Sale price: $99-499
- **Margin: 70-95%** (after first sale, pure profit)
- No hosting costs — model runs on user's device

---

## Tier 3: SaaS — Cloud Agent Platform

### What's Included

Cloud-hosted medical AI with:
- Powerful models (GPT-4 / Claude / Qwen-Max via API)
- Multi-step agent workflows (RAG, validation, cross-referencing)
- Team features (shared templates, audit logs, role-based access)
- HIPAA BAA (Business Associate Agreement)
- Web dashboard (no install required)

### Architecture

```
Your SaaS (lightweight server):
  ├── Your agent code (prompts, RAG pipeline, workflows)
  ├── Paid LLM API (Claude / GPT-4 / Qwen-Max)
  │     → You pay per API call (~$0.01-0.05 per request)
  │     → User pays monthly subscription
  ├── Vector DB for RAG (Supabase/Pinecone)
  └── Auth + billing (Clerk/Stripe)

You do NOT host GPUs. The API provider handles compute.
```

### Features Beyond Edge

| Feature | Edge (Free/Paid) | SaaS |
|---------|:---:|:---:|
| Basic SOAP notes | ✅ | ✅ |
| Multi-step validation | ❌ | ✅ Agent re-reads and corrects |
| RAG (patient history) | ❌ | ✅ Pulls from EHR context |
| ICD-10 code suggestion | ❌ | ✅ Cross-references coding DB |
| Team templates | ❌ | ✅ Shared across clinic |
| Audit trail | ❌ | ✅ Compliance logging |
| Multi-language | Limited | ✅ GPT-4 handles 50+ languages |
| HIPAA BAA | N/A (on-device) | ✅ Included |

### Pricing

| Plan | Price | Target | API Cost/User |
|------|:---:|--------|:---:|
| Solo | $49/mo | Individual provider | ~$5-10/mo |
| Practice | $99/mo per seat | Small clinic (2-10) | ~$10-15/mo |
| Enterprise | $199/mo per seat | Hospital/health system | ~$15-20/mo |

### Margin

- Revenue per user: $49-199/mo
- API cost per user: $5-20/mo
- Infrastructure (Supabase, Vercel): ~$50-200/mo total
- **Margin: 75-90%** at scale

### Tech Stack (Lean)

```
Frontend:     Next.js (Vercel — free tier to start)
Auth:         Clerk ($0 up to 10K MAU)
Billing:      Stripe / LemonSqueezy
LLM API:      Claude API / OpenAI API / Qwen API
RAG:          Supabase (pgvector) or Pinecone
Agent logic:  Python / TypeScript — your custom prompts + workflows
Hosting:      Vercel + Supabase (~$50/mo to start)
```

No GPU servers. No ML infrastructure. Just API calls + your agent code.

---

## GTM Phases

### Phase 1: Launch Open Source (Month 1-3)

```
Actions:
  ├── Ship EDGESCRIBE v1.0 on GitHub
  ├── Product Hunt launch
  ├── Hacker News "Show HN" post
  ├── Reddit r/LocalLLaMA, r/medicine, r/healthIT
  ├── YouTube demo video (2 min)
  └── Dev.to / Medium blog post

Goal: 1,000 GitHub stars, 500 active users
Cost: $0 (your time only)
```

### Phase 2: Launch Paid Models (Month 3-6)

```
Actions:
  ├── Fine-tune first specialty model (General SOAP-Pro)
  ├── Set up purchase flow (Gumroad/LemonSqueezy → HF token)
  ├── Landing page with demo + comparison (SOAP-Pro vs base)
  ├── Email list from Phase 1 users
  └── "Launch discount: $69 instead of $99"

Goal: 50-100 model sales, $5K-10K revenue
Cost: ~$200 (training) + $50/mo (landing page + payment)
```

### Phase 3: Launch SaaS (Month 6-12)

```
Actions:
  ├── Build web dashboard (Next.js + Claude API)
  ├── Add agent workflows (multi-step SOAP, ICD-10 coding)
  ├── Beta with 10 friendly clinics
  ├── HIPAA BAA (use Aptible or AWS HIPAA-eligible services)
  └── Sales outreach to small practices

Goal: 20-50 paying SaaS users, $2K-10K MRR
Cost: ~$200/mo infrastructure
```

---

## Target Customers

### Phase 1-2 (Edge)

| Segment | Why They Care | How They Find You |
|---------|--------------|-------------------|
| Solo practitioners | Privacy, cost savings vs Dragon Medical | Google, Reddit |
| Medical students | Free AI scribe for rotations | Twitter, TikTok |
| Telehealth providers | On-device = no cloud risk | Product Hunt |
| Rural clinics | Poor internet, need offline | Word of mouth |
| Privacy advocates | Open source, auditable | Hacker News |

### Phase 3 (SaaS)

| Segment | Why They Care | How They Find You |
|---------|--------------|-------------------|
| Small practices (2-10 docs) | Team features, shared templates | Upgraded from edge |
| Urgent care chains | Standardized SOAP across locations | Sales outreach |
| Specialty clinics | Domain-specific models + cloud accuracy | Referrals |

---

## Competitive Positioning

```
                    Privacy
                      ▲
                      │
        EDGESCRIBE    │     (empty quadrant —
        (edge)        │      your opportunity)
                      │
  ◄───────────────────┼───────────────────►
  Low Quality         │          High Quality
                      │
        Basic         │     Nuance/Dragon
        open-source   │     AWS Transcribe
        tools         │     (cloud, expensive)
                      │
                      ▼
                   Cloud-dependent
```

EDGESCRIBE occupies the **top-right quadrant**: high quality (fine-tuned models)
AND private (on-device). No competitor currently sits here.

---

## Key Metrics to Track

| Phase | Metric | Target |
|-------|--------|:---:|
| 1 | GitHub stars | 1,000 |
| 1 | Monthly active users | 500 |
| 1 | Discord/community members | 200 |
| 2 | Model purchases (total) | 100 |
| 2 | Revenue | $10K |
| 2 | Repeat buyers (bundle) | 30% |
| 3 | SaaS MRR | $5K |
| 3 | SaaS churn rate | <5%/mo |
| 3 | NPS score | >50 |
