#ifndef VINYL_FH6_INJECT_H
#define VINYL_FH6_INJECT_H

#include <stdint.h>
#include <wchar.h>

/* Injection des formes directement dans la memoire du processus Forza Horizon 6
 * (etape 2b). Porte depuis ForzaDesigner6 "Multi-Support v3456" (MIT) :
 * localisation dynamique du CLiveryGroup (empreinte de sspheres + fallback RTTI),
 * validation stricte avant toute ecriture, puis ecriture des champs de chaque
 * Layer (position, echelle, rotation, couleur, shape_id, masque).
 *
 * Offsets confirmes sur FH6 build 354.221. Tout est surchargable via le profil
 * (et un fichier fh6_inject.cfg optionnel) pour s'adapter a un autre build sans
 * recompiler. */

/* Profil de jeu : tout ce qui peut varier entre builds/jeux Forge. Initialise par
 * fh6_profile_default() aux valeurs FH6 confirmees, puis eventuellement surcharge
 * par fh6_profile_load_cfg(). Concu pour rester FH6 mais extensible (FH4/FH5). */
typedef struct {
    /* Noms de processus a chercher (case-insensitive, sous-chaine). */
    const char *process_names[4];
    int         n_process_names;

    /* Chaine RTTI MSVC de la classe du groupe de vinyles (fallback). */
    const char *rtti_class_name;

    /* Offsets de la struct LiveryGroup. */
    uint32_t count_off;        /* u16 : nombre de layers */
    uint32_t table_off;        /* u64 : pointeur vers le tableau de pointeurs de layers */

    /* Offsets dans chaque struct Layer. */
    uint32_t layer_pos_off;    /* 2x f32 : x, y */
    uint32_t layer_scale_off;  /* 2x f32 : sx, sy */
    uint32_t layer_rot_off;    /* f32 : rotation (deg) */
    uint32_t layer_color_off;  /* 4x u8 : R, G, B, A */
    uint32_t layer_mask_off;   /* u8 : 0/1 */
    uint32_t layer_shape_id_off; /* u8 : 102 ellipse / 101 autre */

    /* Conversion taille JSON -> unites monde FH6. */
    float scale_div_ellipse;   /* 63.0 */
    float scale_div_other;     /* 127.0 */
    int   half_extent;         /* 1 : nos w/h sont pleins -> diviser par 2 avant /div */

    /* Valeurs d'octet shape_id. */
    int shape_id_ellipse;      /* 102 */
    int shape_id_other;        /* 101 */

    /* Options de comportement (surchargeables par cfg). */
    int force_layer_count;     /* 0 = auto (essaie le count JSON puis templates communs) */
    int clear_unused;          /* 1 = effacer (alpha 0) les slots non utilises */
    int add_boundary_masks;    /* 1 = ajouter les 4 masques de bordure (forza-painter) */
} Fh6Profile;

/* Session attachee a un processus FH6. */
typedef struct {
    unsigned long     pid;        /* DWORD */
    void             *handle;     /* HANDLE du processus (NULL si non attache) */
    const Fh6Profile *profile;
    uint64_t          group_addr;
    uint64_t          table_addr;
    int               layer_count;
    uint64_t         *layer_addrs; /* malloc, taille layer_count ; libere par fh6_detach */
    int               located_via_rtti; /* 1 si trouve par RTTI, 0 par empreinte */
} Fh6Session;

/* Forme issue d'un JSON FH6, deja convertie (centre + dimensions pleines). */
typedef struct {
    int     type;        /* 1 = rect, 16 = ellipse tournee */
    float   x, y, w, h, angle;
    uint8_t rgba[4];
    int     is_bg;       /* 1 = fond (shapes[0]) */
} Fh6JsonShape;

/* Document JSON FH6 charge en memoire (pour l'apercu + l'injection). */
typedef struct {
    Fh6JsonShape *shapes;     /* malloc ; [0] = fond */
    int           count;      /* total, fond inclus */
    int           image_w, image_h;
    int           n_rect;     /* drawables type 1 (alpha > 0) */
    int           n_ellipse;  /* drawables type 16 (alpha > 0) */
    int           bg_alpha;   /* alpha du fond (0 = sticker) */
} Fh6JsonDoc;

/* Progression / annulation / timeout pour les scans memoire (longs). Passe (peut
 * etre NULL) a fh6_locate / fh6_inject_doc. Le scan verifie cancel + deadline
 * regulierement et appelle on_status pour rapporter l'avancement. */
typedef struct {
    volatile long *cancel;       /* != 0 => abandon propre ; peut etre NULL */
    void (*on_status)(void *ctx, const char *msg); /* peut etre NULL */
    void          *ctx;          /* passe a on_status */
    unsigned long long deadline_ms; /* limite GetTickCount64() ; 0 = pas de limite */
} Fh6Progress;

/* Initialise le profil aux valeurs FH6 confirmees. */
void fh6_profile_default(Fh6Profile *p);

/* Charge et parse un JSON FH6 exporte par l'app. Renvoie 0 (remplit out), -1 sinon. */
int  fh6_load_json(const wchar_t *json_path, Fh6JsonDoc *out);

/* Libere un document charge par fh6_load_json. */
void fh6_free_json(Fh6JsonDoc *doc);

/* Applique les surcharges du fichier INI cfg_path s'il existe. Renvoie le nombre
 * de cles appliquees, ou -1 si le fichier est absent/illisible (non fatal). */
int fh6_profile_load_cfg(Fh6Profile *p, const wchar_t *cfg_path);

/* Trouve le pid de FH6 et ouvre le processus. report : message lisible (peut etre
 * NULL). Renvoie 0 si OK, <0 sinon (FH6 introuvable, OpenProcess echoue...). */
int fh6_attach(Fh6Session *s, const Fh6Profile *p, char *report, int report_sz);

/* Localise le groupe de vinyles actif (LECTURE SEULE, diagnostic). want_count :
 * nombre de layers attendu, ou 0 pour essayer les templates communs. prog : peut
 * etre NULL (progression/annulation/timeout). Remplit s->group_addr/table_addr/
 * layer_count/layer_addrs. Renvoie 0 si trouve, <0 sinon. report decrit le chemin
 * (empreinte/RTTI), les adresses et le % de layers valides, ou un diagnostic d'echec. */
int fh6_locate(Fh6Session *s, int want_count, const Fh6Progress *prog,
               char *report, int report_sz);

/* Injecte un document deja charge (fh6_load_json). template_count : nombre de
 * layers du template charge en jeu (0 = auto : essaie le count du JSON puis les
 * templates communs plus grands). prog : peut etre NULL. Localise le groupe puis
 * ecrit chaque forme. Renvoie le nombre de formes ecrites (>=0), ou <0 en cas
 * d'erreur. */
int fh6_inject_doc(Fh6Session *s, const Fh6JsonDoc *doc, int template_count,
                   const Fh6Progress *prog, char *report, int report_sz);

/* Convenance : charge le JSON puis l'injecte (fh6_load_json + fh6_inject_doc). */
int fh6_inject_json(Fh6Session *s, const wchar_t *json_path, int template_count,
                    const Fh6Progress *prog, char *report, int report_sz);

/* Ferme le handle et libere layer_addrs. */
void fh6_detach(Fh6Session *s);

#endif