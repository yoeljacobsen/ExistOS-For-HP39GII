include_directories(.)
include_directories(${CMAKE_SOURCE_DIR}/System/filesystem/fatfs)

add_library(dmcp dmcp_existos.c)

target_compile_options(dmcp PRIVATE
    -mtune=arm926ej-s -mcpu=arm926ej-s -mlittle-endian
    -mfloat-abi=soft -mthumb-interwork -mthumb -Os
)
