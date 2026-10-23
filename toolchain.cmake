SET(CMAKE_SYSTEM_NAME Generic)
SET(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(ARM_TOOLCHAIN_PREFIX "")
set(_CANDIDATE_DIRS
    "$ENV{HOME}/.local/opt/gcc-arm-none-eabi-10.3-2021.10/bin"
    "/opt/gcc-arm-none-eabi-10.3-2021.10/bin"
)
if(DEFINED ENV{ARM_GCC_DIR} AND NOT "$ENV{ARM_GCC_DIR}" STREQUAL "")
    list(PREPEND _CANDIDATE_DIRS "$ENV{ARM_GCC_DIR}/bin")
endif()

foreach(_dir ${_CANDIDATE_DIRS})
    if(EXISTS "${_dir}/arm-none-eabi-gcc")
        set(ARM_TOOLCHAIN_PREFIX "${_dir}/")
        set(ENV{PATH} "${_dir}:$ENV{PATH}")
        break()
    endif()
endforeach()

SET(CMAKE_C_COMPILER   "${ARM_TOOLCHAIN_PREFIX}arm-none-eabi-gcc")
SET(CMAKE_CXX_COMPILER "${ARM_TOOLCHAIN_PREFIX}arm-none-eabi-g++")
SET(CMAKE_ASM_COMPILER "${ARM_TOOLCHAIN_PREFIX}arm-none-eabi-gcc")
SET(CMAKE_OBJCOPY      "${ARM_TOOLCHAIN_PREFIX}arm-none-eabi-objcopy")
SET(CMAKE_NM           "${ARM_TOOLCHAIN_PREFIX}arm-none-eabi-nm")
SET(CMAKE_SIZE         "${ARM_TOOLCHAIN_PREFIX}arm-none-eabi-size")

set(CMAKE_C_COMPILER_WORKS 1)
set(CMAKE_CXX_COMPILER_WORKS 1)
