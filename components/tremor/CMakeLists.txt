# Tremor: Xiph.org's integer-only Ogg Vorbis decoder (BSD licence, see COPYING),
# copied from https://github.com/feelfreelinux/bell (external/tremor).
file(GLOB TREMOR_SRCS "*.c")
idf_component_register(SRCS ${TREMOR_SRCS} INCLUDE_DIRS ".")
target_compile_options(${COMPONENT_LIB} PRIVATE -O2 -Wno-unused-variable -Wno-unused-but-set-variable -Wno-maybe-uninitialized -Wno-misleading-indentation -Wno-shift-negative-value -Wno-char-subscripts -Wno-unused-label)
