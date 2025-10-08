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

// client.h -- primary header for client

#pragma once

#include "shared/shared.h"
#include "shared/list.h"
#include "shared/game.h"

#include "common/bsp.h"
#include "common/cmd.h"
#include "common/cmodel.h"
#include "common/common.h"
#include "common/cvar.h"
#include "common/field.h"
#include "common/files.h"
#include "common/math.h"
#include "common/msg.h"
#include "common/net/chan.h"
#include "common/net/net.h"
#include "common/pmove.h"
#include "common/prompt.h"
#include "common/protocol.h"
#include "common/q2proto_shared.h"
#include "common/sizebuf.h"
#include "common/zone.h"

#include "refresh/refresh.h"
#include "server/server.h"
#include "system/system.h"

#include "client/client.h"
#include "client/input.h"
#include "client/keys.h"
#include "client/sound/sound.h"
#include "client/ui.h"
#include "client/video.h"

#include "q2proto/q2proto.h"

#if USE_ZLIB
#include <zlib.h>
#endif

//=============================================================================

typedef struct {
    entity_state_t     current;
    entity_state_t     prev;           // will always be valid, but might just be a copy of current

    vec3_t          mins, maxs;
    float           radius;             // from mid point

    int             serverframe;        // if not current, this ent isn't in the frame

    int             trailcount;         // for diminishing grenade trails
    vec3_t          lerp_origin;        // for trails (variable hz)

#if USE_FPS
    int             prev_frame;
    int             anim_start;

    int             event_frame;
#endif

    int             fly_stoptime;

    float           flashlightfrac;

// KEX
    int32_t         current_frame, last_frame, frame_servertime;
    int             stair_time;
    float           stair_height;
// KEX
} centity_t;

extern centity_t    cl_entities[MAX_EDICTS];

#define MAX_CLIENTWEAPONMODELS        256       // PGM -- upped from 16 to fit the chainfist vwep

typedef struct {
    char name[MAX_QPATH];
    qhandle_t skin;
    char icon_name[MAX_QPATH];
    char model_name[MAX_QPATH];
    char skin_name[MAX_QPATH];
    char dogtag_name[MAX_QPATH];
    qhandle_t model;
    qhandle_t weaponmodel[MAX_CLIENTWEAPONMODELS];
} clientinfo_t;

typedef struct {
    unsigned    sent;    // time sent, for calculating pings
    unsigned    rcvd;    // time rcvd, for calculating pings
    unsigned    cmdNumber;    // current cmdNumber for this frame
} client_history_t;

typedef struct {
    bool            valid;

    int             number;
    int             delta;

    byte            areabits[MAX_MAP_AREA_BYTES];
    int             areabytes;

    player_state_t  ps;
    int             clientNum;

    int             numEntities;
    unsigned        firstEntity;
} server_frame_t;

// locally calculated frame flags for debug display
#define FF_SERVERDROP   BIT(4)
#define FF_BADFRAME     BIT(5)
#define FF_OLDFRAME     BIT(6)
#define FF_OLDENT       BIT(7)
#define FF_NODELTA      BIT(8)

// variable server FPS
#define CL_FRAMETIME    cl.frametime.time
#define CL_1_FRAMETIME  cl.frametime_inv
#define CL_FRAMEDIV     cl.frametime.div
#if USE_FPS
#define CL_FRAMESYNC    !(cl.frame.number % cl.frametime.div)
#define CL_KEYPS        (&cl.keyframe.ps)
#define CL_OLDKEYPS     (&cl.oldkeyframe.ps)
#define CL_KEYLERPFRAC  cl.keylerpfrac
#else
#define CL_FRAMESYNC    1
#define CL_KEYPS        (&cl.frame.ps)
#define CL_OLDKEYPS     (&cl.oldframe.ps)
#define CL_KEYLERPFRAC  cl.lerpfrac
#endif

// Time over which step climbing is smoothed
#define STEP_TIME       100

typedef struct {
    int         main;
    int         wheel;
    int         selected;
} cl_wheel_icon_t;

typedef struct {
    int             item_index;
    cl_wheel_icon_t icons;
    int             ammo_index;
    int             min_ammo;
    int             sort_id;
    int             quantity_warn;
    bool            is_powerup;
    bool            can_drop;
} cl_wheel_weapon_t;

typedef struct {
    int             item_index;
    cl_wheel_icon_t icons;
} cl_wheel_ammo_t;

typedef struct {
    int             item_index;
    cl_wheel_icon_t icons;
    int             sort_id;
    int             ammo_index;
    bool            is_toggle;
    bool            can_drop;
} cl_wheel_powerup_t;

typedef enum {
    WHEEL_CLOSED,   // release holster
    WHEEL_CLOSING,  // do not draw or process, but keep holster held
    WHEEL_OPEN      // draw & process + holster
} cl_wheel_state_t;

typedef struct {
    bool                  has_item;
    bool                  is_powerup;
    bool                  has_ammo;
    int                   data_id;
    int                   item_index;
    int                   sort_id;
    const cl_wheel_icon_t *icons;

    // cached data
    float   angle;
    vec2_t  dir;
    float   dot;
} cl_wheel_slot_t;

typedef struct {
    player_fog_t linear;
    player_heightfog_t height;
} cl_fog_params_t;

typedef struct {
    vec3_t      origin;
    float       radius;
    int         resolution;
    float       intensity;
    float       fade_start, fade_end;
    int         lightstyle;
    float       coneangle; // spot if non-zero
    vec3_t      conedirection;
    color_t     color;
} cl_shadow_light_t;

//
// the client_state_t structure is wiped completely at every
// server map change
//
typedef struct {
    int         timeoutcount;

    unsigned    lastTransmitTime;
    unsigned    lastTransmitCmdNumber;
    unsigned    lastTransmitCmdNumberReal;
    bool        sendPacketNow;

    usercmd_t    cmd;
    usercmd_t    cmds[CMD_BACKUP];    // each message will send several old cmds
    unsigned     cmdNumber;
    vec3_t       predicted_origins[CMD_BACKUP];    // for debug comparing against server
    client_history_t    history[CMD_BACKUP];
    unsigned    initialSeq;

    float       predicted_step;                // for stair up smoothing
    unsigned    predicted_step_time;

    vec3_t      predicted_origin;    // generated by CL_PredictMovement
    vec3_t      predicted_angles;
    vec3_t      predicted_velocity;
    vec4_t      predicted_screen_blend;
    refdef_flags_t predicted_rdflags;
    vec3_t      prediction_error;

    int8_t      current_viewheight; // current viewheight from client Pmove()
    int8_t      prev_viewheight;    // viewheight before last change
    int         viewheight_change_time; // time when a viewheight change was detected

    edict_t     *last_groundentity; // last groundentity reported by pmove
    cplane_t    last_groundplane; // last groundplane reported by pmove

    // rebuilt each valid frame
    centity_t       *solidEntities[MAX_PACKET_ENTITIES];
    int             numSolidEntities;

    entity_state_t  baselines[MAX_EDICTS];

    entity_state_t  entityStates[MAX_PARSE_ENTITIES];
    unsigned        numEntityStates;

    msgEsFlags_t    esFlags;
    msgPsFlags_t    psFlags;

    server_frame_t  frames[UPDATE_BACKUP];
    unsigned        frameflags;
    int             suppress_count;

    server_frame_t  frame;                // received from server
    server_frame_t  oldframe;
    int             servertime;
    int             serverdelta;

#if USE_FPS
    server_frame_t  keyframe;
    server_frame_t  oldkeyframe;
    int             keyservertime;
#endif

    size_t          dcs[BC_COUNT(MAX_CONFIGSTRINGS)];

    // the client maintains its own idea of view angles, which are
    // sent to the server each frame.  It is cleared to 0 upon entering each level.
    // the server sends a delta each frame which is added to the locally
    // tracked view angles to account for standing on rotating objects,
    // and teleport direction changes
    vec3_t      viewangles;

    // interpolated movement vector used for local prediction,
    // never sent to server, rebuilt each client frame
    vec2_t      localmove;

    // accumulated mouse forward/side movement, added to both
    // localmove and pending cmd, cleared each time cmd is finalized
    vec2_t      mousemove;

    int         time;           // this is the time value that the client
                                // is rendering at.  always <= cl.servertime
    float       lerpfrac;       // between oldframe and frame

#if USE_FPS
    int         keytime;
    float       keylerpfrac;
#endif

    refdef_t    refdef;
    float       fov_x;      // interpolated
    float       fov_y;      // derived from fov_x assuming 4/3 aspect ratio
    int         lightlevel;

    vec3_t      v_forward, v_right, v_up;    // set when refdef.angles is set

    bool        thirdPersonView;
    float       thirdPersonAlpha;

    // predicted values, used for smooth player entity movement in thirdperson view
    vec3_t      playerEntityOrigin;
    vec3_t      playerEntityAngles;

    //
    // transient data from server
    //
    cg_server_data_t cgame_data;

    //
    // server state information
    //
    int         serverstate;    // ss_* constants
    int         servercount;    // server identification for prespawns
    char        gamedir[MAX_QPATH];
    int         clientNum;            // never changed during gameplay, set by serverdata packet
    int         maxclients;
    int         max_stats;
    pmoveParams_t pmp;

    frametime_t frametime;
    float       frametime_inv;  // 1/frametime

    configstring_t  baseconfigstrings[MAX_CONFIGSTRINGS];
    configstring_t  configstrings[MAX_CONFIGSTRINGS];
    cs_remap_t      csr;
    q2proto_game_api_t game_api;

    char        mapname[MAX_QPATH]; // short format - q2dm1, etc

#if USE_AUTOREPLY
    unsigned    reply_time;
    unsigned    reply_delta;
#endif

    //
    // locally derived information from server state
    //
    bsp_t        *bsp;

    qhandle_t model_draw[MAX_MODELS];
    const mmodel_t *model_clip[MAX_MODELS];

    qhandle_t sound_precache[MAX_SOUNDS];
    qhandle_t image_precache[MAX_IMAGES];
    qhandle_t sfx_hit_marker;

    clientinfo_t    clientinfo[MAX_CLIENTS];
    clientinfo_t    baseclientinfo;

    char    weaponModels[MAX_CLIENTWEAPONMODELS][MAX_QPATH];
    int     numWeaponModels;
    
    bool    need_powerscreen_scale;

    int hit_marker_frame;
    unsigned hit_marker_time;
    int hit_marker_count;

    // data for view weapon
    struct {
        int32_t frame, last_frame;
        int32_t server_time;

        struct {
            qhandle_t   model;
            int         time;
            float       roll, scale;
            vec3_t      offset;
        } muzzle;
    } weapon;

    struct {
        cl_fog_params_t     start, end;
        int                 lerp_time, lerp_time_start;
    } fog;

    // data for weapon wheel stuff
    struct {
        cl_wheel_weapon_t weapons[MAX_WHEEL_ITEMS];
        int               num_weapons;

        cl_wheel_ammo_t ammo[MAX_WHEEL_ITEMS];
        int             num_ammo;

        cl_wheel_powerup_t powerups[MAX_WHEEL_ITEMS];
        int                num_powerups;
    } wheel_data;

    // carousel state
    struct {
        cl_wheel_state_t state;
        int              close_time; // time when we will close
        int              selected; // selected item index

        struct {
            bool    has_ammo;
            int     data_id;
            int     item_index;
        } slots[MAX_WHEEL_ITEMS * 2];
        size_t      num_slots;
    } carousel;

    // weapon wheel state
    struct {
        cl_wheel_state_t state;
        vec2_t           position;
        float            distance;
        vec2_t           dir;
        bool             is_powerup_wheel;
        float            timer, timescale;

        cl_wheel_slot_t slots[MAX_WHEEL_ITEMS * 2];
        size_t          num_slots;

        float       slice_deg;
        float       slice_sin;

        int         selected; // -1 = no selection
        int         deselect_time; // if non-zero, deselect after < com_localTime
    } wheel;

    int weapon_lock_time; // don't allow BUTTON_ATTACK within this time

    // shadow lights
    struct {
        int                 number;
        cl_shadow_light_t   light;
    } shadowdefs[MAX_SHADOW_LIGHTS];
} client_state_t;

extern client_state_t   cl;

/*
==================================================================

the client_static_t structure is persistent through an arbitrary number
of server connections

==================================================================
*/

// resend delay for challenge/connect packets
#define CONNECT_DELAY       3000u

#define CONNECT_INSTANT     CONNECT_DELAY
#define CONNECT_FAST        (CONNECT_DELAY - 1000u)

typedef enum {
    ca_uninitialized,
    ca_disconnected,    // not talking to a server
    ca_challenging,     // sending getchallenge packets to the server
    ca_connecting,      // sending connect packets to the server
    ca_connected,       // netchan_t established, waiting for svc_serverdata
    ca_loading,         // loading level data
    ca_precached,       // loaded level data, waiting for svc_frame
    ca_active,          // game views should be displayed
    ca_cinematic        // running a cinematic
} connstate_t;

#define FOR_EACH_DLQ(q) \
    LIST_FOR_EACH(dlqueue_t, q, &cls.download.queue, entry)
#define FOR_EACH_DLQ_SAFE(q, n) \
    LIST_FOR_EACH_SAFE(dlqueue_t, q, n, &cls.download.queue, entry)

typedef enum {
    // generic types
    DL_OTHER,
    DL_MAP,
    DL_MODEL,
#if USE_CURL
    // special types
    DL_LIST,
    DL_PAK
#endif
} dltype_t;

typedef enum {
    DL_FREE,
    DL_PENDING,
    DL_RUNNING,
    DL_DONE
} dlstate_t;

typedef struct {
    list_t      entry;
    dltype_t    type;
    dlstate_t   state;
    char        path[1];
} dlqueue_t;

typedef struct {
    int         framenum;
    unsigned    msglen;
    int64_t     filepos;
    byte        data[1];
} demosnap_t;

typedef struct {
    connstate_t state;
    keydest_t   key_dest;

    active_t    active;

    bool        ref_initialized;
    unsigned    disable_screen;

    int         userinfo_modified;
    cvar_t      *userinfo_updates[MAX_PACKET_USERINFOS];
// this is set each time a CVAR_USERINFO variable is changed
// so that the client knows to send it to the server

    unsigned    realtime;           // always increasing, no clamping, etc
    float       frametime;          // seconds since last video frame

// performance measurement
#define C_FPS   cls.measure.fps[0]
#define R_FPS   cls.measure.fps[1]
#define C_MPS   cls.measure.fps[2]
#define C_PPS   cls.measure.fps[3]
#define C_FRAMES    cls.measure.frames[0]
#define R_FRAMES    cls.measure.frames[1]
#define M_FRAMES    cls.measure.frames[2]
#define P_FRAMES    cls.measure.frames[3]
    struct {
        unsigned    time;
        int         frames[4];
        int         fps[4];
        int         ping;
    } measure;

// connection information
    netadr_t    serverAddress;
    char        servername[MAX_OSPATH]; // name of server from original connect
    unsigned    connect_time;           // for connection retransmits
    int         connect_count;
    bool        passive;

#if USE_ZLIB
    z_stream    z;
#endif

    int         quakePort;          // a 16 bit value that allows quake servers
                                    // to work around address translating routers
    netchan_t   netchan;
    int         serverProtocol;     // in case we are doing some kind of version hack
    int         protocolVersion;    // minor version
    q2proto_clientcontext_t q2proto_ctx;

    int         challenge;          // from the server to use for connecting

#if USE_ICMP
    bool        errorReceived;      // got an ICMP error from server
#endif

#define RECENT_ADDR 4
#define RECENT_MASK (RECENT_ADDR - 1)

    netadr_t    recent_addr[RECENT_ADDR];
    unsigned    recent_head;

    string_entry_t  *stufftextwhitelist;

    struct {
        list_t      queue;              // queue of paths we need
        int         pending;            // number of non-finished entries in queue
        dlqueue_t   *current;           // path being downloaded
        int         percent;            // how much downloaded
        int64_t     position;           // how much downloaded (in bytes)
        qhandle_t   file;               // UDP file transfer from server
        char        temp[MAX_QPATH + 4];// account 4 bytes for .tmp suffix
        string_entry_t  *ignores;       // list of ignored paths
    } download;

// demo recording info must be here, so it isn't cleared on level change
    struct {
        qhandle_t   playback;
        qhandle_t   recording;
        unsigned    time_start;
        unsigned    time_frames;
        int         last_server_frame;  // number of server frame the last svc_frame was written
        int         frames_written;     // number of frames written to demo file
        int         frames_dropped;     // number of svc_frames that didn't fit
        int         others_dropped;     // number of misc svc_* messages that didn't fit
        int         frames_read;        // number of frames read from demo file
        int         last_snapshot;      // number of demo frame the last snapshot was saved
        int64_t     file_size;
        int64_t     file_offset;
        float       file_progress;
        sizebuf_t   buffer;
        demosnap_t  **snapshots;
        int         numsnapshots;
        bool        paused;
        bool        seeking;
        bool        eof;
        bool        compat;             // demomap compatibility mode
        msgEsFlags_t    esFlags;        // for snapshots/recording
        msgPsFlags_t    psFlags;

        // q2proto fields
        q2proto_server_info_t server_info;
        q2proto_servercontext_t q2proto_context;
#if USE_ZLIB
        z_stream        z;  // for compressing in demo snaps
        byte            *z_buffer;
        unsigned        z_buffer_size;
        q2protoio_deflate_args_t q2proto_deflate;
#endif
    } demo;

#if USE_CLIENT_GTV
    struct {
        connstate_t     state;

        netstream_t     stream;
        unsigned        msglen;

        player_packed_t     ps;
        entity_packed_t     entities[MAX_EDICTS];
        msgPsFlags_t        psFlags;
        msgEsFlags_t        esFlags;    // for writing
        msgPsFlags_t        psFlags;

        sizebuf_t       message;
    } gtv;
#endif
} client_static_t;

extern client_static_t      cls;

extern cmdbuf_t     cl_cmdbuf;
extern char         cl_cmdbuf_text[MAX_STRING_CHARS];

//=============================================================================

#define NOPART_GRENADE_EXPLOSION    BIT(0)
#define NOPART_GRENADE_TRAIL        BIT(1)
#define NOPART_ROCKET_EXPLOSION     BIT(2)
#define NOPART_ROCKET_TRAIL         BIT(3)
#define NOPART_BLOOD                BIT(4)
#define NOPART_BLASTER_TRAIL        BIT(5)

#define NOEXP_GRENADE               BIT(0)
#define NOEXP_ROCKET                BIT(1)

#define DLHACK_ROCKET_COLOR         BIT(0)
#define DLHACK_SMALLER_EXPLOSION    BIT(1)
#define DLHACK_NO_MUZZLEFLASH       BIT(2)

//
// cvars
//
extern cvar_t   *cl_gun;
extern cvar_t   *cl_gunalpha;
extern cvar_t   *cl_gunfov;
extern cvar_t   *cl_gun_x;
extern cvar_t   *cl_gun_y;
extern cvar_t   *cl_gun_z;
extern cvar_t   *cl_predict;
extern cvar_t   *cl_footsteps;
extern cvar_t   *cl_noskins;
extern cvar_t   *cl_kickangles;
extern cvar_t   *cl_rollhack;
extern cvar_t   *cl_noglow;
extern cvar_t   *cl_nobob;
extern cvar_t   *cl_nolerp;
extern cvar_t   *cl_shadowlights;

#if USE_DEBUG
#define SHOWNET(level, ...) \
    do { if (cl_shownet->integer >= level) \
        Com_LPrintf(PRINT_DEVELOPER, __VA_ARGS__); } while (0)
#define SHOWCLAMP(level, ...) \
    do { if (cl_showclamp->integer >= level) \
        Com_LPrintf(PRINT_DEVELOPER, __VA_ARGS__); } while (0)
#define SHOWMISS(...) \
    do { if (cl_showmiss->integer) \
        Com_LPrintf(PRINT_DEVELOPER, __VA_ARGS__); } while (0)
extern cvar_t   *cl_shownet;
extern cvar_t   *cl_showmiss;
extern cvar_t   *cl_showclamp;
#else
#define SHOWNET(...)
#define SHOWCLAMP(...)
#define SHOWMISS(...)
#endif

extern cvar_t   *cl_vwep;

extern cvar_t   *cl_disable_particles;
extern cvar_t   *cl_disable_explosions;
extern cvar_t   *cl_dlight_hacks;
extern cvar_t   *cl_smooth_explosions;

extern cvar_t   *cl_cgame_notify;
extern cvar_t   *cl_chat_notify;
extern cvar_t   *cl_chat_sound;
extern cvar_t   *cl_chat_filter;

extern cvar_t   *cl_disconnectcmd;
extern cvar_t   *cl_changemapcmd;
extern cvar_t   *cl_beginmapcmd;

extern cvar_t   *cl_gibs;
extern cvar_t   *cl_flares;

extern cvar_t   *cl_thirdperson;
extern cvar_t   *cl_thirdperson_angle;
extern cvar_t   *cl_thirdperson_range;

extern cvar_t   *cl_async;

//
// userinfo
//
extern cvar_t   *info_password;
extern cvar_t   *info_spectator;
extern cvar_t   *info_name;
extern cvar_t   *info_dogtag;
extern cvar_t   *info_skin;
extern cvar_t   *info_rate;
extern cvar_t   *info_fov;
extern cvar_t   *info_msg;
extern cvar_t   *info_hand;
extern cvar_t   *info_gender;
extern cvar_t   *info_uf;

//=============================================================================

static inline void CL_AdvanceValue(float *restrict val, float target, float speed)
{
    if (*val < target) {
        *val += speed * cls.frametime;
        if (*val > target)
            *val = target;
    } else if (*val > target) {
        *val -= speed * cls.frametime;
        if (*val < target)
            *val = target;
    }
}

//
// main.c
//

void CL_Init(void);
void CL_Quit_f(void);
void CL_Disconnect(error_type_t type);
void CL_UpdateRecordingSetting(void);
void CL_Begin(void);
void CL_CheckForResend(void);
void CL_ClearState(void);
void CL_RestartFilesystem(bool total);
void CL_RestartRefresh(bool total);
void CL_ClientCommand(const char *string);
void CL_SendRcon(const netadr_t *adr, const char *pass, const char *cmd);
const char *CL_Server_g(const char *partial, int argnum, int state);
void CL_CheckForPause(void);
void CL_UpdateFrameTimes(void);
void CL_AddHitMarker(void);
bool CL_CheckForIgnore(const char *s);
void CL_LoadFilterList(string_entry_t **list, const char *name, const char *comments, size_t maxlen);

void cl_timeout_changed(cvar_t *self);

//
// precache.c
//

typedef enum {
    LOAD_NONE,
    LOAD_MAP,
    LOAD_MODELS,
    LOAD_IMAGES,
    LOAD_CLIENTS,
    LOAD_SOUNDS
} load_state_t;

void CL_ParsePlayerSkin(char *name, char *model, char *skin, char *dogtag, bool parse_dogtag, const char *s);
void CL_LoadClientinfo(clientinfo_t *ci, const char *s);
void CL_LoadState(load_state_t state);
void CL_RegisterSounds(void);
void CL_RegisterBspModels(void);
void CL_RegisterVWepModels(void);
void CL_PrepRefresh(void);
void CL_UpdateConfigstring(int index);

//
// download.c
//
int CL_QueueDownload(const char *path, dltype_t type);
bool CL_IgnoreDownload(const char *path);
void CL_FinishDownload(dlqueue_t *q);
void CL_CleanupDownloads(void);
void CL_LoadDownloadIgnores(void);
void CL_HandleDownload(const byte *data, int size, int percent);
bool CL_CheckDownloadExtension(const char *ext);
void CL_StartNextDownload(void);
void CL_RequestNextDownload(void);
void CL_ResetPrecacheCheck(void);
void CL_InitDownloads(void);


//
// input.c
//
void IN_Init(void);
void IN_Shutdown(void);
void IN_Frame(void);
void IN_Activate(void);

void CL_RegisterInput(void);
void CL_UpdateCmd(int msec);
void CL_FinalizeCmd(void);
void CL_SendCmd(void);


//
// parse.c
//

#define CL_ES_EXTENDED_MASK \
    (MSG_ES_LONGSOLID | MSG_ES_UMASK | MSG_ES_BEAMORIGIN | MSG_ES_SHORTANGLES | MSG_ES_EXTENSIONS)

#define CL_ES_EXTENDED_MASK_2 (CL_ES_EXTENDED_MASK | MSG_ES_EXTENSIONS_2)
#define CL_PS_EXTENDED_MASK_2 (MSG_PS_EXTENSIONS | MSG_PS_EXTENSIONS_2 | MSG_PS_MOREBITS)

typedef struct {
    int type;
    vec3_t pos1;
    vec3_t pos2;
    vec3_t offset;
    vec3_t dir;
    int count;
    int color;
    int entity1;
    int entity2;
    int time;
} tent_params_t;

typedef struct {
    int entity;
    int weapon;
    bool silenced;
} mz_params_t;

extern tent_params_t    te;
extern mz_params_t      mz;
extern q2proto_sound_t  snd;

void CL_ParseServerMessage(void);
bool CL_SeekDemoMessage(void);


//
// entities.c
//

#define EF_TRAIL_MASK   (EF_ROCKET | EF_BLASTER | EF_HYPERBLASTER | EF_GIB | EF_GRENADE | \
                         EF_FLIES | EF_BFG | EF_TRAP | EF_FLAG1 | EF_FLAG2 | EF_TAGTRAIL | \
                         EF_TRACKERTRAIL | EF_TRACKER | EF_GREENGIB | EF_IONRIPPER | \
                         EF_BLUEHYPERBLASTER | EF_PLASMA)

#define IS_TRACKER(effects) \
    (((effects) & (EF_TRACKERTRAIL | EF_TRACKER)) == EF_TRACKERTRAIL)

void CL_DeltaFrame(void);
void CL_AddEntities(void);
void CL_CalcViewValues(void);

#if USE_DEBUG
void CL_CheckEntityPresent(int entnum, const char *what);
#endif

// the sound code makes callbacks to the client for entity position
// information, so entities can be dynamically re-spatialized
void CL_GetEntitySoundOrigin(unsigned entnum, vec3_t org);


//
// view.c
//
extern int          gun_frame;
extern qhandle_t    gun_model;

void V_Init(void);
void V_Shutdown(void);
void V_RenderView(void);
void V_AddEntity(const entity_t *ent);
void V_AddParticle(const particle_t *p);
void V_AddLightEx(cl_shadow_light_t *light);
void V_AddLight(const vec3_t org, float intensity, float r, float g, float b);
void V_AddLightStyle(int style, float value);
void CL_UpdateBlendSetting(void);
void V_FogParamsChanged(unsigned bits, unsigned color_bits, unsigned hf_start_color_bits, unsigned hf_end_color_bits, const cl_fog_params_t *params, int time);

// wheel.c
void CL_Wheel_WeapNext(void);
void CL_Wheel_WeapPrev(void);
void CL_Carousel_Draw(void);
void CL_Carousel_Input(void);
void CL_Carousel_ClearInput(void);
void CL_Wheel_Precache(void);
void CL_Wheel_Init(void);
void CL_Wheel_Open(bool powerup);
void CL_Wheel_Close(bool released);
void CL_Wheel_Input(int x, int y);
void CL_Wheel_Draw(void);
void CL_Wheel_Update(void);
void CL_Wheel_ClearInput(void);
float CL_Wheel_TimeScale(void);

//
// tent.c
//

typedef struct cl_sustain_s {
    int     id;
    int     type;
    int     endtime;
    int     nextthink;
    vec3_t  org;
    vec3_t  dir;
    int     color;
    int     count;
    int     magnitude;
    void    (*think)(struct cl_sustain_s *self);
} cl_sustain_t;

typedef enum {
    MFLASH_MACHN,
    MFLASH_SHOTG2,
    MFLASH_SHOTG,
    MFLASH_ROCKET,
    MFLASH_RAIL,
    MFLASH_LAUNCH,
    MFLASH_ETF_RIFLE,
    MFLASH_DIST,
    MFLASH_BOOMER,
    MFLASH_BLAST, // 0 = orange, 1 = blue, 2 = green
    MFLASH_BFG,
    MFLASH_BEAMER,

    MFLASH_TOTAL
} cl_muzzlefx_t;

void CL_AddWeaponMuzzleFX(cl_muzzlefx_t fx, const vec3_t offset, float scale);
void CL_AddMuzzleFX(const vec3_t origin, const vec3_t angles, cl_muzzlefx_t fx, int skin, float scale);
void CL_AddHelpPath(const vec3_t origin, const vec3_t dir, bool first);

void CL_SmokeAndFlash(const vec3_t origin);
void CL_DrawBeam(const vec3_t org, const vec3_t end, float model_length, qhandle_t model);
void CL_PlayFootstepSfx(int step_id, int entnum, float volume, float attenuation);

void CL_RegisterTEntSounds(void);
void CL_RegisterTEntModels(void);
void CL_ParseTEnt(void);
void CL_AddTEnts(void);
void CL_ClearTEnts(void);
void CL_InitTEnts(void);


//
// predict.c
//
void CL_PredictAngles(void);
void CL_PredictMovement(void);
void CL_CheckPredictionError(void);
void CL_Trace(trace_t *tr, const vec3_t start, const vec3_t end, const vec3_t mins, const vec3_t maxs, const struct edict_s *passent, contents_t contentmask);


//
// effects.c
//
#define PARTICLE_GRAVITY    40
#define INSTANT_PARTICLE    -10000.0f

typedef struct cparticle_s {
    struct cparticle_s    *next;

    int     time;
    vec3_t  org;
    vec3_t  vel;
    vec3_t  accel;
    int     color;      // -1 => use rgba
    float   scale;
    float   alpha;
    float   alphavel;
    color_t rgba;
} cparticle_t;

typedef struct {
    int     key;        // so entities can reuse same entry
    vec3_t  color;
    vec3_t  origin;
    float   radius;
    int     die;        // stop lighting after this time
    int     start;
    bool    fade;
} cdlight_t;

typedef enum {
    DT_GIB,
    DT_GREENGIB,
    DT_ROCKET,
    DT_GRENADE,
    DT_FIREBALL,

    DT_COUNT
} diminishing_trail_t;

void CL_BigTeleportParticles(const vec3_t org);
void CL_DiminishingTrail(centity_t *ent, const vec3_t end, diminishing_trail_t type);
void CL_FlyEffect(centity_t *ent, const vec3_t origin);
void CL_BfgParticles(const entity_t *ent);
void CL_ItemRespawnParticles(const vec3_t org);
void CL_InitEffects(void);
void CL_ClearEffects(void);
void CL_BlasterParticles(const vec3_t org, const vec3_t dir);
void CL_ExplosionParticles(const vec3_t org);
void CL_BFGExplosionParticles(const vec3_t org);
void CL_BlasterTrail(centity_t *ent, const vec3_t end);
void CL_OldRailTrail(void);
void CL_BubbleTrail(const vec3_t start, const vec3_t end);
void CL_FlagTrail(centity_t *ent, const vec3_t end, int color);
void CL_MuzzleFlash(void);
void CL_MuzzleFlash2(void);
void CL_TeleporterParticles(const vec3_t org);
void CL_TeleportParticles(const vec3_t org);
void CL_ParticleEffect(const vec3_t org, const vec3_t dir, int color, int count);
void CL_ParticleEffect2(const vec3_t org, const vec3_t dir, int color, int count);
cparticle_t *CL_AllocParticle(void);
void CL_AddParticles(void);
cdlight_t *CL_AllocDlight(int key);
void CL_AddDLights(void);
void CL_SetLightStyle(int index, const char *s);
void CL_AddLightStyles(void);
void CL_AddShadowLights(void);

//
// newfx.c
//

void CL_BlasterParticles2(const vec3_t org, const vec3_t dir, unsigned int color);
void CL_BlasterTrail2(centity_t *ent, const vec3_t end);
void CL_DebugTrail(const vec3_t start, const vec3_t end);
void CL_Flashlight(int ent, const vec3_t pos);
void CL_ForceWall(const vec3_t start, const vec3_t end, int color);
void CL_BubbleTrail2(const vec3_t start, const vec3_t end, int dist);
void CL_Heatbeam(const vec3_t start, const vec3_t end);
void CL_ParticleSteamEffect(const vec3_t org, const vec3_t dir, int color, int count, int magnitude);
void CL_TrackerTrail(centity_t *ent, const vec3_t end);
void CL_TagTrail(centity_t *ent, const vec3_t end, int color);
void CL_ColorFlash(const vec3_t pos, int ent, int intensity, float r, float g, float b);
void CL_Tracker_Shell(const centity_t *cent, const vec3_t origin);
void CL_MonsterPlasma_Shell(const vec3_t origin);
void CL_ColorExplosionParticles(const vec3_t org, int color, int run);
void CL_ParticleSmokeEffect(const vec3_t org, const vec3_t dir, int color, int count, int magnitude);
void CL_Widowbeamout(cl_sustain_t *self);
void CL_Nukeblast(cl_sustain_t *self);
void CL_WidowSplash(void);
void CL_IonripperTrail(centity_t *ent, const vec3_t end);
void CL_TrapParticles(centity_t *ent, const vec3_t origin);
void CL_ParticleEffect3(const vec3_t org, const vec3_t dir, int color, int count);
void CL_ParticleSteamEffect2(cl_sustain_t *self);
void CL_BerserkSlamParticles(const vec3_t org, const vec3_t dir);
void CL_PowerSplash(void);
void CL_TeleporterParticles2(const vec3_t org);
void CL_HologramParticles(const vec3_t org);
void CL_BarrelExplodingParticles(const vec3_t org);


//
// demo.c
//
void CL_InitDemos(void);
void CL_CleanupDemos(void);
void CL_DemoFrame(void);
bool CL_WriteDemoMessage(sizebuf_t *buf);
void CL_PackEntity(entity_packed_t *out, const entity_state_t *in);
void CL_EmitDemoFrame(void);
void CL_EmitDemoSnapshot(void);
void CL_FreeDemoSnapshots(void);
void CL_FirstDemoFrame(void);
void CL_Stop_f(void);
bool CL_GetDemoInfo(const char *path, demoInfo_t *info);

extern q2protoio_ioarg_t demo_q2protoio_ioarg;
#define Q2PROTO_IOARG_DEMO_WRITE    ((uintptr_t)&demo_q2protoio_ioarg)


//
// locs.c
//
void LOC_Init(void);
void LOC_LoadLocations(void);
void LOC_FreeLocations(void);
void LOC_AddLocationsToScene(void);


//
// console.c
//
void Con_Init(void);
void Con_PostInit(void);
void Con_Shutdown(void);
void Con_DrawConsole(void);
void Con_RunConsole(void);
void Con_Print(const char *txt);
void Con_ClearNotify_f(void);
void Con_ToggleConsole_f(void);
void Con_ClearTyping(void);
void Con_Close(bool force);
void Con_Popup(bool force);
void Con_SkipNotify(bool skip);
void Con_RegisterMedia(void);
void Con_CheckResize(void);

void Key_Console(int key);
void Key_Message(int key);
void Char_Console(int key);
void Char_Message(int key);


//
// refresh.c
//
void    CL_InitRefresh(void);
void    CL_ShutdownRefresh(void);
void    CL_RunRefresh(void);


//
// screen.c
//
#define STAT_PICS       11
#define STAT_MINUS      (STAT_PICS - 1)  // num frame for '-' stats digit

typedef struct {
    int         damage;
    vec3_t      color;
    vec3_t      dir;
    int         time;
} scr_damage_entry_t;

#define MAX_DAMAGE_ENTRIES      32
#define DAMAGE_ENTRY_BASE_SIZE  3

typedef struct {
    int         id;
    int         time;
    int         color;
    int         flags;
    qhandle_t   image;
    int         width, height;
    vec3_t      position;
} scr_poi_t;

#define MAX_TRACKED_POIS        32

typedef struct {
    bool        initialized;        // ready to draw

    qhandle_t   crosshair_pic;
    int         crosshair_width, crosshair_height;
    color_t     crosshair_color;

    qhandle_t   pause_pic;

    qhandle_t   loading_pic;
    bool        draw_loading;

    qhandle_t   hit_marker_pic;
    int         hit_marker_time;
    int         hit_marker_width, hit_marker_height;

    qhandle_t   damage_display_pic;
    int         damage_display_width, damage_display_height;
    scr_damage_entry_t  damage_entries[MAX_DAMAGE_ENTRIES];

    scr_poi_t   pois[MAX_TRACKED_POIS];

    qhandle_t   sb_pics[2][STAT_PICS];
    qhandle_t   inven_pic;
    qhandle_t   field_pic;

    qhandle_t   backtile_pic;

    qhandle_t   net_pic;
    qhandle_t   font_pic;

    int         hud_width, hud_height;
    float       hud_scale;
    vrect_t     vrect;        // position of render window
    
    kfont_t     kfont;

    qhandle_t   carousel_selected;
    qhandle_t   wheel_circle;
    int         wheel_size;
    qhandle_t   wheel_button;
    int         wheel_button_size;
} cl_scr_t;

extern cl_scr_t scr;

void    SCR_Init(void);
void    SCR_Shutdown(void);
void    SCR_UpdateScreen(void);
void    SCR_SizeUp(void);
void    SCR_SizeDown(void);
void    SCR_BeginLoadingPlaque(void);
void    SCR_EndLoadingPlaque(void);
void    SCR_RegisterMedia(void);
void    SCR_ModeChanged(void);
void    SCR_LagSample(void);
void    SCR_LagClear(void);
void    SCR_SetCrosshairColor(void);
void    SCR_AddNetgraph(void);

float   SCR_FadeAlpha(unsigned startTime, unsigned visTime, unsigned fadeTime);
int     SCR_DrawStringStretch(int x, int y, int scale, int flags, size_t maxlen, const char *s, color_t color, qhandle_t font);
void    SCR_DrawStringMultiStretch(int x, int y, int scale, int flags, size_t maxlen, const char *s, color_t color, qhandle_t font);
void    SCR_DrawKStringMultiStretch(int x, int y, int scale, int flags, size_t maxlen, const char *s, color_t color, const kfont_t *kfont);

#define SCR_DrawString(x, y, flags, color, string) \
    SCR_DrawStringStretch(x, y, 1, flags, MAX_STRING_CHARS, string, color, scr.font_pic)

void    SCR_ClearChatHUD_f(void);
void    SCR_AddToChatHUD(const char *text);

void    SCR_AddToDamageDisplay(int damage, const vec3_t color, const vec3_t dir);
void    SCR_RemovePOI(int id);
void    SCR_AddPOI(int id, int time, const vec3_t p, int image, int color, int flags);
void    SCR_Clear(void);

//
// cin.c
//

#if USE_AVCODEC

typedef struct {
    const char *ext;
    const char *fmt;
    int codec_id;
} avformat_t;

void    SCR_InitCinematics(void);
void    SCR_StopCinematic(void);
void    SCR_FinishCinematic(void);
void    SCR_RunCinematic(void);
void    SCR_DrawCinematic(void);
void    SCR_ReloadCinematic(void);
void    SCR_PlayCinematic(const char *name);

#else

static inline void SCR_FinishCinematic(void)
{
    // tell the server to advance to the next map / cinematic
    CL_ClientCommand(va("nextserver %i\n", cl.servercount));
}

#define SCR_InitCinematics()    (void)0
#define SCR_StopCinematic()     (void)0
#define SCR_RunCinematic()      (void)0
#define SCR_DrawCinematic()     (void)0
#define SCR_ReloadCinematic()   (void)0
#define SCR_PlayCinematic(name) SCR_FinishCinematic()

#endif

//
// ascii.c
//
void CL_InitAscii(void);


//
// http.c
//
#if USE_CURL
void HTTP_Init(void);
void HTTP_Shutdown(void);
void HTTP_SetServer(const char *url);
int HTTP_QueueDownload(const char *path, dltype_t type);
void HTTP_RunDownloads(void);
void HTTP_CleanupDownloads(void);
#else
#define HTTP_Init()                     (void)0
#define HTTP_Shutdown()                 (void)0
#define HTTP_SetServer(url)             (void)0
#define HTTP_QueueDownload(path, type)  Q_ERR(ENOSYS)
#define HTTP_RunDownloads()             (void)0
#define HTTP_CleanupDownloads()         (void)0
#endif

//
// gtv.c
//

#if USE_CLIENT_GTV
void CL_GTV_EmitFrame(void);
void CL_GTV_WriteMessage(const byte *data, size_t len);
void CL_GTV_Resume(void);
void CL_GTV_Suspend(void);
void CL_GTV_Transmit(void);
void CL_GTV_Run(void);
void CL_GTV_Init(void);
void CL_GTV_Shutdown(void);
#else
#define CL_GTV_EmitFrame()              (void)0
#define CL_GTV_WriteMessage(data, len)  (void)0
#define CL_GTV_Resume()                 (void)0
#define CL_GTV_Suspend()                (void)0
#define CL_GTV_Transmit()               (void)0
#define CL_GTV_Run()                    (void)0
#define CL_GTV_Init()                   (void)0
#define CL_GTV_Shutdown()               (void)0
#endif

//
// cgame.c
//

extern const cgame_export_t *cgame;

void CG_Init(void);
void CG_Load(const char* new_game, bool is_rerelease_server);
void CG_Unload(void);
