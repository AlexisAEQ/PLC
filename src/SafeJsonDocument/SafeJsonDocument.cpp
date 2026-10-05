#include "SafeJsonDocument/SafeJsonDocument.h"
#include "PLC_Tools/PLC_Tools.h"  // ✅ Ici on peut l'inclure !
#include <esp_heap_caps.h>

// ========================================
// IMPLÉMENTATION JsonPSRAMAllocator
// ========================================

void* JsonPSRAMAllocator::allocate(size_t size) {
    void* ptr = PLC_Tools::allocateMemory(size);
    
    #ifdef DEBUG_JSON_ALLOCATOR
    if (ptr) {
        Serial.printf("JSON Alloc: %zu bytes at %p\n", size, ptr);
    } else {
        Serial.printf("JSON Alloc FAILED: %zu bytes\n", size);
    }
    #endif
    
    return ptr;
}

void JsonPSRAMAllocator::deallocate(void* pointer) {
    if (pointer) {
        #ifdef DEBUG_JSON_ALLOCATOR
        Serial.printf("JSON Free: %p\n", pointer);
        #endif
        
        PLC_Tools::freeMemory(pointer);
    }
}

void* JsonPSRAMAllocator::reallocate(void* ptr, size_t new_size) {
    #ifdef DEBUG_JSON_ALLOCATOR
    Serial.printf("JSON Realloc: %p → %zu bytes\n", ptr, new_size);
    #endif
    
    if (!ptr) {
        return allocate(new_size);
    }

    if (new_size == 0) {
        deallocate(ptr);
        return nullptr;
    }

    // Allouer nouveau bloc
    void* new_ptr = allocate(new_size);
    if (!new_ptr) {
        return nullptr;
    }

    // Copier ancien contenu
    size_t old_size = heap_caps_get_allocated_size(ptr);
    size_t copy_size = (old_size < new_size) ? old_size : new_size;
    memcpy(new_ptr, ptr, copy_size);

    // Libérer ancien
    deallocate(ptr);

    return new_ptr;
}