# EDGESCRIBE Cloud API Integration — Enterprise v2.0 Consideration

## Decision: NOT in v1.x Consumer Product

Cloud LLM integration (Claude, OpenAI, etc.) is **intentionally excluded** from the consumer product. This is a deliberate choice, not a missing feature.

### Why we don't add it for consumers

| Risk | Detail |
|---|---|
| **HIPAA liability** | If a doctor sends PHI to Claude without a BAA, they violate HIPAA. EDGESCRIBE enabling this creates legal exposure. |
| **API key confusion** | Doctors are not developers. API keys are confusing — users will paste wrong, lose them, or share them accidentally. |
| **Mixed messaging** | "100% private" on homepage + "send data to cloud" in settings = contradictory, erodes trust. |
| **Support burden** | "Why is my API key not working?" becomes the #1 support ticket. Key rotation, rate limits, billing — all your problem now. |
| **Competing on wrong axis** | We win on privacy, not on LLM quality. Adding cloud makes users compare our UX to ChatGPT — a losing game. |
| **BAA complexity** | OpenAI/Anthropic offer BAAs for their API, but YOUR software as the intermediary creates additional legal obligations. Lawyers get involved. |

### What to do instead (improving local quality)

```
❌ Add cloud API       → confusing, risky, off-brand
✅ Fine-tune models    → better quality AND 100% private
✅ Better prompts/RAG  → improve output without any cloud
✅ Bigger local models → Qwen 7B/14B when hardware allows
✅ Specialty adapters  → cardiology-lora, dermatology-lora, etc.
```

---

## Enterprise v2.0: Cloud LLM as a Managed Feature

For hospitals and large clinics that **demand** cloud LLM integration and have their own legal agreements (BAAs) with providers, this can be offered as an **enterprise-only, IT-managed** feature.

### Key constraints

1. **NEVER exposed in consumer product** — no toggle in Settings, no CLI flag in the free version
2. **IT department configures** — not the doctor. API keys managed by hospital IT.
3. **Hospital's own BAA** — the hospital has a signed BAA with OpenAI/Anthropic. EDGESCRIBE is just the proxy.
4. **Hospital accepts liability** — EDGESCRIBE's enterprise agreement explicitly states this.
5. **Clear audit trail** — log what was sent to cloud, when, by whom.

### Architecture

```
Doctor's device                    Hospital IT managed
┌──────────────────┐              ┌───────────────────────┐
│  EDGESCRIBE      │              │  Cloud API            │
│                  │              │  (hospital's account) │
│  ASR: LOCAL ✅   │              │                       │
│  TTS: LOCAL ✅   │              │  OpenAI / Claude      │
│  Vision: LOCAL ✅│              │  w/ BAA signed        │
│                  │   text       │                       │
│  LLM: ──────────┼──────────►  │  Generate SOAP notes  │
│  (cloud proxy)   │   only      │                       │
│                  │◄────────────│  Return result        │
└──────────────────┘              └───────────────────────┘

Audio NEVER leaves the device.
Only de-identified text goes to cloud (if IT enables it).
```

### What gets sent to cloud

| Data type | Sent to cloud? | Notes |
|---|---|---|
| **Raw audio** | ❌ NEVER | ASR always runs locally |
| **Images** | ❌ NEVER | Vision always runs locally |
| **Transcript text** | ⚠️ Only if IT enables | De-identified text only |
| **SOAP note prompts** | ⚠️ Only if IT enables | Contains transcript excerpts |
| **Chat messages** | ⚠️ Only if IT enables | User's questions + LLM responses |
| **API keys** | Stored locally | Encrypted in config, managed by IT |

### Configuration (IT-managed, not user-facing)

```yaml
# ~/.EDGESCRIBE/enterprise.yaml (placed by IT deployment tool)
enterprise:
  cloud_llm:
    enabled: true
    provider: "anthropic"        # or "openai", "azure-openai"
    api_key_env: "EDGESCRIBE_CLOUD_API_KEY"  # Read from env var
    model: "claude-sonnet-4-20250514"
    
    # Privacy controls
    send_audio: false            # ALWAYS false — enforced, not configurable
    send_images: false           # ALWAYS false — enforced, not configurable
    
    # Audit
    log_cloud_requests: true     # Log all text sent to cloud
    log_path: "/var/log/edgescribe/cloud-audit.log"

  # Hospital info (for audit trail)
  organization: "Memorial General Hospital"
  baa_reference: "BAA-2026-0042-Anthropic"
```

### UI changes (enterprise only)

Settings page shows provider info (read-only for doctors):

```
┌──────────────────────────────────────────────────┐
│  LLM Provider                                    │
│  ☁️ Claude (Anthropic) — configured by IT        │
│  Organization: Memorial General Hospital         │
│                                                  │
│  ⚠️ Chat text is processed by Anthropic's API    │
│  under your organization's Business Associate    │
│  Agreement. Audio stays on this device.          │
└──────────────────────────────────────────────────┘
```

Doctors don't see API keys. They don't configure anything. IT sets it up during deployment.

### Enterprise license terms

```
EDGESCRIBE Enterprise Cloud Integration Addendum:

1. Customer acknowledges that enabling Cloud LLM integration
   will cause text data (not audio) to be transmitted to the
   selected third-party AI provider.

2. Customer is responsible for maintaining a valid Business
   Associate Agreement (BAA) with the selected AI provider.

3. EDGESCRIBE Inc. is not a Business Associate of Customer
   for data processed by third-party AI providers.

4. Customer's IT department is responsible for API key
   management, rotation, and access control.

5. EDGESCRIBE provides audit logging of all cloud API
   requests for Customer's compliance records.
```

---

## Implementation (v2.0+)

### Code changes

```cpp
// New: src/llm/cloud_llm.h
class CloudLlmProvider {
 public:
  explicit CloudLlmProvider(const CloudConfig& config);
  
  std::string Chat(const std::string& system,
                   const std::string& user_message,
                   TokenCallback on_token = nullptr);

 private:
  std::string provider_;   // "anthropic", "openai"
  std::string api_key_;
  std::string model_;
  // Uses httplib.h for HTTPS requests — already in the binary
};
```

```cpp
// Modified: LLM routing in api_server.cpp
if (enterprise_config.cloud_llm_enabled) {
    result = cloud_llm->Chat(system, prompt);
    AuditLog(session_id, prompt, result);  // Log for compliance
} else {
    result = llm->Chat(system, prompt, max_length);  // Local
}
```

### Effort estimate

| Task | Lines | Notes |
|---|---|---|
| CloudLlmProvider (HTTP client) | ~150 | Uses httplib.h, supports OpenAI + Anthropic API format |
| Enterprise config parser | ~80 | YAML or JSON config file |
| Audit logging | ~50 | Append to log file with timestamps |
| Settings UI (read-only display) | ~30 | Show provider info, no editing |
| **Total** | ~310 | Small feature, but big legal/compliance consideration |

---

## Timeline

```
v1.0 — v1.x:  LOCAL ONLY. No cloud option. Period.
v2.0+:         Enterprise-only cloud proxy (IT-managed, with BAA requirement)
Never:         Consumer cloud toggle (too risky, off-brand)
```

## Summary

```
Consumer product:   100% local, always. This is the brand.
Enterprise add-on:  Cloud LLM proxy, IT-configured, BAA required.
Audio/Images:       NEVER sent to cloud, regardless of config.
API keys:           Managed by hospital IT, never shown to doctors.
Liability:          Hospital's BAA with provider, not ours.
```
