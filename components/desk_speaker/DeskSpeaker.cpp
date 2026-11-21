#include "DeskSpeaker.h"

#include <memory>
#include <string>

#include "AudioControl.h"
#include "BellLogger.h"
#include "SpotifyContext.h"
#include "SpotifyStream.h"
#include "StreamCoreFile.h"
#include "ZeroConf.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_memory_utils.h"
#include "freertos/semphr.h"
#include <functional>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mdns.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char* TAG = "desk_speaker";

// StreamCore32's logger can forward lines to its web UI; the Desk Thing has
// none, so it only prints to the console.
std::function<bool(const std::string&)> WsSendJsonSCLogger = nullptr;
std::function<bool(const std::string&)> EINK_LOG_CALLBACK = nullptr;

namespace {

// Flash writes (and reads) switch the cache off, which is only allowed from a
// task whose stack is in internal RAM. StreamCore32 runs its Spotify tasks with
// PSRAM stacks, so NVS calls from them run here on a short-lived helper task.
struct NvsJob {
  std::function<int()> fn;
  int result;
  SemaphoreHandle_t done;
};

static void nvsJobTask(void* arg) {
  auto* job = static_cast<NvsJob*>(arg);
  job->result = job->fn();
  xSemaphoreGive(job->done);
  vTaskDelete(nullptr);
}

static int onInternalStack(std::function<int()> fn) {
  int probe = 0;
  if (!esp_ptr_external_ram(&probe))
    return fn();
  NvsJob job{std::move(fn), -1, xSemaphoreCreateBinary()};
  if (!job.done)
    return -1;
  // xTaskCreate allocates the stack from internal RAM.
  if (xTaskCreatePinnedToCore(nvsJobTask, "spk_nvs", 4096, &job, 5, nullptr,
                              tskNO_AFFINITY) != pdPASS) {
    vSemaphoreDelete(job.done);
    return -1;
  }
  xSemaphoreTake(job.done, portMAX_DELAY);
  vSemaphoreDelete(job.done);
  return job.result;
}

// ------------------------------------------------- saved Spotify login (NVS)
// One record (the current user) is enough for the Desk Thing.
class NvsLoginStore : public StreamCoreFile {
 public:
  explicit NvsLoginStore(const char* ns) : ns_(ns) {}

  int save(const Record& r, bool overwrite = true) override {
    return onInternalStack([&] { return saveNow(r); });
  }
  int erase(const std::string&) override {
    return onInternalStack([&] { return eraseNow(); });
  }
  int get_current(Record* out) override {
    return onInternalStack([&] { return getCurrentNow(out); });
  }

  int saveNow(const Record& r) {
    nvs_handle_t h;
    if (nvs_open(ns_, NVS_READWRITE, &h) != ESP_OK)
      return -1;
    std::vector<uint8_t> blob;
    for (auto& f : r.fields) {
      if (f.name == "authType" && !f.value.empty())
        nvs_set_u8(h, "authType", f.value[0]);
      if (f.name == "authData")
        blob = f.value;
    }
    nvs_set_str(h, "user", r.userkey.c_str());
    esp_err_t e = nvs_set_blob(h, "authData", blob.data(), blob.size());
    nvs_commit(h);
    nvs_close(h);
    return e == ESP_OK ? 0 : -1;
  }
  int load(const std::string& userkey, Record* out) override {
    if (get_current(out) != 0 || out->userkey != userkey)
      return -1;
    return 0;
  }
  int list(std::vector<std::string>& keys) override {
    Record r;
    if (get_current(&r) == 0)
      keys.push_back(r.userkey);
    return 0;
  }
  int exists(const std::string& userkey) override {
    Record r;
    return get_current(&r) == 0 && r.userkey == userkey ? 1 : 0;
  }
  int eraseNow() {
    nvs_handle_t h;
    if (nvs_open(ns_, NVS_READWRITE, &h) != ESP_OK)
      return -1;
    nvs_erase_all(h);
    nvs_commit(h);
    nvs_close(h);
    return 0;
  }
  int set_current(const std::string&) override { return 0; }
  int getCurrentNow(Record* out) {
    nvs_handle_t h;
    if (nvs_open(ns_, NVS_READONLY, &h) != ESP_OK)
      return -1;
    size_t ulen = 0, blen = 0;
    int rc = -1;
    if (nvs_get_str(h, "user", nullptr, &ulen) == ESP_OK && ulen > 1 &&
        nvs_get_blob(h, "authData", nullptr, &blen) == ESP_OK && blen > 0) {
      std::string user(ulen, '\0');
      std::vector<uint8_t> data(blen);
      nvs_get_str(h, "user", user.data(), &ulen);
      nvs_get_blob(h, "authData", data.data(), &blen);
      user.resize(strlen(user.c_str()));
      uint8_t type = 0;
      nvs_get_u8(h, "authType", &type);
      out->userkey = user;
      out->fields.clear();
      out->fields.push_back(Field{"authType", std::vector<uint8_t>{type}});
      out->fields.push_back(Field{"authData", data});
      rc = 0;
    }
    nvs_close(h);
    return rc;
  }

 private:
  const char* ns_;
};

std::shared_ptr<AudioControl> g_audio;
std::shared_ptr<SpotifyStream> g_spotify;
std::string g_name = "Desk Thing";
volatile bool g_connected = false;

}  // namespace

namespace desk_speaker {

bool initCodec(const RegWrite& w, int ampPin, bool ampActiveLow) {
  struct R {
    uint8_t reg, val;
    uint16_t delayMs;
  };
  // ES8311 as I2S slave, MCLK from the MCLK pin at 256 x fs, 16-bit Philips
  // I2S, DAC only (based on Espressif's esp_codec_dev ES8311 driver).
  static const R seq[] = {
      {0x00, 0x1F, 20},  // reset
      {0x00, 0x00, 0},
      {0x45, 0x00, 0},
      {0x01, 0x30, 0},  // clock manager: MCLK / BCLK on
      {0x02, 0x00, 0},  // pre-divider 1, multiplier 1
      {0x03, 0x10, 0},  // ADC fs mode single speed, OSR 16
      {0x16, 0x24, 0},
      {0x04, 0x10, 0},  // DAC OSR 16
      {0x05, 0x00, 0},  // ADC / DAC clock dividers 1
      {0x06, 0x03, 0},  // BCLK divider 4
      {0x07, 0x00, 0},  // LRCK divider 256
      {0x08, 0xFF, 0},
      {0x0B, 0x00, 0},
      {0x0C, 0x00, 0},
      {0x10, 0x1F, 0},
      {0x11, 0x7F, 0},
      {0x00, 0x80, 0},  // power on, slave mode
      {0x01, 0x3F, 0},  // all clocks on, MCLK from the pin
      {0x13, 0x10, 0},
      {0x1B, 0x0A, 0},
      {0x1C, 0x6A, 0},
      {0x44, 0x08, 0},
      {0x09, 0x0C, 0},  // DAC serial port: I2S, 16 bit
      {0x0A, 0x0C, 0},  // ADC serial port: I2S, 16 bit
      {0x17, 0xBF, 0},
      {0x0E, 0x02, 0},  // power up the analog circuits
      {0x12, 0x00, 0},  // power up the DAC
      {0x14, 0x1A, 0},
      {0x0D, 0x01, 0},  // power up
      {0x15, 0x40, 0},
      {0x37, 0x08, 0},  // DAC equalizer bypassed
      {0x45, 0x00, 0},
      {0x32, 0xBF, 0},  // DAC volume 0 dB (the player scales in software)
      {0x31, 0x00, 0},  // DAC unmuted
  };
  bool ok = true;
  for (auto& r : seq) {
    if (!w(r.reg, r.val)) {
      ESP_LOGE(TAG, "ES8311 write 0x%02x failed", r.reg);
      ok = false;
      break;
    }
    if (r.delayMs)
      vTaskDelay(pdMS_TO_TICKS(r.delayMs));
  }
  if (ampPin >= 0) {
    gpio_reset_pin((gpio_num_t)ampPin);
    gpio_set_direction((gpio_num_t)ampPin, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t)ampPin, ok ? (ampActiveLow ? 0 : 1) : (ampActiveLow ? 1 : 0));
  }
  ESP_LOGI(TAG, "ES8311 %s", ok ? "ready" : "not found");
  return ok;
}

bool start(const Config& cfg) {
  if (g_spotify)
    return true;
  if (cfg.name && cfg.name[0])
    g_name = cfg.name;
  bell::setDefaultLogger();
  if (cfg.clientId && cfg.clientId[0])
    spotify::clientCredentials() = {cfg.clientId, cfg.clientSecret ? cfg.clientSecret : ""};

  // The sketch starts mDNS (carthing.local); start it here too in case it
  // didn't. A second init only returns "already running".
  esp_err_t e = mdns_init();
  if (e != ESP_OK && e != ESP_ERR_INVALID_STATE)
    ESP_LOGW(TAG, "mdns_init: %s", esp_err_to_name(e));

  if (!InitZeroconf(g_name, 7864)) {
    ESP_LOGE(TAG, "zeroconf server failed to start");
    return false;
  }
  g_audio = std::make_shared<AudioControl>();
  g_spotify = std::make_shared<SpotifyStream>(
      g_audio, std::make_unique<NvsLoginStore>("spk_login"),
      [](bool c) {
        g_connected = c;
        ESP_LOGI(TAG, "Spotify %s", c ? "connected" : "disconnected");
      },
      g_name);
  ESP_LOGI(TAG, "speaker \"%s\" started", g_name.c_str());
  return true;
}

bool started() { return g_spotify != nullptr; }

bool connected() {
  return g_spotify && g_connected && g_spotify->getHandler() != nullptr;
}

bool active() { return g_spotify && g_spotify->isActive(); }

const char* name() { return g_name.c_str(); }

void setVolume(int percent) {
  if (!g_spotify)
    return;
  if (percent < 0) percent = 0;
  if (percent > 100) percent = 100;
  g_spotify->setLocalVolume((uint16_t)((uint32_t)percent * 65535 / 100));
}

void setVolumeAndReport(int percent) {
  if (!g_spotify)
    return;
  if (percent < 0) percent = 0;
  if (percent > 100) percent = 100;
  g_spotify->setVolume((uint8_t)percent);
}

void forgetLogin() {
  NvsLoginStore("spk_login").erase("");
}

}  // namespace desk_speaker
