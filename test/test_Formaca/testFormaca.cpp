// test/test_Formaca/testFormaca.cpp
#include <unity.h>
#include <cstring>
#include <cmath>
#include <Arduino.h>  // IMPORTANT: Pour delay() et autres fonctions Arduino

// ============================================
// DÉFINITIONS DES CONSTANTES
// ============================================
#define CONSTR_FORMACA "FORMACA"
#define WIDTH_LENGTH "WIDTH_LENGTH"
#define LENGTH "LENGTH"
#define WASTE_LENGTH "WASTE_LENGTH"
#define BLADE_TICKNESS 0.5f

// ============================================
// CLASSES MOCK POUR LES TESTS
// ============================================

// Classe de base Node
class Node {
public:
    virtual ~Node() = default;
};

// Mock pour les valeurs booléennes
class BooleanOutputNode : public Node {
public:
    bool value = false;
    void setValue(bool val) { value = val; }
    bool getValue() const { return value; }
};

// Mock pour les valeurs flottantes
class FloatOutputNode : public Node {
public:
    float value = 0.0f;
    void setValue(float val) { value = val; }
    float getValue() const { return value; }
};

// ============================================
// MOCK DU NAMESPACE BorneUniverselle
// ============================================
// TOUJOURS défini dans le contexte de test

namespace BorneUniverselle {
    // Stockage statique des mocks
    static FloatOutputNode mockWidth;
    static FloatOutputNode mockLength;
    static FloatOutputNode mockWaste;
    static BooleanOutputNode mockBool;
    
    inline Node* findNode(const char* context, const char* name) {
        if (strcmp(name, WIDTH_LENGTH) == 0) {
            return &mockWidth;
        } else if (strcmp(name, LENGTH) == 0) {
            return &mockLength;
        } else if (strcmp(name, WASTE_LENGTH) == 0) {
            return &mockWaste;
        }
        return &mockBool;
    }
    
    inline bool isPlcBroken() { 
        return false; 
    }
    
    inline void setPlcBroken(const char* error) {
        // Log pour debug si nécessaire
    }
    
    inline void prepareMessage(const char* type, const char* message) {
        // Log pour debug si nécessaire
    }
}

// ============================================
// STRUCTURE DE RECETTE
// ============================================
struct Recette {
    char name[32];
    float width;
    float longlength;
    float angle;
};

// ============================================
// STRUCTURE PARAMETERS
// ============================================
struct Parameters {
    Recette recettes[10];
    int nbRecettes;
};

// ============================================
// CLASSE FORMACA SIMPLIFIÉE POUR LES TESTS
// ============================================
class Formaca {
protected:
    Parameters parameters;
    FloatOutputNode* widthLength;
    FloatOutputNode* longLength;
    FloatOutputNode* wasteLength;
    
    float angleRadians;
    float bladelostLength;
    float totalWasteLength;
    
public:
    Formaca() {
        // Initialisation des pointeurs vers les mocks
        widthLength = (FloatOutputNode*)BorneUniverselle::findNode(CONSTR_FORMACA, WIDTH_LENGTH);
        longLength = (FloatOutputNode*)BorneUniverselle::findNode(CONSTR_FORMACA, LENGTH);
        wasteLength = (FloatOutputNode*)BorneUniverselle::findNode(CONSTR_FORMACA, WASTE_LENGTH);
        
        angleRadians = 0.0f;
        bladelostLength = 0.0f;
        totalWasteLength = 0.0f;
    }
    
    bool setNewRecette(const char* recetteName) {
        for (int i = 0; i < parameters.nbRecettes; i++) {
            if (strcmp(parameters.recettes[i].name, recetteName) == 0) {
                // Mise à jour des valeurs
                widthLength->setValue(parameters.recettes[i].width);
                longLength->setValue(parameters.recettes[i].longlength);
                
                // Calcul de l'angle en radians
                angleRadians = parameters.recettes[i].angle * M_PI / 180.0f;
                
                // Calcul de la perte de lame
                bladelostLength = BLADE_TICKNESS / (2 * sin(angleRadians));
                
                // Calcul de la perte totale
                totalWasteLength = wasteLength->getValue() + bladelostLength;
                
                return true;
            }
        }
        return false;
    }
    
    // Accesseurs pour les tests
    float getAngleRadians() const { return angleRadians; }
    float getTotalWasteLength() const { return totalWasteLength; }
    float getBladeLostLength() const { return bladelostLength; }
};

// ============================================
// CLASSE DE TEST HÉRITÉE
// ============================================
class FormacaTest : public Formaca {
public:
    FormacaTest() : Formaca() {
        // Configuration des recettes de test
        parameters.nbRecettes = 2;
        
        // Recette 1
        strcpy(parameters.recettes[0].name, "Recette1");
        parameters.recettes[0].width = 10.0f;
        parameters.recettes[0].longlength = 20.0f;
        parameters.recettes[0].angle = 45.0f;
        
        // Recette 2
        strcpy(parameters.recettes[1].name, "Recette2");
        parameters.recettes[1].width = 15.0f;
        parameters.recettes[1].longlength = 25.0f;
        parameters.recettes[1].angle = 30.0f;
        
        // Valeur initiale pour wasteLength
        wasteLength->setValue(5.0f);
    }
    
    // Exposition des membres pour les tests
    FloatOutputNode* getWidthLength() { return widthLength; }
    FloatOutputNode* getLongLength() { return longLength; }
    FloatOutputNode* getWasteLength() { return wasteLength; }
    
    // Méthode pour ajouter une recette (pour test_angle_calculation_edge_cases)
    void addRecette(const char* name, float width, float longlength, float angle) {
        if (parameters.nbRecettes < 10) {
            strcpy(parameters.recettes[parameters.nbRecettes].name, name);
            parameters.recettes[parameters.nbRecettes].width = width;
            parameters.recettes[parameters.nbRecettes].longlength = longlength;
            parameters.recettes[parameters.nbRecettes].angle = angle;
            parameters.nbRecettes++;
        }
    }
};

// ============================================
// DÉCLARATION DES TESTS
// ============================================
void test_setNewRecette_updates_parameters_correctly();
void test_setNewRecette_invalid_recipe();
void test_angle_calculation_edge_cases();
void test_waste_length_accumulation();

// ============================================
// TESTS UNITAIRES
// ============================================

void test_setNewRecette_updates_parameters_correctly() {
    // Arrange
    FormacaTest formaca;
    
    // Act & Assert - Test avec la première recette
    TEST_MESSAGE("Test avec Recette1");
    bool result = formaca.setNewRecette("Recette1");
    
    TEST_ASSERT_TRUE_MESSAGE(result, "setNewRecette devrait retourner true pour une recette valide");
    TEST_ASSERT_EQUAL_FLOAT(10.0f, formaca.getWidthLength()->getValue());
    TEST_ASSERT_EQUAL_FLOAT(20.0f, formaca.getLongLength()->getValue());
    
    // Vérification de l'angle (45° = π/4 ≈ 0.7854 radians)
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.7854f, formaca.getAngleRadians());
    
    // Vérification du calcul de perte de lame
    float expectedBladeLostLength = BLADE_TICKNESS / (2 * sin(0.7854f));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, expectedBladeLostLength, formaca.getBladeLostLength());
    
    // Vérification de la perte totale
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 5.0f + expectedBladeLostLength, formaca.getTotalWasteLength());
    
    // Act & Assert - Test avec la deuxième recette
    TEST_MESSAGE("Test avec Recette2");
    result = formaca.setNewRecette("Recette2");
    
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL_FLOAT(15.0f, formaca.getWidthLength()->getValue());
    TEST_ASSERT_EQUAL_FLOAT(25.0f, formaca.getLongLength()->getValue());
    
    // Vérification de l'angle (30° = π/6 ≈ 0.5236 radians)
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.5236f, formaca.getAngleRadians());
    
    // Vérification des calculs pour 30°
    expectedBladeLostLength = BLADE_TICKNESS / (2 * sin(0.5236f));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, expectedBladeLostLength, formaca.getBladeLostLength());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 5.0f + expectedBladeLostLength, formaca.getTotalWasteLength());
}

void test_setNewRecette_invalid_recipe() {
    // Arrange
    FormacaTest formaca;
    
    // Sauvegarder les valeurs initiales
    float initialWidth = formaca.getWidthLength()->getValue();
    float initialLength = formaca.getLongLength()->getValue();
    
    // Act
    TEST_MESSAGE("Test avec une recette inexistante");
    bool result = formaca.setNewRecette("RecetteInvalide");
    
    // Assert
    TEST_ASSERT_FALSE_MESSAGE(result, "setNewRecette devrait retourner false pour une recette invalide");
    
    // Vérifier que les valeurs n'ont pas changé
    TEST_ASSERT_EQUAL_FLOAT_MESSAGE(
        initialWidth, 
        formaca.getWidthLength()->getValue(),
        "La largeur ne devrait pas changer avec une recette invalide"
    );
    TEST_ASSERT_EQUAL_FLOAT_MESSAGE(
        initialLength, 
        formaca.getLongLength()->getValue(),
        "La longueur ne devrait pas changer avec une recette invalide"
    );
}

void test_angle_calculation_edge_cases() {
    // Arrange
    FormacaTest formaca;
    
    // Ajouter une recette avec angle = 90° via la méthode publique
    formaca.addRecette("Recette90", 10.0f, 10.0f, 90.0f);
    
    // Act
    TEST_MESSAGE("Test avec angle = 90°");
    bool result = formaca.setNewRecette("Recette90");
    
    // Assert
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, M_PI/2, formaca.getAngleRadians());
    
    // Pour 90°, sin(90°) = 1, donc bladeLostLength = BLADE_TICKNESS / 2
    float expectedBladeLostLength = BLADE_TICKNESS / 2.0f;
    TEST_ASSERT_FLOAT_WITHIN(0.01f, expectedBladeLostLength, formaca.getBladeLostLength());
}

void test_waste_length_accumulation() {
    // Arrange
    FormacaTest formaca;
    
    // Modifier la valeur de waste initiale
    TEST_MESSAGE("Test de l'accumulation des pertes");
    formaca.getWasteLength()->setValue(10.0f);
    
    // Act
    formaca.setNewRecette("Recette1");
    
    // Assert
    float bladeLost = BLADE_TICKNESS / (2 * sin(M_PI/4));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 10.0f + bladeLost, formaca.getTotalWasteLength());
}

// ============================================
// POINT D'ENTRÉE DES TESTS
// ============================================

void setUp(void) {
    // Réinitialisation des mocks avant chaque test
    BorneUniverselle::mockWidth.setValue(0.0f);
    BorneUniverselle::mockLength.setValue(0.0f);
    BorneUniverselle::mockWaste.setValue(0.0f);
}

void tearDown(void) {
    // Nettoyage après chaque test si nécessaire
}

int runUnityTests(void) {
    UNITY_BEGIN();
    
    RUN_TEST(test_setNewRecette_updates_parameters_correctly);
    RUN_TEST(test_setNewRecette_invalid_recipe);
    RUN_TEST(test_angle_calculation_edge_cases);
    RUN_TEST(test_waste_length_accumulation);
    
    return UNITY_END();
}

// Pour Arduino/ESP32
#ifdef ARDUINO
void setup() {
    // Attendre 2 secondes pour la connexion série
    delay(2000);
    
    Serial.begin(115200);
    Serial.println("\n=====================================");
    Serial.println("     FORMACA UNIT TESTS");
    Serial.println("=====================================");
    
    runUnityTests();
    
    Serial.println("\n=====================================");
    Serial.println("     ALL TESTS COMPLETED");
    Serial.println("=====================================");
}

void loop() {
    // Rien à faire
    delay(1000);
}
#endif

// Pour test natif sur PC
#ifndef ARDUINO
int main(int argc, char **argv) {
    return runUnityTests();
}
#endif