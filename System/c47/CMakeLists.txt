include_directories(.)
include_directories(${CMAKE_SOURCE_DIR}/System/filesystem/fatfs)

add_library(c47
    c47_existos.c
    c47_ico.c
)

target_compile_options(c47 PRIVATE
    -mtune=arm926ej-s
    -mcpu=arm926ej-s
    -mlittle-endian
    -mfloat-abi=soft
    -mthumb-interwork
    -mthumb
    -Os
)
