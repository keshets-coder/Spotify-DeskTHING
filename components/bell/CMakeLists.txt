# bell — reduced copy of https://github.com/feelfreelinux/bell, taken from
# StreamCore32 (https://github.com/tobiasguyer/StreamCore32).
# Spotify Desk Thing: the VS1053 sink is replaced by an ES8311 (I2S) sink that
# decodes Ogg Vorbis in software with Tremor.
# Options come from Kconfig (menuconfig: Spotify Desk Thing speaker).

file(GLOB BELL_SRCS
  "main/io/*.cpp" "main/io/*.c"
  "main/utilities/*.cpp" "main/utilities/*.c"
  "main/platform/esp/*.cpp"
  "main/audio-sinks/esp/ES8311Sink.cpp"
  "external/nanopb/pb_common.c" "external/nanopb/pb_decode.c" "external/nanopb/pb_encode.c"
  "external/cJSON/cJSON.c"
  "external/civetweb/civetweb.c" "external/civetweb/CivetServer.cpp")

idf_component_register(
  SRCS ${BELL_SRCS}
  INCLUDE_DIRS
    "main/io/include"
    "main/utilities/include"
    "main/platform"
    "main/audio-sinks/include/ES8311"
    "external/nanopb"
    "external/cJSON"
    "external/civetweb/include"
    "external/nlohmann_json/include"
  REQUIRES mbedtls esp_driver_i2s esp_driver_gpio pthread lwip nvs_flash esp_timer espressif__mdns tremor
  PRIV_REQUIRES esp_wifi efuse)

target_compile_definitions(${COMPONENT_LIB} PUBLIC
  PB_ENABLE_MALLOC PB_FIELD_32BIT BELL_DISABLE_FMT BELL_DISABLE_CODECS
  USE_DEFAULT_STDLIB=1 TARGET_OS_IPHONE=0)
target_compile_options(${COMPONENT_LIB} PRIVATE
  -Wno-error=format -Wno-format -Wno-stringop-overflow -Wno-stringop-overread
  -Wno-unused-variable -Wno-unused-function)

# path of the nanopb generator for components that compile .proto files
set(BELL_NANOPB_ROOT "${CMAKE_CURRENT_LIST_DIR}/external/nanopb" CACHE INTERNAL "")
