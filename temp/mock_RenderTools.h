// test/test_transition/mock_RenderTools.h
#ifndef RENDER_TOOLS_H
#define RENDER_TOOLS_H

#include <ArduinoJson.h>

class Node;
class VirtualBooleanOutputNode;

class Render {
public:
    Render(Node* node);
    void addProperty(const char* name, uint32_t value);
    void addProperty(const char* name, const char* value);
    void sendJson();
};

class VisualIndicator : public Render {
public:
    VisualIndicator(Node* node);
    void setMaxValue(uint32_t maxValue);
    void setMinValue(uint32_t minValue);
};

class DropDown : public Render {
public:
    DropDown(Node* node);
    void setItems(JsonDocument doc);
};

class RxIndicator {
public:
    RxIndicator(VirtualBooleanOutputNode* existingNode);
    void setValue(bool value);
    bool getValue();
    void setBlinking(bool status);
    void setColor(const char* color);  // ← Déclaration seulement
    VirtualBooleanOutputNode* getNode();
    
private:
    VirtualBooleanOutputNode* node;
};

#endif // RENDER_TOOLS_H