include_directories(.)
include_directories(${CMAKE_SOURCE_DIR}/System/dmcp)
include_directories(${CMAKE_SOURCE_DIR}/System/filesystem/fatfs)

add_library(db48x db48x_app.c)

target_compile_options(db48x PRIVATE
    -mtune=arm926ej-s -mcpu=arm926ej-s -mlittle-endian
    -mfloat-abi=soft -mthumb-interwork -mthumb -Os
)
