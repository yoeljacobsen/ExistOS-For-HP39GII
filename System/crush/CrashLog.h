#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Crash log type definitions
typedef enum {
    CRASH_TYPE_ASSERT = 0,      // Assertion failure
    CRASH_TYPE_STACK_OVERFLOW,  // Stack overflow
    CRASH_TYPE_OUT_OF_MEMORY,   // Out of memory
    CRASH_TYPE_HARD_FAULT,      // Hardware fault
    CRASH_TYPE_UNKNOWN          // Unknown error
} crash_type_t;

// Crash log information structure
typedef struct {
    uint32_t timestamp;         // Crash timestamp
    crash_type_t type;          // Crash type
    uint32_t pc;                // Program counter
    uint32_t lr;                // Link register
    uint32_t sp;                // Stack pointer
    uint32_t psr;               // Program status register
    uint32_t task_name_addr;    // Task name address
    char task_name[16];         // Task name
    char file[64];              // Source filename
    int line;                   // Line number
    char description[128];      // Crash description
    uint8_t stack_trace[256];   // Stack trace data
    uint32_t stack_trace_size;  // Stack trace data size
} crash_log_t;

// Initialize crash log system
void crash_log_init(void);

// Save crash log entry
void crash_log_save(crash_type_t type, const char* file, int line, const char* description, 
                   uint32_t pc, uint32_t lr, uint32_t sp, uint32_t psr, const char* task_name);

// Get number of crash logs
int crash_log_get_count(void);

// Get crash log entry
bool crash_log_get(int index, crash_log_t* log);

// Clear all crash logs
void crash_log_clear_all(void);

// Create crash log file
bool crash_log_create_file(const crash_log_t* log);

// System crash handler (replaces legacy PANIC macro)
void crash_handler(crash_type_t type, const char* file, int line, const char* description, ...);

// Get current stack trace
uint32_t crash_log_get_stack_trace(uint8_t* buffer, uint32_t buffer_size);

#ifdef __cplusplus
}
#endif
