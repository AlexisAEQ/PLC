#pragma once
#include "ArduinoJson.h"

/**
 * @file SafeJsonDocument.h
 * @brief Document JSON avec allocateur PSRAM pour ESP32
 */

// ========================================
// FORWARD DECLARATION
// ========================================
class PLC_Tools;  // Forward declaration

// ========================================
// ALLOCATEUR PSRAM POUR JSON (DÉCLARATION)
// ========================================

struct JsonPSRAMAllocator : ArduinoJson::Allocator {
    // Déclarations seulement - implémentées dans le .cpp
    void* allocate(size_t size) override;
    void deallocate(void* pointer) override;
    void* reallocate(void* ptr, size_t new_size) override;
};

// ========================================
// SAFEJSONDOCUMENT
// ========================================

class SafeJsonDocument : public JsonDocument {
private:
    JsonPSRAMAllocator allocator;  // ✅ Nouveau nom

public:
    SafeJsonDocument() : JsonDocument(&allocator) {}
    
    explicit SafeJsonDocument(size_t capacity) : JsonDocument(&allocator) {}
    
    ~SafeJsonDocument() = default;

    // Interdire la copie
    SafeJsonDocument(const SafeJsonDocument&) = delete;
    SafeJsonDocument& operator=(const SafeJsonDocument&) = delete;

    // Autoriser le move
    SafeJsonDocument(SafeJsonDocument&& other) noexcept : JsonDocument(&allocator) {
        JsonDocument::operator=(std::move(other));
    }

    SafeJsonDocument& operator=(SafeJsonDocument&& other) noexcept {
        if (this != &other) {
            JsonDocument::operator=(std::move(other));
        }
        return *this;
    }
};