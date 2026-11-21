idf_component_register(
  SRCS "SpotifyCarThing.cpp"
  INCLUDE_DIRS "."
  REQUIRES espressif__arduino-esp32 lovyangfx arduinojson desk_speaker esp_wifi esp_netif nvs_flash espressif__mdns espressif__network_provisioning)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error -Wno-format-truncation -Wno-unused-variable -Wno-unused-function -Wno-missing-field-initializers -Wno-dangling-reference -Wno-error=dangling-reference)
