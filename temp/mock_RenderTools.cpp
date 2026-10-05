// test/test_transition/mock_RenderTools.cpp
#include "../test/test_transition/mock_RenderTools.h"
#include <cstdio>

// Implémentations de Render
Render::Render(Node* node) {
    // Mock
}

void Render::addProperty(const char* name, uint32_t value) {
    // Mock
}

void Render::addProperty(const char* name, const char* value) {
    // Mock
}

void Render::sendJson() {
    // Mock
}

// Implémentations de VisualIndicator
VisualIndicator::VisualIndicator(Node* node) : Render(node) {
    // Mock
}

void VisualIndicator::setMaxValue(uint32_t maxValue) {
    // Mock
}

void VisualIndicator::setMinValue(uint32_t minValue) {
    // Mock
}

// Implémentations de DropDown
DropDown::DropDown(Node* node) : Render(node) {
    // Mock
}

void DropDown::setItems(JsonDocument doc) {
    // Mock
}

// Implémentations de RxIndicator
RxIndicator::RxIndicator(VirtualBooleanOutputNode* existingNode) : node(existingNode) {
    // Mock
}

void RxIndicator::setValue(bool value) {
    // Mock
}

bool RxIndicator::getValue() {
    return false;  // Mock
}

void RxIndicator::setBlinking(bool status) {
    #ifdef DEBUG_MOCK
    printf("[MOCK] RxIndicator::setBlinking: %s\n", status ? "true" : "false");
    #endif
}

void RxIndicator::setColor(const char* color) {  // ← IMPLÉMENTATION ICI
    #ifdef DEBUG_MOCK
    printf("[MOCK] RxIndicator::setColor: %s\n", color);
    #endif
}

VirtualBooleanOutputNode* RxIndicator::getNode() {
    return node;
}