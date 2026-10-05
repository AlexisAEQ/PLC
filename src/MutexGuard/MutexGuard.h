#pragma once
class MutexGuard {
    SemaphoreHandle_t& mutex;
    const char* mutexName;
    const char *context;
    bool acquired;
    //unsigned long startTime;
    
    // Empêcher la copie  
    MutexGuard(const MutexGuard&) = delete;
    MutexGuard& operator=(const MutexGuard&) = delete;

public:
    explicit MutexGuard(SemaphoreHandle_t& m, const char* _mutexName, const char *_context) 
        : mutex(m), mutexName(_mutexName), context(_context), acquired(false) {

        //startTime = millis();
        //Serial.printf("%lu:: Attempting to take mutex %s in context %s\n", startTime, mutexName, context);

        // Tenter de prendre le mutex avec un timeout
        acquired = xSemaphoreTakeRecursive(mutex, pdMS_TO_TICKS(300)) == pdTRUE;

        if (!acquired) {
            Serial.printf("%lu:: Failed to acquire mutex %s in context: %s\n", millis(), mutexName, context);
        }
    }

    bool isAcquired() const { return acquired;}

    ~MutexGuard() {
        if (acquired) {
            //unsigned long releaseTime = millis();
            //Serial.printf("%lu:: Released mutex %s in context: %s (Held for: %lu ms)\n", releaseTime, mutexName, context, releaseTime - startTime);
            xSemaphoreGiveRecursive(mutex);
        }
    }
};