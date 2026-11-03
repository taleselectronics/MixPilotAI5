# MixPilot AI 5 – modular offline mixing VST3

**Source code / experimental build.** This has NOT yet been compiled on Windows or tested in Mixcraft. Requires Windows x64, Mixcraft Pro Studio 10 (or another VST3 host), and LM Studio (Qwen3.5 9B on 127.0.0.1:1235) for AI planning.

## Features
- 10 ordered effect slots; each can hold another copy of any supported effect. Empty slots are available. Select a slot and use `ADD / CHANGE MODULE`, `MOVE LEFT / RIGHT`, `REMOVE`, or `BYPASS`.
- Parametric bell EQ (20–20,000 Hz), **low-cut / high-pass**, **high-cut / low-pass** (both 12 or 24 dB/oct), compressor, broadband de-esser, saturation, gain, stereo width, echo delay, and soft limiter. Per-slot mix controls are available in the parameter model. Some controls are not all exposed at once in the compact interface.
- Combined filter response graph (accurate for EQ/cut filters in fully wet mode) and live spectrum; select an EQ or cutoff slot to drag its frequency on the graph; drag vertically to set EQ gain.
- Simplified live 6-second measurement of RMS/peak, 3 tonal bands and 9-band resonance hint. Load a WAV source or WAV reference (first 180 seconds) for **broad** tonal measurement; WAV reference uses only 3 bands, not full matching.
- Sends *measurements* and target prompt to local Qwen3.5 9B, receives a list of up to 10 effect modules, validates all numeric parameters, and applies only a valid response. AI can propose repeated effects and high/low cuts.
- Rack state stored with DAW project; unique plugin ID, separate from v4.

## Important limitations
- This is **not a full generative audio model**. Qwen does not hear waveforms directly. It receives measurements and proposes effects. Narrow resonance hint is low confidence and does not indicate a definite problem.
- Current de-esser is **broadband**, not split band. Compression, saturation and stereo width are simplified. There is no pitch correction, reverb, multiband dynamics, true-peak limiter, auto level matching, LUFS metering, or neural mastering model yet. Echo delay is a basic feedback delay, not a tempo-synced professional delay.
- Analyzer and GUI are experimental; test cautiously at low volume. Audio module changes can cause audible clicks; do not change module types during critical recording.
- Parametric filters recalculate periodically; effect parameter changes may click, especially at high gain. Designed as a functional prototype, not production-grade DSP.
- Plugin source package alone is not installable. Build with GitHub Actions; the successful workflow artifact contains the VST3 bundle.

## How to build and install
1. Create a new GitHub repository `MixPilotAI5`. Upload **the contents** of this project, including `CMakeLists.txt`, `Source`, and `.github/workflows/windows-vst3.yml`. Workflow must be rooted in the repository `.github/workflows/` directory.
2. Open **Actions** and run **Build MixPilot AI 5 - Windows VST3**. If the source was uploaded in a nested folder, the workflow searches for `CMakeLists.txt`.
3. After a successful build, open **Summary → Artifacts** and download **MixPilot-AI5-Windows-VST3**. Extract it, then copy the complete `MixPilot AI 5.vst3` bundle to `C:\Program Files\Common Files\VST3`.
4. Restart Mixcraft and rescan. Load **MixPilot AI 5**, leaving MixPilot AI 4 intact.
5. Keep LM Studio local API running at `http://localhost:1235/v1`, model ID `qwen/qwen3.5-9b` available. Analyze a live track or load source WAV, then run AI Generate.

## Licenses
Source copyright held by its author. JUCE and other dependencies have their own license conditions; check JUCE's current licensing before distribution, particularly commercial release. Ozone screenshot was used for **general visual inspiration only**; no Ozone art, assets or code have been copied.
