// Axes du projet : équipements de rôle « axis » (SureServo, Lichuan, moteur pas-à-pas).
//
// Chaque axe est désigné dans les expressions par le symbole de son nom (« Servo »,
// « Axe_X »…) : Axe_X.enPosition, et dans les actions SERVO_MOVE / SERVO_HOME / SERVO_STOP
// par l'uid de son équipement.

import { CATALOG } from './catalog.js';
import { toSymbol } from './model.js';

export function isAxis(eq) {
  return CATALOG[eq?.type]?.role === 'axis';
}

export function axisSymbol(eq) {
  return toSymbol(eq.label);
}

// [{ uid, symbol, label, eq, entry }] dans l'ordre des équipements.
export function axisList(project) {
  return (project.equipment || []).filter(isAxis).map((eq) => ({ uid: eq.uid, symbol: axisSymbol(eq), label: eq.label, eq, entry: CATALOG[eq.type] }));
}

export function axisByUid(project, uid) {
  return axisList(project).find((a) => a.uid === uid) || null;
}

// Axe implicite des projets à un seul axe (ancien format : un seul servo, sans uid d'axe).
export function defaultAxisUid(project) {
  const axes = axisList(project);
  return axes.length === 1 ? axes[0].uid : null;
}
