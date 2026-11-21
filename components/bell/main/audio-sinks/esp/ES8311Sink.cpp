#include "ES8311Sink.h"

#include <cmath>
#include <cstring>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "ivorbisfile.h"
#include "sdkconfig.h"

static const char* TAG = "ES8311_SINK";

// decoded PCM per ov_read() call (16-bit interleaved)
static constexpr size_t PCM_BYTES = 4096;
static constexpr size_t OUT_FRAMES = PCM_BYTES / 4;

static std::mutex s_cmdMutex;

static int ovSeekCb(void*, ogg_int64_t, int) {
  return -1;
}
static long ovTellCb(void*) {
  return 0;
}

static size_t ovReadCb(void* ptr, size_t size, size_t nmemb, void* ds) {
  return static_cast<ES8311Sink*>(ds)->readEncoded(ptr, size * nmemb);
}

static void sinkTask(void* self) {
  static_cast<ES8311Sink*>(self)->run_feed(0);
}

static inline bool isCancelled(ES8311Sink::Stream::State st) {
  return st == ES8311Sink::Stream::State::Cancel ||
         st == ES8311Sink::Stream::State::CancelAwait ||
         st == ES8311Sink::Stream::State::Stopped;
}

// ------------------------------------------------------------------ Stream --
ES8311Sink::Stream::Stream(void* source, size_t streamId, size_t buffer_size) {
  this->source = source;
  this->streamId = streamId;
  // +1: FreeRTOS stream buffers need one spare byte in the storage area
  ucBufferStorage = (uint8_t*)heap_caps_malloc(buffer_size + 1,
                                               MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (ucBufferStorage == nullptr)
    ucBufferStorage = (uint8_t*)malloc(buffer_size + 1);
  if (ucBufferStorage != nullptr)
    dataBuffer = xStreamBufferCreateStatic(buffer_size, 1, ucBufferStorage,
                                           &xStaticStreamBuffer);
  if (dataBuffer == nullptr)
    ESP_LOGE(TAG, "not enough memory for a %u byte stream buffer",
             (unsigned)buffer_size);
}

ES8311Sink::Stream::~Stream() {
  if (dataBuffer != nullptr)
    vStreamBufferDelete(dataBuffer);
  free(ucBufferStorage);
  ESP_LOGI(TAG, "stream %u deleted", (unsigned)streamId);
}

size_t ES8311Sink::Stream::feed_data(uint8_t* data, size_t len,
                                     bool STORAGE_VOLATILE) {
  if (dataBuffer == nullptr)
    return len;  // drop it rather than block the player forever
  // a seek: everything queued so far belongs to the old position
  if (STORAGE_VOLATILE && header_size)
    xStreamBufferReset(dataBuffer);
  return xStreamBufferSend(dataBuffer, (void*)data, len, pdMS_TO_TICKS(30));
}

void ES8311Sink::Stream::empty_feed() {
  if (dataBuffer != nullptr)
    xStreamBufferReset(dataBuffer);
}

// -------------------------------------------------------------------- sink --
ES8311Sink::ES8311Sink(SemaphoreHandle_t* SPI_semaphore) {
  (void)SPI_semaphore;
  isRunning = true;
  out_ = (int16_t*)heap_caps_malloc(OUT_FRAMES * 2 * sizeof(int16_t),
                                    MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

  i2s_chan_config_t chan_cfg =
      I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  chan_cfg.dma_desc_num = 8;
  chan_cfg.dma_frame_num = 256;
  chan_cfg.auto_clear = true;  // silence instead of a buzz when starved
  ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_, nullptr));

  i2s_std_config_t std_cfg = {
      .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(44100),
      .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                      I2S_SLOT_MODE_STEREO),
      .gpio_cfg =
          {
              .mclk = (gpio_num_t)CONFIG_DESK_I2S_MCLK,
              .bclk = (gpio_num_t)CONFIG_DESK_I2S_BCLK,
              .ws = (gpio_num_t)CONFIG_DESK_I2S_LRCK,
              .dout = (gpio_num_t)CONFIG_DESK_I2S_DOUT,
              .din = I2S_GPIO_UNUSED,
              .invert_flags = {.mclk_inv = false, .bclk_inv = false, .ws_inv = false},
          },
  };
  // the ES8311 is set up for MCLK = 256 x sample rate
  std_cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
  ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_, &std_cfg));
  ESP_ERROR_CHECK(i2s_channel_enable(tx_));
  rate_ = 44100;
  set_volume(100);

  // Tremor needs a fair bit of stack; keep it in internal RAM. Core 0 runs
  // Wi-Fi, core 1 runs the Arduino loop (display), so decoding goes on core 0
  // below Wi-Fi (23) and lwIP (18).
  xTaskCreatePinnedToCore(sinkTask, "es8311_sink", 24 * 1024, this, 10,
                          &task_handle, 0);
  ESP_LOGI(TAG, "I2S ready (MCLK %d, BCLK %d, LRCK %d, DOUT %d)",
           CONFIG_DESK_I2S_MCLK, CONFIG_DESK_I2S_BCLK, CONFIG_DESK_I2S_LRCK,
           CONFIG_DESK_I2S_DOUT);
}

ES8311Sink::~ES8311Sink() {
  isRunning = false;
  if (task_handle != nullptr)
    vTaskDelete(task_handle);
  task_handle = nullptr;
  if (tx_ != nullptr) {
    i2s_channel_disable(tx_);
    i2s_del_channel(tx_);
  }
  free(out_);
}

void ES8311Sink::new_stream(std::shared_ptr<Stream> stream) {
  streams.push_back(stream);
}

size_t ES8311Sink::stream_seekable(size_t streamId) {
  if (streams.size() && streams[0]->streamId == streamId)
    return streams[0]->header_size;
  return 0;
}

size_t ES8311Sink::data_request(std::shared_ptr<Stream> stream) {
  return stream->dataBuffer ? xStreamBufferSpacesAvailable(stream->dataBuffer) : 0;
}

size_t ES8311Sink::spaces_available(size_t streamId) {
  for (auto& stream : streams)
    if (stream->streamId == streamId)
      return data_request(stream);
  return 0;
}

void ES8311Sink::new_state(std::shared_ptr<Stream> stream, Stream::State s) {
  stream->state = s;
  if (state_callback != nullptr)
    state_callback(s, stream->source);
  ESP_LOGI(TAG, "stream %u state changed to %d", (unsigned)stream->streamId, (int)s);
}

void ES8311Sink::delete_all_streams(void) {
  if (streams.size() > 1)
    streams.erase(streams.begin() + 1, streams.end());
  if (streams.size() && streams[0]->state != Stream::State::Stopped)
    new_state(streams[0], Stream::State::Cancel);
}

void ES8311Sink::stop_feed() {
  if (streams.size() && streams[0]->state <= Stream::State::SoftCancel)
    new_state(streams[0], Stream::State::Cancel);
}

void ES8311Sink::soft_stop_feed() {
  if (streams.size() && streams[0]->state <= Stream::State::PlaybackPaused)
    new_state(streams[0], Stream::State::SoftCancel);
}

uint8_t ES8311Sink::feed_command(command_callback commandCallback) {
  if (streams.size()) {
    std::scoped_lock l(s_cmdMutex);
    command_callbacks.push_back(commandCallback);
  } else {
    commandCallback(0);
  }
  return 0;
}

void ES8311Sink::runCommands(size_t streamId) {
  std::deque<command_callback> todo;
  {
    std::scoped_lock l(s_cmdMutex);
    todo.swap(command_callbacks);
  }
  for (auto& cb : todo)
    cb(streamId);
}

uint8_t ES8311Sink::set_volume(uint8_t vol) {
  if (vol > 100)
    vol = 100;
  curvol_ = vol;
  // same curve as the VS1053: 0.5 dB per step below 100
  int32_t g = vol == 0 ? 0
                       : (int32_t)std::lround(
                             32768.0 * std::pow(10.0, -(100 - vol) * 0.5 / 20.0));
  gainQ15_.store(g);
  return vol;
}

bool ES8311Sink::setSampleRate(uint32_t rate) {
  if (rate == 0 || rate == rate_)
    return true;
  i2s_std_clk_config_t clk = I2S_STD_CLK_DEFAULT_CONFIG(rate);
  clk.mclk_multiple = I2S_MCLK_MULTIPLE_256;
  i2s_channel_disable(tx_);
  esp_err_t err = i2s_channel_reconfig_std_clock(tx_, &clk);
  i2s_channel_enable(tx_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "can't switch to %u Hz: %s", (unsigned)rate, esp_err_to_name(err));
    return false;
  }
  rate_ = rate;
  ESP_LOGI(TAG, "sample rate %u Hz", (unsigned)rate);
  return true;
}

// The board has one speaker: mix to mono, apply the volume and send the
// same sample to both I2S slots.
void ES8311Sink::writePcm(int16_t* in, size_t frames, int channels) {
  const int32_t g = gainQ15_.load();
  while (frames) {
    size_t n = std::min(frames, OUT_FRAMES);
    for (size_t i = 0; i < n; i++) {
      int32_t s = channels >= 2 ? ((int32_t)in[0] + in[1]) / 2 : in[0];
      s = (s * g) >> 15;
      out_[2 * i] = out_[2 * i + 1] = (int16_t)s;
      in += channels;
    }
    size_t written = 0;
    i2s_channel_write(tx_, out_, n * 2 * sizeof(int16_t), &written, portMAX_DELAY);
    frames -= n;
  }
}

// Called by Tremor: block until encoded data arrives, the stream is cancelled
// (returns 0 = end of file) or, after a soft stop, everything is played.
size_t ES8311Sink::readEncoded(void* ptr, size_t len) {
  if (prefixPos_ < prefix_.size()) {
    size_t n = std::min(len, prefix_.size() - prefixPos_);
    memcpy(ptr, prefix_.data() + prefixPos_, n);
    prefixPos_ += n;
    return n;
  }
  return readRaw(ptr, len);
}

// Read exactly len bytes (false if the stream ends first).
static bool readFull(ES8311Sink* sink, uint8_t* p, size_t len) {
  while (len) {
    size_t n = sink->readRaw(p, len);
    if (!n)
      return false;
    p += n;
    len -= n;
  }
  return true;
}

// Spotify's files start with their own Ogg page (167 bytes of metadata)
// before the Vorbis headers. Tremor gives up on it (OV_ENOTVORBIS), so skip
// pages until the one holding the Vorbis identification header, and keep
// that page for Tremor.
bool ES8311Sink::skipToVorbis() {
  prefix_.clear();
  prefixPos_ = 0;
  for (int page = 0; page < 4; page++) {
    std::vector<uint8_t> buf(27);
    if (!readFull(this, buf.data(), 27))
      return false;
    if (memcmp(buf.data(), "OggS", 4) != 0)
      return false;
    uint8_t segs = buf[26];
    buf.resize(27 + segs);
    if (!readFull(this, buf.data() + 27, segs))
      return false;
    size_t body = 0;
    for (int i = 0; i < segs; i++)
      body += buf[27 + i];
    size_t head = buf.size();
    buf.resize(head + body);
    if (!readFull(this, buf.data() + head, body))
      return false;
    if (body >= 7 && memcmp(buf.data() + head, "\x01vorbis", 7) == 0) {
      prefix_ = std::move(buf);
      return true;
    }
    ESP_LOGI(TAG, "skipped a %u-byte non-Vorbis Ogg page", (unsigned)buf.size());
  }
  return false;
}

size_t ES8311Sink::readRaw(void* ptr, size_t len) {
  auto s = current_;
  if (!s || s->dataBuffer == nullptr)
    return 0;
  while (isRunning) {
    runCommands(s->streamId);
    auto st = s->state;
    if (isCancelled(st))
      return 0;
    size_t got = xStreamBufferReceive(s->dataBuffer, ptr, len, pdMS_TO_TICKS(30));
    if (got) {
      consumed_ += got;
      return got;
    }
    if (st == Stream::State::SoftCancel)
      return 0;
  }
  return 0;
}

void ES8311Sink::decodeStream(std::shared_ptr<Stream> s) {
  new_state(s, Stream::State::Playback);

  OggVorbis_File vf;
  memset(&vf, 0, sizeof(vf));
  // Tremor calls seek and tell unconditionally: the stream isn't seekable.
  ov_callbacks cb = {ovReadCb, ovSeekCb, nullptr, ovTellCb};
  int r = skipToVorbis() ? ov_open_callbacks(this, &vf, nullptr, 0, cb)
                         : OV_ENOTVORBIS;
  if (r < 0) {
    if (!isCancelled(s->state))
      ESP_LOGE(TAG, "stream %u is not Ogg Vorbis (%d), skipping it",
               (unsigned)s->streamId, r);
    // swallow the rest of it until the player moves on
    uint8_t junk[256];
    while (isRunning && readEncoded(junk, sizeof(junk)) > 0) {
    }
    s->empty_feed();
    return;
  }
  // seeks only throw away data queued after the headers
  s->header_size = consumed_ ? consumed_ : 1;
  new_state(s, Stream::State::PlaybackSeekable);

  vorbis_info* vi = ov_info(&vf, -1);
  int channels = vi ? vi->channels : 2;
  if (vi)
    setSampleRate(vi->rate);
  ESP_LOGI(TAG, "stream %u: Ogg Vorbis %d Hz, %d ch", (unsigned)s->streamId,
           vi ? (int)vi->rate : 0, channels);

  static int16_t pcm[PCM_BYTES / 2];
  int errors = 0;
  while (isRunning) {
    runCommands(s->streamId);
    auto st = s->state;
    if (isCancelled(st)) {
      s->empty_feed();
      break;
    }
    if (st == Stream::State::PlaybackPaused) {
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }
    int section = 0;
    long n = ov_read(&vf, (char*)pcm, sizeof(pcm), &section);
    if (n == 0)
      break;  // end of the track (or cancelled)
    if (n < 0) {
      // OV_HOLE after a seek is normal: the decoder resyncs on the next page
      if (n != OV_HOLE && ++errors > 200) {
        ESP_LOGE(TAG, "too many decode errors (%ld), dropping the track", n);
        break;
      }
      continue;
    }
    errors = 0;
    writePcm(pcm, (size_t)n / (sizeof(int16_t) * channels), channels);
  }
  ov_clear(&vf);
}

void ES8311Sink::run_feed(size_t) {
  while (isRunning) {
    if (streams.size()) {
      std::shared_ptr<Stream> s = streams.front();
      current_ = s;
      consumed_ = 0;
      new_state(s, Stream::State::PlaybackStart);
      decodeStream(s);
      if (s->state != Stream::State::Stopped)
        new_state(s, Stream::State::Stopped);
      current_ = nullptr;
      if (streams.size() && streams.front() == s)
        streams.pop_front();
    }
    runCommands(0);
    vTaskDelay(pdMS_TO_TICKS(20));
  }
  task_handle = nullptr;
  vTaskDelete(nullptr);
}
