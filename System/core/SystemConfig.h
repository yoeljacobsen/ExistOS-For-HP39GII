#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Configuration file path
#define CONFIG_FILE_PATH "/existos_config.json"

// Default configuration values
#define DEFAULT_LANGUAGE UI_LANG_EN
#define DEFAULT_POWER_SAVE ' '  // ' ' = disabled, 'S' = power save, 'L' = ultra power save
#define DEFAULT_ENABLE_CHARGE false
#define DEFAULT_ENABLE_MEM_SWAP false

// System configuration structure
typedef struct {
    int language;           // Language setting (0 = English, 1 = Secondary/Legacy)
    char power_save;        // Power save mode (' ' = off, 'S' = low power, 'L' = ultra low power)
    bool enable_charge;     // Enable battery charging
    bool charging;          // Current charging status
    bool enable_mem_swap;   // Enable virtual memory swap
    bool mem_swap;          // Memory swap status
    bool dirty;             // Configuration modified but not saved
} SystemConfig;

// Configuration management functions
void config_init(void);
void config_load(void);
void config_save(void);
void config_reset_to_default(void);
SystemConfig* config_get(void);

// Getters and setters for specific configuration items
int config_get_language(void);
void config_set_language(int lang);
char config_get_power_save(void);
void config_set_power_save(char mode);
bool config_get_enable_charge(void);
void config_set_enable_charge(bool enable);
// Get/set charging status
bool config_get_charging(void);
void config_set_charging(bool charging);

bool config_get_enable_mem_swap(void);
void config_set_enable_mem_swap(bool enable);

#ifdef __cplusplus
}
#endif
