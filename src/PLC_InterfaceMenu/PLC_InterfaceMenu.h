#pragma once

#include <string>
#include <vector>
#include <ArduinoJson.h>
#include "PLC_Persistence/PLC_Persistence.h"

// Json keys
#define PAGES           "pages"
#define PAGE            "page"
#define PAGE_NAME       "PageName"
#define SECTIONS        "sections"
#define SECTION_NAME    "SectionName"
#define NODES_LIST      "NodesList"
#define NODE_NAME       "name"
#define HASH_NAME       "hash"

struct MenuNode {
    uint32_t hash;
    std::string name;
    std::string sectionName;
    bool presentInConfigFile;
};

class PLC_InterfaceMenu {
public:
    PLC_InterfaceMenu();
    virtual ~PLC_InterfaceMenu();
    bool parseFile(const char* jsonContent);
    MenuNode* findNodeByHash(uint32_t hash);

    // Méthodes pour l'itération
    std::vector<MenuNode>::iterator begin() { return nodes.begin(); }
    std::vector<MenuNode>::iterator end() { return nodes.end(); }
    const std::vector<MenuNode>::const_iterator begin() const { return nodes.begin(); }
    const std::vector<MenuNode>::const_iterator end() const { return nodes.end(); }


private:
    std::vector<MenuNode> nodes;
    bool parseSection(JsonObject section);
    
};