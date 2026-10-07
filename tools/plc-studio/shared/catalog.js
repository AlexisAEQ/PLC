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

// Variateurs pas-à-pas StepperOnline « RS » (src/StepperOnlineRS, protocole Leadshine PR) :
// positions en pas, vitesses en tr/min. La position 32 bits du variateur est en mots inversés
// par rapport aux nœuds « Dobble » : l'écran affiche le nœud displayPosition, recopié de
// AxisController::position() par la classe générée.
const STEPPERONLINE_RS_AXIS = {
  kind: 'stepperonline-rs',
  cppClass: 'StepperOnlineRS',
  include: 'StepperOnlineRS/StepperOnlineRS.h',
  nodesStruct: 'StepperOnlineRSNodes',
  positionUnit: 'pas (impulsions Pr0.00)',
  speedUnit: 'tr/min',
  speedMin: 1,
  speedMax: 3000,
  defaultSpeed: 120,
  jogSpeedUnit: 'tr/min',
  jogSpeedMin: 1,
  jogSpeedMax: 3000,
  defaultJogSpeed: 60,
  torque: true, // abaisse le courant crête ; effectif seulement si le courant de référence est réglé
  hmi: {
    numeric: ['displayPosition', 'alarmCode'],
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
    position: 'displayPosition',
    codeAlarme: 'alarmCode',
    vitesse: 'actualSpeed',
    etat: 'status',
  },
};

const stepperOnlineRsSections = (closedLoop) => [
  {
    suffix: 'registres lus',
    type: 'ModbusReadHoldingRegister',
    modbus: true,
    nodes: [
      { id: 1, name: 'etat', field: 'status', cpp: 'Uint16InputNode', refreshInterval: 200, label: 'Mot d’état' },
      { id: 2, name: 'code alarme', field: 'alarmCode', cpp: 'Uint16InputNode', refreshInterval: 500, label: 'Code d’alarme' },
      { id: 3, name: 'declenchement lu', field: 'triggerStatus', cpp: 'Uint16InputNode', refreshInterval: 200 },
      { id: 4, name: 'avertissement pr', field: 'prWarning', cpp: 'Uint16InputNode', refreshInterval: 500 },
      ...(closedLoop ? [{ id: 5, name: 'alimentation relue', field: 'softwareEnableReadback', cpp: 'Uint16InputNode', refreshInterval: 1000 }] : []),
      { id: 6, name: 'vitesse', field: 'actualSpeed', cpp: 'Uint16InputNode', refreshInterval: 500, label: 'Vitesse (tr/min)' },
    ],
  },
  {
    suffix: 'registres ecrits',
    type: 'ModbusWriteHoldingRegister',
    modbus: true,
    nodes: [
      { id: 1, name: 'mode pr0', field: 'prMode', cpp: 'Uint16OutputNode' },
      { id: 2, name: 'vitesse', field: 'moveSpeed', cpp: 'Uint16OutputNode' },
      { id: 3, name: 'acceleration', field: 'accelTime', cpp: 'Uint16OutputNode' },
      { id: 4, name: 'deceleration', field: 'decelTime', cpp: 'Uint16OutputNode' },
      { id: 5, name: 'declenchement', field: 'trigger', cpp: 'Uint16OutputNode' },
      { id: 6, name: 'mot de commande', field: 'controlWord', cpp: 'Uint16OutputNode' },
      ...(closedLoop ? [{ id: 7, name: 'alimentation', field: 'softwareEnable', cpp: 'Uint16OutputNode' }] : []),
      { id: 9, name: 'courant crete', field: 'peakCurrent', cpp: 'Uint16OutputNode' },
    ],
  },
  {
    suffix: 'position lue',
    type: 'ModbusReadDobbleHoldingRegister',
    modbus: true,
    nodes: [
      { id: 1, name: 'position brute', field: 'position', cpp: 'Uint32InputNode', refreshInterval: 250 },
      ...(closedLoop ? [{ id: 2, name: 'position codeur', field: 'feedbackPosition', cpp: 'Uint32InputNode', refreshInterval: 1000 }] : []),
    ],
  },
  {
    suffix: 'consignes',
    type: 'ModbusWriteDobbleHoldingRegister',
    modbus: true,
    nodes: [{ id: 1, name: 'cible', field: 'targetPosition', cpp: 'Uint32OutputNode' }],
  },
  {
    suffix: 'position',
    type: 'tx-uint32',
    virtual: true,
    nodes: [{ id: 1, name: 'position', field: 'displayPosition', cpp: 'Uint32OutputNode', label: 'Position (pas)', display: 'position' }],
  },
  {
    suffix: 'etat',
    type: 'tx-bool',
    virtual: true,
    nodes: [
      { id: 1, name: 'pret', field: 'servoReady', cpp: 'BooleanOutputNode', label: 'Variateur prêt' },
      { id: 2, name: 'en position', field: 'inPosition', cpp: 'BooleanOutputNode', label: 'En position' },
      { id: 3, name: 'vitesse nulle', field: 'zeroSpeed', cpp: 'BooleanOutputNode', label: 'Moteur à l’arrêt' },
      { id: 4, name: 'origine faite', field: 'homeDone', cpp: 'BooleanOutputNode', label: 'Origine faite' },
      { id: 5, name: 'alarme', field: 'servoAlarm', cpp: 'BooleanOutputNode', label: 'Alarme variateur' },
      { id: 6, name: 'initialise', field: 'driveInitialised', cpp: 'BooleanOutputNode', label: 'Variateur initialisé' },
      { id: 7, name: 'erreur modbus', field: 'modbusError', cpp: 'BooleanOutputNode', label: 'Erreur Modbus' },
    ],
  },
];

const stepperOnlineRsOptions = (models, maxCurrent) => [
  { key: 'model', label: 'Modèle', type: 'select', default: models[0], choices: models.map((m) => ({ value: m, label: m })) },
  { key: 'pulsesPerUnit', label: 'Pas par unité', type: 'number', default: 1, help: '1 = travail direct en pas (Pr0.00 impulsions par tour).' },
  { key: 'maxRange', label: 'Course maximale (unités)', type: 'number', default: 0, help: 'Butée logicielle haute ; 0 = pas de butée haute.' },
  { key: 'accelTime', label: 'Rampe (ms pour 1000 tr/min)', type: 'number', default: 300, help: '0 = rampe du variateur conservée.' },
  { key: 'torqueRefCurrent', label: 'Courant crête du moteur (0,1 A)', type: 'number', default: 0, help: `100 % du couple de l’axe ; 0 = couple non géré. Maximum ${maxCurrent / 10} A.` },
];

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
    ethernet: true, // bloc ETH du fichier matériel
    label: 'Waveshare ESP32-S3-POE-ETH-8DI-8DO',
    description: 'Automate ESP32-S3 : 8 entrées optocouplées, 8 sorties transistor (TCA9554), RS485 isolé, horloge PCF85063, buzzer, Ethernet W5500 (PoE).',
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
    ethernet: true, // bloc ETH du fichier matériel
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
    ethernet: true, // bloc ETH du fichier matériel
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
    ethernet: true, // bloc ETH du fichier matériel
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
    ethernet: true, // bloc ETH du fichier matériel
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

  ModbusGeneric: {
    defaultLabel: 'Robot',
    label: 'Équipement Modbus générique (robot, automate, passerelle…)',
    description: "Tout équipement Modbus (RTU ou TCP) : 16 bobines écrites et lues, 16 registres écrits, lus et d'entrée, à partir des adresses de départ réglées dans les options. Idéal pour piloter un robot par poignée de main (n° de programme, départ, en cours, fini).",
    role: 'modbus-io',
    hardware: null, // fichier matériel généré par PLC Studio : data/hardware/Modbus_<nom>.json
    generic: true,
    modbus: true,
    defaultAddress: 1,
    defaultTransport: 'tcp',
    options: [
      { key: 'oneBased', label: 'Adresses de la documentation comptées à partir de 1', type: 'bool', default: false, help: 'Coché : « 40001 », « 1 » = premier registre (l’outil retire 1).' },
      { key: 'coilOutStart', label: 'Adresse de Q1 (bobines écrites)', type: 'number', default: 0 },
      { key: 'coilInStart', label: 'Adresse de I1 (bobines lues)', type: 'number', default: 0 },
      { key: 'regOutStart', label: 'Adresse de W1 (registres écrits)', type: 'number', default: 0 },
      { key: 'regInStart', label: 'Adresse de R1 (registres lus)', type: 'number', default: 0 },
      { key: 'inputRegStart', label: 'Adresse de IR1 (registres d’entrée)', type: 'number', default: 0 },
    ],
    groups: [
      { key: 'q', label: 'Bobines écrites (FC05)', short: 'Bobines ecrites', type: 'ModbusWriteCoil', dir: 'out', dataType: 'bool', ids: range(1, 16), prefix: 'Q' },
      { key: 'i', label: 'Bobines lues (FC01)', short: 'Bobines lues', type: 'ModbusReadCoil', dir: 'in', dataType: 'bool', ids: range(1, 16), prefix: 'I', supportsInverse: true, refreshInterval: 100 },
      { key: 'w', label: 'Registres écrits (FC06)', short: 'Registres ecrits', type: 'ModbusWriteHoldingRegister', dir: 'out', dataType: 'int', ids: range(1, 16), prefix: 'W' },
      { key: 'r', label: 'Registres lus (FC03)', short: 'Registres lus', type: 'ModbusReadHoldingRegister', dir: 'in', dataType: 'int', ids: range(1, 16), prefix: 'R', refreshInterval: 200 },
      { key: 'ir', label: 'Registres d’entrée lus (FC04)', short: 'Registres entree', type: 'ModbusReadInputRegister', dir: 'in', dataType: 'int', ids: range(1, 16), prefix: 'IR', refreshInterval: 200 },
    ],
  },

  Robot_Aubo: {
    defaultLabel: 'Robot',
    label: 'Robot AUBO (contrôleur ARCS, Modbus TCP)',
    description: "Cobot AUBO à contrôleur ARCS, serveur Modbus TCP (port 502, unité 1). Poignée de main dans les registres généraux 300 à 331, avec un programme robot qui suit cette convention ; départ, arrêt, pause et états critiques en TOR câblés. Mise en service : docs/robots/aubo.md.",
    role: 'modbus-io',
    hardware: 'Robot_Aubo',
    modbus: true,
    defaultAddress: 1,
    defaultTransport: 'tcp',
    groups: [
      { key: 'cmd', label: 'Consignes vers le robot (300-308)', short: 'Consignes', type: 'ModbusWriteHoldingRegister', dir: 'out', dataType: 'int', ids: range(1, 9), prefix: 'CMD',
        names: { 1: 'PROG', 2: 'DEPART', 3: 'ABANDON', 4: 'ACQUIT', 5: 'P1', 6: 'P2', 7: 'P3', 8: 'P4', 9: 'VIE_API' } },
      { key: 'sts', label: 'Retours du robot (310-319)', short: 'Retours', type: 'ModbusReadHoldingRegister', dir: 'in', dataType: 'int', ids: range(1, 10), prefix: 'STS', refreshInterval: 200,
        names: { 1: 'PRET', 2: 'EN_COURS', 3: 'FINI', 4: 'DEFAUT', 5: 'ECHO_PROG', 6: 'VIE_ROBOT', 7: 'V1', 8: 'V2', 9: 'V3', 10: 'V4' } },
      { key: 'pos', label: 'Positions (320-331, 0,1 mm / 0,1°, signées)', short: 'Positions', type: 'ModbusReadHoldingRegister', dir: 'in', dataType: 'int', ids: range(11, 22), prefix: 'POS', refreshInterval: 1000, signed: true,
        names: { 11: 'TCP_X', 12: 'TCP_Y', 13: 'TCP_Z', 14: 'TCP_RX', 15: 'TCP_RY', 16: 'TCP_RZ', 17: 'J1', 18: 'J2', 19: 'J3', 20: 'J4', 21: 'J5', 22: 'J6' } },
    ],
  },

  Robot_Fairino: {
    defaultLabel: 'Robot',
    label: 'Robot FAIRINO série FR (Modbus TCP)',
    description: "Cobot FAIRINO série FR (contrôleur V3.7.3 ou plus), serveur Modbus TCP (192.168.58.2, port 502, unité 1). Bobine 502 = départ du programme configuré ; poignée de main par les signaux utilisateur de l'esclave (adresses à confirmer) ; arrêt, pause et acquittement en TOR câblés. Mise en service : docs/robots/fairino.md.",
    role: 'modbus-io',
    hardware: 'Robot_Fairino',
    modbus: true,
    defaultAddress: 1,
    defaultTransport: 'tcp',
    defaultIp: '192.168.58.2',
    groups: [
      { key: 'start', label: 'Départ programme (bobine 502)', short: 'Depart', type: 'ModbusWriteCoil', dir: 'out', dataType: 'bool', ids: [1], prefix: 'START', names: { 1: 'DEMARRER' } },
      { key: 'do', label: 'Sorties du coffret robot (bobines 300-307)', short: 'Sorties robot', type: 'ModbusWriteCoil', dir: 'out', dataType: 'bool', ids: range(2, 9), prefix: 'DO',
        names: { 2: 'DO0', 3: 'DO1', 4: 'DO2', 5: 'DO3', 6: 'DO4', 7: 'DO5', 8: 'DO6', 9: 'DO7' } },
      { key: 'cmd', label: 'Commandes vers le robot (bobines utilisateur)', short: 'Commandes', type: 'ModbusWriteMultipleCoils', dir: 'out', dataType: 'bool', bank: { nodeId: 1, bits: 8 }, prefix: 'CMD',
        names: { 0: 'DEPART', 1: 'ABANDON', 2: 'ACQUIT' } },
      { key: 'sts', label: 'États du robot (entrées TOR utilisateur)', short: 'Etats', type: 'ModbusReadMultipleInputsStatus', dir: 'in', dataType: 'bool', bank: { nodeId: 1, bits: 8 }, prefix: 'STS', supportsInverse: true,
        names: { 0: 'PRET', 1: 'EN_COURS', 2: 'FINI', 3: 'DEFAUT' } },
      { key: 'w', label: 'Consignes vers le robot', short: 'Consignes', type: 'ModbusWriteHoldingRegister', dir: 'out', dataType: 'int', ids: range(1, 5), prefix: 'W',
        names: { 1: 'PROG', 2: 'P1', 3: 'P2', 4: 'P3', 5: 'VIE_API' } },
      { key: 'ir', label: 'Retours du robot', short: 'Retours', type: 'ModbusReadInputRegister', dir: 'in', dataType: 'int', ids: range(1, 5), prefix: 'IR', refreshInterval: 200,
        names: { 1: 'ECHO_PROG', 2: 'CODE_DEFAUT', 3: 'VIE_ROBOT', 4: 'V1', 5: 'V2' } },
      { key: 'pos', label: 'Positions (0,1 mm / 0,1°, signées)', short: 'Positions', type: 'ModbusReadInputRegister', dir: 'in', dataType: 'int', ids: range(6, 17), prefix: 'POS', refreshInterval: 1000, signed: true,
        names: { 6: 'TCP_X', 7: 'TCP_Y', 8: 'TCP_Z', 9: 'TCP_RX', 10: 'TCP_RY', 11: 'TCP_RZ', 12: 'J1', 13: 'J2', 14: 'J3', 15: 'J4', 16: 'J5', 17: 'J6' } },
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

  StepperOnlineDM_RS: {
    defaultLabel: 'Axe',
    label: 'Pas-à-pas StepperOnline DM556RS / DM882RS (Modbus)',
    description: "Variateur pas-à-pas boucle ouverte StepperOnline RS (mode PR, protocole Leadshine) piloté en Modbus RTU (usine 38400 8N1). Mise en service : docs/axes/stepperonline-rs.md.",
    role: 'axis',
    hardware: 'StepperOnline_DM_RS',
    modbus: true,
    defaultAddress: 1,
    groups: [],
    axis: { ...STEPPERONLINE_RS_AXIS, closedLoop: false, models: ['DM556RS', 'DM882RS'] },
    options: stepperOnlineRsOptions(['DM556RS', 'DM882RS'], 82),
    sections: stepperOnlineRsSections(false),
  },

  StepperOnlineCL_RS: {
    defaultLabel: 'Axe',
    label: 'Pas-à-pas boucle fermée StepperOnline CL57RS / CL86RS (Modbus)',
    description: "Variateur pas-à-pas boucle fermée StepperOnline RS (mode PR, protocole Leadshine) piloté en Modbus RTU (usine 38400 8N1). Mise en service : docs/axes/stepperonline-rs.md.",
    role: 'axis',
    hardware: 'StepperOnline_CL_RS',
    modbus: true,
    defaultAddress: 1,
    groups: [],
    axis: { ...STEPPERONLINE_RS_AXIS, closedLoop: true, models: ['CL57RS', 'CL86RS'] },
    options: stepperOnlineRsOptions(['CL57RS', 'CL86RS'], 80),
    sections: stepperOnlineRsSections(true),
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

// Transport de chaque équipement Modbus : bus RS485 de l'automate (RTU) ou réseau (TCP,
// équipement natif ou passerelle Modbus TCP -> RTU : l'adresse est alors le n° d'unité).
export const MODBUS_TRANSPORT_OPTIONS = [
  {
    key: 'transport',
    label: 'Liaison Modbus',
    type: 'select',
    default: 'rtu',
    choices: [
      { value: 'rtu', label: 'RS485 (Modbus RTU)' },
      { value: 'tcp', label: 'Réseau (Modbus TCP)' },
    ],
    help: 'TCP : équipement Modbus TCP, ou passerelle TCP → RTU (l’adresse devient le n° d’unité).',
  },
  { key: 'ip', label: 'Adresse IP (TCP)', type: 'text', default: '', help: 'Modbus TCP seulement, ex. 192.168.1.50.' },
  { key: 'port', label: 'Port (TCP)', type: 'number', default: 502, help: 'Modbus TCP seulement (502 par défaut).' },
];
for (const entry of Object.values(CATALOG)) {
  if (!entry.modbus) continue;
  const transport = MODBUS_TRANSPORT_OPTIONS.map((o) => {
    if (o.key === 'transport' && entry.defaultTransport) return { ...o, default: entry.defaultTransport };
    if (o.key === 'ip' && entry.defaultIp) return { ...o, default: entry.defaultIp };
    return o;
  });
  entry.options = [...transport, ...(entry.options || [])];
}

// Types de variables "internes" (non câblées) proposées à l'étape Variables.
export const VARIABLE_KINDS = {
  command: { label: 'Commande',  help: "Bouton ou interrupteur à l'écran, lu par la logique.", dataTypes: ['bool', 'int', 'float'] },
  parameter: { label: 'Paramètre', help: "Consigne réglable à l'écran et conservée après redémarrage.", dataTypes: ['int', 'float', 'text', 'bool'] },
  indicator: { label: 'Indicateur', help: "Valeur calculée par la logique et affichée à l'écran.", dataTypes: ['bool', 'int', 'float', 'text'] },
  memory: { label: 'Mémoire', help: 'Variable de travail de la logique (compteur, mémoire), sans affichage.', dataTypes: ['bool', 'int', 'float'] },
  input: { label: 'Entrée câblée', help: 'Voie d\'entrée d\'un équipement.', dataTypes: ['bool', 'int'] },
  output: { label: 'Sortie câblée', help: "Voie de sortie d'un équipement (bobine, relais, ou registre d'un équipement Modbus).", dataTypes: ['bool', 'int'] },
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
      if (t === 'bool') return ['rx-indicator', 'rx-bool'];
      return ['rx-numeric', 'rx-level-bar'];
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
