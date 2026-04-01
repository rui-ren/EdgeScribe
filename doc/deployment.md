# EDGESCRIBE Deployment Guide

## Supported Platforms

### CI-Built Releases (Pre-built Binaries)

| Platform | Architecture | GPU Acceleration | Artifact |
|----------|:---:|:---:|----------|
| Windows | x86_64 | — (CPU only) | `openscribe-win-x64.zip` + installer |
| macOS Apple Silicon | ARM64 | Metal | `openscribe-osx-arm64.tar.gz` |
| macOS Intel | x86_64 | — (CPU only) | `openscribe-osx-x64.tar.gz` |
| Linux | x86_64 | — (CPU only) | `openscribe-linux-x64.tar.gz` |

### Manually Built (Build-It-Yourself)

| Platform | Architecture | GPU Acceleration | Notes |
|----------|:---:|:---:|-------|
| Linux ARM64 | AArch64 | — (CPU) | NVIDIA Jetson, Raspberry Pi 5, AWS Graviton |
| Linux ARM64 | AArch64 | CUDA | NVIDIA Jetson (JetPack) |
| Windows ARM64 | AArch64 | — (CPU) | Snapdragon laptops |
| Any Linux | x86_64 | CUDA | Build llama.cpp with `-DGGML_CUDA=ON` |

---

## Quick Deploy (Pre-built)

### Windows

```powershell
# Option A: Installer (recommended)
# Download EDGESCRIBESetup.exe from Releases → run it
# Installs to %LOCALAPPDATA%\Programs\EDGESCRIBE, adds to PATH
# Creates a desktop shortcut that launches the native GUI

# Option B: ZIP
Expand-Archive openscribe-win-x64.zip -DestinationPath C:\edgescribe
$env:PATH += ";C:\edgescribe\openscribe-win-x64"

# Download models and run
edgescribe pull nemotron
edgescribe pull qwen3-vl
edgescribe gui              # Opens native desktop window
# or: edgescribe serve      # Opens browser-based UI at localhost:8080
```

### macOS

```bash
tar xzf openscribe-osx-arm64.tar.gz
sudo cp openscribe-osx-arm64/edgescribe /usr/local/bin/
sudo cp openscribe-osx-arm64/*.dylib /usr/local/lib/

edgescribe pull nemotron
edgescribe pull qwen3-vl
edgescribe --version
```

### Linux x64

```bash
tar xzf openscribe-linux-x64.tar.gz
sudo cp openscribe-linux-x64/edgescribe /usr/local/bin/
sudo cp openscribe-linux-x64/*.so* /usr/local/lib/
sudo ldconfig

edgescribe pull nemotron
edgescribe pull qwen3-vl
edgescribe --version
```

---

## NVIDIA Jetson Deployment (Build from Source)

NVIDIA Jetson boards (Orin Nano, Orin NX, AGX Orin) run Linux ARM64 with
optional CUDA support. Pre-built binaries are not provided — build on the
device or cross-compile.

### Prerequisites (on Jetson)

```bash
# JetPack should already include CUDA toolkit
nvcc --version        # Verify CUDA
cmake --version       # Need 3.18+
g++ --version         # Need C++20 (GCC 12+)

# If CMake is too old:
sudo apt-get install -y cmake

# If GCC is too old (Ubuntu 20.04 on older JetPack):
sudo apt-get install -y gcc-12 g++-12
sudo update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-12 100
sudo update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-12 100
```

### Build on Jetson

```bash
# 1. Clone EDGESCRIBE
git clone https://github.com/EDGESCRIBE/EDGESCRIBE.git
cd EDGESCRIBE
curl -sL -o include/miniaudio.h \
  https://raw.githubusercontent.com/mackron/miniaudio/master/miniaudio.h

# 2. Build llama.cpp (with CUDA for Jetson GPU)
git clone --depth 1 https://github.com/ggerganov/llama.cpp.git
cd llama.cpp

cmake -B build -DCMAKE_BUILD_TYPE=Release \
  -DGGML_CUDA=ON \
  -DLLAMA_BUILD_TESTS=OFF \
  -DLLAMA_BUILD_EXAMPLES=OFF \
  -DLLAMA_BUILD_SERVER=OFF
cmake --build build --config Release -j$(nproc)
cmake --install build --prefix ./install
cd ..

# 3. Download onnxruntime-genai (ARM64 Linux)
GENAI_VERSION="0.12.0"
mkdir -p genai_extracted
curl -sL "https://github.com/microsoft/onnxruntime-genai/releases/download/v${GENAI_VERSION}/onnxruntime-genai-${GENAI_VERSION}-linux-arm64.tar.gz" \
  | tar xz -C genai_extracted
GENAI_DIR=$(find genai_extracted -name "include" -type d | head -1 | xargs dirname)

# 4. Build EDGESCRIBE
cmake -B build -DCMAKE_BUILD_TYPE=Release \
  -DORT_GENAI_PATH=$GENAI_DIR \
  -DLLAMA_CPP_PATH=./llama.cpp/install
cmake --build build --config Release -j$(nproc)

# 5. Install
sudo cp build/edgescribe /usr/local/bin/
sudo cp build/*.so* /usr/local/lib/
sudo ldconfig
```

### Build on Jetson (CPU-only, no CUDA)

If you don't need GPU acceleration or have a Jetson Nano with limited VRAM:

```bash
# Same as above but change step 2:
cmake -B build -DCMAKE_BUILD_TYPE=Release \
  -DGGML_CUDA=OFF \
  -DLLAMA_BUILD_TESTS=OFF \
  -DLLAMA_BUILD_EXAMPLES=OFF \
  -DLLAMA_BUILD_SERVER=OFF
```

### Jetson Memory Considerations

| Jetson Model | RAM | Recommended Model | Notes |
|:---:|:---:|:---:|-------|
| Orin Nano (4GB) | 4 GB shared | Qwen3-VL Q4_K_S | Tight — skip TTS, reduce n_ctx |
| Orin Nano (8GB) | 8 GB shared | Qwen3-VL Q4_K_M | Full stack works |
| Orin NX (8GB) | 8 GB shared | Qwen3-VL Q4_K_M | Full stack works |
| Orin NX (16GB) | 16 GB shared | Qwen3-VL Q8_0 | Higher quality possible |
| AGX Orin (32/64GB) | 32-64 GB | Qwen3-VL Q8_0 or 8B | Can run larger models |

> Jetson uses **unified memory** — CPU and GPU share the same RAM. The model
> must fit in total available memory minus OS overhead (~1-2 GB).

### Running on Jetson with GPU

```bash
# Use CUDA for LLM/Vision (offload all layers to GPU)
edgescribe chat "Hello" --device cuda

# Start server with GPU
edgescribe serve --device cuda --port 8080
```

---

## Raspberry Pi 5 Deployment (ARM64, CPU-only)

### Prerequisites

```bash
# Raspberry Pi OS (64-bit) with GCC 12+
sudo apt-get update
sudo apt-get install -y cmake g++ git curl libpulse-dev
```

### Build

Same as Jetson CPU-only build, but skip CUDA:

```bash
# Follow the Jetson steps above with DGGML_CUDA=OFF
# Raspberry Pi 5 has 4-8 GB RAM — use Q4_K_S or Q4_K_M
```

### Pi 5 Performance Expectations

| Task | Model | Speed |
|------|-------|-------|
| LLM chat (Q4_K_M) | Qwen3-VL-2B | ~15-25 tok/s |
| Vision (Q4_K_M) | Qwen3-VL-2B | ~10-15 tok/s |
| ASR (Nemotron) | Parakeet 0.6B | Near real-time |
| TTS (Kokoro/Piper) | — | Real-time |

---

## Cross-Compilation (Build on x64, Run on ARM64)

If you prefer to build on a faster x64 machine for ARM64 targets:

### Install Toolchain

```bash
# Ubuntu/Debian
sudo apt-get install -y gcc-aarch64-linux-gnu g++-aarch64-linux-gnu
```

### Cross-Compile llama.cpp

```bash
cd llama.cpp
cmake -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_SYSTEM_NAME=Linux \
  -DCMAKE_SYSTEM_PROCESSOR=aarch64 \
  -DCMAKE_C_COMPILER=aarch64-linux-gnu-gcc \
  -DCMAKE_CXX_COMPILER=aarch64-linux-gnu-g++ \
  -DGGML_CUDA=OFF \
  -DLLAMA_BUILD_TESTS=OFF \
  -DLLAMA_BUILD_EXAMPLES=OFF \
  -DLLAMA_BUILD_SERVER=OFF
cmake --build build -j$(nproc)
cmake --install build --prefix ./install
```

### Cross-Compile EDGESCRIBE

```bash
# You also need ARM64 onnxruntime-genai libs
# Download the linux-arm64 release from GitHub

cmake -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_SYSTEM_NAME=Linux \
  -DCMAKE_SYSTEM_PROCESSOR=aarch64 \
  -DCMAKE_C_COMPILER=aarch64-linux-gnu-gcc \
  -DCMAKE_CXX_COMPILER=aarch64-linux-gnu-g++ \
  -DORT_GENAI_PATH=./genai_extracted \
  -DLLAMA_CPP_PATH=./llama.cpp/install
cmake --build build -j$(nproc)
```

### Deploy to Target

```bash
# Copy to Jetson/Pi via scp
scp build/edgescribe user@jetson:/usr/local/bin/
scp build/*.so* user@jetson:/usr/local/lib/
ssh user@jetson "sudo ldconfig && edgescribe --version"
```

---

## Server Deployment

### Run as a Systemd Service (Linux)

```bash
# Create service file
sudo tee /etc/systemd/system/edgescribe.service > /dev/null << 'EOF'
[Unit]
Description=EDGESCRIBE AI Server
After=network.target

[Service]
Type=simple
User=edgescribe
ExecStart=/usr/local/bin/edgescribe serve --port 8080
Restart=always
RestartSec=5
Environment=EDGESCRIBE_MODEL_DIR=/opt/edgescribe/models

[Install]
WantedBy=multi-user.target
EOF

# Create user and model directory
sudo useradd -r -s /bin/false edgescribe
sudo mkdir -p /opt/edgescribe/models
sudo chown edgescribe:edgescribe /opt/edgescribe/models

# Download models as the service user
sudo -u edgescribe EDGESCRIBE_MODEL_DIR=/opt/edgescribe/models edgescribe pull nemotron
sudo -u edgescribe EDGESCRIBE_MODEL_DIR=/opt/edgescribe/models edgescribe pull qwen3-vl

# Enable and start
sudo systemctl daemon-reload
sudo systemctl enable edgescribe
sudo systemctl start edgescribe

# Check status
sudo systemctl status edgescribe
curl http://localhost:8080/v1/health
```

### Run with Docker (x64 Linux)

```dockerfile
FROM ubuntu:22.04

RUN apt-get update && apt-get install -y curl && rm -rf /var/lib/apt/lists/*

COPY openscribe-linux-x64/ /opt/edgescribe/
ENV PATH="/opt/edgescribe:$PATH"
ENV EDGESCRIBE_MODEL_DIR=/models

EXPOSE 8080
VOLUME /models

CMD ["edgescribe", "serve", "--port", "8080"]
```

```bash
# Build and run
docker build -t edgescribe .
docker run -d -p 8080:8080 -v /path/to/models:/models edgescribe

# Download models into the volume first
docker run --rm -v /path/to/models:/models edgescribe edgescribe pull nemotron
docker run --rm -v /path/to/models:/models edgescribe edgescribe pull qwen3-vl
```

---

## Environment Variables

| Variable | Default | Description |
|----------|---------|-------------|
| `EDGESCRIBE_MODEL_DIR` | Platform-specific (see below) | Override model cache directory |

**Default model directories:**
- Windows: `%LOCALAPPDATA%\EDGESCRIBE\models\`
- macOS/Linux: `~/.EDGESCRIBE/models/`

---

## Hardware Requirements

### Minimum

| Component | Requirement |
|-----------|------------|
| CPU | x86_64 with AVX2, or ARM64 with NEON |
| RAM | 4 GB (ASR only) / 8 GB (full stack) |
| Disk | 2 GB free (after models downloaded) |
| OS | Windows 10+, macOS 12+, Linux (glibc 2.31+) |

### Recommended

| Component | Recommendation |
|-----------|---------------|
| CPU | 8+ cores, AVX2 or Apple Silicon |
| RAM | 16 GB |
| GPU (optional) | NVIDIA (CUDA 11.8+), Apple Silicon (Metal) |
| Disk | SSD for model loading speed |
