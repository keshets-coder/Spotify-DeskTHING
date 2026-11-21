# Spotify Connect receiver (from StreamCore32, derived from feelfreelinux/cspot).
# The nanopb sources in generated/ were made from protobuf/*.proto with
# bell/external/nanopb (see generated/README.md), so no protoc is needed.
set(srcs)
if(CONFIG_SC32_SPOTIFY)
  file(GLOB srcs "src/*.cpp" "src/*.c" "generated/protobuf/*.pb.c")
endif()
idf_component_register(
  SRCS ${srcs}
  INCLUDE_DIRS "include" "generated"
  REQUIRES streamcore bell mbedtls nvs_flash)
