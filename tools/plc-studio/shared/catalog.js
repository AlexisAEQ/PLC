// Catalogue des équipements connus de PLC Studio.
//
// Chaque équipement référence un fichier data/hardware/<hardware>.json du firmware.
// Les "groupes" décrivent les voies câblables ; "type" est le type de section
// config.json (qui choisit la classe C++ du nœud) et "ids" les id présents dans
// le tableau correspondant du fichier hardware.

const range = (a, b) => Array.from({ length: b - a + 1 }, (_, i) => a + i);

export const CATALOG = {
  Kincony_KC868_A8S: {
    defaultLabel: 'Automate',
    label: 'Kincony KC868-A8S',
    description: 'Automate ESP32 : 8 entrées optocouplées, 8 relais, RS485 (Modbus maître), horloge DS3231.',
    role: 'controller',
    hardware: 'Kincony_KC868_A8S',
    max: 1,
    modbus: false,
    rs485: true,
    options: [
      {
        key: 'rtc',
        label: 'Horloge DS3231 installée',
        type: 'bool',
        default: true,
        help: "Si décoché, l'outil génère une variante du fichier matériel sans la clé RTC (sinon le démarrage bloque quand l'horloge est absente).",
      },
    ],
    groups: [
      { key: 'di', label: 'Entrées TOR', short: 'Entrees', type: 'PF8574_rx-bool', dir: 'in', dataType: 'bool', ids: range(1, 8), prefix: 'X', supportsInverse: true },
      { key: 'do', label: 'Sorties relais', short: 'Relais', type: 'PF8574_tx-bool', dir: 'out', dataType: 'bool', ids: range(1, 8), prefix: 'Y' },
      { key: 'gpio', label: 'Sortie directe GPIO2', short: 'GPIO', type: 'tx-bool', dir: 'out', dataType: 'bool', ids: [1], prefix: 'GPIO2_' },
    ],
  },

  Waveshare_8DIO: {
    defaultLabel: 'Module ES',
    label: 'Waveshare Modbus RTU IO 8CH',
    description: 'Module déporté RS485 : 8 entrées TOR et 8 sorties TOR.',
    role: 'modbus-io',
    hardware: 'Waveshare_8DIO',
    modbus: true,
    defaultAddress: 1,
    groups: [
      { key: 'di', label: 'Entrées TOR', short: 'Entrees', type: 'ModbusReadMultipleInputsStatus', dir: 'in', dataType: 'bool', bank: { nodeId: 1, bits: 8 }, prefix: 'DI', supportsInverse: true },
      { key: 'do', label: 'Sorties TOR', short: 'Sorties', type: 'ModbusWriteMultipleCoils', dir: 'out', dataType: 'bool', bank: { nodeId: 1, bits: 8 }, prefix: 'DO' },
    ],
  },

  Waveshare_16DO: {
    defaultLabel: 'Module relais',
    label: 'Waveshare Modbus RTU Relay 16CH',
    description: 'Module déporté RS485 : 16 sorties TOR.',
    role: 'modbus-io',
    hardware: 'Waveshare_16DO',
    modbus: true,
    defaultAddress: 2,
    groups: [
      { key: 'do', label: 'Sorties TOR', short: 'Sorties', type: 'ModbusWriteMultipleCoils', dir: 'out', dataType: 'bool', bank: { nodeId: 1, bits: 16 }, prefix: 'DO' },
    ],
  },

  Waveshare_8AI: {
    defaultLabel: 'Module analogique',
    label: 'Waveshare Modbus RTU Analog Input 8CH',
    description: 'Module déporté RS485 : 8 entrées analogiques (registres 16 bits).',
    role: 'modbus-io',
    hardware: 'Waveshare_8AI',
    modbus: true,
    defaultAddress: 3,
    groups: [
      { key: 'ai', label: 'Entrées analogiques', short: 'Analogiques', type: 'ModbusReadInputRegister', dir: 'in', dataType: 'int', ids: range(1, 8), prefix: 'AI', refreshInterval: 200 },
    ],
  },

  SureServo: {
    defaultLabel: 'Servo',
    label: 'Servo AutomationDirect SureServo 2',
    description: "Variateur piloté en Modbus RTU. Ses signaux TOR (servo ON, arrêt immédiat, reset alarmes) se câblent sur des sorties de l'automate.",
    role: 'servo',
    hardware: 'SureServo',
    modbus: true,
    defaultAddress: 127,
    max: 1, // SureServo utilise des variables statiques locales : une seule instance possible.
    groups: [],
    options: [
      { key: 'pulsesPerUnit', label: 'Impulsions par unité', type: 'number', default: 1, help: '1 = travail direct en impulsions (PUU).' },
      { key: 'maxRange', label: 'Course maximale (unités)', type: 'number', default: 1000000, help: 'Butée logicielle : toute cible au-delà est refusée (avec 1 impulsion par unité, la course est en impulsions).' },
      {
        key: 'homePolicy',
        label: 'Position négative',
        type: 'select',
        default: 'NEGATIVE_INVALIDATES_HOME',
        choices: [
          { value: 'NEGATIVE_INVALIDATES_HOME', label: "Invalide l'origine" },
          { value: 'NEGATIVE_TOLERATED', label: 'Tolérée (dérive normale)' },
        ],
      },
    ],
    // Signaux TOR à câbler sur des sorties d'un autre équipement.
    signals: [
      { key: 'servoOn', label: 'Servo ON', field: 'servoOn' },
      { key: 'immediateStop', label: 'Arrêt immédiat', field: 'immediateStop' },
      { key: 'alarmsReset', label: 'Reset alarmes', field: 'alarmsReset' },
    ],
    // Nœuds créés automatiquement (sections fixes). "field" = champ de ServoNodes.
    sections: [
      {
        name: 'Servo registres lus',
        type: 'ModbusReadHoldingRegister',
        modbus: true,
        nodes: [
          { id: 1, name: 'status', field: 'status', cpp: 'Uint16InputNode', refreshInterval: 200, label: 'Mot d’état servo' },
          { id: 2, name: 'abs coord status', field: 'absoluteCoordonateSystemStatus', cpp: 'Uint16InputNode', refreshInterval: 1000 },
          { id: 3, name: 'alarms', field: 'alarms', cpp: 'Uint16InputNode', refreshInterval: 1000, label: 'Code d’alarme servo' },
        ],
      },
      {
        name: 'Servo registres ecrits',
        type: 'ModbusWriteHoldingRegister',
        modbus: true,
        nodes: [
          { id: 1, name: 'aux function', field: 'auxFunctions', cpp: 'Uint16OutputNode' },
          { id: 2, name: 'trigger', field: 'trigger', cpp: 'Uint16OutputNode' },
          { id: 4, name: 'jog speed', field: 'jogSpeed', cpp: 'Uint16OutputNode' },
          { id: 5, name: 'max torque', field: 'maxTorque', cpp: 'Uint16OutputNode' },
        ],
      },
      {
        name: 'Servo position',
        type: 'ModbusReadDobbleHoldingRegister',
        modbus: true,
        nodes: [{ id: 1, name: 'position', field: 'position', cpp: 'Uint32InputNode', refreshInterval: 250, label: 'Position actuelle' }],
      },
      {
        name: 'Servo consignes',
        type: 'ModbusWriteDobbleHoldingRegister',
        modbus: true,
        nodes: [
          { id: 1, name: 'target A', field: 'targetA', cpp: 'Uint32OutputNode' },
          { id: 2, name: 'target B', field: 'targetB', cpp: 'Uint32OutputNode' },
          { id: 3, name: 'PR1 speed', field: 'pr1Speed', cpp: 'Uint32OutputNode' },
          { id: 4, name: 'PR2 speed', field: 'pr2Speed', cpp: 'Uint32OutputNode' },
          { id: 5, name: 'PR3 speed', field: 'pr3Speed', cpp: 'Uint32OutputNode' },
          { id: 6, name: 'PR3 data', field: 'targetC', cpp: 'Uint32OutputNode' },
        ],
      },
      {
        name: 'Servo etat',
        type: 'tx-bool',
        virtual: true,
        nodes: [
          { id: 1, name: 'pret', field: 'servoReady', cpp: 'BooleanOutputNode', label: 'Servo prêt' },
          { id: 2, name: 'active', field: 'servoActivated', cpp: 'BooleanOutputNode', label: 'Servo activé' },
          { id: 3, name: 'vitesse nulle', field: 'zeroSpeed', cpp: 'BooleanOutputNode', label: 'Vitesse nulle' },
          { id: 4, name: 'vitesse atteinte', field: 'targetSpeedRated', cpp: 'BooleanOutputNode', label: 'Vitesse atteinte' },
          { id: 5, name: 'en position', field: 'targetPositionReached', cpp: 'BooleanOutputNode', label: 'En position' },
          { id: 7, name: 'alarme', field: 'servoAlarm', cpp: 'BooleanOutputNode', label: 'Alarme servo' },
          { id: 9, name: 'position absolue perdue', field: 'absolutePositionLost', cpp: 'BooleanOutputNode', label: 'Position absolue perdue' },
          { id: 10, name: 'alarme batterie', field: 'batteryAlarm', cpp: 'BooleanOutputNode', label: 'Alarme batterie' },
          { id: 11, name: 'depassement tours', field: 'multipleTurnsOverflow', cpp: 'BooleanOutputNode', label: 'Dépassement multi-tours' },
          { id: 12, name: 'depassement PUU', field: 'puuOverflow', cpp: 'BooleanOutputNode', label: 'Dépassement PUU' },
          { id: 13, name: 'coordonnees non definies', field: 'absoluteCoordonateNotSet', cpp: 'BooleanOutputNode', label: 'Coordonnées non définies' },
          { id: 14, name: 'origine faite', field: 'homeDone', cpp: 'BooleanOutputNode', label: 'Origine faite' },
          { id: 15, name: 'erreur modbus', field: 'modbusError', cpp: 'BooleanOutputNode', label: 'Erreur Modbus' },
          { id: 16, name: 'initialise', field: 'driveInitialised', cpp: 'BooleanOutputNode', label: 'Variateur initialisé' },
        ],
      },
    ],
  },
};

// Types de variables "internes" (non câblées) proposées à l'étape Variables.
export const VARIABLE_KINDS = {
  command: { label: 'Commande',  help: "Bouton ou interrupteur à l'écran, lu par la logique.", dataTypes: ['bool', 'int', 'float'] },
  parameter: { label: 'Paramètre', help: "Consigne réglable à l'écran et conservée après redémarrage.", dataTypes: ['int', 'float', 'text', 'bool'] },
  indicator: { label: 'Indicateur', help: "Valeur calculée par la logique et affichée à l'écran.", dataTypes: ['bool', 'int', 'float', 'text'] },
  memory: { label: 'Mémoire', help: 'Variable de travail de la logique (compteur, mémoire), sans affichage.', dataTypes: ['bool', 'int', 'float'] },
  input: { label: 'Entrée câblée', help: 'Voie d\'entrée d\'un équipement.', dataTypes: ['bool', 'int'] },
  output: { label: 'Sortie câblée', help: "Voie de sortie d'un équipement.", dataTypes: ['bool'] },
};

export const DATA_TYPE_LABELS = { bool: 'Booléen', int: 'Entier', float: 'Réel', text: 'Texte' };

// Widgets IHM (data/component-manifest.json) compatibles avec chaque nature de variable.
export function widgetsFor(variable) {
  const t = variable.dataType;
  switch (variable.kind) {
    case 'command':
      if (t === 'bool') return ['tx-button-hold', 'tx-bool'];
      return ['tx-numeric', 'tx-slider'];
    case 'parameter':
      if (t === 'bool') return ['tx-bool'];
      if (t === 'text') return ['tx-string', 'tx-dropdown'];
      return ['tx-numeric', 'tx-slider'];
    case 'indicator':
    case 'memory':
      if (t === 'bool') return ['rx-indicator', 'rx-bool'];
      if (t === 'text') return ['rx-label'];
      return ['rx-numeric', 'rx-level-bar'];
    case 'input':
      if (t === 'bool') return ['rx-indicator', 'rx-bool'];
      return ['rx-numeric', 'rx-level-bar'];
    case 'output':
      return ['rx-indicator', 'rx-bool'];
    default:
      return ['rx-label'];
  }
}

export const WIDGET_LABELS = {
  'tx-button-hold': 'Bouton (maintenu)',
  'tx-bool': 'Interrupteur',
  'tx-numeric': 'Saisie numérique',
  'tx-slider': 'Curseur',
  'tx-string': 'Saisie texte',
  'tx-dropdown': 'Liste déroulante',
  'rx-indicator': 'Voyant',
  'rx-bool': 'Interrupteur (lecture)',
  'rx-numeric': 'Valeur numérique',
  'rx-level-bar': 'Barre de niveau',
  'rx-label': 'Texte',
};

export function catalogEntry(type) {
  return CATALOG[type] || null;
}
