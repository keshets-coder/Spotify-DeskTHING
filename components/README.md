# Components

ESP-IDF components behind the Desk Thing's built-in Spotify Connect speaker.

| Component | What it is | Source | Licence |
|---|---|---|---|
| `desk_speaker` | Glue for the sketch: ES8311 set-up, starting the speaker, its saved login. Its header has no Spotify / bell includes, so it doesn't clash with the Arduino headers. | Spotify Desk Thing | same as this project |
| `sc_spotify` | Spotify Connect receiver | [StreamCore32](https://github.com/tobiasguyer/StreamCore32) (based on [cspot](https://github.com/feelfreelinux/cspot)) | GPL-3.0 |
| `streamcore` | `StreamBase` / `AudioControl` the receiver plugs into | StreamCore32 | GPL-3.0 |
| `bell` | HTTP(S), sockets, crypto, mDNS helpers, nanopb, civetweb, cJSON, nlohmann_json | StreamCore32's reduced copy of [bell](https://github.com/feelfreelinux/bell) | see `bell/external/*` and StreamCore32 |
| `tremor` | Integer Ogg Vorbis decoder | Xiph.org Tremor, via bell | BSD (`tremor/COPYING`) |

StreamCore32's licence is in `LICENSE-StreamCore32.md`. Firmware that includes these components is covered by the GPL-3.0 when you share it.

## Changes from StreamCore32 (October 2026)

- `bell`: the VS1053 sink is replaced by `ES8311Sink` (`bell/main/audio-sinks/esp/ES8311Sink.cpp`). It has the same interface, decodes the Ogg Vorbis stream with Tremor, mixes it to mono for the single speaker, applies the volume in software and sends 16-bit PCM over I2S (MCLK IO4, BCLK IO5, LRCK IO7, DOUT IO8).
- `streamcore/include/AudioControl.h`: picks `ES8311Sink` when `CONFIG_AUDIO_SINK_ES8311` is set. The Kconfig menu is now *Spotify Desk Thing speaker*.
- `sc_spotify/include/SpotifyStream.h`: logs in with the saved credentials once per boot (falls back to zeroconf), so the speaker is in the account's device list after a restart.
- `sc_spotify/include/SpotifyContext.h`: the developer app's client ID / secret can be set at runtime (`spotify::clientCredentials()`), from the sketch's `SPOTIFY_CLIENT_ID` / `SPOTIFY_CLIENT_SECRET`.
- `sc_spotify/generated/`: pre-generated nanopb sources, so no `protoc` is needed to build.
- mDNS is required as `espressif__mdns`.
