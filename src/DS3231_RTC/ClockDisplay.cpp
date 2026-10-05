#include "DS3231_RTC/ClockDisplay.h"
#include "DS3231_RTC/DS3231_RTC.h"
#include "Node/Node.h"
#include "RenderTools/RenderTools.h"

void ClockDisplay::attach(VirtualTextOutputNode* node) {
    if (!node) {
        return;
    }

    if (!RTC_Service::getInstance()->hasHardwareClock()) {
        Render clockRender(node);
        clockRender.setDisabled(true);
        Serial.println("[ClockDisplay] Aucune horloge matérielle détectée : champ horloge masqué");
    }
}
