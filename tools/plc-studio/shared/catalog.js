// Catalogue des équipements connus de PLC Studio.
//
// Chaque équipement référence un fichier data/hardware/<hardware>.json du firmware.
// Les "groupes" décrivent les voies câblables ; "type" est le type de section
// config.json (qui choisit la classe C++ du nœud) et "ids" les id présents dans
// le tableau correspondant du fichier hardware.

const range = (a, b) => Array.from({ length: b - a + 1 }, (_, i) => a + i);

// Options communes des automates : variantes du fichier matériel générées par l'outil.
const RS485_OPTIONS = [
  {
    key: 'rs485Speed',
    label: 'Vitesse RS485 (bauds)',
    type: 'select',
    default: '',
    choices: [
      { value: '', label: 'Celle du fichier matériel' },
      { value: '9600', label: '9600' },
      { value: '19200', label: '19200' },
      { value: '38400', label: '38400' },
      { value: '57600', label: '57600' },
      { value: '115200', label: '115200' },
    ],
    help: 'Tous les équipements Modbus du bus doivent être réglés à cette vitesse.',
  },
  {
    key: 'rs485Config',
    label: 'Format RS485',
    type: 'select',
    default: '',
    choices: [
      { value: '', label: 'Celui du fichier matériel' },
      { value: 'SERIAL_8N1', label: '8N1 (sans parité)' },
      { value: 'SERIAL_8E1', label: '8E1 (parité paire)' },
      { value: 'SERIAL_8O1', label: '8O1 (parité impaire)' },
      { value: 'SERIAL_8N2', label: '8N2 (2 bits de stop)' },
    ],
  },
];

const rtcOption = (chip) => ({
  key: 'rtc',
  label: `Horloge ${chip} installée`,
  type: 'bool',
  default: true,
  help: "Si décoché, l'outil génère une variante du fichier matériel sans horloge (l'heure part alors de la mise sous tension).",
});

// Voies d'une carte : "names" donne le nom d'une voie quand il ne suit pas le préfixe (GPIO).
// "gpio: true" : sorties directes de l'ESP32, seules utilisables pour les impulsions d'un pas-à-pas.
const di = (type, n, prefix = 'X', extra = {}) => ({ key: 'di', label: 'Entrées TOR', short: 'Entrees', type, dir: 'in', dataType: 'bool', ids: range(1, n), prefix, supportsInverse: true, ...extra });
const dout = (type, n, prefix = 'Y', label = 'Sorties relais', short = 'Relais') => ({ key: 'do', label, short, type, dir: 'out', dataType: 'bool', ids: range(1, n), prefix });
const gpio = (pins, label = 'Sorties directes GPIO') => ({
  key: 'gpio',
  label,
  short: 'GPIO',
  type: 'tx-bool',
  dir: 'out',
  dataType: 'bool',
  ids: pins.map((_, i) => i + 1),
  prefix: 'GPIO',
  names: Object.fromEntries(pins.map((pin, i) => [i + 1, `GPIO${pin}`])),
  gpio: true,
});
const buzzer = (pin) => ({ key: 'buzzer', label: `Buzzer (GPIO${pin})`, short: 'Buzzer', type: 'tx-bool', dir: 'out', dataType: 'bool', ids: [9], prefix: 'BUZZER', names: { 9: 'BUZZER' } });

// Axes Lichuan (src/LichuanServo) : positions en unités de commande, vitesses en tr/min.
const LICHUAN_AXIS = {
  kind: 'lichuan',
  cppClass: 'LichuanServo',
  include: 'LichuanServo/LichuanServo.h',
  nodesStruct: 'LichuanNodes',
  positionUnit: 'unités de commande (impulsions)',
  speedUnit: 'tr/min',
  speedMin: 1,
  defaultSpeed: 100,
  jogSpeedUnit: 'tr/min',
  jogSpeedMin: 1,
  defaultJogSpeed: 60,
  torque: true,
  hmi: {
    numeric: ['position', 'alarmCode', 'actualSpeed'],
    indicators: ['servoReady', 'driveInitialised', 'homeDone', 'inPosition', 'servoAlarm', 'modbusError'],
    alarms: ['servoAlarm', 'modbusError'],
  },
  refAliases: {
    pret: 'servoReady',
    initialise: 'driveInitialised',
    origineFaite: 'homeDone',
    enPosition: 'inPosition',
    alarme: 'servoAlarm',
    erreurModbus: 'modbusError',
    position: 'position',
    codeAlarme: 'alarmCode',
    vitesse: 'actualSpeed',
    etat: 'status',
  },
};

export const CATALOG = {
  Kincony_KC868_A8S: {
    defaultLabel: 'Automate',
    label: 'Kincony KC868-A8S',
    description: 'Automate ESP32 : 8 entrées optocouplées, 8 relais, RS485 (Modbus maître), horloge DS3231.',
    role: 'controller',
    hardware: 'Kincony_KC868_A8S',
    pioEnv: 'esp32dev',
    max: 1,
    modbus: false,
    rs485: true,
    options: [rtcOption('DS3231'), ...RS485_OPTIONS],
    groups: [
      di('PF8574_rx-bool', 8),
      dout('PF8574_tx-bool', 8),
      { ...gpio([2], 'Sortie directe GPIO2 (buzzer)'), key: 'gpio' },
    ],
  },

  Waveshare_ESP32S3_POE_8DI8DO: {
    defaultLabel: 'Automate',
    label: 'Waveshare ESP32-S3-POE-ETH-8DI-8DO',
    description: 'Automate ESP32-S3 : 8 entrées optocouplées, 8 sorties transistor (TCA9554), RS485 isolé, horloge PCF85063, buzzer. Ethernet/PoE non utilisé (WiFi).',
    role: 'controller',
    hardware: 'Waveshare_ESP32S3_POE_8DI8DO',
    pioEnv: 'esp32s3_n16r8',
    max: 1,
    modbus: false,
    rs485: true,
    options: [rtcOption('PCF85063'), ...RS485_OPTIONS],
    groups: [
      di('rx-bool', 8, 'DI'),
      dout('EXP_tx-bool', 8, 'DO', 'Sorties transistor', 'Sorties'),
      gpio([40, 1, 47, 48], 'GPIO libres (à repiquer ; 47/48 : lecteur TF inutilisé)'),
      buzzer(46),
    ],
  },

  Waveshare_ESP32S3_RS485_WLED: {
    defaultLabel: 'Automate',
    label: 'Waveshare ESP32-S3-RS485-WLED',
    description: 'Contrôleur ESP32-S3 compact : RS485 isolé (sens automatique), 2 sorties 5 V (prévues pour bandeaux LED, idéales en STEP/DIR), 2 GPIO 3,3 V, bouton BOOT. Pas de relais ni d’entrées isolées : à compléter par des modules Modbus.',
    role: 'controller',
    hardware: 'Waveshare_ESP32S3_RS485_WLED',
    pioEnv: 'esp32s3_n16r8',
    max: 1,
    modbus: false,
    rs485: true,
    options: [...RS485_OPTIONS],
    groups: [
      { key: 'di', label: 'Bouton BOOT (GPIO0)', short: 'Entrees', type: 'rx-bool', dir: 'in', dataType: 'bool', ids: [1], prefix: 'BOOT', names: { 1: 'BOOT' }, supportsInverse: true },
      {
        key: 'gpio',
        label: 'Sorties directes (GPIO2 et GPIO16 en 5 V, GPIO3/4 en 3,3 V)',
        short: 'GPIO',
        type: 'tx-bool',
        dir: 'out',
        dataType: 'bool',
        ids: [1, 2, 3, 4],
        prefix: 'GPIO',
        names: { 1: 'GPIO2', 2: 'GPIO16', 3: 'GPIO3', 4: 'GPIO4' },
        gpio: true,
      },
    ],
  },

  Kincony_KC868_A16: {
    defaultLabel: 'Automate',
    label: 'Kincony KC868-A16',
    description: 'Automate ESP32 : 16 entrées optocouplées, 16 sorties MOSFET (PCF8574), RS485, 3 GPIO libres (HT1-HT3). Pas d’horloge.',
    role: 'controller',
    hardware: 'Kincony_KC868_A16',
    pioEnv: 'quatre_mb_huge',
    max: 1,
    modbus: false,
    rs485: true,
    options: [...RS485_OPTIONS],
    groups: [di('EXP_rx-bool', 16), dout('EXP_tx-bool', 16, 'Y', 'Sorties MOSFET', 'Sorties'), gpio([32, 33, 14], 'GPIO libres HT1-HT3')],
  },

  Kincony_KC868_A16v3: {
    defaultLabel: 'Automate',
    label: 'Kincony KC868-A16v3',
    description: 'Automate ESP32-S3 : 16 entrées optocouplées, 16 sorties MOSFET (PCF8574), RS485, 6 GPIO libres, horloge DS3231.',
    role: 'controller',
    hardware: 'Kincony_KC868_A16v3',
    pioEnv: 'esp32s3_n16r8',
    max: 1,
    modbus: false,
    rs485: true,
    options: [rtcOption('DS3231'), ...RS485_OPTIONS],
    groups: [di('EXP_rx-bool', 16), dout('EXP_tx-bool', 16, 'Y', 'Sorties MOSFET', 'Sorties'), gpio([38, 39, 40, 41, 47, 48], 'GPIO libres')],
  },

  Kincony_KC868_A8v3: {
    defaultLabel: 'Automate',
    label: 'Kincony KC868-A8v3',
    description: 'Automate ESP32-S3 : 8 entrées optocouplées, 8 relais (PCF8575), RS485, 4 GPIO libres, horloge DS3231.',
    role: 'controller',
    hardware: 'Kincony_KC868_A8v3',
    pioEnv: 'esp32s3_n16r8',
    max: 1,
    modbus: false,
    rs485: true,
    options: [rtcOption('DS3231'), ...RS485_OPTIONS],
    groups: [di('EXP_rx-bool', 8), dout('EXP_tx-bool', 8), gpio([13, 14, 40, 48], 'GPIO libres')],
  },

  M5Stack_StamPLC: {
    defaultLabel: 'Automate',
    label: 'M5Stack StamPLC',
    description: 'Automate ESP32-S3 : 8 entrées optocouplées, 4 relais (AW9523), RS485, horloge RX8130, buzzer, ports Grove (GPIO libres).',
    role: 'controller',
    hardware: 'M5Stack_StamPLC',
    pioEnv: 'm5stack_stamplc',
    max: 1,
    modbus: false,
    rs485: true,
    options: [rtcOption('RX8130'), ...RS485_OPTIONS],
    groups: [di('EXP_rx-bool', 8, 'DI'), dout('EXP_tx-bool', 4, 'DO'), gpio([4, 5, 1, 2], 'GPIO libres (ports Grove)'), buzzer(44)],
  },

  Homemaster_MiniPLC: {
    defaultLabel: 'Automate',
    label: 'Homemaster MiniPLC',
    description: 'Automate ESP32 : 4 entrées 24 V, 4 boutons en façade, 6 relais (PCF8574), 2 LED, RS485 isolé, horloge PCF8563, buzzer, 2 GPIO libres (bornes 1-Wire).',
    role: 'controller',
    hardware: 'Homemaster_MiniPLC',
    pioEnv: 'quatre_mb_huge',
    max: 1,
    modbus: false,
    rs485: true,
    options: [rtcOption('PCF8563'), ...RS485_OPTIONS],
    groups: [
      { ...di('rx-bool', 4, 'DI'), supportsInverse: true },
      { key: 'btn', label: 'Boutons en façade', short: 'Boutons', type: 'EXP_rx-bool', dir: 'in', dataType: 'bool', ids: range(1, 4), prefix: 'BTN', supportsInverse: true },
      dout('EXP_tx-bool', 6, 'DO'),
      { key: 'led', label: 'LED en façade', short: 'LED', type: 'EXP_tx-bool', dir: 'out', dataType: 'bool', ids: [7, 8], prefix: 'LED', names: { 7: 'LED2', 8: 'LED3' } },
      gpio([5, 4], 'GPIO libres (bornes 1-Wire)'),
      buzzer(2),
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
    description: "Variateur piloté en Modbus RTU (plusieurs possibles, adresses distinctes). Ses signaux TOR (servo ON, arrêt immédiat, reset alarmes) se câblent sur des sorties de l'automate.",
    role: 'axis',
    hardware: 'SureServo',
    modbus: true,
    defaultAddress: 127,
    groups: [],
    // Pilotage par AxisController (src/AxisController) : unités natives du drive.
    axis: {
      kind: 'sureservo',
      cppClass: 'SureServo',
      include: 'SureServo/SureServo.h',
      nodesStruct: 'ServoNodes',
      positionUnit: 'impulsions (PUU)',
      speedUnit: 'index 0-15 de la table de vitesses',
      speedMin: 0,
      speedMax: 15,
      defaultSpeed: 5,
      jogSpeedUnit: 'tr/min',
      jogSpeedMin: 1,
      jogSpeedMax: 3000,
      defaultJogSpeed: 500,
      torque: true,
    },
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
    // Nœuds créés automatiquement : une section « <nom de l'équipement> <suffix> » par entrée.
    // "field" = champ de la structure de nœuds du drive (ServoNodes).
    sections: [
      {
        suffix: 'registres lus',
        type: 'ModbusReadHoldingRegister',
        modbus: true,
        nodes: [
          { id: 1, name: 'status', field: 'status', cpp: 'Uint16InputNode', refreshInterval: 200, label: 'Mot d’état servo' },
          { id: 2, name: 'abs coord status', field: 'absoluteCoordonateSystemStatus', cpp: 'Uint16InputNode', refreshInterval: 1000 },
          { id: 3, name: 'alarms', field: 'alarms', cpp: 'Uint16InputNode', refreshInterval: 1000, label: 'Code d’alarme servo' },
        ],
      },
      {
        suffix: 'registres ecrits',
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
        suffix: 'position',
        type: 'ModbusReadDobbleHoldingRegister',
        modbus: true,
        nodes: [{ id: 1, name: 'position', field: 'position', cpp: 'Uint32InputNode', refreshInterval: 250, label: 'Position actuelle' }],
      },
      {
        suffix: 'consignes',
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
        suffix: 'etat',
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

  LichuanA6: {
    defaultLabel: 'Servo',
    label: 'Servo Lichuan A6 / LCDA6 (Modbus)',
    description: "Variateur Lichuan série A6 (paramètres PA_xxx) piloté en Modbus RTU, 8E1. Mise en service du variateur : docs/axes/lichuan.md.",
    role: 'axis',
    hardware: 'Lichuan_A6',
    modbus: true,
    defaultAddress: 1,
    groups: [],
    axis: {
      ...LICHUAN_AXIS,
      family: 'A6',
      speedMax: 3000,
      jogSpeedMax: 3000,
    },
    options: [
      { key: 'pulsesPerUnit', label: 'Impulsions par unité', type: 'number', default: 1, help: '1 = travail direct en unités de commande du variateur (PA_04A / P05-02).' },
      { key: 'maxRange', label: 'Course maximale (unités)', type: 'number', default: 0, help: 'Butée logicielle haute ; 0 = pas de butée haute.' },
      { key: 'accelTime', label: 'Temps de rampe (ms)', type: 'number', default: 0, help: '0 = réglage du variateur conservé. A6 : ms pour 0 → 1000 tr/min (PA_058/PA_059).' },
    ],
    sections: [
      {
        suffix: 'registres lus',
        type: 'ModbusReadHoldingRegister',
        modbus: true,
        nodes: [
          { id: 1, name: 'etat', field: 'status', cpp: 'Uint16InputNode', refreshInterval: 200, label: 'Mot d’état' },
          { id: 2, name: 'code alarme', field: 'alarmCode', cpp: 'Uint16InputNode', refreshInterval: 500, label: 'Code d’alarme' },
          { id: 3, name: 'vitesse', field: 'actualSpeed', cpp: 'Uint16InputNode', refreshInterval: 500, label: 'Vitesse (tr/min)' },
          { id: 4, name: 'entrees relues', field: 'virtualInputsReadback', cpp: 'Uint16InputNode', refreshInterval: 1000 },
        ],
      },
      {
        suffix: 'registres ecrits',
        type: 'ModbusWriteHoldingRegister',
        modbus: true,
        nodes: [
          { id: 1, name: 'entrees virtuelles', field: 'virtualInputs', cpp: 'Uint16OutputNode' },
          { id: 2, name: 'vitesse', field: 'moveSpeed', cpp: 'Uint16OutputNode' },
          { id: 3, name: 'acceleration', field: 'accelTime', cpp: 'Uint16OutputNode' },
          { id: 4, name: 'deceleration', field: 'decelTime', cpp: 'Uint16OutputNode' },
          { id: 6, name: 'couple max', field: 'torqueLimit', cpp: 'Uint16OutputNode' },
        ],
      },
      {
        suffix: 'position',
        type: 'ModbusReadDobbleHoldingRegister',
        modbus: true,
        nodes: [{ id: 1, name: 'position', field: 'position', cpp: 'Uint32InputNode', refreshInterval: 250, label: 'Position actuelle' }],
      },
      {
        suffix: 'consignes',
        type: 'ModbusWriteDobbleHoldingRegister',
        modbus: true,
        nodes: [{ id: 1, name: 'cible', field: 'targetPosition', cpp: 'Uint32OutputNode' }],
      },
      {
        suffix: 'etat',
        type: 'tx-bool',
        virtual: true,
        nodes: [
          { id: 1, name: 'pret', field: 'servoReady', cpp: 'BooleanOutputNode', label: 'Servo prêt' },
          { id: 2, name: 'en position', field: 'inPosition', cpp: 'BooleanOutputNode', label: 'En position' },
          { id: 3, name: 'vitesse nulle', field: 'zeroSpeed', cpp: 'BooleanOutputNode', label: 'Vitesse nulle' },
          { id: 4, name: 'origine faite', field: 'homeDone', cpp: 'BooleanOutputNode', label: 'Origine faite' },
          { id: 5, name: 'alarme', field: 'servoAlarm', cpp: 'BooleanOutputNode', label: 'Alarme servo' },
          { id: 6, name: 'initialise', field: 'driveInitialised', cpp: 'BooleanOutputNode', label: 'Variateur initialisé' },
          { id: 7, name: 'erreur modbus', field: 'modbusError', cpp: 'BooleanOutputNode', label: 'Erreur Modbus' },
        ],
      },
    ],
  },

  LichuanA5: {
    defaultLabel: 'Servo',
    label: 'Servo Lichuan A5 / LCDA630P / LC10E (Modbus)',
    description: "Variateur Lichuan série A5 (paramètres Pgg-nn) piloté en Modbus RTU. Mise en service du variateur : docs/axes/lichuan.md.",
    role: 'axis',
    hardware: 'Lichuan_A5',
    modbus: true,
    defaultAddress: 1,
    groups: [],
    axis: {
      ...LICHUAN_AXIS,
      family: 'A5',
      speedMax: 6000,
      jogSpeedMax: 6000,
    },
    options: [
      { key: 'pulsesPerUnit', label: 'Impulsions par unité', type: 'number', default: 1, help: '1 = travail direct en unités de commande du variateur (PA_04A / P05-02).' },
      { key: 'maxRange', label: 'Course maximale (unités)', type: 'number', default: 0, help: 'Butée logicielle haute ; 0 = pas de butée haute.' },
      { key: 'accelTime', label: 'Temps de rampe (ms)', type: 'number', default: 0, help: '0 = réglage du variateur conservé. A5 : ms (P11-15).' },
    ],
    sections: [
      {
        suffix: 'registres lus',
        type: 'ModbusReadHoldingRegister',
        modbus: true,
        nodes: [
          { id: 1, name: 'etat', field: 'status', cpp: 'Uint16InputNode', refreshInterval: 200, label: 'Mot d’état' },
          { id: 2, name: 'code alarme', field: 'alarmCode', cpp: 'Uint16InputNode', refreshInterval: 500, label: 'Code d’alarme' },
          { id: 3, name: 'vitesse', field: 'actualSpeed', cpp: 'Uint16InputNode', refreshInterval: 500, label: 'Vitesse (tr/min)' },
          { id: 4, name: 'entrees relues', field: 'virtualInputsReadback', cpp: 'Uint16InputNode', refreshInterval: 1000 },
        ],
      },
      {
        suffix: 'registres ecrits',
        type: 'ModbusWriteHoldingRegister',
        modbus: true,
        nodes: [
          { id: 1, name: 'entrees virtuelles', field: 'virtualInputs', cpp: 'Uint16OutputNode' },
          { id: 2, name: 'vitesse', field: 'moveSpeed', cpp: 'Uint16OutputNode' },
          { id: 3, name: 'acceleration', field: 'accelTime', cpp: 'Uint16OutputNode' },
          { id: 5, name: 'vitesse jog', field: 'jogSpeed', cpp: 'Uint16OutputNode' },
          { id: 6, name: 'couple max', field: 'torqueLimit', cpp: 'Uint16OutputNode' },
          { id: 7, name: 'couple max inverse', field: 'torqueLimitReverse', cpp: 'Uint16OutputNode' },
          { id: 8, name: 'reset defaut', field: 'faultReset', cpp: 'Uint16OutputNode' },
        ],
      },
      {
        suffix: 'position',
        type: 'ModbusReadDobbleHoldingRegister',
        modbus: true,
        nodes: [{ id: 1, name: 'position', field: 'position', cpp: 'Uint32InputNode', refreshInterval: 250, label: 'Position actuelle' }],
      },
      {
        suffix: 'consignes',
        type: 'ModbusWriteDobbleHoldingRegister',
        modbus: true,
        nodes: [{ id: 1, name: 'cible', field: 'targetPosition', cpp: 'Uint32OutputNode' }],
      },
      {
        suffix: 'etat',
        type: 'tx-bool',
        virtual: true,
        nodes: [
          { id: 1, name: 'pret', field: 'servoReady', cpp: 'BooleanOutputNode', label: 'Servo prêt' },
          { id: 2, name: 'en position', field: 'inPosition', cpp: 'BooleanOutputNode', label: 'En position' },
          { id: 3, name: 'vitesse nulle', field: 'zeroSpeed', cpp: 'BooleanOutputNode', label: 'Vitesse nulle' },
          { id: 4, name: 'origine faite', field: 'homeDone', cpp: 'BooleanOutputNode', label: 'Origine faite' },
          { id: 5, name: 'alarme', field: 'servoAlarm', cpp: 'BooleanOutputNode', label: 'Alarme servo' },
          { id: 6, name: 'initialise', field: 'driveInitialised', cpp: 'BooleanOutputNode', label: 'Variateur initialisé' },
          { id: 7, name: 'erreur modbus', field: 'modbusError', cpp: 'BooleanOutputNode', label: 'Erreur Modbus' },
        ],
      },
    ],
  },

  Stepper: {
    defaultLabel: 'Axe',
    label: 'Moteur pas-à-pas (STEP/DIR)',
    description: "Driver pas-à-pas ou servo à entrée impulsions piloté par l'ESP32 (FastAccelStepper). STEP et DIR se câblent sur des GPIO directs de l'automate ; ENABLE, capteur d'origine, fins de course et alarme driver sont facultatifs.",
    role: 'axis',
    hardware: null,
    modbus: false,
    groups: [],
    axis: {
      kind: 'stepper',
      cppClass: 'StepperMotor',
      include: 'StepperMotor/StepperMotor.h',
      nodesStruct: 'StepperNodes',
      positionUnit: 'pas',
      speedUnit: 'pas/s',
      speedMin: 1,
      speedMax: 200000,
      defaultSpeed: 2000,
      jogSpeedUnit: 'pas/s',
      jogSpeedMin: 1,
      jogSpeedMax: 200000,
      defaultJogSpeed: 1000,
      torque: false,
      hmi: {
        numeric: ['position'],
        indicators: ['stepperReady', 'stepperActivated', 'driveInitialised', 'homeDone', 'targetPositionReached', 'stepperAlarm', 'limitError'],
        alarms: ['stepperAlarm', 'limitError'],
      },
      refAliases: {
        pret: 'stepperReady',
        active: 'stepperActivated',
        initialise: 'driveInitialised',
        origineFaite: 'homeDone',
        enPosition: 'targetPositionReached',
        alarme: 'stepperAlarm',
        finDeCourse: 'limitError',
        position: 'position',
      },
    },
    options: [
      { key: 'stepsPerUnit', label: 'Pas par unité', type: 'number', default: 1, help: '1 = travail direct en pas (positions et course en pas).' },
      { key: 'maxRange', label: 'Course maximale (unités)', type: 'number', default: 0, help: 'Butée logicielle après la prise d’origine ; 0 = pas de butée haute.' },
      { key: 'acceleration', label: 'Accélération (pas/s²)', type: 'number', default: 5000 },
      {
        key: 'homingDirection',
        label: 'Sens de la prise d’origine',
        type: 'select',
        default: '-1',
        choices: [
          { value: '-1', label: 'Vers le négatif' },
          { value: '1', label: 'Vers le positif' },
        ],
        help: 'Sans capteur d’origine câblé, la position courante devient l’origine.',
      },
      { key: 'homingSpeed', label: 'Vitesse de prise d’origine (pas/s)', type: 'number', default: 1000 },
      { key: 'homingBackoff', label: 'Dégagement après origine (pas)', type: 'number', default: 200 },
      { key: 'invertDirection', label: 'Inverser le sens (DIR)', type: 'bool', default: false },
      { key: 'enableActiveLow', label: 'ENABLE actif à l’état bas', type: 'bool', default: true, help: 'Coché : le driver est activé quand la sortie ENABLE est au repos (cas des drivers TB6600, DM542…).' },
    ],
    // Signaux à câbler : "gpio" = sortie GPIO directe obligatoire (impulsions), "dir: 'in'" = entrée.
    signals: [
      { key: 'step', label: 'STEP (impulsions)', field: 'stepPin', gpio: true, cppType: 'HardwareBooleanOutputNode', classId: 'CLASS_HW_BOOLEAN_OUTPUT_NODE' },
      { key: 'dir', label: 'DIR (sens)', field: 'dirPin', gpio: true, cppType: 'HardwareBooleanOutputNode', classId: 'CLASS_HW_BOOLEAN_OUTPUT_NODE' },
      { key: 'enable', label: 'ENABLE', field: 'enable', optional: true },
      { key: 'alarmsReset', label: 'Reset alarme driver', field: 'alarmsReset', optional: true },
      { key: 'home', label: "Capteur d'origine", field: 'homeSwitch', dir: 'in', optional: true },
      { key: 'limitPlus', label: 'Fin de course +', field: 'limitSwitchPositive', dir: 'in', optional: true },
      { key: 'limitMinus', label: 'Fin de course -', field: 'limitSwitchNegative', dir: 'in', optional: true },
      { key: 'driverAlarm', label: 'Alarme driver (ALM)', field: 'hardwareStepperAlarm', dir: 'in', optional: true },
    ],
    // Recopies d'état pour l'écran (nœuds virtuels écrits par StepperMotor).
    sections: [
      {
        suffix: 'etat',
        type: 'tx-bool',
        virtual: true,
        nodes: [
          { id: 1, name: 'pret', field: 'stepperReady', cpp: 'BooleanOutputNode', label: 'Axe prêt' },
          { id: 2, name: 'active', field: 'stepperActivated', cpp: 'BooleanOutputNode', label: 'Driver activé' },
          { id: 3, name: 'initialise', field: 'driveInitialised', cpp: 'BooleanOutputNode', label: 'Axe initialisé' },
          { id: 4, name: 'origine faite', field: 'homeDone', cpp: 'BooleanOutputNode', label: 'Origine faite' },
          { id: 5, name: 'en position', field: 'targetPositionReached', cpp: 'BooleanOutputNode', label: 'En position' },
          { id: 6, name: 'alarme', field: 'stepperAlarm', cpp: 'BooleanOutputNode', label: 'Alarme axe' },
          { id: 7, name: 'fin de course', field: 'limitError', cpp: 'BooleanOutputNode', label: 'Fin de course atteinte' },
          { id: 8, name: 'arret', field: 'zeroSpeed', cpp: 'BooleanOutputNode', label: 'Moteur à l’arrêt' },
        ],
      },
      {
        suffix: 'position',
        type: 'tx-uint32',
        virtual: true,
        nodes: [{ id: 1, name: 'position', field: 'position', cpp: 'Uint32OutputNode', label: 'Position (pas)' }],
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
