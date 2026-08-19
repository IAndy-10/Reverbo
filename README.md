# Reverbo

A stereo algorithmic reverb plugin built around an 8-line **Feedback Delay Network (FDN)**.

## 📥 Download

**Latest Release:** [Reverbo v1.0.0](https://github.com/IAndy-10/Reverbo/releases/latest)

### Quick Install (macOS)

1. **Download** the `.zip` from [Releases](https://github.com/IAndy-10/Reverbo/releases)
2. **Unzip** and copy the plugins:
   - `Reverbo.vst3` → `~/Library/Audio/Plug-Ins/VST3/`
   - `Reverbo.component` → `~/Library/Audio/Plug-Ins/Components/`
   - `Reverbo.app` → `/Applications/` (optional, for standalone)
3. **Rescan** plugins in your DAW (restart DAW if needed)

### Supported Formats
- **VST3** (macOS 11+)
- **AU** (macOS 10.13+)
- **Standalone** (macOS 11+)

---

Produces dense, smooth, natural-sounding reverb tails with frequency-dependent decay, early reflections, and optional modulation. The UI is a Svelte WebView embedded in the plugin window, communicating with the C++ audio engine via JUCE's `juce://` URL-intercept bridge.

---

## Signal Chain

```
Input → InputFilter → Predelay → EarlyReflections ─┐
                                                     ├→ mix → Chorus → StereoWidener → DryWetMixer → Output
                         DiffusionNetwork → FDNReverb ┘
```

The FDN core: 8 delay lines (mutually prime lengths) → per-line CrossoverFilter → per-line decay gain (RT60 formula) → Hadamard 8×8 feedback matrix.

---

## Building

**Requirements:** CMake 3.22+, Xcode CLT, Node.js

```bash
# First time
mkdir build && cd build && cmake .. && make -j$(sysctl -n hw.ncpu)

# Subsequent builds
cd build && make -j$(sysctl -n hw.ncpu)
```

The Svelte frontend (`WebUI/`) is built automatically by `make`. To build it manually:

```bash
cd WebUI && npm run build
```

---

## Running

| Format | How |
|--------|-----|
| Standalone | `open "build/Reverbo_artefacts/Standalone/Reverbo.app"` |
| VST3 | Auto-copied to `~/Library/Audio/Plug-Ins/VST3/` on every build |
| AU | Auto-copied to `~/Library/Audio/Plug-Ins/Components/` on every build |

Rescan plugins in your DAW if the VST3/AU doesn't appear after the first build.

---

## Parameters

**Input** — Lo Cut / Hi Cut filters (freq, Q, enable), Predelay (0–500 ms)

**Early Reflections** — 8-tap reflections with spin modulation (amount, rate, shape), Reflect Gain

**Diffusion Network** — Reverb Mode (High/Low), Crossover Freq, Diffusion, Scale, Damping, Feedback, Chorus (enable, amount, rate), Diffuse Gain

**Decay** — RT60 (0.2–60 s), Size, Smooth, Freeze, Flat, Cut

**Output** — Stereo Width (mid-side), Density (0–4 allpass stages), Dry/Wet

---

## Architecture

**Stack:** C++17 · JUCE 8.0.4 · CMake · Svelte 5 · TypeScript · Vite · Tailwind CSS

**Bridge:** JS → C++ via `window.location.href = "juce://setparameter?name=<id>&value=<0-1>"`. C++ → JS via 30Hz polling that calls `evaluateJavascript("window.setParameterValue(...)")`. All 31 parameters have individual Svelte `writable` stores (normalized 0–1).

**Embedding:** Svelte builds to `WebUI/dist/index.html`, which CMake embeds as binary data (`juce_add_binary_data`). The final `.vst3`/`.component` is self-contained — no external server.

**Real-time safety:** No allocations in `processBlock()`. Parameters smoothed with `SmoothedValue` (10ms exponential). `ScopedNoDenormals` applied per block.

Full DSP and architecture details: [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md)

---

## Development Approach

This project was built with the assistance of **Claude Sonnet 4.6**.

Rather than prototyping small and iterating toward complexity, I took a different approach: drawing on my existing knowledge of audio DSP and frontend development, I wrote a detailed specification document upfront covering signal chain design, DSP math, parameter ranges, UI layout, and the C++/JS bridge protocol. The goal was to front-load all the design decisions so the model could follow explicit constraints from the start rather than making architectural choices along the way.

This workflow — spec-first, then implement — kept the LLM acting as an executor of a defined design rather than a co-designer, which produced more predictable and consistent results across the codebase.
