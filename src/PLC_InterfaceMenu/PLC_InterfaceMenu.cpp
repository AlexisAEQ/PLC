#include "PLC_InterfaceMenu/PLC_InterfaceMenu.h"

PLC_InterfaceMenu::PLC_InterfaceMenu() : nodes() {  // Initialisation explicite du vector
    Serial.println(F("PLC_InterfaceMenu initialized"));
}

PLC_InterfaceMenu::~PLC_InterfaceMenu() {
    nodes.clear();
}

bool PLC_InterfaceMenu::parseFile(const char* filePath) {
    nodes.clear();

    PLC_Persistence& persistence = PLC_Persistence::getInstance();
    
    // Document to hold the parsed JSON
    SafeJsonDocument doc;
    
    // Read and parse the interface file
    if (!persistence.readJsonFromFile(filePath, doc)) {
        Serial.printf("Failed to read interface file: %s\n", persistence.getLastError());
        return false;
    }
    
    // Check that the document is not empty
    if (doc.isNull()) {
        Serial.println("Interface file is empty or contains invalid JSON");
        return false;
    }

    //Serial.println(F("JSON parsing successful - parsing nodes"));
    // Parcourir les pages
    JsonArray pages = doc[PAGES].as<JsonArray>();
    //Serial.printf("Found %u pages\n", pages.size());



    for (JsonObject page : pages) {
        if (page[PAGE_NAME].isNull()){
            Serial.printf("PLC_InterfaceMenu::parseFile one of pages hasn't key %s\r\n", PAGE_NAME);
            return false;
        }
        String pageName = page[PAGE_NAME].as<String>();
        Serial.printf("Page: %s\n", pageName.c_str());

        if (!page[SECTIONS].is<JsonArray>()){
            Serial.printf("PLC_InterfaceMenu::page %s hasn't key %s\r\n", pageName.c_str(), SECTIONS);
            return false;
        }

        JsonArray sections = page[SECTIONS].as<JsonArray>();
        for (JsonObject section : sections) {
            if (section[SECTION_NAME].isNull()) {
                Serial.printf("PLC_InterfaceMenu::parseSection one of sections of page %s hasn't key %s\r\n", SECTION_NAME, pageName.c_str());
                return false;
            }
            
            //const char* sectionName = section[SECTION_NAME];
            //Serial.printf("Will parse section: %s\n", sectionName);
            if (!parseSection(section)) {
                return false;
            }
        }
    }
    return true;      
}

bool PLC_InterfaceMenu::parseSection(JsonObject section) {
   
    const char* sectionName = section[SECTION_NAME];

    if (!section[NODES_LIST].as<JsonArray>()){
        Serial.printf("PLC_InterfaceMenu: section %s hasn't array key %s\r\n", sectionName, NODES_LIST);
        return false;
    }

    JsonArray nodesList = section[NODES_LIST].as<JsonArray>();
    Serial.printf("Found %d nodes in section %s\n", nodesList.size(), sectionName);


    for (JsonObject node : nodesList) {
        MenuNode menuNode;
        menuNode.presentInConfigFile = false;
        menuNode.sectionName = sectionName;

        if (!node[HASH_NAME].isNull()){
            menuNode.hash = node[HASH_NAME].as<uint32_t>();
            
            if (menuNode.hash == 0) {
                Serial.println();
                Serial.println("🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨");
                Serial.printf("🚨 ATTENTION: HASH = 0 dans interface.json !     🚨\n");
                Serial.printf("🚨 Section: %s                                      🚨\n", sectionName);
                const char* nodeName = node[NODE_NAME].isNull() ? "unknown" : node[NODE_NAME].as<const char*>();
                Serial.printf("🚨 Node: %s                                         🚨\n", nodeName);
                Serial.printf("🚨 L'interface ne fonctionnera pas correctement !   🚨\n");
                Serial.println("🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨");
                Serial.println();
                
                char diagnosticMsg[200];
                snprintf(diagnosticMsg, sizeof(diagnosticMsg), 
                        "GROS WARNING: Hash=0 dans interface.json - Section:'%s', Node:'%s'", 
                        sectionName, nodeName);
                PLC_Tools::logDiagnostic(diagnosticMsg);
            }
                
        } else {
            if (!node[NODE_NAME].isNull()){
                Serial.printf("For section %s, node %s doesn't have hash\r\n", sectionName, node[NODE_NAME].as<const char*>());
                return false;
            }

            Serial.printf("For section %s, one of nodes hasn't key hash\r\n", sectionName);
            return false;
        }

        if (!node[NODE_NAME].isNull()){
            menuNode.name = node[NODE_NAME].as<const char*>();
        } else {
            Serial.printf("For section %s, hash %lu, the key %s doesn't exist\r\n", sectionName, (long unsigned int)menuNode.hash, NODE_NAME);
            return false;
        }
           
        nodes.push_back(menuNode);
    }

    return true;
}

MenuNode* PLC_InterfaceMenu::findNodeByHash(uint32_t hash) {
    for (auto& node : nodes) {
        if (node.hash == hash) {
            return &node;
        }
    }
    return nullptr;
}