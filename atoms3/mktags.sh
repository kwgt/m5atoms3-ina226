#! /bin/sh

ctags -R \
    include \
    src \
    .pio/libdeps/m5stack-atoms3 \
    "${HOME}/.platformio/packages/framework-arduinoespressif32/tools/sdk/esp32s3"
