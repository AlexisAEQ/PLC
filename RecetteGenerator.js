// Script pour générer une configuration JSON avec 256 recettes
const baseConfig = {
  "machine": "Formaca scie",
  "home": 700,
  "reference": 3101409,
  "overall length": 96,
  "park offset": 0.5,
  "wast length": 6,
  "right stop": 97,
  "recettes": [
    {
      "longLength": 11.875,
      "angle": 45,
      "name": "11 7/8",
      "width": 3.5
    }
  ],
  "current recette": "11 7/8"
};

// Fonction pour générer des recettes variées
function generateRecipes(count) {
  const recipes = [];
  
  // Garder la première recette originale
  recipes.push(baseConfig.recettes[0]);
  
  // Générer les recettes supplémentaires
  for (let i = 1; i < count; i++) {
    // Générer des valeurs aléatoires mais réalistes
    const longLength = parseFloat((Math.random() * 20 + 5).toFixed(3)); // Entre 5 et 25 pouces
    const angle = Math.floor(Math.random() * 60) + 15; // Entre 15 et 75 degrés
    const width = parseFloat((Math.random() * 5 + 2).toFixed(1)); // Entre 2 et 7 pouces
    
    // Formater les noms de recettes avec des fractions
    let name;
    const wholePart = Math.floor(longLength);
    const fraction = longLength - wholePart;
    
    if (fraction === 0) {
      name = `${wholePart}`;
    } else if (Math.abs(fraction - 0.25) < 0.01) {
      name = `${wholePart} 1/4`;
    } else if (Math.abs(fraction - 0.5) < 0.01) {
      name = `${wholePart} 1/2`;
    } else if (Math.abs(fraction - 0.75) < 0.01) {
      name = `${wholePart} 3/4`;
    } else if (Math.abs(fraction - 0.125) < 0.01) {
      name = `${wholePart} 1/8`;
    } else if (Math.abs(fraction - 0.375) < 0.01) {
      name = `${wholePart} 3/8`;
    } else if (Math.abs(fraction - 0.625) < 0.01) {
      name = `${wholePart} 5/8`;
    } else if (Math.abs(fraction - 0.875) < 0.01) {
      name = `${wholePart} 7/8`;
    } else {
      // Si c'est une fraction inhabituelle, convertir en décimal
      name = longLength.toString();
    }
    
    recipes.push({
      longLength,
      angle,
      name,
      width
    });
  }
  
  return recipes;
}

// Générer la configuration avec 256 recettes
const config = { ...baseConfig };
config.recettes = generateRecipes(256);

// Afficher la configuration complète
console.log(JSON.stringify(config, null, 2));

// Afficher également un aperçu des premières et dernières recettes
console.log("\nAperçu des premières recettes:");
console.log(JSON.stringify(config.recettes.slice(0, 5), null, 2));

console.log("\nAperçu des dernières recettes:");
console.log(JSON.stringify(config.recettes.slice(-5), null, 2));

console.log(`\nNombre total de recettes: ${config.recettes.length}`);