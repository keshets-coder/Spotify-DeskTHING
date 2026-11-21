# Spotify Desk Thing

A small DIY desk controller for Spotify, built on an ESP32-S3 with a 2.8" touchscreen and a volume knob, in a 3D-printed case. It shows what's playing on your Spotify account and lets you play, pause, skip, shuffle, browse your playlists and recently played, and change the volume from your desk.

It controls whatever device Spotify is already playing on (your phone, computer or a speaker). It is also a **Spotify Connect speaker** itself: plug a small speaker into the board and it plays music on its own, with no computer or phone playing the audio.

> This is an unofficial fan project and is not made or supported by Spotify. The built-in speaker uses an unofficial, reverse-engineered Spotify Connect player ([StreamCore32](https://github.com/tobiasguyer/StreamCore32), based on [cspot](https://github.com/feelfreelinux/cspot)). Spotify can change things on their side and break it at any time.

---

## What you need

**Hardware**

- **ESP32-S3 2.8" touchscreen board:** LCDWIKI ES3C28P (320×240, ILI9341 display, FT6336 touch, 16 MB flash, 8 MB PSRAM).
- **EC11 rotary encoder module:** for volume.
- **Small speaker, 8 Ω up to 1.5 W (or 4 Ω up to 2 W)** with a 1.25 mm 2-pin plug, for the built-in speaker. The board has the amplifier and a 2-pin speaker socket but no speaker.
- **USB-C cable**
- **Fasteners:**
  - 3 × M3×16 and 1 × M3×14 screws for the case.
  - 4 × M2×6 screws for the encoder.
- **Printed parts:** front face, text inlay, back face, stand and knob (see the STL files).

**Accounts**

- **Spotify Premium.** Spotify only allows playback control, and Spotify Connect speakers, with Premium.
- **A free Spotify Developer account:** [developer.spotify.com](https://developer.spotify.com/dashboard)

**Software**

The code is still an Arduino sketch (`main/SpotifyCarThing.cpp`), but it is built with Espressif's **ESP-IDF** instead of the Arduino IDE, with Arduino running inside it ("Arduino as a component"). The Spotify Connect player needs chip settings the Arduino IDE can't change.

- **ESP-IDF 5.5** ([install guide](https://docs.espressif.com/projects/esp-idf/en/v5.5.1/esp32s3/get-started/index.html)). The easiest way is the **ESP-IDF extension for VS Code**: install it, run *ESP-IDF: Configure ESP-IDF Extension* and pick version **v5.5.1**. On the command line, follow the guide for your system and use `idf.py`.
- Nothing else. The Arduino core (3.3.4), LovyanGFX and ArduinoJson are downloaded automatically on the first build.

---

## 1. Wire the encoder

Use the board's 4-pin **UART** socket (1.25mm, the one labelled UART). Nothing else on the board uses it, because the serial monitor runs over USB.

| Encoder pin | Board pin (UART socket) |
|---|---|
| CLK (A) | IO43 (TX) |
| DT (B) | IO44 (RX) |
| GND | GND |

The encoder needs no power wire: the code uses the chip's internal pull-ups. Leave the socket's VCC pin unconnected.

The push switch (SW) isn't used. The UART socket has no spare pin for it.

If CLK and DT end up swapped, nothing breaks: the volume just turns the wrong way, which `ENC_REVERSE` below fixes.

**Wire straight to the encoder's own legs, not the module header.** On some modules, the resistors on the header stop the board reading the knob.

If the volume goes the wrong way when you turn the knob, change `#define ENC_REVERSE false` to `true` in the code.

## 1b. Plug in the speaker

Plug the speaker into the 2-pin speaker socket on the board. The board's ES8311 audio chip and FM8002E amplifier drive it; no other wiring is needed. Plug the connector in straight, not at an angle.

---

## 2. Create your Spotify app

1. Go to the [Spotify Developer Dashboard](https://developer.spotify.com/dashboard) and click **Create app**.
2. Give it any name and description.
3. Under **Redirect URIs**, add exactly:
   ```
   http://127.0.0.1:8888/callback
   ```
4. Tick **Web API** and save.
5. Open the app's **Settings** and copy the **Client ID** and **Client secret**.
6. If someone other than the app's owner will log in on the device, add their Spotify email under **User Management**. New apps allow up to 5 users.

---

## 3. Put your details in the code

Download or clone this repository. The parts you need to know:

```
Spotify-DeskTHING/
├── main/SpotifyCarThing.cpp   the Desk Thing code (your settings are at the top)
├── main/InterFonts.h
├── components/                the Spotify Connect speaker and audio driver
├── sdkconfig.defaults         chip settings (PSRAM, USB serial, memory)
└── partitions.csv
```

Open `main/SpotifyCarThing.cpp` and fill in the only two lines you must change:

```cpp
#define SPOTIFY_CLIENT_ID     "your-spotify-client-id"
#define SPOTIFY_CLIENT_SECRET "your-spotify-client-secret"
```

**Optional: time zone.** The clock is set to New Zealand time. To change it, replace `TZ_INFO` with your zone's POSIX string, for example:

| Region | `TZ_INFO` |
|---|---|
| New Zealand | `NZST-12NZDT,M9.5.0,M4.1.0/3` |
| Sydney | `AEST-10AEDT,M10.1.0,M4.1.0/3` |
| London | `GMT0BST,M3.5.0/1,M10.5.0` |
| New York | `EST5EDT,M3.2.0,M11.1.0` |
| Los Angeles | `PST8PDT,M3.2.0,M11.1.0` |

**Optional: speaker.** The speaker shows up in Spotify as **Desk Thing**. To rename it, change `DESK_SPEAKER_NAME`. To turn the built-in speaker off and use the Desk Thing only as a remote, set `#define DESK_SPEAKER false`.

You don't need to enter Wi-Fi in the code. You pick your network on the device.

---

## 4. Build and upload

Connect the board with USB-C.

**VS Code (ESP-IDF extension):** open the repository folder, then in the status bar at the bottom:

1. Set the target to **esp32s3** (the chip icon). If it asks, pick *ESP32-S3 chip (via builtin USB-JTAG)*.
2. Pick the board's serial port (the plug icon).
3. Click **Build, Flash and Monitor** (the flame icon). The first build downloads the Arduino core and libraries and takes several minutes.

**Command line:** in an ESP-IDF terminal, in the repository folder:

```sh
idf.py set-target esp32s3
idf.py -p <port> flash monitor
```

`<port>` is something like `COM5` on Windows or `/dev/ttyACM0` on Linux. Press **Ctrl+]** to leave the monitor.

All the chip settings the Arduino IDE had under **Tools** (16 MB flash, OPI PSRAM, USB CDC On Boot, 240 MHz) are already in `sdkconfig.defaults`. If you change `sdkconfig.defaults`, delete the generated `sdkconfig` file so the change is picked up.

- **If the upload won't start:** hold **BOOT**, tap **RST**, release **BOOT**, then upload again. Press **RST** when it finishes.
- **To watch what's happening:** the monitor (or any serial monitor at **115200** baud) shows the log over USB.

---

## 5. First start

1. **Wi-Fi.** The device looks for networks and shows a list. Tap yours, type the password on the on-screen keyboard and tap **Join**. It remembers the network from then on.
2. **Log in to Spotify.**
   1. The screen shows a QR code and an address such as `http://192.168.1.50`. You can also use **http://carthing.local**.
   2. Open that address on a phone or computer on the same Wi-Fi.
   3. On that page, tap **Log in with Spotify** and agree.
   4. Spotify then opens a page that won't load. That's expected. Copy the **whole address** from the browser's address bar.
   5. Paste it into the box on the device's page and tap **Connect**.
3. **Done.** Start playing something on any device signed in to your Spotify account, and it appears on the Desk Thing within a couple of seconds.
4. **Speaker (once).** Open the Spotify app on a phone or computer **on the same Wi-Fi**, tap the devices icon and pick **Desk Thing**. Music now plays from the board's speaker. The Desk Thing saves this login, so after a restart the speaker is ready without the app.

The login is saved, so you only do this once.

---

## Using it

**Screens**

- **Now Playing:**
  - Album art, song, artist and progress bar.
  - Shuffle, previous, play/pause, next and repeat buttons.
  - Drag the progress bar to seek.
  - Swipe left or right on the art to skip.
- **Library:**
  - **Recently played:** the last six things you listened to.
  - **Your Library:** all your playlists, with Liked Songs at the top.
  - The mini player at the bottom takes you back to Now Playing.
- **Playlist:** tap any song to play it, or use the green play and shuffle buttons.
- **Standby:** a large clock with the date and current song. Tap anywhere to wake it.

**Top bar**

- **Library icon or back arrow:** go to the Library, or back.
- **Clock:** tap to switch between 12-hour and 24-hour time.
- **Wi-Fi icon:** tap to change network.
- **Moon:** go to Standby now.

**Gestures**

- **Swipe down from the top edge:** Settings.
- **Swipe up from the bottom edge:** Account info.
- **Swipe in from the left edge:** back.

**Speaker**

- From then on you can start music on the Desk Thing itself: open the Library and play a playlist. When nothing else is playing, the Desk Thing plays it on its own speaker.
- **Account screen (swipe up):** the **Speaker** row shows the speaker's state. Tap it to move whatever is playing (on your phone, for example) to the Desk Thing's speaker.
- The volume knob, play, pause, skip and seek work the same as on any other device.
- To play on another device again, pick it in the Spotify app.

**Knob**

- Turn it to change the volume. A volume bar drops down from the top: green up to 60%, orange up to 80%, red above that.

**Settings**

- **Theme:** dark or light.
- **Brightness:** slider.
- **Sleep:** 1, 5, 10 or 20 minutes, or never.
- **Screensaver:** on or off, with four styles:
  - Standby
  - White
  - Green
  - Pink Propeller
- **Wi-Fi:** change network.
- **Log out:** tap twice to confirm.

**Sleep**

- With the screensaver **on**, the device shows the screensaver after the sleep time.
- With it **off**, the screen turns off.
- A tap, a turn of the knob, or a song starting on Spotify wakes it.

---

## Troubleshooting

| Problem | Fix |
|---|---|
| Screen stays black after upload | The monitor shows `PSRAM allocation FAILED` if PSRAM is off. Delete `sdkconfig` and build again so the settings from `sdkconfig.defaults` are used. |
| Screen shows stripes, glitches or wrong colours | Change `#define SPI_WRITE_MHZ 80` to `40` and upload again. |
| "Spotify only shares songs from playlists you made or collaborate on" | Spotify only lets apps like this one read songs from playlists you own or collaborate on. Playlists you just follow show this message. |
| "Spotify Premium is needed for playback control" | Playback control needs Premium. |
| "No Spotify device yet" / "No Spotify device found" | Pick **Desk Thing** once in the Spotify app's device list (phone or computer on the same Wi-Fi), or play something on another device. |
| "Desk Thing" isn't in the Spotify app's device list | The phone or computer must be on the same Wi-Fi network, and the network must allow mDNS (some guest and office networks block it). Check the monitor for `desk_speaker` lines. |
| Speaker row says "No audio chip found" | The ES8311 didn't answer on I2C. Check the monitor for `[spk]` lines. |
| No sound, but Spotify shows it playing on Desk Thing | Check the speaker plug, and the volume in Spotify. The monitor shows `ES8311_SINK` lines when a song starts. |
| Sound stutters | Weak Wi-Fi. Move closer to the router. |
| Recently played asks for permission | Log out (in Settings) and log in again, so the new permission is granted. |
| Nothing shows up after logging in | Check the Client ID and secret, that the Redirect URI matches exactly, and that your account is listed under **User Management** if you're not the app owner. |
| Knob does nothing | Check it's wired to IO43/IO44 on the UART socket, from the encoder's legs. Watch the monitor for `[enc]` lines while turning it. |
| "Reconnecting…" in the top bar | Wi-Fi or Spotify is briefly unreachable. It recovers by itself. |
| Can't open carthing.local | Use the IP address shown on the device's screen instead. Some networks block `.local` names. |

**Starting fresh:** to wipe the saved Wi-Fi, logins and settings, run `idf.py -p <port> erase-flash`, then flash again.

---

## Credits

Designed and built by **Pink Propeller**. Interface text uses the [Inter](https://rsms.me/inter/) typeface (SIL Open Font License).

The built-in speaker uses code from [StreamCore32](https://github.com/tobiasguyer/StreamCore32) by tobiasguyer (GPL-3.0, see `components/LICENSE-StreamCore32.md`), which is based on [cspot](https://github.com/feelfreelinux/cspot) and [bell](https://github.com/feelfreelinux/bell), and Xiph.org's [Tremor](https://gitlab.xiph.org/xiph/tremor) Vorbis decoder (BSD). See `components/README.md`.
