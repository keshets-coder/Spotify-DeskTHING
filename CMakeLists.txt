# Spotify Desk Thing: ESP-IDF project with Arduino as a component.
# Build with `idf.py build`, flash with `idf.py -p <port> flash monitor`.
cmake_minimum_required(VERSION 3.16)

# Developers without access to the ESP component registry can point
# DESKTHING_EXTRA_COMPONENTS at a folder holding the dependencies instead.
if(DEFINED ENV{DESKTHING_EXTRA_COMPONENTS})
  list(APPEND EXTRA_COMPONENT_DIRS "$ENV{DESKTHING_EXTRA_COMPONENTS}")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(SpotifyDeskThing)

# Same as "USB CDC On Boot: Enabled" + "USB Mode: Hardware CDC and JTAG" in Arduino IDE:
# Serial prints go to the USB-C port.
idf_build_set_property(COMPILE_DEFINITIONS "ARDUINO_USB_MODE=1" APPEND)
idf_build_set_property(COMPILE_DEFINITIONS "ARDUINO_USB_CDC_ON_BOOT=1" APPEND)
