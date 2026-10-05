#ifndef CLOCK_DISPLAY_H
#define CLOCK_DISPLAY_H

class VirtualTextOutputNode;

/**
 * @brief Masque le champ horloge du HMI quand aucune horloge matérielle n'est détectée
 *
 * Préoccupation orthogonale à shouldShowClock()/clearClock() (BusinessLogic), qui gèrent
 * l'affichage selon l'état machine (IDLE/EMERGENCY/STOP). Ici il s'agit uniquement de la
 * présence du matériel RTC, vérifiée une seule fois au démarrage.
 *
 * Utilisation (une seule fois, au moment de la création du node horloge) :
 *   initNodePtr(v_clock, V_CLOCK, contexte);
 *   ClockDisplay::attach(v_clock);
 *
 * Un projet qui n'appelle jamais attach() n'est impacté par rien de cette classe.
 */
class ClockDisplay {
public:
    /**
     * @brief Masque le node horloge si RTC_Service ne détecte aucune horloge matérielle
     * @param node Node d'affichage de l'horloge (peut être nullptr, ignoré dans ce cas)
     */
    static void attach(VirtualTextOutputNode* node);
};

#endif // CLOCK_DISPLAY_H
