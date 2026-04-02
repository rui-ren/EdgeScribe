# EDGESCRIBE Workflow Pipelines — Design Document

## Why Pipelines, Not Agents

Traditional agentic AI (LLM plans → calls tools → observes → repeats) doesn't work well on edge devices:

| | Cloud Agent (GPT-4) | Edge Agent (Qwen3-VL-2B) |
|---|---|---|
| **Per-step latency** | ~1s | ~5-10s |
| **10-step task** | ~10s (acceptable) | ~60-100s (unusable) |
| **Planning ability** | ✅ Strong | ⚠️ Weak at 2B params |
| **Self-correction** | ✅ Reasons about errors | ❌ Loops or hallucinates |
| **Tool routing** | ✅ Reliable | ⚠️ Misroutes frequently |

**Solution**: The code decides the pipeline (deterministic, fast), the LLM executes each step (focused, reliable). Users get the "agentic" feeling without the latency and unreliability.

---

## Workflow Concept

A workflow is an ordered chain of steps that automatically executes after a trigger:

```
Trigger → Step 1 → Step 2 → Step 3 → Output
```

Each step is a known operation (transcribe, SOAP, summarize, speak, etc.). The output of one step flows as input to the next.

### Built-in Workflows

#### 1. Clinical Session (most common)

```
🎤 Record → 📝 Transcribe → 🏥 SOAP Notes → 🔊 Read Aloud
```

```bash
edgescribe workflow clinical --live
# 1. Records from microphone
# 2. Transcribes in real-time
# 3. On stop: generates SOAP notes from transcript
# 4. Reads SOAP notes aloud via TTS
# 5. Saves everything to history
```

#### 2. Document Review

```
📁 Audio file → 📝 Transcribe → 📋 Summarize → 💾 Save
```

```bash
edgescribe workflow review meeting.wav -o summary.txt
# 1. Transcribes the audio file
# 2. Summarizes the transcript
# 3. Saves summary to file
```

#### 3. Image-Assisted SOAP

```
🎤 Record → 📝 Transcribe → 🖼️ Analyze image → 🏥 SOAP Notes (with image context)
```

```bash
edgescribe workflow clinical --live --image xray.jpg
# 1. Records and transcribes
# 2. Analyzes the attached image
# 3. Generates SOAP notes using both transcript + image analysis
```

#### 4. Terminology Check

```
🎤 Record → 📝 Transcribe → 🔧 Fix Medical Terms → 📋 SOAP Notes
```

```bash
edgescribe workflow clinical --live --fix-terms
# 1. Records and transcribes
# 2. Fixes medical terminology errors in transcript
# 3. Generates SOAP notes from corrected transcript
```

---

## CLI Interface

```bash
# Run a named workflow
edgescribe workflow <name> [options]

# Built-in workflows:
edgescribe workflow clinical --live          # Record → transcribe → SOAP → speak
edgescribe workflow clinical file.wav        # File → transcribe → SOAP
edgescribe workflow review file.wav          # File → transcribe → summarize
edgescribe workflow review --live            # Record → transcribe → summarize

# Options (apply to any workflow):
  --image <file>      Attach image for context
  --fix-terms         Add terminology correction step
  --no-speak          Skip TTS at the end
  -o <file>           Save final output to file

# Custom chain (power users):
edgescribe workflow --steps "transcribe,fix-terms,soap,speak" --live
```

---

## REST API

```
POST /v1/workflow/run
{
  "workflow": "clinical",
  "source": "live",           // or "file"
  "audio_path": null,         // for file-based
  "image_path": null,         // optional
  "fix_terms": false,
  "speak_output": true,
  "output_steps": ["transcript", "soap"]  // which step outputs to return
}

Response (streamed as steps complete):
{
  "status": "running",
  "steps": [
    {"name": "transcribe", "status": "done", "output": "Patient presents with..."},
    {"name": "soap", "status": "running", "output": ""},
    {"name": "speak", "status": "pending"}
  ]
}
```

---

## Architecture

### Step Registry

Each step is a simple function with a standard interface:

```cpp
namespace EDGESCRIBE::workflow {

struct StepResult {
    std::string text;           // Text output of this step
    std::string audio_path;     // Audio file output (for TTS)
    bool success = true;
    std::string error;
};

// Every step takes text in, produces text out
using StepFn = std::function<StepResult(const std::string& input,
                                         const WorkflowContext& ctx)>;

// Registry of available steps
std::map<std::string, StepFn> GetStepRegistry(
    llm::LlmEngine* llm,
    vision::VisionEngine* vision,
    tts::TtsEngine* tts,
    Transcriber* asr);

}  // namespace EDGESCRIBE::workflow
```

### Step Definitions

```cpp
auto registry = {
    {"transcribe", [&](const std::string& input, const WorkflowContext& ctx) {
        // input = audio file path or "live"
        // output = transcript text
        StepResult r;
        if (ctx.live) {
            r.text = RunLiveTranscription(asr);
        } else {
            r.text = TranscribeFile(asr, input);
        }
        return r;
    }},

    {"soap", [&](const std::string& input, const WorkflowContext& ctx) {
        // input = transcript text
        StepResult r;
        if (ctx.image_path.empty()) {
            r.text = llm->GenerateSOAPNotes(input);
        } else {
            r.text = vision->GenerateSOAPNotes(input, ctx.image_path);
        }
        return r;
    }},

    {"summarize", [&](const std::string& input, const WorkflowContext& ctx) {
        StepResult r;
        r.text = llm->Summarize(input);
        return r;
    }},

    {"fix-terms", [&](const std::string& input, const WorkflowContext& ctx) {
        StepResult r;
        r.text = llm->FixMedicalTerms(input);
        return r;
    }},

    {"speak", [&](const std::string& input, const WorkflowContext& ctx) {
        StepResult r;
        tts->Synthesize(input, "af_heart", 1.0f);  // Play through speakers
        r.text = input;
        return r;
    }},

    {"ocr", [&](const std::string& input, const WorkflowContext& ctx) {
        StepResult r;
        r.text = vision->OCR(ctx.image_path);
        return r;
    }},
};
```

### Pipeline Runner

```cpp
StepResult RunWorkflow(const std::vector<std::string>& step_names,
                       const WorkflowContext& ctx,
                       ProgressCallback on_step) {
    StepResult result;
    result.text = ctx.initial_input;  // Starting input (empty for live)

    for (const auto& name : step_names) {
        auto it = registry.find(name);
        if (it == registry.end()) {
            result.success = false;
            result.error = "Unknown step: " + name;
            return result;
        }

        if (on_step) on_step(name, "running");

        result = it->second(result.text, ctx);

        if (!result.success) {
            if (on_step) on_step(name, "failed");
            return result;
        }

        if (on_step) on_step(name, "done");

        // Auto-save each step to memory
        if (memory) {
            memory->SaveNote(ctx.session_id, name, "", result.text);
        }
    }

    return result;
}
```

### Named Workflow Definitions

```cpp
std::map<std::string, std::vector<std::string>> kNamedWorkflows = {
    {"clinical",  {"transcribe", "soap", "speak"}},
    {"review",    {"transcribe", "summarize"}},
    {"dictate",   {"transcribe", "fix-terms"}},
};

// With --fix-terms flag, inject "fix-terms" before "soap":
// clinical → {"transcribe", "fix-terms", "soap", "speak"}
```

---

## Smart Suggestions (Post-Workflow)

After a workflow completes, suggest next actions based on the output:

```
✅ Workflow "clinical" complete.

Suggested next steps:
  [1] 📋 Summarize this session
  [2] 🔍 Search similar past sessions
  [3] 📤 Export as PDF
  [4] 🔄 Re-generate SOAP with different template

Press 1-4 or Enter to skip:
```

This is a **single LLM call** (not agent planning):

```cpp
std::string suggestions = llm->Chat(
    "Given this SOAP note, suggest 3 useful follow-up actions. "
    "Return as a numbered list.",
    soap_output, 256);
```

---

## Why This is Better Than Agents

| | Agent (LLM plans) | Pipeline (code plans) |
|---|---|---|
| **Latency** | ~60-100s (10 LLM calls) | ~30s (3 LLM calls, no planning) |
| **Reliability** | ⚠️ 2B model misroutes | ✅ Deterministic — always correct |
| **User control** | ❌ LLM decides | ✅ User picks workflow |
| **Debuggability** | ❌ "Why did it do that?" | ✅ Clear step-by-step |
| **Error handling** | ❌ LLM can't reason about errors | ✅ Code catches + reports |
| **Feels agentic?** | ✅ Yes | ✅ Yes — automatic multi-step |

The user experience is identical: "I press one button and get SOAP notes from my voice." The implementation is just more reliable.

---

## When to Revisit True Agents

When these conditions are met:

```
1. Local model ≥ 7B parameters (better planning/routing)
2. GPU acceleration (inference < 1s per call)
3. Clear use case that pipelines can't handle
```

At that point, consider:
- MCP (Model Context Protocol) for tool calling
- ReAct-style loop with tool registry
- But still prefer pipelines for known workflows

---

## Implementation Roadmap

```
v1.0:  Individual commands (chat, process, speak — already done)
v1.2:  Named workflows (edgescribe workflow clinical --live)
v1.3:  Custom chains (--steps "transcribe,fix-terms,soap")
v2.0:  Smart suggestions after workflow completion
v2.0+: True agent mode (if local models get good enough)
```

---

## Files to Create/Modify

| File | Changes |
|---|---|
| `src/workflow/workflow_runner.h` | NEW — StepResult, WorkflowContext, RunWorkflow() |
| `src/workflow/workflow_runner.cpp` | NEW — Step registry, pipeline runner, named workflows |
| `src/cli/main.cpp` | Add `workflow` command routing |
| `src/server/api_server.cpp` | Add `POST /v1/workflow/run` endpoint |
| `CMakeLists.txt` | Add workflow source files |
