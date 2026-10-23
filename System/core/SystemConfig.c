#include "SystemConfig.h"
#include "UI_Config.h"
#include "filesystem/fatfs/ff.h"
#include "sys_llapi.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Global configuration instance
static SystemConfig g_config = {0};

// Simple JSON parser helper function
static int find_json_value(const char* json, const char* key, char* value, int max_len) {
    char search_key[64];
    sprintf(search_key, "\"%s\":", key);
    
    char* key_pos = strstr(json, search_key);
    if (!key_pos) return -1;
    
    char* value_start = key_pos + strlen(search_key);
    // Skip spaces
    while (*value_start == ' ') value_start++;
    
    // Handle string value
    if (*value_start == '"') {
        value_start++;
        char* value_end = strchr(value_start, '"');
        if (!value_end) return -1;
        
        int len = value_end - value_start;
        if (len >= max_len) len = max_len - 1;
        strncpy(value, value_start, len);
        value[len] = '\0';
        return 0;
    }
    // Handle numeric/boolean value
    else {
        char* value_end = value_start;
        while (*value_end != '\0' && *value_end != ',' && *value_end != '}' && *value_end != ' ') value_end++;
        
        int len = value_end - value_start;
        if (len >= max_len) len = max_len - 1;
        strncpy(value, value_start, len);
        value[len] = '\0';
        return 0;
    }
}

// Initialize configuration system
void config_init(void) {
    // Set default configuration
    config_reset_to_default();
    
    // Try loading configuration file
    config_load();
}

// Reset configuration to defaults
void config_reset_to_default(void) {
    g_config.language = DEFAULT_LANGUAGE;
    g_config.power_save = DEFAULT_POWER_SAVE;
    g_config.enable_charge = DEFAULT_ENABLE_CHARGE;
    g_config.charging = false;
    g_config.enable_mem_swap = DEFAULT_ENABLE_MEM_SWAP;
    g_config.mem_swap = false;
    g_config.dirty = false;
}

// Load configuration from file
void config_load(void) {
    FIL file;
    FRESULT res;
    
    // Try opening configuration file
    res = f_open(&file, CONFIG_FILE_PATH, FA_READ);
    if (res != FR_OK) {
        // File does not exist, use default configuration
        return;
    }
    
    // Read file contents
    char* buffer = (char*)malloc(1024);
    if (!buffer) {
        f_close(&file);
        return;
    }
    
    UINT bytes_read;
    res = f_read(&file, buffer, 1023, &bytes_read);
    buffer[bytes_read] = '\0';
    f_close(&file);
    
    if (res != FR_OK) {
        free(buffer);
        return;
    }
    
    // Parse JSON
    char value[32];
    
    // Parse language setting
    if (find_json_value(buffer, "language", value, sizeof(value)) == 0) {
        g_config.language = atoi(value);
        if (g_config.language != UI_LANG_EN && g_config.language != UI_LANG_CN) {
            g_config.language = DEFAULT_LANGUAGE;
        }
    }
    
    // Parse power save mode
    if (find_json_value(buffer, "power_save", value, sizeof(value)) == 0) {
        g_config.power_save = value[0];
        if (g_config.power_save != ' ' && g_config.power_save != 'S' && g_config.power_save != 'L') {
            g_config.power_save = DEFAULT_POWER_SAVE;
        }
    }
    
    // Parse charging setting
    if (find_json_value(buffer, "enable_charge", value, sizeof(value)) == 0) {
        g_config.enable_charge = (strcmp(value, "true") == 0);
    }
    
    // Parse memory swap setting
    if (find_json_value(buffer, "enable_mem_swap", value, sizeof(value)) == 0) {
        g_config.enable_mem_swap = (strcmp(value, "true") == 0);
    }
    
    free(buffer);
}

// Save configuration to file
void config_save(void) {
    FIL file;
    FRESULT res;
    
    // Create JSON string
    char json_buffer[512];
    sprintf(json_buffer, 
        "{\n"
        "  \"language\": %d,\n"
        "  \"power_save\": \"%c\",\n"
        "  \"enable_charge\": %s,\n"
        "  \"enable_mem_swap\": %s\n"
        "}",
        g_config.language,
        g_config.power_save,
        g_config.enable_charge ? "true" : "false",
        g_config.enable_mem_swap ? "true" : "false"
    );
    
    // Open file for writing
    res = f_open(&file, CONFIG_FILE_PATH, FA_CREATE_ALWAYS | FA_WRITE);
    if (res != FR_OK) {
        return;
    }
    
    // Write JSON data
    UINT bytes_written;
    res = f_write(&file, json_buffer, strlen(json_buffer), &bytes_written);
    f_close(&file);
    
    // If write failed, delete potentially corrupted file
    if (res != FR_OK || bytes_written != strlen(json_buffer)) {
        f_unlink(CONFIG_FILE_PATH);
    }
}

// Get pointer to configuration struct
SystemConfig* config_get(void) {
    return &g_config;
}

// Getters and setters for specific configuration items
int config_get_language(void) {
    return g_config.language;
}

void config_set_language(int lang) {
    if (lang == UI_LANG_EN || lang == UI_LANG_CN) {
        g_config.language = lang;
        config_save();
    }
}

char config_get_power_save(void) {
    return g_config.power_save;
}

void config_set_power_save(char mode) {
    if (mode == ' ' || mode == 'S' || mode == 'L') {
        g_config.power_save = mode;
        config_save();
    }
}

bool config_get_enable_charge(void) {
    return g_config.enable_charge;
}

void config_set_enable_charge(bool enable) {
    g_config.enable_charge = enable;
    config_save();
}

// Get/set charging state
bool config_get_charging(void) {
    return g_config.charging;
}

void config_set_charging(bool charging) {
    g_config.charging = charging;
    config_save();
}

bool config_get_enable_mem_swap(void) {
    return g_config.enable_mem_swap;
}

void config_set_enable_mem_swap(bool enable) {
    g_config.enable_mem_swap = enable;
    config_save();
}
