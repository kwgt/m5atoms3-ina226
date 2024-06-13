#! /bin/sh

PLATFORM_PREFIX="${HOME}/.platformio/packages/framework-arduinoespressif32"

ctags -R \
    include \
    src \
    .pio/libdeps/m5stack-atoms3 \
    "${PLATFORM_PREFIX}/tools/sdk/esp32s3" \
    "${PLATFORM_PREFIX}/libraries"
