#include "CrashLog.h"
#include "SystemFs.h"
#include "debug.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>

#if FS_TYPE == FS_FATFS
    #include "filesystem/fatfs/ff.h"
#else
    #include "filesystem/littlefs/lfs.h"
#endif

// Crash log storage constants
#define CRASH_LOG_MAX_COUNT 10
#define CRASH_LOG_FILE_PREFIX "crash_"
#define CRASH_LOG_FILE_EXTENSION ".log"
#define CRASH_LOG_DIR "/crash"

// Crash log storage buffer
static crash_log_t crash_logs[CRASH_LOG_MAX_COUNT];
static int crash_log_count = 0;
static bool crash_log_initialized = false;

// Get current timestamp in seconds
static uint32_t get_timestamp(void) {
    // Use system uptime (converted from ms to seconds)
    extern uint32_t ll_get_time_ms(void);
    return ll_get_time_ms() / 1000;
}

// Initialize crash log system
void crash_log_init(void) {
    if (crash_log_initialized) {
        return;
    }
    
    // Clear crash log buffer
    memset(crash_logs, 0, sizeof(crash_logs));
    crash_log_count = 0;
    
    // Create crash directory
#if FS_TYPE == FS_FATFS
    FRESULT res;
    res = f_mkdir(CRASH_LOG_DIR);
    if (res != FR_OK && res != FR_EXIST) {
        printf("Failed to create crash directory: %d\n", res);
    }
#else
    // LittleFS directory creation
    lfs_t* lfs = (lfs_t*)GetFsObj();
    int err = lfs_mkdir(lfs, CRASH_LOG_DIR);
    if (err != LFS_ERR_OK && err != LFS_ERR_EXIST) {
        printf("Failed to create crash directory: %d\n", err);
    }
#endif
    
    crash_log_initialized = true;
    printf("Crash log system initialized\n");
}

// Capture current stack trace
uint32_t crash_log_get_stack_trace(uint8_t* buffer, uint32_t buffer_size) {
    if (!buffer || buffer_size == 0) {
        return 0;
    }
    
    // Get current stack pointer
    uint32_t sp;
    __asm volatile ("mov %0, sp" : "=r" (sp));
    
    // Copy stack data
    uint32_t stack_size = buffer_size;
    if (stack_size > 256) {
        stack_size = 256;
    }
    
    memcpy(buffer, (void*)sp, stack_size);
    return stack_size;
}

// Save crash log entry
void crash_log_save(crash_type_t type, const char* file, int line, const char* description, 
                   uint32_t pc, uint32_t lr, uint32_t sp, uint32_t psr, const char* task_name) {
    if (!crash_log_initialized) {
        crash_log_init();
    }
    
    // If log buffer is full, drop the oldest entry
    if (crash_log_count >= CRASH_LOG_MAX_COUNT) {
        for (int i = 0; i < CRASH_LOG_MAX_COUNT - 1; i++) {
            memcpy(&crash_logs[i], &crash_logs[i + 1], sizeof(crash_log_t));
        }
        crash_log_count--;
    }
    
    // Populate new crash log entry
    crash_log_t* log = &crash_logs[crash_log_count];
    memset(log, 0, sizeof(crash_log_t));
    
    log->timestamp = get_timestamp();
    log->type = type;
    log->pc = pc;
    log->lr = lr;
    log->sp = sp;
    log->psr = psr;
    log->line = line;
    
    if (file) {
        strncpy(log->file, file, sizeof(log->file) - 1);
        log->file[sizeof(log->file) - 1] = '\0';
    }
    
    if (description) {
        strncpy(log->description, description, sizeof(log->description) - 1);
        log->description[sizeof(log->description) - 1] = '\0';
    }
    
    if (task_name) {
        strncpy(log->task_name, task_name, sizeof(log->task_name) - 1);
        log->task_name[sizeof(log->task_name) - 1] = '\0';
    }
    
    // Capture stack trace
    log->stack_trace_size = crash_log_get_stack_trace(log->stack_trace, sizeof(log->stack_trace));
    
    // Write crash log file
    crash_log_create_file(log);
    
    crash_log_count++;
    
    printf("Crash log saved: %s\n", description);
}

// Create crash log file on filesystem
bool crash_log_create_file(const crash_log_t* log) {
    if (!log || !crash_log_initialized) {
        return false;
    }
    
    char filename[64];
    snprintf(filename, sizeof(filename), "%s/%s%lu%s", 
             CRASH_LOG_DIR, CRASH_LOG_FILE_PREFIX, log->timestamp, CRASH_LOG_FILE_EXTENSION);
    
#if FS_TYPE == FS_FATFS
    FIL file;
    FRESULT res = f_open(&file, filename, FA_CREATE_ALWAYS | FA_WRITE);
    if (res != FR_OK) {
        printf("Failed to create crash log file: %s, error: %d\n", filename, res);
        return false;
    }
    
    // Write crash log contents
    char buffer[512];
    UINT bytes_written;
    
    // Write header information
    snprintf(buffer, sizeof(buffer), 
             "=== ExistOS Crash Log ===\n"
             "Timestamp: %lu\n"
             "Type: %d\n"
             "PC: 0x%08lX\n"
             "LR: 0x%08lX\n"
             "SP: 0x%08lX\n"
             "PSR: 0x%08lX\n"
             "Task: %s\n"
             "File: %s\n"
             "Line: %d\n"
             "Description: %s\n"
             "Stack Trace Size: %lu\n",
             log->timestamp, log->type, log->pc, log->lr, log->sp, log->psr,
             log->task_name, log->file, log->line, log->description, log->stack_trace_size);
    
    f_write(&file, buffer, strlen(buffer), &bytes_written);
    
    // Write stack trace data in hex format
    if (log->stack_trace_size > 0) {
        f_write(&file, "Stack Trace:\n", 13, &bytes_written);
        
        for (uint32_t i = 0; i < log->stack_trace_size; i += 16) {
            int line_len = snprintf(buffer, sizeof(buffer), "%08lX: ", (uint32_t)(log->sp + i));
            
            for (uint32_t j = 0; j < 16 && (i + j) < log->stack_trace_size; j++) {
                line_len += snprintf(buffer + line_len, sizeof(buffer) - line_len, 
                                    "%02X ", log->stack_trace[i + j]);
            }
            
            line_len += snprintf(buffer + line_len, sizeof(buffer) - line_len, "\n");
            f_write(&file, buffer, line_len, &bytes_written);
        }
    }
    
    f_close(&file);
#else
    lfs_t* lfs = (lfs_t*)GetFsObj();
    lfs_file_t file;
    
    int err = lfs_file_open(lfs, &file, filename, LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC);
    if (err != LFS_ERR_OK) {
        printf("Failed to create crash log file: %s, error: %d\n", filename, err);
        return false;
    }
    
    // Write crash log contents
    char buffer[512];
    
    // Write header information
    snprintf(buffer, sizeof(buffer), 
             "=== ExistOS Crash Log ===\n"
             "Timestamp: %lu\n"
             "Type: %d\n"
             "PC: 0x%08lX\n"
             "LR: 0x%08lX\n"
             "SP: 0x%08lX\n"
             "PSR: 0x%08lX\n"
             "Task: %s\n"
             "File: %s\n"
             "Line: %d\n"
             "Description: %s\n"
             "Stack Trace Size: %lu\n",
             log->timestamp, log->type, log->pc, log->lr, log->sp, log->psr,
             log->task_name, log->file, log->line, log->description, log->stack_trace_size);
    
    lfs_file_write(lfs, &file, buffer, strlen(buffer));
    
    // Write stack trace data in hex format
    if (log->stack_trace_size > 0) {
        lfs_file_write(lfs, &file, "Stack Trace:\n", 13);
        
        for (uint32_t i = 0; i < log->stack_trace_size; i += 16) {
            int line_len = snprintf(buffer, sizeof(buffer), "%08lX: ", (uint32_t)(log->sp + i));
            
            for (uint32_t j = 0; j < 16 && (i + j) < log->stack_trace_size; j++) {
                line_len += snprintf(buffer + line_len, sizeof(buffer) - line_len, 
                                    "%02X ", log->stack_trace[i + j]);
            }
            
            line_len += snprintf(buffer + line_len, sizeof(buffer) - line_len, "\n");
            lfs_file_write(lfs, &file, buffer, line_len);
        }
    }
    
    lfs_file_close(lfs, &file);
#endif
    
    printf("Crash log file created: %s\n", filename);
    return true;
}

// Get number of recorded crash logs
int crash_log_get_count(void) {
    return crash_log_count;
}

// Get crash log by index
bool crash_log_get(int index, crash_log_t* log) {
    if (!log || index < 0 || index >= crash_log_count) {
        return false;
    }
    
    memcpy(log, &crash_logs[index], sizeof(crash_log_t));
    return true;
}

// Clear all crash logs
void crash_log_clear_all(void) {
    crash_log_count = 0;
    memset(crash_logs, 0, sizeof(crash_logs));
    
    // Delete all crash log files
#if FS_TYPE == FS_FATFS
    DIR dir;
    FILINFO fno;
    FRESULT res;
    
    res = f_opendir(&dir, CRASH_LOG_DIR);
    if (res == FR_OK) {
        for (;;) {
            res = f_readdir(&dir, &fno);
            if (res != FR_OK || fno.fname[0] == '\0') break;
            
            if (strncmp(fno.fname, CRASH_LOG_FILE_PREFIX, strlen(CRASH_LOG_FILE_PREFIX)) == 0) {
                char filepath[64];
                snprintf(filepath, sizeof(filepath), "%s/%s", CRASH_LOG_DIR, fno.fname);
                f_unlink(filepath);
            }
        }
        f_closedir(&dir);
    }
#else
    lfs_t* lfs = (lfs_t*)GetFsObj();
    lfs_dir_t dir;
    struct lfs_info info;
    
    int err = lfs_dir_open(lfs, &dir, CRASH_LOG_DIR);
    if (err == LFS_ERR_OK) {
        while (true) {
            err = lfs_dir_read(lfs, &dir, &info);
            if (err <= 0) break;
            
            if (strncmp(info.name, CRASH_LOG_FILE_PREFIX, strlen(CRASH_LOG_FILE_PREFIX)) == 0) {
                char filepath[64];
                snprintf(filepath, sizeof(filepath), "%s/%s", CRASH_LOG_DIR, info.name);
                lfs_remove(lfs, filepath);
            }
        }
        lfs_dir_close(lfs, &dir);
    }
#endif
    
    printf("All crash logs cleared\n");
}

// System crash handler
void crash_handler(crash_type_t type, const char* file, int line, const char* description, ...) {
    // Capture current CPU registers
    uint32_t pc, lr, sp, psr;
    
    __asm volatile ("mov %0, pc" : "=r" (pc));
    __asm volatile ("mov %0, lr" : "=r" (lr));
    __asm volatile ("mov %0, sp" : "=r" (sp));
    // PSR is not directly read here in user mode; set to 0
    psr = 0;
    
    // Get current FreeRTOS task name
    char task_name[16] = "Unknown";
    extern TaskHandle_t xTaskGetCurrentTaskHandle(void);
    TaskHandle_t current_task = xTaskGetCurrentTaskHandle();
    if (current_task) {
        extern char* pcTaskGetName(TaskHandle_t xTaskToQuery);
        char* name = pcTaskGetName(current_task);
        if (name) {
            strncpy(task_name, name, sizeof(task_name) - 1);
            task_name[sizeof(task_name) - 1] = '\0';
        }
    }
    
    // Format crash description
    char formatted_desc[128];
    if (description) {
        va_list args;
        va_start(args, description);
        vsnprintf(formatted_desc, sizeof(formatted_desc), description, args);
        va_end(args);
    } else {
        formatted_desc[0] = '\0';
    }
    
    // Save crash log
    crash_log_save(type, file, line, formatted_desc, pc, lr, sp, psr, task_name);
    
    // Print crash details
    printf("\n\n=== SYSTEM CRASH ===\n");
    printf("Type: %d\n", type);
    printf("Description: %s\n", formatted_desc);
    printf("File: %s:%d\n", file ? file : "Unknown", line);
    printf("Task: %s\n", task_name);
    printf("PC: 0x%08lX\n", pc);
    printf("LR: 0x%08lX\n", lr);
    printf("SP: 0x%08lX\n", sp);
    printf("PSR: 0x%08lX\n", psr);
    printf("===================\n\n");
    
    // Halt system
    while (1) {
        ;
    }
}
