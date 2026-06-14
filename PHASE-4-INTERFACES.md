# Phase 4 Internal Interface Contract (for swarm)

> **Audience:** AdvDeck Agent swarm agents working on Phase 4. Read this FIRST.
> **Purpose:** Define the shared C++ + Python surface that the swarm depends on. Get it right once; let the rest of the swarm move fast.
> **Status:** Authoritative for `phase-4-voice-capture` branch. If something here looks wrong, do NOT silently change it — flag in your task notes.

## 1. What Phase 4 means

From `roadmap/advdeck-agent-plan.md` §First Build Target: voice comes **after** the text-to-plan loop is solid. Phase 4 ships:

- A09: real ES8311 audio capture on the Cardputer-Adv, WAV/PCM writer to SD, a recorder route in the firmware
- A10: a transcription adapter in the bridge, transcript.md writer, transcript review flow on the firmware
- The deferred Phase 1/2/3 piece: **real `SdStorage` impl** against `<SD.h>` (the Phase 1 stub returns errors and is the last blocker before the firmware actually does anything with the SD card)

Validation (per `roadmap/advdeck-agent-swarm-tasks.md`):
- A09: records 15s to SD; tests 15/60/180s; documents max stable length
- A10: bridge produces `transcript.md` from a WAV; provider failures recoverable; firmware shows transcript before planning

## 2. SD layout changes in Phase 4

```
/advdeck/
  config.json
  inbox/
  projects/<slug>/
    idea.md
    transcript.md                  # Phase 4 NEW: the user-facing transcript (Phase 1 scaffold had this as a stub)
    voice/                         # Phase 4 NEW
      recording-<n>.wav            # raw 16-bit PCM WAV, mono, 16 kHz (matches M5Cardputer Mic)
      manifest.json                # per-recording metadata: duration, sample_rate, captured_at, sha256
    brief.md                       # from bridge (Phase 3 unchanged)
    plan.md
    tasks.json
    tasks.md
    calendar-suggestions.json
    agent-prompt.md
    export/
      ...
  calendar/
  outbox/
    pending.jsonl
    results/<id>/...
    staging/<id>/...
    rejected/<id>/...
  logs/
```

**Critical:** `idea.md` is still never modified by the bridge. `transcript.md` is also write-once (set when the user accepts the transcript in the review flow). The recorder writes WAVs to `voice/`; the bridge does NOT read those directly — it reads the manifest, and the user manually triggers a transcription (Phase 4 keeps transcription explicit, not auto).

## 3. Schemas (one new schema for Phase 4)

### 3.1 `recording-manifest.schema.json` (new)
Lives at `bridge/advdeck-agent-bridge/schemas/recording-manifest.schema.json` and (byte-identical copy) at `projects/advdeck-agent/schemas/recording-manifest.schema.json`.
```json
{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "title": "RecordingManifest",
  "type": "object",
  "required": ["version", "file", "duration_ms", "sample_rate", "captured_at", "sha256"],
  "properties": {
    "version":      { "type": "number", "const": 1 },
    "file":         { "type": "string", "pattern": "^recording-[0-9]+.wav$" },
    "duration_ms":  { "type": "number", "minimum": 0 },
    "sample_rate":  { "type": "number", "enum": [8000, 11025, 16000, 22050, 44100, 48000] },
    "channels":     { "type": "number", "enum": [1, 2] },
    "bits_per_sample": { "type": "number", "enum": [8, 16] },
    "captured_at":  { "type": "string", "format": "date-time" },
    "sha256":       { "type": "string", "pattern": "^[0-9a-f]{64}$" }
  },
  "additionalProperties": false
}
```

## 4. C++ interface surface

All headers in `projects/advdeck-agent/include/advdeck/`. Namespace `advdeck`. Pure C++17. No Arduino headers in services. Host tests in `test/host/` use the existing `IStorage` so we can drive the whole flow with a temp dir.

### 4.1 `sd_storage.{h,cpp}` (real impl — replaces the Phase 1 stub)
The Phase 1 `SdStorage` returned errors from every method. Phase 4 implements the real one. The public API stays exactly the same; only the implementation changes. Behind `#ifdef ADVDECK_FIRMWARE`:

- `SdStorage` holds an `SdFat` or `<SD.h>` handle (M5Cardputer uses the Arduino `<SD.h>` from the SD library; the pinout is the standard SPI per `boards/pinouts/m5stack-cardputer.h` — SDCARD_CS=12, SDCARD_SCK=40, SDCARD_MISO=39, SDCARD_MOSI=14).
- `mount()` calls `SD.begin(SDCARD_CS, SPI, ...)` with the right SPI clock divider for the Cardputer-Adv (40 MHz max; default 20 MHz is safe). Returns true on success.
- `is_mounted()` returns the cached mount state.
- `exists(path)`, `read_file(path)`, `read_file_or(path, fallback)`, `write_file(path, data)`, `list_dir(path)`, `ensure_dir(path)`, `join(a, b)`, `root()` — all real, on the real SD card.
- On the host build (no `ADVDECK_FIRMWARE`), `SdStorage` is a no-op stub that returns the same errors as Phase 1. This keeps the existing host tests working unchanged.

### 4.2 `wav_writer.h` (new)
```cpp
namespace advdeck {

struct WavSpec {
  uint32_t sample_rate = 16000;
  uint16_t channels = 1;        // 1 = mono
  uint16_t bits_per_sample = 16;
};

class WavWriter {
 public:
  // Create a writer for `path` with the given spec. Writes the WAV
  // header on construction. Returns "" on success, error on failure.
  static std::unique_ptr<WavWriter> create(
      IStorage& storage, const std::string& path, const WavSpec& spec,
      std::string* err);

  virtual ~WavWriter() = default;

  // Append a buffer of 16-bit PCM samples. Returns "" on success.
  virtual std::string write_samples(const int16_t* samples, size_t n) = 0;

  // Finalize: write the data size fields in the WAV header. Idempotent.
  // Must be called before the file is consumed.
  virtual std::string finalize(std::string* err) = 0;

  // Returns the absolute on-storage path of the WAV file.
  virtual std::string path() const = 0;

  // Returns the number of bytes written to the data section so far
  // (excluding the 44-byte header).
  virtual size_t bytes_written() const = 0;
};

}  // namespace advdeck
```

The WAV format is the standard 16-bit PCM:
- `RIFF<size>WAVEfmt ` chunk: 16-byte format chunk with PCM format (1), channels, sample rate, byte rate, block align, bits per sample
- `data<size>` chunk: 4-byte size + the samples

WavWriter on host uses `HostStorage`. On firmware uses `SdStorage`. Same `IStorage` interface. **Host-testable end-to-end** — the firmware build reuses the same .o.

### 4.3 `recorder.h` (new)
```cpp
namespace advdeck {

struct RecordingMetadata {
  std::string file_name;       // "recording-1.wav"
  std::string captured_at;     // ISO8601
  uint32_t duration_ms = 0;
  uint32_t sample_rate = 16000;
  uint16_t channels = 1;
  uint16_t bits_per_sample = 16;
  std::string sha256;          // hex
};

class Recorder {
 public:
  // Construct a recorder that will write to
  // <storage_root>/projects/<slug>/voice/recording-<n>.wav. The next
  // available n is computed by listing the voice/ dir. The manifest
  // is written to voice/recording-<n>.manifest.json on finalize.
  Recorder(IStorage& storage, std::string storage_root, std::string project_slug);

  // Begin recording. The mic is platform-specific; on host this is
  // a no-op (recorder is exercised via the host test framework).
  // Returns "" on success.
  std::string start(std::string* err);

  // Append samples. The Recorder buffers in memory and periodically
  // flushes to the WAV file.
  std::string append_samples(const int16_t* samples, size_t n, std::string* err);

  // Stop, finalize, write the manifest. Returns the metadata on
  // success. err is populated with "no active recording" if start
  // was not called.
  std::string stop(RecordingMetadata* out, std::string* err);

  // The recorder is also stateful: a host test can call start(),
  // append_samples() with a known buffer, then stop() and assert
  // the resulting WAV file is byte-correct.
  bool is_recording() const;

  // Total bytes written to the WAV data section so far.
  size_t bytes_written() const;

 private:
  // Pimpl with the WavWriter, the SHA-256 hasher, and the start time.
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace advdeck
```

### 4.4 `Ctx` extension
No new field on `Ctx`. The recorder is per-route. `route_recorder_impl` constructs a `Recorder(ctx.storage, ctx.storage_root, slug)`.

## 5. Bridge additions (transcription)

### 5.1 New schema
- `bridge/advdeck-agent-bridge/schemas/recording-manifest.schema.json` per §3.1
- Vendored to `projects/advdeck-agent/schemas/`
- Added to `scripts/build_schema_embed.py` SCHEMA_FILES list; regenerate `schema_embed.h`

### 5.2 Provider protocol
The existing `Provider` protocol in `providers/__init__.py` is the seam. New `TranscriptionProvider` protocol lives next to it:
```python
# bridge/advdeck-agent-bridge/src/advdeck_bridge/providers/__init__.py
class TranscriptionProvider(Protocol):
    def name(self) -> str: ...
    def transcribe(self, wav_path: Path, *, language: str = "en") -> str:
        """Returns the transcript as a single string. May include
        a leading header line like 'DURATION: 1.5s\\nWORDS: 240\\n'."""
        ...
```

Implementations:
- `providers/transcription/mock.py` — `MockTranscriptionProvider` returns canned transcripts. Used by Z04 and end-to-end tests.
- `providers/transcription/local_whisper.py` — `LocalWhisperProvider` calls a local `whisper-cli` binary (assumed at `~/.local/bin/whisper-cli` or `$WHISPER_CLI`). The provider spawns the binary as a subprocess and parses stdout for the transcript text. The protocol method is `transcribe()`. Failure modes raise `ProviderRetryable` (subprocess timeout, missing binary) or `ProviderUnrecoverable` (corrupt output). **No `openai` cloud transcription in Phase 4** — that would be a separate task.
- `providers/transcription/openai.py` — stub. Disabled unless `OPENAI_API_KEY` is set; raises `ProviderUnavailable` otherwise. Not used in Phase 4 tests; included as the seam for Phase 5.

`get_transcription_provider(name, **kwargs)` factory mirrors `get_provider`.

### 5.3 New CLI subcommands
- `advdeck-bridge transcribe --wav <path> --out <dir> [--language en] [--provider mock|local-whisper|openai]` — transcribes a single WAV and writes `transcript.md` to `<out>/transcript.md`. Doesn't touch the project folder.
- `advdeck-bridge run-once-transcribe [--storage-root <path>]` — picks the first pending `transcribe` request from the outbox, runs the provider, writes the result. Phase 4 doesn't add a `transcribe` request type to `pending.jsonl` (that's Phase 4.5 polish). For now, this subcommand is invoked manually with `--wav`.
- `advdeck-bridge transcribe-and-plan` (Phase 4 deliverable that closes the voice-to-plan loop) — combines the two: transcribe the WAV, write the transcript, then run the existing `plan` flow with `transcript.md` as the project idea text. Writes the standard result manifest. Returns the request id.

## 6. UI surface

### 6.1 New home menu entry "Record"
- Pressing 'r' from the home menu (NOT from the project list — direct record is the simpler flow for Phase 4) goes to `route_record_list_impl`.
- The screen lists existing recordings in the current project's `voice/` dir with timestamp + duration.
- 'n' starts a new recording (calls `route_recorder_impl`).
- 'e' shows the existing transcript.md (read-only) if one exists.
- 't' transcribes the most recent recording: writes a "transcribe" request to the outbox (Phase 4 keeps it on-device only — the user manually runs the bridge). For now, the 't' key just lists the recordings and exits; the actual transcription is the user's next step (run the bridge CLI on a desktop). Document this in the screen.
- 'Esc' returns home.

### 6.2 New screen `route_recorder_impl` (D1.2)
- Shows elapsed time (mm:ss), audio level meter (filled bar 0-100% of recent chunk peak), current duration target (default 30s, configurable by '1'/'2'/'3' keys to set 15s/30s/60s).
- 'r' starts. Once started: 'r' again stops. 'Esc' cancels (deletes the partial file).
- On stop: writes the manifest, returns to the record list.

### 6.3 Transcript review flow
A new `route_transcript_review_impl(ctx, project_slug)` — reuses the `route_review_impl` shape (e-render-reviews, e/t/c/a read-only views). Triggered from the project detail screen when the user presses 'p' to view the transcript. The on-device review is the same as the agent pack review from Phase 3.

## 7. Build commands agents must verify

From `projects/advdeck-agent/`:
```bash
/home/pi/.platformio/penv/bin/platformio run
make -C test/host test
```

From `bridge/advdeck-agent-bridge/`:
```bash
.venv/bin/python -m pytest tests/ -v
```

End-to-end (Z04):
```bash
./projects/advdeck-agent/test/host/verify.sh
```

## 8. Coding rules

Same as Phase 1-3: C++17, no Arduino headers in services, no exceptions in firmware, Python 3.11+ for the bridge, type hints on all public functions, all tests under the existing test frameworks.

## 9. What's NOT in Phase 4

- A12 UX polish (240x135 UX). Defer to Phase 4.5.
- Real LLM transcription (the local whisper is a thin wrapper; no GPT-4o-transcribe in Phase 4)
- Auto-transcription on stop. The user still manually runs the bridge.
- Auto-bridge-routing from the firmware. The firmware queues a `transcribe` request, but does not call out to a desktop.
- C5 companion integration (Phase 7)
- `.ics` calendar export (Phase 5)
- Real audio playback (the Cardputer-Adv speaker can play back WAV, but A09 is recording only; playback is A09.5)
