# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

**Reverbo** — JUCE audio plugin (VST3 / AU / Standalone) with a Svelte WebView UI. Stereo FDN Reverb effect. CMake target: `Reverbo`.

**Stack:** C++17, JUCE 8.0.4, CMake 3.22+, Svelte 5, TypeScript, Vite, Tailwind CSS.

## Build Commands

```bash
# First time or after CMakeLists.txt changes
mkdir -p build && cd build && cmake .. && make -j$(sysctl -n hw.ncpu)

# Subsequent builds (C++ or Svelte changes)
cd build && make -j$(sysctl -n hw.ncpu)

# Frontend only (manual)
cd WebUI && npm run build

# Frontend dev server (browser preview only — bridge won't work outside JUCE)
cd WebUI && npm run dev
```

If `make` fails with a Vite module error: `rm -rf WebUI/node_modules WebUI/package-lock.json && cd WebUI && npm install`.

## Running / Testing

- **Standalone:** `open "build/Reverbo_artefacts/Standalone/Reverbo.app"`
- **VST3:** Auto-copied to `~/Library/Audio/Plug-Ins/VST3/` on every build (rescan in DAW if needed)

There are no automated tests. Validation is done by running the standalone or loading the VST3 in a DAW.

## Architecture Overview

### Signal Chain (per audio block)

```
Input → InputFilter → Predelay → EarlyReflections → FDNReverb → Chorus → StereoWidener → DryWetMixer → Output
```

The FDN internally runs: `DiffusionNetwork → 8×DelayLine → CrossoverFilter → decay gain → FeedbackMatrix (Hadamard 8×8)`.

**Real-time safety rules:** No heap allocations in `processBlock()`. All buffers pre-allocated in `prepareToPlay()`. All parameters use `SmoothedValue` (10ms exponential) to prevent clicks.

### C++ ↔ JS Bridge

Two-direction communication through JUCE's `WebBrowserComponent`:

- **JS → C++:** Frontend navigates to `juce://setparameter?name=<id>&value=<normalizedFloat>`. `WebviewBridge::pageAboutToLoad()` intercepts and calls `apvts.getParameter(id)->setValueNotifyingHost(value)`.
- **C++ → JS:** `PluginEditor` polls at 30Hz; if any APVTS parameter changed by >0.001, calls `evaluateJavascript("window.setParameterValue('<id>', <value>)")`, which updates the corresponding Svelte writable store.

The `juce://` scheme only works inside the plugin. The bridge in `bridge.ts` guards for `http://` context (Vite dev server).

### Web Asset Embedding

Svelte builds to `WebUI/dist/index.html`. CMake embeds it via `juce_add_binary_data`. At runtime, `PluginEditor` unpacks the HTML to a temp dir and loads it locally in the `WebBrowserComponent`. The `BuildSvelteUI` custom target runs `npm install && npm run build` automatically during `make`.

### Frontend State

All 31 parameters have individual `writable` Svelte stores (normalized 0–1) in `WebUI/src/state/store.ts`. Derived stores compute display units (Hz, ms, dB). Fine-grained reactivity: only components subscribed to a changed store re-render.

## Key Files

| File | Purpose |
|------|---------|
| `Source/Plugin/PluginProcessor.cpp` | `processBlock()` — orchestrates full signal chain |
| `Source/Plugin/PluginEditor.cpp` | WebView host, 30Hz param sync timer |
| `Source/Plugin/WebviewBridge.h` | `juce://` URL protocol handler (JS→C++) |
| `Source/Parameters/ParameterIDs.h` | String ID constants for all 31 parameters |
| `Source/Parameters/ParameterLayout.h` | APVTS layout (ranges, skews, defaults) |
| `Source/DSP/Decay/FDNReverb.cpp` | 8-line FDN core |
| `Source/DSP/DiffusionNetwork/DiffusionNetwork.cpp` | 4-stage Schroeder input diffusion |
| `WebUI/src/App.svelte` | Root UI layout (5-panel + bottom row) |
| `WebUI/src/bridge/bridge.ts` | Frontend bridge, registers `window.setParameterValue` |
| `WebUI/src/state/store.ts` | Per-parameter Svelte stores + derived display values |
| `WebUI/src/types/parameters.ts` | `ParameterId` union type |

## Parameters

31 parameters across 5 panels, all in APVTS as normalized 0–1 floats. IDs defined in `ParameterIDs.h`.

- **Panel 1 — Input:** `loCutEnabled`, `hiCutEnabled`, `loCutFreq`, `hiCutFreq`, `hiCutQ`, `predelay`
- **Panel 2 — Early Reflections:** `erEnabled`, `erAmount`, `erRate`, `erShape`, `reflectGain`
- **Panel 3 — Diffusion Network:** `reverbMode`, `highFilterType`, `crossoverFreq`, `diffusion`, `scale`, `damping`, `feedback`, `chorusEnabled`, `chorusAmount`, `chorusRate`, `diffuseGain`
- **Panel 4 — Decay:** `decay`, `smooth`, `size`, `freeze`, `flatEnabled`, `cutEnabled`
- **Panel 5 — Output:** `stereo`, `density`, `dryWet`

## UI Design

Neumorphic style (warm beige `#ede6da`, inset/outset shadows). Window: 960×700px default, resizable 450×280 → 1800×1120. Dark canvases for `FilterGraph.svelte` and `NeuDiffusionNetworkGraph.svelte`.

`FilterGraph.svelte` — interactive log-frequency display; dragging adjusts `hiCutFreq` (horizontal) and `hiCutQ` (vertical).

`NeuDiffusionNetworkGraph.svelte` — two draggable nodes: teal (crossoverFreq × diffusion), orange (damping × feedback).

## FDN Delay Line Lengths (@ 44.1kHz, mutually prime)

`557, 617, 683, 743, 809, 877, 947, 1019` samples (≈12–23 ms). Scaled by `size` parameter at runtime.

## Docs

Full architecture and DSP details: `docs/ARCHITECTURE.md`
