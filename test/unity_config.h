#ifndef UNITY_CONFIG_H
#define UNITY_CONFIG_H

// ========================================
// Configuration Unity pour tests natifs
// ========================================

// Types de données personnalisés
#define UNITY_INT_WIDTH 32
#define UNITY_FLOAT_TYPE float
#define UNITY_DOUBLE_TYPE double

// Options de sortie
#ifdef NATIVE_TEST
    #include <stdio.h>
    #define UNITY_OUTPUT_CHAR(a) putchar(a)
    #define UNITY_OUTPUT_START() 
    #define UNITY_OUTPUT_COMPLETE() 
#endif

// Options de mémoire (pour embedded)
#ifdef ESP32
    #define UNITY_EXCLUDE_STDLIB_MALLOC
    #define UNITY_OUTPUT_COLOR
#endif

// Options de test
#define UNITY_INCLUDE_FLOAT
#define UNITY_INCLUDE_DOUBLE
#define UNITY_FLOAT_PRECISION 0.00001
#define UNITY_DOUBLE_PRECISION 0.000001

#endif // UNITY_CONFIG_H