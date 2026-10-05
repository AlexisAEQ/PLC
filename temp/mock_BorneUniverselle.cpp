#include "../test/test_transition/mock_BorneUniverselle.h"

BorneUniverselle* BorneUniverselle::getInstance() {
    static BorneUniverselle instance;
    return &instance;
}

bool BorneUniverselle::sendTextToClient(const char *text) {
    return true;
}

void BorneUniverselle::prepareMessage(unsigned char type, const char* message) {}
void BorneUniverselle::refresHardwareInputs() {}
void BorneUniverselle::clearAllClientNotifications() {}

void BorneUniverselle::setPlcBroken(const char* message) {
    plcBroken = true;
}

bool BorneUniverselle::isPlcBroken() {
    return plcBroken;
}

Node* BorneUniverselle::findNode(const char* context, uint32_t hash) {
    return nullptr;
}

bool BorneUniverselle::getLogOnDiagnosticFile(){
    return logOnDiagnosticFile;
}

void BorneUniverselle::setInitialStateLoadedCallback(std::function<void()> callback){}

void  BorneUniverselle::enableInterface(bool status){}