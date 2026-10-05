// test/test_transition/mock_MyModbus.h
#ifndef MY_MODBUS_H
#define MY_MODBUS_H

#include <cstdint>
#include <cstddef>

// Mock pour ModbusRTU si nécessaire
class ModbusRTU;

// Mock namespace Modbus pour les types
namespace Modbus {
    enum ResultCode {
        EX_SUCCESS = 0x00,
        EX_ILLEGAL_FUNCTION = 0x01,
        EX_ILLEGAL_ADDRESS = 0x02,
        EX_ILLEGAL_VALUE = 0x03,
        EX_SLAVE_FAILURE = 0x04,
        EX_ACKNOWLEDGE = 0x05,
        EX_SLAVE_DEVICE_BUSY = 0x06,
        EX_MEMORY_PARITY_ERROR = 0x08,
        EX_PATH_UNAVAILABLE = 0x0A,
        EX_DEVICE_FAILED_TO_RESPOND = 0x0B,
        EX_TIMEOUT = 0xE0,
        EX_CONNECTION = 0xE1,
        EX_NO_REPLY = 0xE2,
        EX_GENERAL = 0xFF
    };
}

class MyModbus {
public:
    struct ModbusStats {
        uint32_t crcErrors = 0;
        uint32_t timeoutErrors = 0;
        uint32_t connectionErrors = 0;
        uint32_t hardErrors = 0;
        uint32_t totalErrors = 0;
        uint32_t successfulTransactions = 0;
        uint32_t totalTransactions = 0;
        uint32_t lastErrorTime = 0;
        uint32_t lastSuccessTime = 0;
        uint32_t maxTransactionTime = 0;
        uint32_t lastTransactionTime = 0;
        Modbus::ResultCode lastErrorCode = Modbus::EX_SUCCESS;
    };

    // Singleton
    MyModbus(const MyModbus&) = delete;
    void operator=(const MyModbus&) = delete;
    
    static MyModbus& getInstance();
    
    // Configuration Modbus
    void setModbus(ModbusRTU* _modbus);
    ModbusRTU* getModbus();
    
    // Gestion du timeout
    void setTimeout(uint32_t ms);
    uint32_t getTimeout();
    void resetTimeout();
    
    // Gestion du bus Modbus
    void waitUntilModbusFree();
    bool waitEndTransaction();
    bool isModbusFree();
    Modbus::ResultCode getLastEvent();
    
    // Gestion des statistiques
    const ModbusStats& getStats();
    void resetStats();
    void getErrorReport(char* buffer, size_t size);
    static bool cbRead(Modbus::ResultCode event, uint16_t transactionId, void* data);
    static bool cbWrite(Modbus::ResultCode event, uint16_t transactionId, void* data);
    
    // Gestion des messages
    typedef void (*MessageCallback)(uint8_t severity, const char* message);
    void setShowMessages(bool status);
    void setMessageCallback(MessageCallback callback);

private:
    MyModbus();
    
    ModbusRTU* modbus;
    uint32_t lastTransaction;
    Modbus::ResultCode lastEvent;
    uint32_t lastProblem;
    bool busy;
    ModbusStats stats;
    uint32_t transactionTimeout;
    bool isShowMessages;
    MessageCallback messageCallback;
    
    void processEvent(Modbus::ResultCode event);
    void handleError(Modbus::ResultCode event);
    void getModbusErrorMessage(Modbus::ResultCode event, char* message, size_t size);
    void showMessages(const char* text);
};

#endif // MY_MODBUS_H