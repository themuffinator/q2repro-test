/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#ifndef GAME3_H
#define GAME3_H

#include "shared/list.h"

typedef struct game3_entity_state_s {
    int     number;         // edict index

    vec3_t  origin;
    vec3_t  angles;
    vec3_t  old_origin;     // for lerping
    int     modelindex;
    int     modelindex2, modelindex3, modelindex4;  // weapons, CTF flags, etc
    int     frame;
    int     skinnum;
    unsigned int        effects;        // PGM - we're filling it, so it needs to be unsigned
    int     renderfx;
    int     solid;          // for client side prediction, 8*(bits 0-4) is x/y radius
                            // 8*(bits 5-9) is z down distance, 8(bits10-15) is z up
                            // gi.linkentity sets this properly
    int     sound;          // for looping sounds, to guarantee shutoff
    int     event;          // impulse events -- muzzle flashes, footsteps, etc
                            // events only go out for a single frame, they
                            // are automatically cleared each frame
} game3_entity_state_t;

typedef struct {
    int         morefx;
    float       alpha;
    float       scale;
    float       loop_volume;
    float       loop_attenuation;
} game3_entity_state_extension_t;

typedef struct {
    game3_pmove_state_old_t   pmove;  // for prediction

    // these fields do not need to be communicated bit-precise

    vec3_t      viewangles;     // for fixed views
    vec3_t      viewoffset;     // add to pmovestate->origin
    vec3_t      kick_angles;    // add to view direction to get render angles
                                // set by weapon kicks, pain effects, etc

    vec3_t      gunangles;
    vec3_t      gunoffset;
    int         gunindex;
    int         gunframe;

    vec4_t      blend;          // rgba full screen effect

    float       fov;            // horizontal field of view

    int         rdflags;        // refdef flags

    short       stats[MAX_STATS_OLD];   // fast status bar updates
} game3_player_state_old_t;

#if USE_NEW_GAME_API
typedef struct {
    game3_pmove_state_new_t   pmove;  // for prediction

    // these fields do not need to be communicated bit-precise

    vec3_t      viewangles;     // for fixed views
    vec3_t      viewoffset;     // add to pmovestate->origin
    vec3_t      kick_angles;    // add to view direction to get render angles
                                // set by weapon kicks, pain effects, etc

    vec3_t      gunangles;
    vec3_t      gunoffset;
    int         gunindex;
    int         gunframe;
    int         reserved_1;
    int         reserved_2;

    vec4_t      blend;          // rgba full screen effect
    vec4_t      damage_blend;

    player_fog_t        fog;
    player_heightfog_t  heightfog;

    float       fov;            // horizontal field of view

    int         rdflags;        // refdef flags

    int         reserved_3;
    int         reserved_4;

    int16_t     stats[MAX_STATS_NEW];   // fast status bar updates
} game3_player_state_new_t;
#endif

#if !defined(GAME_INCLUDE)
struct game3_gclient_old_s {
    game3_player_state_old_t  ps;     // communicated by server to clients
    int                 ping;

    // set to (client POV entity number) - 1 by game,
    // only valid if g_features has GMF_CLIENTNUM bit
    int             clientNum;

    // the game dll can add anything it wants after
    // this point in the structure
};

struct game3_gclient_new_s {
    game3_player_state_new_t  ps;     // communicated by server to clients
    int                 ping;

    // set to (client POV entity number) - 1 by game,
    // only valid if g_features has GMF_CLIENTNUM bit
    int             clientNum;

    // the game dll can add anything it wants after
    // this point in the structure
};

struct game3_edict_s {
    game3_entity_state_t  s;
    void        *client;
    qboolean    inuse;
    int         linkcount;

    // FIXME: move these fields to a server private sv_entity_t
    list_t      area;               // linked to a division node or leaf

    int         num_clusters;       // if -1, use headnode instead
    int         clusternums[MAX_ENT_CLUSTERS];
    int         headnode;           // unused if num_clusters != -1
    int         areanum, areanum2;

    //================================

    int         svflags;            // SVF_NOCLIENT, SVF_DEADMONSTER, SVF_MONSTER, etc
    vec3_t      mins, maxs;
    vec3_t      absmin, absmax, size;
    solid_t     solid;
    int         clipmask;
    game3_edict_t *owner;

    // the game dll can add anything it wants after
    // this point in the structure
};
#endif // !defined(GAME_INCLUDE)

#if USE_NEW_GAME_API

#define MAX_STATS_GAME3           MAX_STATS_NEW
typedef game3_player_state_new_t  game3_player_state_t;

#else

#define MAX_STATS_GAME3           MAX_STATS_OLD
typedef game3_player_state_old_t  game3_player_state_t;

#endif

//===============================================================

//
// functions provided by the main engine
//
typedef struct {
    // special messages
    void (* q_printf(2, 3) bprintf)(int printlevel, const char *fmt, ...);
    void (* q_printf(1, 2) dprintf)(const char *fmt, ...);
    void (* q_printf(3, 4) cprintf)(game3_edict_t *ent, int printlevel, const char *fmt, ...);
    void (* q_printf(2, 3) centerprintf)(game3_edict_t *ent, const char *fmt, ...);
    void (*sound)(game3_edict_t *ent, int channel, int soundindex, float volume, float attenuation, float timeofs);
    void (*positioned_sound)(const vec3_t origin, game3_edict_t *ent, int channel, int soundinedex, float volume, float attenuation, float timeofs);

    // config strings hold all the index strings, the lightstyles,
    // and misc data like the sky definition and cdtrack.
    // All of the current configstrings are sent to clients when
    // they connect, and changes are sent to all connected clients.
    void (*configstring)(int num, const char *string);

    void (* q_noreturn_ptr q_printf(1, 2) error)(const char *fmt, ...);

    // the *index functions create configstrings and some internal server state
    int (*modelindex)(const char *name);
    int (*soundindex)(const char *name);
    int (*imageindex)(const char *name);

    void (*setmodel)(game3_edict_t *ent, const char *name);

    // collision detection
    game3_trace_t (* q_gameabi trace)(const vec3_t start, const vec3_t mins, const vec3_t maxs, const vec3_t end, game3_edict_t *passent, int contentmask);
    int (*pointcontents)(const vec3_t point);
    qboolean (*inPVS)(const vec3_t p1, const vec3_t p2);
    qboolean (*inPHS)(const vec3_t p1, const vec3_t p2);
    void (*SetAreaPortalState)(int portalnum, qboolean open);
    qboolean (*AreasConnected)(int area1, int area2);

    // an entity will never be sent to a client or used for collision
    // if it is not passed to linkentity.  If the size, position, or
    // solidity changes, it must be relinked.
    void (*linkentity)(game3_edict_t *ent);
    void (*unlinkentity)(game3_edict_t *ent);     // call before removing an interactive edict
    int (*BoxEdicts)(const vec3_t mins, const vec3_t maxs, game3_edict_t **list, int maxcount, int areatype);
    void (*Pmove)(void *pmove);          // player movement code common with client prediction

    // network messaging
    void (*multicast)(const vec3_t origin, multicast_t to);
    void (*unicast)(game3_edict_t *ent, qboolean reliable);
    void (*WriteChar)(int c);
    void (*WriteByte)(int c);
    void (*WriteShort)(int c);
    void (*WriteLong)(int c);
    void (*WriteFloat)(float f);
    void (*WriteString)(const char *s);
    void (*WritePosition)(const vec3_t pos);    // some fractional bits
    void (*WriteDir)(const vec3_t pos);         // single byte encoded, very coarse
    void (*WriteAngle)(float f);

    // managed memory allocation
    void *(*TagMalloc)(unsigned size, unsigned tag);
    void (*TagFree)(void *block);
    void (*FreeTags)(unsigned tag);

    // console variable interaction
    cvar_t *(*cvar)(const char *var_name, const char *value, int flags);
    cvar_t *(*cvar_set)(const char *var_name, const char *value);
    cvar_t *(*cvar_forceset)(const char *var_name, const char *value);

    // ClientCommand and ServerCommand parameter access
    int (*argc)(void);
    char *(*argv)(int n);
    char *(*args)(void);     // concatenation of all argv >= 1

    // add commands to the server console as if they were typed in
    // for map changing, etc
    void (*AddCommandString)(const char *text);

    void (*DebugGraph)(float value, int color);
} game3_import_t;

//
// functions exported by the game subsystem
//
typedef struct {
    int         apiversion;

    // the init function will only be called when a game starts,
    // not each time a level is loaded.  Persistant data for clients
    // and the server can be allocated in init
    void (*Init)(void);
    void (*Shutdown)(void);

    // each new level entered will cause a call to SpawnEntities
    void (*SpawnEntities)(const char *mapname, const char *entstring, const char *spawnpoint);

    // Read/Write Game is for storing persistant cross level information
    // about the world state and the clients.
    // WriteGame is called every time a level is exited.
    // ReadGame is called on a loadgame.
    void (*WriteGame)(const char *filename, qboolean autosave);
    void (*ReadGame)(const char *filename);

    // ReadLevel is called after the default map information has been
    // loaded with SpawnEntities
    void (*WriteLevel)(const char *filename);
    void (*ReadLevel)(const char *filename);

    qboolean (*ClientConnect)(game3_edict_t *ent, char *userinfo);
    void (*ClientBegin)(game3_edict_t *ent);
    void (*ClientUserinfoChanged)(game3_edict_t *ent, char *userinfo);
    void (*ClientDisconnect)(game3_edict_t *ent);
    void (*ClientCommand)(game3_edict_t *ent);
    void (*ClientThink)(game3_edict_t *ent, game3_usercmd_t *cmd);

    void (*RunFrame)(void);

    // ServerCommand will be called when an "sv <command>" command is issued on the
    // server console.
    // The game can issue gi.argc() / gi.argv() commands to get the rest
    // of the parameters
    void (*ServerCommand)(void);

    //
    // global variables shared between game and server
    //

    // The edict array is allocated in the game dll so it
    // can vary in size from one game to another.
    //
    // The size will be fixed when ge->Init() is called
    game3_edict_t *edicts;
    int         edict_size;
    int         num_edicts;     // current number, <= max_edicts
    int         max_edicts;
} game3_export_t;

//===============================================================

/*
 * GetGameAPIEx() is guaranteed to be called after GetGameAPI() and before
 * ge->Init().
 *
 * Unlike GetGameAPI(), passed game_import_ex_t * is valid as long as game
 * library is loaded. Pointed to structure can be used directly without making
 * a copy of it. If copying is necessary, no more than structsize bytes must
 * be copied.
 *
 * New fields can be safely added at the end of game_import_ex_t and
 * game_export_ex_t structures, provided GAME_API_VERSION_EX is also bumped.
 *
 * Game is not required to implement all entry points in game_export_ex_t.
 * Non-implemented entry points must be set to NULL. Server, however, must
 * implement all entry points in game_import_ex_t for advertised API version.
 *
 * Only upstream Q2PRO engine may define new extended API versions. Mods should
 * never do this on their own, but may implement private extensions obtainable
 * via GetExtension() callback.
 *
 * API version history:
 * 1 - Initial release.
 * 2 - Added CustomizeEntity().
 * 3 - Added EntityVisibleToClient(), renamed CustomizeEntity() to
 * CustomizeEntityToClient() and changed the meaning of return value.
 */

#define GAME3_API_VERSION_EX_MINIMUM             1
#define GAME3_API_VERSION_EX_CUSTOMIZE_ENTITY    2
#define GAME3_API_VERSION_EX_ENTITY_VISIBLE      3
#define GAME3_API_VERSION_EX                     3

typedef enum {
    VIS_PVS     = 0,
    VIS_PHS     = 1,
    VIS_NOAREAS = 2     // can be OR'ed with one of above
} vis_t;

typedef struct {
    game3_entity_state_t s;
    game3_entity_state_extension_t x;
} game3_customize_entity_t;

typedef struct {
    uint32_t    apiversion;
    uint32_t    structsize;

    void        (*local_sound)(game3_edict_t *target, const vec3_t origin, game3_edict_t *ent, int channel, int soundindex, float volume, float attenuation, float timeofs);
    const char  *(*get_configstring)(int index);
    trace_t     (*q_gameabi clip)(const vec3_t start, const vec3_t mins, const vec3_t maxs, const vec3_t end, game3_edict_t *clip, int contentmask);
    qboolean    (*inVIS)(const vec3_t p1, const vec3_t p2, vis_t vis);

    void        *(*GetExtension)(const char *name);
    void        *(*TagRealloc)(void *ptr, size_t size);
} game3_import_ex_t;

typedef struct {
    uint32_t    apiversion;
    uint32_t    structsize;

    void        *(*GetExtension)(const char *name);
    qboolean    (*CanSave)(void);
    void        (*PrepFrame)(void);
    void        (*RestartFilesystem)(void); // called when fs_restart is issued
    qboolean    (*CustomizeEntityToClient)(game3_edict_t *client, game3_edict_t *ent, game3_customize_entity_t *temp); // if true is returned, `temp' must be initialized
    qboolean    (*EntityVisibleToClient)(game3_edict_t *client, game3_edict_t *ent);
} game3_export_ex_t;

#endif // GAME3_H
