# Glue between the Desk Thing sketch (main/) and the Spotify Connect speaker
# (sc_spotify + streamcore + bell). Its public header has no bell / Spotify
# includes, so the sketch's Arduino headers (HTTPClient.h, ...) never clash
# with bell's.
idf_component_register(
  SRCS "DeskSpeaker.cpp"
  INCLUDE_DIRS "include"
  PRIV_REQUIRES sc_spotify streamcore bell nvs_flash esp_driver_gpio espressif__mdns)
