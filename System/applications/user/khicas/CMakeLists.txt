# KhiCAS application CMakeLists.txt

# Add source files
aux_source_directory(. KHICAS_SRCS)

# Create static library
add_library(khicas_lib STATIC ${KHICAS_SRCS})

# Configure library include directories
target_include_directories(khicas_lib PUBLIC 
    .
    ${CMAKE_SOURCE_DIR}/Libs/src/khicas/
)

# Compile options
target_compile_options(khicas_lib PRIVATE 
    -mtune=arm926ej-s 
    -mcpu=arm926ej-s 
    -mlittle-endian 
    -mfloat-abi=soft 
    -marm
    -Os 
    -pipe 
    -DHP39
)