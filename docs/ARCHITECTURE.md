# Reverbo — Architecture

**Version:** 0.3.0
**Type:** JUCE audio plugin (VST3 / AU / Standalone)
**Effect:** Stereo FDN Reverb with WebView UI
**Build system:** CMake 3.22+ / C++17
**Key deps:** JUCE 8.0.4, Svelte, TypeScript, Vite

---

## 1. Project Structure

```
Reverbo/
├── CMakeLists.txt
├── Source/
│   ├── Plugin/
│   │   ├── PluginProcessor.h / .cpp     — audio engine + APVTS
│   │   ├── PluginEditor.h / .cpp        — WebView host + 30Hz parameter sync
│   │   └── WebviewBridge.h              — juce:// URL protocol handler
│   ├── Parameters/
│   │   ├── ParameterIDs.h               — string ID constants
│   │   └── ParameterLayout.h            — APVTS layout (ranges, defaults)
│   └── DSP/
│       ├── Decay/
│       │   ├── FDNReverb.h / .cpp       — 8-line FDN core
│       │   └── Freeze.h                 — state carrier for freeze mode
│       ├── DiffusionNetwork/
│       │   ├── DiffusionNetwork.h / .cpp — 4-stage Schroeder input diffusion
│       │   ├── DiffusionStage.h          — stereo allpass stage
│       │   ├── CrossoverFilter.h / .cpp  — per-line frequency-dependent damping
│       │   ├── Chorus.h / .cpp           — LFO-modulated stereo delay
│       │   ├── DelayLine.h               — circular buffer with linear interpolation
│       │   ├── FeedbackMatrix.h          — Hadamard 8×8 matrix
│       │   ├── LFO.h                     — phase-accumulator sine oscillator
│       │   └── SmoothedValue.h           — exponential parameter smoother
│       ├── EarlyReflections/
│       │   ├── EarlyReflections.h / .cpp — 8-tap comb-like reflections
│       │   └── SpinModulator.h / .cpp    — early reflection modulation
│       ├── Input/
│       │   ├── InputFilter.h / .cpp      — hi-pass + lo-pass Butterworth biquads
│       │   └── Predelay.h / .cpp         — 0–500 ms stereo pre-reverb delay
│       └── Output/
│           ├── DryWetMixer.h             — per-sample crossfade with smoothing
│           └── StereoWidener.h           — mid-side width control
└── WebUI/
    ├── vite.config.ts
    ├── src/
    │   ├── App.svelte                   — root layout component
    │   ├── bridge/bridge.ts             — juce:// protocol, C++↔JS glue
    │   ├── state/store.ts               — per-parameter Svelte writable stores
    │   ├── types/parameters.ts          — ParameterId union type
    │   └── components/
    │       ├── FilterGraph.svelte        — interactive filter frequency response
    │       ├── NeuDiffusionNetworkGraph.svelte — FDN parameter node editor
    │       ├── NeuNumber.svelte          — spinbutton numeric input
    │       ├── NeuKnob.svelte            — rotary dial
    │       ├── NeuButton.svelte          — toggle button
    │       └── NeuSelector.svelte        — discrete option selector
    └── dist/index.html                  — built Svelte output (embedded in binary)
```

---

## 2. Audio Signal Chain

Per audio block, processing is sequential:

```
Input Buffer (stereo)
  │
  ├─ [InputFilter]          Hi-pass (Lo Cut) + Lo-pass (Hi Cut) Butterworth biquads
  ├─ [Predelay]             Stereo delay up to 500ms
  ├─ [EarlyReflections]     8-tap comb-like delays with spin modulation (additive)
  ├─ [FDNReverb]            8-line feedback delay network (main reverb tail)
  │     ├─ DiffusionNetwork     4-stage Schroeder allpass input pre-diffusion
  │     ├─ DelayLine[8]         Mutually prime delays (557–1019 samples @ 44.1kHz)
  │     ├─ CrossoverFilter[8]   Per-line frequency-dependent decay (one-pole LP)
  │     ├─ LFO[8]               Per-line modulation (staggered 0.31–0.56 Hz)
  │     └─ FeedbackMatrix       Normalized Hadamard 8×8 (±1/√8)
  ├─ [Reflect/Diffuse Gains] Per-component level trim (dB)
  ├─ [Chorus]               LFO-modulated stereo delay (additive movement)
  ├─ [StereoWidener]        Mid-side width control
  └─ [DryWetMixer]          Per-sample smooth crossfade with dry signal
```

**Real-time safety guarantees:**
- No heap allocations inside `processBlock()`
- All delay buffers pre-allocated at `prepareToPlay()`
- `juce::ScopedNoDenormals` disables flush-to-zero in the audio callback
- All parameters use 10ms exponential smoothing to prevent clicks

---

## 3. DSP Modules

### FDNReverb — Core Reverb Engine

8-line feedback delay network. Each line carries delayed + filtered signal back into itself through a shared Hadamard feedback matrix.

**Base delay lengths (@ 44.1kHz, mutually prime):**
```
557, 617, 683, 743, 809, 877, 947, 1019 samples  (≈ 12–23 ms)
```

**Per-sample loop:**
1. Diffuse stereo input through DiffusionNetwork
2. Read from 8 delay lines (with per-line LFO modulation, depth ±2 samples)
3. Apply CrossoverFilter (frequency-dependent decay per line)
4. Apply RT60-based decay gain: `g = 10^(-3 * delayTime / RT60)`
5. Mix through Hadamard feedback matrix (fast Walsh-Hadamard, O(N log N))
6. Write feedback + new input back to delay lines
7. Output: even lines → L, odd lines → R, scaled by 1/√8

**Key parameters controlling FDN behavior:**
| Parameter | Effect |
|-----------|--------|
| Decay (RT60) | Per-line gain coefficient → tail length |
| Size | Scales all delay lengths (longer = larger room) |
| Diffusion | Allpass coefficient in DiffusionNetwork stages |
| Damping | LP cutoff depth per CrossoverFilter |
| Crossover Freq | Base frequency for per-line damping |
| Feedback | Additional scaling of RT60 gain |
| Reverb Mode | High (1.5× crossover, brighter) / Low (0.6×, darker) |
| Freeze | Forces gain to 0.9999 → infinite sustain |

### DiffusionNetwork — Input Pre-Diffusion

4 cascaded Schroeder allpass stages spread input energy before it enters the FDN. Prevents coloration and comb filtering on transients.

Prime delays (L / R, @ 44.1kHz):
- Stage 1: 151 / 167
- Stage 2: 211 / 239
- Stage 3: 379 / 397
- Stage 4: 479 / 509 samples

### CrossoverFilter — Per-Line Damping

One-pole lowpass per FDN delay line: `y[n] = c·y[n-1] + (1−c)·x[n]`

Simulates frequency-dependent room absorption (high frequencies decay faster). Cutoff and depth controlled by `crossoverFreq` and `damping` parameters, scaled by reverb mode.

### InputFilter — Tone Shaping

Two independent 2nd-order Butterworth biquads (direct form II), one per stereo channel:
- **Hi-pass (Lo Cut):** 50–18 000 Hz, Q = 0.7071 (fixed)
- **Lo-pass (Hi Cut):** 50–18 000 Hz, Q = 0.5–9.0 (variable resonance)

### EarlyReflections — 8-Tap Reflections

8 stereo delay taps with exponential gain decay and spin modulation. Adds transient richness before the FDN tail.

Tap gains: `[0.85, 0.70, 0.60, 0.50, 0.40, 0.35, 0.28, 0.22]`
Tap delays (L/R, prime): 89/107 → 809/839 samples

### Predelay

Stereo circular buffer delay, 0–500ms. Separates dry signal from reverb onset, simulating room depth.

### Chorus

Two LFO-modulated delay lines (L/R). Base delay: 12ms, max modulation: ±6ms. Adds movement and prevents the reverb tail from sounding static.

### DryWetMixer

Per-sample linear interpolation between dry and wet over the block:
```
out[i] = lerp(dry[i], wet[i], w_i)   where w_i ramps from w_prev to w_curr
```
Prevents zipper noise on fast mix changes.

### StereoWidener

Mid-side processing:
```
mid  = (L + R) / 2
side = (L − R) / 2 × width
L_out = mid + side,  R_out = mid − side
```
Width 0 = mono, 1.0 = unchanged, 2.0 = doubled stereo spread.

### DelayLine (Primitive)

Fixed-capacity circular buffer. Supports integer reads and fractional-delay reads via linear interpolation. Used by all delay-based modules.

### FeedbackMatrix

Normalized 8×8 Hadamard matrix (±1/√8). Applied via fast Walsh-Hadamard transform: energy-preserving, orthogonal, provides full decorrelation between all 8 FDN lines.

---

## 4. Parameters (31 total)

All parameters are stored in JUCE `AudioProcessorValueTreeState` (APVTS) and communicated to the UI in normalized 0–1 form.

### Panel 1 — Input (6)
| ID | Name | Type | Range | Default |
|----|------|------|-------|---------|
| `loCutEnabled` | Lo Cut | Bool | — | false |
| `hiCutEnabled` | Hi Cut | Bool | — | false |
| `loCutFreq` | Lo Cut Freq | Float | 50–18 000 Hz (skew 0.3) | 80 Hz |
| `hiCutFreq` | Hi Cut Freq | Float | 50–18 000 Hz (skew 0.3) | 8000 Hz |
| `hiCutQ` | Hi Cut Q | Float | 0.5–9.0 | 0.7071 |
| `predelay` | Predelay | Float | 0–500 ms | 20 ms |

### Panel 2 — Early Reflections (5)
| ID | Name | Type | Range | Default |
|----|------|------|-------|---------|
| `erEnabled` | ER Spin | Bool | — | false |
| `erAmount` | ER Amount | Float | 2–55 | 10 |
| `erRate` | ER Rate | Float | 0.07–1.3 Hz | 0.5 Hz |
| `erShape` | ER Shape | Float | 0–1 | 0.5 |
| `reflectGain` | Reflect Gain | Float | −30 to +6 dB | 0 dB |

### Panel 3 — Diffusion Network (11)
| ID | Name | Type | Range | Default |
|----|------|------|-------|---------|
| `reverbMode` | Mode | Int | 0–1 | 0 (High) |
| `highFilterType` | High Filter Type | Bool | false = LP, true = High Shelf | false |
| `crossoverFreq` | Crossover Freq | Float | 200–8 000 Hz | 3000 Hz |
| `diffusion` | Diffusion | Float | 0–1 | 0.6 |
| `scale` | Scale | Float | 0–1 | 0.5 |
| `damping` | Damping | Float | 0–1 | 0.5 |
| `feedback` | Feedback | Float | 0–1 | 0.75 |
| `chorusEnabled` | Chorus Enable | Bool | — | false |
| `chorusAmount` | Chorus Amount | Float | 0.01–4.0 | 0.2 |
| `chorusRate` | Chorus Rate | Float | 0.01–8.0 Hz | 1.5 Hz |
| `diffuseGain` | Diffuse Gain | Float | −30 to +6 dB | 0 dB |

### Panel 4 — Decay (6)
| ID | Name | Type | Range | Default |
|----|------|------|-------|---------|
| `decay` | Decay (RT60) | Float | 200–60 000 ms | 1500 ms |
| `smooth` | Smooth | Int | 0–3 (Off/Low/Med/High) | 0 |
| `size` | Size | Float | 0–1 (maps to 0.22–500 in UI, log) | 0.5 |
| `freeze` | Freeze | Bool | — | false |
| `flatEnabled` | Flat | Bool | — | false |
| `cutEnabled` | Cut | Bool | — | false |

### Panel 5 — Output (3)
| ID | Name | Type | Range | Default |
|----|------|------|-------|---------|
| `stereo` | Stereo Width | Float | 0–120° | 120° |
| `density` | Density | Int | 0–3 (Sparse/Low/Mid/High) | 3 (High) |
| `dryWet` | Dry/Wet | Float | 0–100% | 50% |

---

## 5. C++ ↔ JavaScript Bridge

Communication between the JUCE C++ backend and the Svelte frontend uses a two-direction protocol over JUCE's `WebBrowserComponent`.

### JS → C++ (user interaction)

The frontend navigates to a `juce://` URL. JUCE intercepts it in `WebviewBridge::pageAboutToLoad()` before any navigation occurs.

```typescript
// bridge.ts
window.location.href = `juce://setparameter?name=${id}&value=${normalizedValue}`;
```

```cpp
// WebviewBridge.h — pageAboutToLoad()
// Parses URL, extracts name + value
// Calls: apvts.getParameter(paramId)->setValueNotifyingHost(value)
```

### C++ → JS (30Hz polling timer)

`PluginEditor` fires a timer at 30Hz. For each parameter, if the APVTS value changed by more than 0.001 since last sync, it calls `evaluateJavascript()`:

```cpp
// PluginEditor.cpp
evaluateJavascript("if (window.setParameterValue) { window.setParameterValue('"
                   + paramId + "', " + value + "); }");
```

```typescript
// bridge.ts — registered at startup
(window as any).setParameterValue = (id: ParameterId, value: number) => {
  params[id].set(value);  // updates Svelte store
};
```

**Constraint:** The `juce://` URL scheme only works when the plugin is loaded (not during Vite dev server). Bridge functions include a guard for `http://` context.

---

## 6. Frontend (Svelte WebUI)

### State Management

Each of the 31 parameters has its own `writable` Svelte store (normalized 0–1). Derived stores convert normalized values to display units:

```typescript
// store.ts
export const params = {
  decay: writable(0.42),
  loCutFreq: writable(0),
  // ... 29 more
};

export const loCutHz = derived(params.loCutFreq, $v => Math.round(50 + 17950 * Math.pow($v, 1 / 0.3)));
export const modeSelected = derived(params.reverbMode, $v => Math.round($v));
```

Fine-grained reactivity: updating one store only re-renders components subscribed to that store.

### UI Layout

```
┌────────────────────────────────────────────────────────┐
│ Header: "Reverbo"                              │
├──────────┬──────────┬────────────────┬────────┬───────┤
│  Input   │  Early   │   Diffusion    │ Chorus │  Out  │
│  Filter  │ Reflect. │   Network      │        │       │
│  (flex   │  (flex   │  (flex 2.5)    │(flex   │(flex  │
│   1.2)   │   1.2)   │                │  0.9)  │  0.7) │
├──────────┴──────────┴────────────────┴────────┴───────┤
│ Bottom utility row                                     │
│ Smooth · Size · Decay · Freeze · Flat · Cut            │
│ Stereo · Density · Dry/Wet                             │
└────────────────────────────────────────────────────────┘
```

**Visual design:** Neumorphic (warm beige #ede6da, inset/outset shadows), dark canvases for graphs.
**Window size:** 960×700px default, resizable 450×280 → 1800×1120.

### Key Components

**FilterGraph.svelte** — 300×110px canvas. Interactive log-frequency display (50–18kHz). Draggable handle adjusts Hi Cut frequency (horizontal) and Q (vertical). Real-time biquad magnitude computed in-component.

**NeuDiffusionNetworkGraph.svelte** — 320×130px canvas. Two draggable nodes on a spectral envelope:
- Node 1 (teal): Crossover Freq × Diffusion
- Node 2 (orange): Damping Freq × Feedback

**NeuNumber.svelte** — Spinbutton numeric input. Supports click-to-edit, arrow keys, scroll wheel, min/max clamping.

**NeuKnob.svelte** — Rotary dial. Vertical drag to adjust value.

**NeuButton.svelte** — Toggle button with active state styling.

**NeuSelector.svelte** — Discrete option selector (button group).

---

## 7. Build & Deployment

**Build:**
```bash
cd build && cmake .. && make -j$(sysctl -n hw.ncpu)
```

**Frontend build** (runs automatically via `make`, or manually):
```bash
cd WebUI && npm run build
```

**Web asset embedding:** Svelte builds to `WebUI/dist/index.html`. CMake embeds it as binary data (`juce_add_binary_data`). At runtime, `PluginEditor` unpacks the HTML to a temp directory and loads it locally in the WebBrowserComponent.

**Plugin output paths:**
- Standalone: `build/Reverbo_artefacts/Standalone/Reverbo.app`
- VST3: auto-copied to `~/Library/Audio/Plug-Ins/VST3/` after each build

---

## 8. Key Architectural Decisions

| Decision | Rationale |
|----------|-----------|
| 8-line FDN over stereo pair | Richer decorrelation and spatial diffusion |
| Hadamard feedback matrix | Energy-preserving, efficient (O(N log N)) |
| Per-line LFO modulation | Prevents metallic ringing and mode flutter |
| 4-stage Schroeder input diffusion | Spreads transient energy before FDN, reduces coloration |
| Frequency-dependent per-line decay | Realistic room behavior (highs decay faster) |
| Per-sample dry/wet interpolation | Smooth automation without zipper noise |
| 30Hz C++→JS polling (not push) | Covers DAW automation + host changes without callback complexity |
| juce:// URL scheme for JS→C++ | Decouples frontend and backend; no native JS binding needed |
| Per-parameter Svelte stores | Fine-grained reactivity, avoids full-tree re-renders |
| Neumorphic UI aesthetic | Tactile, modern look consistent with hardware plugin conventions |
