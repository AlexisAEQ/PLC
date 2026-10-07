// =============================================================================
// BancAubo.cpp — généré par PLC Studio (tools/plc-studio)
// NE PAS MODIFIER À LA MAIN : ce fichier est régénéré depuis projets/BancAubo.plc.json
// =============================================================================
#include "BancAubo.h"

const int BancAubo::STEP_NUMBERS[BancAubo::STEP_COUNT] = { 0, 1, 2, 3, 4, 5 };

BancAubo::BancAubo() : BusinessLogic() {
    currentState = static_cast<int>(State::UNDEFINED);
    previousState = static_cast<int>(State::UNDEFINED);
    stateStartTime = millis();
    BorneUniverselle::getInstance()->setInitialStateLoadedCallback([this]() { this->initialStateLoadedHandler(); });
    PLC_Persistence::getInstance().registerDeferredSave(PARAMS_FILE, [this]() { return this->saveParams(); });
    readParams();
}

BancAubo::~BancAubo() {}

bool BancAubo::initializePointers(const char* context) {
    initNodePtr(n_Sel_start, BancAuboNodes::H_Sel_start, context);
    initNodePtr(n_Sel_stop, BancAuboNodes::H_Sel_stop, context);
    initNodePtr(n_Rob_marche, BancAuboNodes::H_Rob_marche, context);
    initNodePtr(n_Rob_pause, BancAuboNodes::H_Rob_pause, context);
    initNodePtr(n_Rob_sans_defaut, BancAuboNodes::H_Rob_sans_defaut, context);
    initNodePtr(n_Rob_operable, BancAuboNodes::H_Rob_operable, context);
    initNodePtr(n_Cmd_start, BancAuboNodes::H_Cmd_start, context);
    initNodePtr(n_Cmd_stop, BancAuboNodes::H_Cmd_stop, context);
    initNodePtr(n_Mb_pret, BancAuboNodes::H_Mb_pret, context);
    initNodePtr(n_Mb_en_cours, BancAuboNodes::H_Mb_en_cours, context);
    initNodePtr(n_Mb_fini, BancAuboNodes::H_Mb_fini, context);
    initNodePtr(n_Mb_defaut, BancAuboNodes::H_Mb_defaut, context);
    initNodePtr(n_Mb_echo_prog, BancAuboNodes::H_Mb_echo_prog, context);
    initNodePtr(n_Mb_vie, BancAuboNodes::H_Mb_vie, context);
    initNodePtr(n_Pos_X, BancAuboNodes::H_Pos_X, context);
    initNodePtr(n_Pos_Y, BancAuboNodes::H_Pos_Y, context);
    initNodePtr(n_Pos_Z, BancAuboNodes::H_Pos_Z, context);
    initNodePtr(n_Tempo_reponse, BancAuboNodes::H_Tempo_reponse, context);
    initNodePtr(n_Etat_programme, BancAuboNodes::H_Etat_programme, context);
    initNodePtr(n_Defaut_cmd, BancAuboNodes::H_Defaut_cmd, context);
    initNodePtr(sysStateNode, BancAuboNodes::H_SYS_STATE, context);
    if (BorneUniverselle::getInstance()->isPlcBroken()) {
        Serial.println("BancAubo: erreur pendant l'initialisation des pointeurs");
        return false;
    }
    applyParamsToNodes();
    return true;
}

bool BancAubo::startStateMachine() {
    go(State::INITIALIZING);
    return true;
}

void BancAubo::initialStateLoadedHandler() {
    if (cur() == State::EMERGENCY) BorneUniverselle::prepareMessage(ERROR, "Machine en arrêt d'urgence");
    updateStateDisplay();
    BorneUniverselle::enableInterface(true);
    BorneUniverselle::getInstance()->clearAllClientNotifications();
    initialised = true;
}

bool BancAubo::doBusinessLogic() {
    now = millis();
    if (BorneUniverselle::getInstance()->isPlcBroken()) return false;
    captureEdges();

    // Arrêt d'urgence : prioritaire sur tous les modes (y compris pendant le réarmement).
    State s = cur();
    if (s != State::EMERGENCY && s != State::UNDEFINED) {
        if (emergencyCondition()) {
            char reason[160];
            snprintf(reason, sizeof(reason), "Arrêt d'urgence : %s", "");
            BorneUniverselle::prepareMessage(ERROR, reason);
            PLC_Tools::logDiagnostic(reason);
            go(State::EMERGENCY);
        }
    }

    switch (cur()) {
        case State::UNDEFINED:    go(State::INITIALIZING); break;
        case State::INITIALIZING: handleInitializing(); break;
        case State::STOP:         handleStop(); break;
        case State::HOMING:       handleHoming(); break;
        case State::IDLE:
        case State::RUNNING:      handleRun(); break;
        case State::JOGGING:      handleJogging(); break;
        case State::EMERGENCY:    handleEmergency(); break;
        case State::RESETTING:    handleResetting(); break;
        default:                  go(State::EMERGENCY); break;
    }

    computeOutputs();
    checkParams();
    updateEdges();
    return true;
}

bool BancAubo::emergencyCondition() {
    return false;
}

void BancAubo::handleInitializing() {
    go(State::STOP);
}

void BancAubo::handleStop() {
    if (false) return;  // arrêt maintenu
    if (!(true)) return;
    grafcetReset();
    go(State::IDLE);
}

void BancAubo::handleHoming() {
    go(State::STOP);
}

void BancAubo::handleRun() {
    // Actions à l'activation en attente (étapes initiales après une remise à zéro).
    grafcetActivationActions();
    if (cur() == State::EMERGENCY) return;  // déclenchée par une action (ex. cible hors course)
    // Recherche de stabilité : on franchit tant que possible dans le même cycle ; les étapes
    // instables exécutent leurs actions à l'activation mais pas leurs actions continues.
    for (int iter = 0; iter < MAX_EVOLUTIONS; iter++) {
        if (!grafcetEvolve()) break;
        if (iter == 0) for (int k = 0; k < EDGE_COUNT; k++) E[k] = S[k];  // un front ne vaut qu'une fois
        grafcetActivationActions();
        if (cur() == State::EMERGENCY) return;
        if (iter == MAX_EVOLUTIONS - 1 && !unstableReported) {
            BorneUniverselle::prepareMessage(WARNING, "Grafcet instable : boucle de franchissements sans fin");
            unstableReported = true;
        }
    }
    for (int i = 0; i < STEP_COUNT; i++) Xnew[i] = false;
    go(grafcetInInitialSituation() && !axesMoving() ? State::IDLE : State::RUNNING);
    updateStateDisplay();
}

void BancAubo::handleJogging() {
    go(State::STOP);
}

void BancAubo::handleEmergency() {
    // Seule la condition d'urgence bloque la sortie : une alarme d'axe est justement
    // effacée par le réarmement (startReset). Si elle persiste, l'urgence se redéclenche.
    if (emergencyCondition()) { emergencyClearSince = 0; return; }
    if (emergencyClearSince == 0) emergencyClearSince = now ? now : 1;
    if (now - emergencyClearSince < 1000) return;   // sortie automatique après 1 s
    BorneUniverselle::prepareMessage(SUCCESS, "Arrêt d'urgence acquitté");
    go(State::STOP);
}

void BancAubo::handleResetting() {
    go(State::STOP);
}

void BancAubo::grafcetClear() {
    for (int i = 0; i < STEP_COUNT; i++) { X[i] = false; Xnew[i] = false; Xstart[i] = 0; }
    unstableReported = false;
}

void BancAubo::grafcetReset() {
    grafcetClear();
    activateStep(0);  // étape 0 initiale
}

bool BancAubo::grafcetInInitialSituation() const {
    return X[0] && !X[1] && !X[2] && !X[3] && !X[4] && !X[5];
}

void BancAubo::grafcetActivationActions() {
    if (Xnew[0]) {  // étape 0 — Programme arrêté
        set_Etat_programme("Arrêté");
    }
    if (Xnew[1]) {  // étape 1 — Départ programme
        set_Etat_programme("Démarrage");
    }
    if (Xnew[2]) {  // étape 2 — Programme en cours
        set_Etat_programme("En cours");
    }
    if (Xnew[3]) {  // étape 3 — Arrêt programme
        set_Etat_programme("Arrêt demandé");
    }
    if (Xnew[4]) {  // étape 4 — Attente sélecteur
        set_Etat_programme("Terminé");
        BorneUniverselle::prepareMessage(INFO, "Programme robot terminé");
    }
    if (Xnew[5]) {  // étape 5 — Robot muet
        set_Etat_programme("Défaut");
        BorneUniverselle::prepareMessage(ERROR, "Robot ne répond pas");
    }
    for (int i = 0; i < STEP_COUNT; i++) Xnew[i] = false;
}

bool BancAubo::grafcetEvolve() {
    bool fire[TRANSITION_COUNT] = {};
    bool any = false;
    fire[0] = X[0] && ((((v_Sel_start() && !v_Sel_stop()) && !v_Rob_marche()) && v_Rob_operable()) && v_Rob_sans_defaut());  // T0: Sel_start ET NON Sel_stop ET NON Rob_marche ET Rob_operable ET Rob_sans_defaut
    any = any || fire[0];
    fire[1] = X[0] && v_Rob_marche();  // T1: Rob_marche
    any = any || fire[1];
    fire[2] = X[1] && v_Rob_marche();  // T2: Rob_marche
    any = any || fire[2];
    fire[3] = X[1] && (!v_Rob_marche() && (stepTime(1) >= v_Tempo_reponse()));  // T3: NON Rob_marche ET X1.t >= Tempo_reponse
    any = any || fire[3];
    fire[4] = X[2] && v_Sel_stop();  // T4: Sel_stop
    any = any || fire[4];
    fire[5] = X[2] && (!v_Rob_marche() && !v_Sel_stop());  // T5: NON Rob_marche ET NON Sel_stop
    any = any || fire[5];
    fire[6] = X[3] && !v_Rob_marche();  // T6: NON Rob_marche
    any = any || fire[6];
    fire[7] = X[3] && (v_Rob_marche() && (stepTime(3) >= v_Tempo_reponse()));  // T7: Rob_marche ET X3.t >= Tempo_reponse
    any = any || fire[7];
    fire[8] = X[4] && !v_Sel_start();  // T8: NON Sel_start
    any = any || fire[8];
    fire[9] = X[5] && !v_Sel_start();  // T9: NON Sel_start
    any = any || fire[9];
    if (!any) return false;
    // Désactivation des étapes amont, puis activation des étapes aval (prioritaire).
    if (fire[0]) { X[0] = false; }
    if (fire[1]) { X[0] = false; }
    if (fire[2]) { X[1] = false; }
    if (fire[3]) { X[1] = false; }
    if (fire[4]) { X[2] = false; }
    if (fire[5]) { X[2] = false; }
    if (fire[6]) { X[3] = false; }
    if (fire[7]) { X[3] = false; }
    if (fire[8]) { X[4] = false; }
    if (fire[9]) { X[5] = false; }
    if (fire[0]) { activateStep(1); }
    if (fire[1]) { activateStep(2); }
    if (fire[2]) { activateStep(2); }
    if (fire[3]) { activateStep(5); }
    if (fire[4]) { activateStep(3); }
    if (fire[5]) { activateStep(4); }
    if (fire[6]) { activateStep(4); }
    if (fire[7]) { activateStep(5); }
    if (fire[8]) { activateStep(0); }
    if (fire[9]) { activateStep(0); }
    return true;
}

void BancAubo::computeOutputs() {
    const State s = cur();
    const bool run = (s == State::IDLE || s == State::RUNNING);
    const bool emergency = (s == State::EMERGENCY || s == State::RESETTING);
    (void)run; (void)emergency;
    set_Cmd_start(run ? (X[1]) : (emergency ? false : false));
    set_Cmd_stop(run ? (X[3]) : (emergency ? false : false));
    set_Defaut_cmd(run ? (X[5]) : (emergency ? false : false));
}

void BancAubo::captureEdges() {
}

void BancAubo::updateEdges() {
    for (int k = 0; k < EDGE_COUNT; k++) E[k] = S[k];
}

void BancAubo::updateStateDisplay() {
    if (!sysStateNode) return;
    char text[160];
    int n = snprintf(text, sizeof(text), "%s", stateToString(static_cast<int>(cur())));
    if (cur() == State::IDLE || cur() == State::RUNNING) {
        n += snprintf(text + n, sizeof(text) - n, " :");
        for (int i = 0; i < STEP_COUNT && n < (int)sizeof(text) - 12; i++) {
            if (X[i]) n += snprintf(text + n, sizeof(text) - n, " X%d", STEP_NUMBERS[i]);
        }
    }
    if (strcmp(sysStateNode->getValue(), text) != 0) sysStateNode->setValue(text);
}

bool BancAubo::readParams() {
    SafeJsonDocument doc(4096);
    PLC_Persistence& p = PLC_Persistence::getInstance();
    if (!p.readJsonFromFile(PARAMS_FILE, doc)) {
        Serial.printf("BancAubo: %s absent ou illisible, valeurs par défaut utilisées\n", PARAMS_FILE);
        p.markDirty(PARAMS_FILE);
        return false;
    }
    if (doc["Tempo_reponse"].is<int32_t>()) init_Tempo_reponse = doc["Tempo_reponse"].as<int32_t>();
    return true;
}

void BancAubo::applyParamsToNodes() {
    set_Tempo_reponse(init_Tempo_reponse);
    saved_Tempo_reponse = init_Tempo_reponse;
}

void BancAubo::checkParams() {
    bool dirty = false;
    if (v_Tempo_reponse() < 500) set_Tempo_reponse(500);
    if (v_Tempo_reponse() > 30000) set_Tempo_reponse(30000);
    if (saved_Tempo_reponse != v_Tempo_reponse()) { saved_Tempo_reponse = v_Tempo_reponse(); dirty = true; }
    if (dirty) PLC_Persistence::getInstance().markDirty(PARAMS_FILE);
}

bool BancAubo::saveParams() {
    SafeJsonDocument doc(4096);
    doc["machine name"] = "BancAubo";
    doc["Tempo_reponse"] = saved_Tempo_reponse;
    markInternalSave(PARAMS_FILE);
    if (!PLC_Persistence::getInstance().saveJsonToFile(PARAMS_FILE, doc)) {
        BorneUniverselle::prepareMessage(ERROR, "Échec de la sauvegarde des paramètres");
        return false;
    }
    return true;
}

const char* BancAubo::stateToString(int state) const {
    switch (static_cast<State>(state)) {
        case State::UNDEFINED:    return "INDÉFINI";
        case State::INITIALIZING: return "INITIALISATION";
        case State::STOP:         return "ARRÊT";
        case State::IDLE:         return "REPOS";
        case State::EMERGENCY:    return "URGENCE";
        case State::JOGGING:      return "MANUEL";
        case State::HOMING:       return "PRISE D'ORIGINE";
        case State::RUNNING:      return "CYCLE";
        case State::RESETTING:    return "RÉARMEMENT";
        default:                  return "INCONNU";
    }
}

BusinessLogic::StateType BancAubo::classifyState(int state) const {
    switch (static_cast<State>(state)) {
        case State::EMERGENCY:
        case State::RESETTING:    return StateType::EMERGENCY;
        case State::STOP:
        case State::IDLE:         return StateType::STABLE;
        case State::JOGGING:
        case State::HOMING:       return StateType::UTILITY;
        default:                  return StateType::TRANSIENT;
    }
}

bool BancAubo::shouldShowClock() const {
    State s = cur();
    return s == State::IDLE || s == State::STOP || s == State::EMERGENCY;
}

void BancAubo::onStateEnter(int newState, int oldState) {
    // Pas de changement d'état ici (règle BusinessLogic).
    switch (static_cast<State>(newState)) {
        case State::EMERGENCY:
            emergencyClearSince = 0;
            grafcetClear();
            break;
        case State::STOP:
            grafcetClear();
            break;
        default:
            break;
    }
    (void)oldState;
    updateStateDisplay();
}

void BancAubo::onStateExit(int oldState, int newState) {
    (void)oldState; (void)newState;
}

bool BancAubo::canDeleteFile(const char* path) {
    return strcmp(path, PARAMS_FILE) != 0;
}

bool BancAubo::validateConfigFile(const char* path, const JsonDocument& doc, String& errorMsg) {
    (void)path; (void)doc; (void)errorMsg;
    return true;
}

void BancAubo::onFileSaved(const char* path, bool success) {
    if (!path || !success || !shouldProcessFileCallback(path)) return;
    if (strcmp(path, PARAMS_FILE) == 0) {
        if (!isIdle()) {
            BorneUniverselle::prepareMessage(WARNING, "Paramètres modifiés : rechargement à l'arrêt seulement");
            return;
        }
        readParams();
        applyParamsToNodes();
        BorneUniverselle::prepareMessage(SUCCESS, "Paramètres rechargés");
    }
}

void BancAubo::printPersistance() {
    Serial.printf("BancAubo: fichier de paramètres %s\n", PARAMS_FILE);
    Serial.printf("  Tempo_reponse = %ld\n", (long)saved_Tempo_reponse);
}

