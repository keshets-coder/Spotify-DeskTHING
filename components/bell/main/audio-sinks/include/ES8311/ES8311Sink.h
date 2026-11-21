#pragma once
// Audio sink for the ES8311 codec + FM8002E amplifier on the LCDWIKI ES3C28P
// board (Spotify Desk Thing).
//
// It has the same interface as StreamCore32's VS1053 sink, so AudioControl,
// StreamBase and the Spotify Connect source work unchanged. The VS1053 decodes
// Ogg Vorbis in hardware; this sink decodes it in software (Tremor, integer
// Vorbis) and sends 16-bit PCM over I2S to the ES8311.
//
// The ES8311 itself is set up over I2C by the application (it shares the bus
// with the touch controller), see components/desk_speaker.

#include <inttypes.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <deque>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <type_traits>

#include "driver/i2s_std.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/stream_buffer.h"
#include "freertos/task.h"

class ES8311Sink {
 public:
  explicit ES8311Sink(SemaphoreHandle_t* SPI_semaphore);
  ~ES8311Sink();

  class Stream {
   public:
    Stream(void* source, size_t streamId = 0, size_t buffer_size = 16384);
    ~Stream();
    /** Queue encoded (Ogg Vorbis) data. Returns the bytes taken. */
    size_t feed_data(uint8_t* data, size_t len, bool STORAGE_VOLATILE = 0);
    void empty_feed();
    enum State {
      PlaybackStart = 0,
      Playback = 1,
      PlaybackSeekable = 2,
      PlaybackPaused = 3,
      SoftCancel = 4,
      Cancel = 5,
      CancelAwait = 6,
      Stopped = 7
    };
    volatile State state = Stopped;
    size_t header_size = 0;
    size_t streamId;
    StaticStreamBuffer_t xStaticStreamBuffer;
    StreamBufferHandle_t dataBuffer = nullptr;
    uint8_t* ucBufferStorage = nullptr;
    void* source = nullptr;
  };

  typedef std::function<void(size_t)> command_callback;

  void stop_feed();
  void soft_stop_feed();
  uint8_t feed_command(command_callback commandCallback);

  template <class T>
  uint8_t to_logarithmic_volume(T value, std::optional<T> limit = std::nullopt) {
    static_assert(std::is_arithmetic<T>::value, "T must be arithmetic");
    long double m = limit.has_value() ? (long double)*limit
                                      : (long double)std::numeric_limits<T>::max();
    long double v = (long double)value;
    if (!(m > 0)) m = 1;
    v = std::clamp<long double>(v, 0, m);
    long double y = 50.0L * std::log10(1.0L + 100.0L * (v / m));
    return (uint8_t)std::llround(std::clamp<long double>(y, 0, 100));
  }
  template <class T>
  uint8_t to_linear_volume(T value, std::optional<T> limit = std::nullopt) {
    static_assert(std::is_arithmetic<T>::value, "T must be arithmetic");
    long double m = limit.has_value() ? (long double)*limit
                                      : (long double)std::numeric_limits<T>::max();
    long double v = (long double)value;
    if (!(m > 0)) m = 1;
    v = std::clamp<long double>(v, 0, m);
    return (uint8_t)std::llround(std::clamp<long double>(100.0L * v / m, 0, 100));
  }
  template <class T>
  T get_logarithmic_volume(uint8_t logVolume, std::optional<T> limit = std::nullopt) {
    static_assert(std::is_arithmetic<T>::value, "T must be arithmetic");
    const long double y = std::clamp<int>(logVolume, 0, 100);
    long double M = limit.has_value() ? (long double)*limit
                                      : (long double)std::numeric_limits<T>::max();
    if (!(M > 0.0L)) M = 1.0L;
    const long double x = (std::pow(10.0L, y / 50.0L) - 1.0L) / 100.0L;
    long double val = std::clamp<long double>(x * M, 0.0L, M);
    if constexpr (std::is_floating_point<T>::value) return (T)val;
    return (T)std::min<long double>(std::floor(val + 0.5L),
                                    (long double)std::numeric_limits<T>::max());
  }
  template <class T>
  T get_linear_volume(uint8_t vol, std::optional<T> limit = std::nullopt) {
    long double M = limit.has_value() ? (long double)*limit
                                      : (long double)std::numeric_limits<T>::max();
    long double val = std::clamp<int>(vol, 0, 100) / 100.0L * M;
    if constexpr (std::is_floating_point<T>::value) return (T)val;
    return (T)std::floor(val + 0.5L);
  }

  /** 0..100, like the VS1053 sink: 100 = 0 dB, each step 0.5 dB, 0 = mute. */
  uint8_t set_volume(uint8_t vol);
  void set_volume(uint8_t left, uint8_t right) { set_volume(std::max(left, right)); }

  size_t data_request(std::shared_ptr<Stream> stream);
  size_t stream_seekable(size_t streamId);
  size_t spaces_available(size_t streamId);
  void new_state(std::shared_ptr<Stream>, Stream::State);
  void new_stream(std::shared_ptr<Stream> stream);
  void delete_all_streams(void);
  void remove_stream(std::shared_ptr<Stream> stream) {
    for (size_t i = 0; i < streams.size(); i++)
      if (streams[i]->streamId == stream->streamId) {
        streams.erase(streams.begin() + i);
        return;
      }
  }
  void setParams(uint32_t sampleRate, uint8_t channelCount, uint8_t bitDepth) {}

  void run_feed(size_t);

  std::function<void(Stream::State, void* source)> state_callback = nullptr;
  std::deque<command_callback> command_callbacks;
  std::deque<std::shared_ptr<Stream>> streams;
  bool isRunning = false;

  // used by the decoder callbacks
  size_t readEncoded(void* ptr, size_t len);

 private:
  void runCommands(size_t streamId);
  void decodeStream(std::shared_ptr<Stream> stream);
  bool setSampleRate(uint32_t rate);
  void writePcm(int16_t* samples, size_t frames, int channels);
  void writeSilence(size_t frames);

  i2s_chan_handle_t tx_ = nullptr;
  uint32_t rate_ = 44100;
  std::atomic<int32_t> gainQ15_{32768};
  uint8_t curvol_ = 100;
  std::shared_ptr<Stream> current_;
  size_t consumed_ = 0;
  int16_t* out_ = nullptr;
  TaskHandle_t task_handle = nullptr;
};
