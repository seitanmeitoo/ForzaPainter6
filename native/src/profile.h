#ifndef VINYL_PROFILE_H
#define VINYL_PROFILE_H

/* Profils de generation portes depuis legacy/shapegen/profile.py.
 * Definit le triple (random_samples, mutated_samples, stop_at) par defaut
 * pour 6 niveaux de qualite. Les valeurs sont indicatives : l'UI permet
 * de les ajuster individuellement apres selection du preset. */

typedef struct Preset {
    const char *name;
    int random_samples;
    int mutated_samples;
    int stop_at;
} Preset;

#define FH6_MAX_SHAPES 3000

static const Preset PRESETS[] = {
    { "Apercu",         150,  40,  200 },
    { "Rapide",         300,  80,  500 },
    { "Equilibre",     1000, 200, 1500 },
    { "Detaille",      1500, 300, 2500 },
    { "Qualite",       2500, 500, 2800 },
    { "Ultra qualite", 4000, 800, 3000 },
};

#define N_PRESETS          ((int)(sizeof(PRESETS) / sizeof(PRESETS[0])))
#define DEFAULT_PRESET_IDX 2  /* Equilibre */

#endif
