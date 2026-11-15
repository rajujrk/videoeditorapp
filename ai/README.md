# AI services

Built (Phases 3 and 4). The code lives with the engine and the player rather than here:

- **On the Mac** — `engine/speech/` (captions with whisper.cpp; speech cleanup with DeepFilterNet3 on
  ONNX Runtime), `engine/analysis/` (pauses, scenes, beats, motion, tracking, faces and people with
  Apple Vision, depth with Core ML) and `engine/encode/VideoProcessing.mm` (upscaling, smooth slow
  motion, object removal). Models are downloaded on request and checked by
  `ui/player/SpeechModels.cpp`.
- **With your own keys** — `ui/player/cloud/` (Anthropic, OpenAI, Google and ElevenLabs: writing,
  voices, generation; keys in the Keychain), `ui/player/Copilot.cpp` (edits proposed in plain words)
  and `ui/player/VoiceCloning.cpp` (a copied voice, only with recorded consent).

This folder only points there.
