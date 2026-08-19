# Release Notes

## v1.0.0 (Initial Release)

**Date:** August 19, 2026

### Features
- 8-line Feedback Delay Network (FDN) reverb engine
- Frequency-dependent decay and diffusion (4-stage Schroeder input pre-diffusion)
- Early reflections with spin modulation (8 stereo taps)
- Chorus and stereo widening
- Svelte WebUI bridge for parameter control (31 parameters)
- VST3, AU, and Standalone formats

### Technical
- Built with JUCE 8.0.4 + C++17 + CMake 3.22+
- Real-time safe: no heap allocations in `processBlock()`
- All parameters smoothed with 10ms exponential `SmoothedValue` (no zipper noise)
- CPU usage: estimated 3–5% on Apple Silicon at typical settings (not formally benchmarked)
- Latency: buffer-size dependent; no additional algorithmic latency introduced

### System Requirements
- **VST3:** macOS 11+
- **AU:** macOS 10.13+
- **Standalone:** macOS 11+
- Intel or Apple Silicon

### Known Limitations
- macOS only (Windows/Linux not currently supported)
- UI requires a WebView-capable host (all major DAWs on macOS qualify)

### Download
[Download Reverbo v1.0.0](https://github.com/IAndy-10/Reverbo/releases/tag/v1.0.0)
