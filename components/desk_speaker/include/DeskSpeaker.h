#pragma once
// Spotify Desk Thing speaker: makes the board a Spotify Connect speaker that
// plays through the ES8311 codec and the speaker plugged into the board.
//
// Unofficial: it uses a reverse-engineered Spotify Connect implementation
// (StreamCore32 / cspot) and needs Spotify Premium.

#include <stdint.h>
#include <functional>

namespace desk_speaker {

/** Writes one ES8311 register over I2C; returns true on success. */
using RegWrite = std::function<bool(uint8_t reg, uint8_t value)>;

/** Sets up the ES8311 codec (I2C address 0x18) for 16-bit I2S playback with
 *  MCLK = 256 x sample rate, and switches the FM8002E amplifier on.
 *  ampEnablePin: the amplifier's shutdown pin (IO1 on the ES3C28P, low = on). */
bool initCodec(const RegWrite& write, int ampEnablePin = 1, bool ampActiveLow = true);

struct Config {
  const char* name = "Desk Thing";  // shown in Spotify's device list
  const char* clientId = "";        // Spotify developer app
  const char* clientSecret = "";
};

/** Starts the speaker. Call once, after Wi-Fi is up and mDNS is running. */
bool start(const Config& cfg);

bool started();
/** Logged in to Spotify, so the speaker is in the account's device list. */
bool connected();
/** Spotify is playing (or paused) on this speaker. */
bool active();
const char* name();

/** Forgets the saved Spotify login of the speaker (used on log out). */
void forgetLogin();

}  // namespace desk_speaker
