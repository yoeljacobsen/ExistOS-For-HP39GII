#ifndef SERVICES_H
#define SERVICES_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// USB service
void vTaskTinyUSB(void *pvParameters);

// MTD service
void vMTDSvc(void *pvParameters);

// FTL service
void vFTLSvc(void *pvParameters);

// Keyboard service
void vKeysSvc(void *pvParameters);

// Virtual memory manager service
void vVMMgrSvc(void *pvParameters);

// LLAPI service
void vLLAPISvc(void *pvParameters);

// Display service
void vDispSvc(void *pvParameters);

// External variable declarations
extern bool g_vm_inited;

#ifdef __cplusplus
}
#endif

#endif /* SERVICES_H */