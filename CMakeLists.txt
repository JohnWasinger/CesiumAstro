cmake_minimum_required(VERSION 3.10)
project(CesiumAstro_CCSDS C)

# Set C standard
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Compiler flags
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -pedantic")
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -g -O0")
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O3")

# CCSDS CRC-16 example (CCSDS 133.0-B-2 compliant)
add_executable(ccsds_crc ccsds_crc.c)

# CRC-32 telemetry intro (generic satellite telemetry)
add_executable(crc32_telemetry_intro crc32_telemetry_intro.c)

# Rust target (using custom command)
find_program(RUSTC rustc)
if(RUSTC)
    set(RUST_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/ccsds_crc.rs)
    set(RUST_OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/ccsds_crc_rust)
    
    add_custom_command(
        OUTPUT ${RUST_OUTPUT}
        COMMAND ${RUSTC} ${RUST_SOURCE} -o ${RUST_OUTPUT}
        DEPENDS ${RUST_SOURCE}
        COMMENT "Building Rust CCSDS CRC implementation"
    )
    
    add_custom_target(ccsds_crc_rust ALL DEPENDS ${RUST_OUTPUT})
    
    # Install Rust executable
    install(PROGRAMS ${RUST_OUTPUT} DESTINATION bin)
    
    message(STATUS "Rust compiler found: ${RUSTC}")
else()
    message(WARNING "Rust compiler not found - skipping ccsds_crc_rust target")
endif()

# Install C targets
install(TARGETS ccsds_crc crc32_telemetry_intro DESTINATION bin)

# Print build info
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C Compiler: ${CMAKE_C_COMPILER}")
message(STATUS "C Flags: ${CMAKE_C_FLAGS}")
