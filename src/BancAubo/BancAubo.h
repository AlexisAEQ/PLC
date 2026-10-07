// =============================================================================
// BancAubo.h — généré par PLC Studio (tools/plc-studio)
// NE PAS MODIFIER À LA MAIN : ce fichier est régénéré depuis projets/BancAubo.plc.json
// =============================================================================
#pragma once

#include "ArduinoJson.h"
#include <memory>
#include <string.h>
#include "Node/Node.h"
#include "PLC_Persistence/PLC_Persistence.h"
#include "PLC_CommonTypes/PLC_CommonTypes.h"
#include "BusinessLogic/BusinessLogic.h"
#include "PLC_Tools/PLC_Tools.h"
#include "BusinessLogic/BusinessLogic.inl"

// Identifiants des nœuds (CRC-32 de "<id><section>", voir data/config.json).
namespace BancAuboNodes {
    constexpr uint32_t H_Sel_start = 1501735426u;  // Automate Entrees / id 1
    constexpr uint32_t H_Sel_stop = 169406854u;  // Automate Entrees / id 2
    constexpr uint32_t H_Rob_marche = 2378054341u;  // Automate Entrees / id 3
    constexpr uint32_t H_Rob_pause = 2905360014u;  // Automate Entrees / id 4
    constexpr uint32_t H_Rob_sans_defaut = 713723341u;  // Automate Entrees / id 5
    constexpr uint32_t H_Rob_operable = 2031146569u;  // Automate Entrees / id 6
    constexpr uint32_t H_Cmd_start = 903971659u;  // Automate Sorties / id 1
    constexpr uint32_t H_Cmd_stop = 1719392463u;  // Automate Sorties / id 2
    constexpr uint32_t H_Mb_pret = 2906946473u;  // Robot Retours / id 1
    constexpr uint32_t H_Mb_en_cours = 3508887154u;  // Robot Retours / id 2
    constexpr uint32_t H_Mb_fini = 1277869828u;  // Robot Retours / id 3
    constexpr uint32_t H_Mb_defaut = 703010244u;  // Robot Retours / id 4
    constexpr uint32_t H_Mb_echo_prog = 3035165874u;  // Robot Retours / id 5
    constexpr uint32_t H_Mb_vie = 3364475241u;  // Robot Retours / id 6
    constexpr uint32_t H_Pos_X = 1396041419u;  // Robot Positions / id 11
    constexpr uint32_t H_Pos_Y = 2063422009u;  // Robot Positions / id 12
    constexpr uint32_t H_Pos_Z = 3566584744u;  // Robot Positions / id 13
    constexpr uint32_t H_Tempo_reponse = 885864203u;  // Parametres / id 1
    constexpr uint32_t H_Etat_programme = 2551538582u;  // Messages / id 1
    constexpr uint32_t H_Defaut_cmd = 581731598u;  // Voyants / id 1
    constexpr uint32_t H_SYS_STATE = 3958179723u;
}

class BancAubo : public BusinessLogic {
public:
    enum class State : int {
        UNDEFINED    = static_cast<int>(BaseState::UNDEFINED),
        INITIALIZING = static_cast<int>(BaseState::INITIALIZING),
        STOP         = static_cast<int>(BaseState::STOP),
        IDLE         = static_cast<int>(BaseState::IDLE),       // grafcet en situation initiale
        EMERGENCY    = static_cast<int>(BaseState::EMERGENCY),
        JOGGING      = static_cast<int>(BaseState::JOGGING),
        HOMING       = 101,
        RUNNING      = 102,                                      // grafcet en cycle
        RESETTING    = 103
    };

    static constexpr const char* PARAMS_FILE = "/BancAubo.json";

    BancAubo();
    virtual ~BancAubo();

    // Appelées par main.cpp
    bool initializePointers(const char* context);
    bool startStateMachine();
    void printPersistance();
#ifdef PERF_MONITOR
    bool isPmActive() const { return false; }
    void pmAccumulateRefresh(uint32_t) {}
#endif

    // BusinessLogic
    bool doBusinessLogic() override;
    const char* stateToString(int state) const override;
    bool canDeleteFile(const char* path) override;
    bool validateConfigFile(const char* path, const JsonDocument& doc, String& errorMsg) override;
    void initialStateLoadedHandler();

protected:
    StateType classifyState(int state) const override;
    bool shouldShowClock() const override;
    void onStateEnter(int newState, int oldState) override;
    void onStateExit(int oldState, int newState) override;
    void onFileSaved(const char* path, bool success) override;

private:
    // ---- Nœuds
    BooleanInputNode* n_Sel_start = nullptr;
    BooleanInputNode* n_Sel_stop = nullptr;
    BooleanInputNode* n_Rob_marche = nullptr;
    BooleanInputNode* n_Rob_pause = nullptr;
    BooleanInputNode* n_Rob_sans_defaut = nullptr;
    BooleanInputNode* n_Rob_operable = nullptr;
    BooleanOutputNode* n_Cmd_start = nullptr;
    BooleanOutputNode* n_Cmd_stop = nullptr;
    Uint16InputNode* n_Mb_pret = nullptr;
    Uint16InputNode* n_Mb_en_cours = nullptr;
    Uint16InputNode* n_Mb_fini = nullptr;
    Uint16InputNode* n_Mb_defaut = nullptr;
    Uint16InputNode* n_Mb_echo_prog = nullptr;
    Uint16InputNode* n_Mb_vie = nullptr;
    Uint16InputNode* n_Pos_X = nullptr;
    Uint16InputNode* n_Pos_Y = nullptr;
    Uint16InputNode* n_Pos_Z = nullptr;
    Uint32OutputNode* n_Tempo_reponse = nullptr;
    VirtualTextOutputNode* n_Etat_programme = nullptr;
    BooleanOutputNode* n_Defaut_cmd = nullptr;
    VirtualTextOutputNode* sysStateNode = nullptr;

    // ---- Grafcet (6 étapes, 10 transitions)
    static constexpr int STEP_COUNT = 6;
    static constexpr int TRANSITION_COUNT = 10;
    static constexpr int EDGE_COUNT = 1;
    static constexpr int MAX_EVOLUTIONS = 16;  // recherche de stabilité
    static const int STEP_NUMBERS[STEP_COUNT];
    bool X[STEP_COUNT] = {};          // étapes actives
    bool Xnew[STEP_COUNT] = {};       // étapes activées, actions à l'activation à exécuter
    uint32_t Xstart[STEP_COUNT] = {}; // instant d'activation
    bool S[EDGE_COUNT] = {};          // opérandes des fronts, capturés en début de cycle
    bool E[EDGE_COUNT] = {};          // mêmes opérandes au cycle précédent
    uint32_t now = 0;
    uint32_t emergencyClearSince = 0;
    bool unstableReported = false;
    bool initialised = false;

    // ---- Paramètres sauvegardés dans /BancAubo.json
    int32_t init_Tempo_reponse = 5000;
    int32_t saved_Tempo_reponse = 5000;

    // ---- Accès aux variables
    inline bool v_Sel_start() { return n_Sel_start->getValue(); }
    inline bool v_Sel_stop() { return n_Sel_stop->getValue(); }
    inline bool v_Rob_marche() { return n_Rob_marche->getValue(); }
    inline bool v_Rob_pause() { return n_Rob_pause->getValue(); }
    inline bool v_Rob_sans_defaut() { return n_Rob_sans_defaut->getValue(); }
    inline bool v_Rob_operable() { return n_Rob_operable->getValue(); }
    inline bool v_Cmd_start() { return n_Cmd_start->getValue(); }
    inline void set_Cmd_start(bool x) { n_Cmd_start->setValue(x); }
    inline bool v_Cmd_stop() { return n_Cmd_stop->getValue(); }
    inline void set_Cmd_stop(bool x) { n_Cmd_stop->setValue(x); }
    inline int32_t v_Mb_pret() { return (int32_t)n_Mb_pret->getValue(); }
    inline int32_t v_Mb_en_cours() { return (int32_t)n_Mb_en_cours->getValue(); }
    inline int32_t v_Mb_fini() { return (int32_t)n_Mb_fini->getValue(); }
    inline int32_t v_Mb_defaut() { return (int32_t)n_Mb_defaut->getValue(); }
    inline int32_t v_Mb_echo_prog() { return (int32_t)n_Mb_echo_prog->getValue(); }
    inline int32_t v_Mb_vie() { return (int32_t)n_Mb_vie->getValue(); }
    inline int32_t v_Pos_X() { return (int32_t)(int16_t)n_Pos_X->getValue(); }
    inline int32_t v_Pos_Y() { return (int32_t)(int16_t)n_Pos_Y->getValue(); }
    inline int32_t v_Pos_Z() { return (int32_t)(int16_t)n_Pos_Z->getValue(); }
    inline int32_t v_Tempo_reponse() { return n_Tempo_reponse->getValueAsInt32(); }
    inline void set_Tempo_reponse(int32_t x) { { int32_t _v = x; if (n_Tempo_reponse->getValueAsInt32() != _v) n_Tempo_reponse->setValueFromInt32(_v); } }
    inline const char* v_Etat_programme() { return (const char*)n_Etat_programme->getValue(); }
    inline void set_Etat_programme(const char* x) { { const char* _v = x; if (strcmp(n_Etat_programme->getValue(), _v) != 0) n_Etat_programme->setValue(_v); } }
    inline bool v_Defaut_cmd() { return n_Defaut_cmd->getValue(); }
    inline void set_Defaut_cmd(bool x) { n_Defaut_cmd->setValue(x); }

    // ---- Fonctions internes
    State cur() const { return static_cast<State>(BusinessLogic::getCurrentState()); }
    void go(State s) { if (cur() != s) transitionWithContext(static_cast<int>(s), 0); }
    int32_t stepTime(int i) const { return X[i] ? (int32_t)(now - Xstart[i]) : 0; }
    void activateStep(int i) { X[i] = true; Xnew[i] = true; Xstart[i] = now; }
    static bool bankBit(ModbusReadMultipleInputsRegistersNode* b, uint8_t bit) {
        if (!b) return false;
        auto* x = b->getBit(bit);
        return x ? x->getValue() : false;
    }
    static int32_t plcDivI(int32_t a, int32_t b) { return b == 0 ? 0 : a / b; }
    static float plcDivF(float a, float b) { return b == 0.0f ? 0.0f : a / b; }
    static int32_t plcMod(int32_t a, int32_t b) { return b == 0 ? 0 : a % b; }
    static int32_t plcMinI(int32_t a, int32_t b) { return a < b ? a : b; }
    static int32_t plcMaxI(int32_t a, int32_t b) { return a > b ? a : b; }
    static float plcMinF(float a, float b) { return a < b ? a : b; }
    static float plcMaxF(float a, float b) { return a > b ? a : b; }
    static int32_t plcAbsI(int32_t a) { return a < 0 ? -a : a; }
    static float plcAbsF(float a) { return a < 0 ? -a : a; }

    void grafcetReset();
    void grafcetClear();
    bool grafcetInInitialSituation() const;
    void grafcetActivationActions();
    bool grafcetEvolve();
    void computeOutputs();
    void captureEdges();
    void updateEdges();
    bool emergencyCondition();
    void handleInitializing();
    void handleStop();
    void handleRun();
    void handleEmergency();
    void handleResetting();
    void handleHoming();
    void handleJogging();
    void updateStateDisplay();
    bool readParams();
    bool saveParams();
    void checkParams();
    void applyParamsToNodes();
    bool axesMoving() const { return false; }
};

