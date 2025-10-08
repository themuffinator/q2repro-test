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

#pragma once

//
// protocol.h -- communications protocols
//

#define MAX_MSGLEN  0x8000      // max length of a message, 32 KiB

#define PROTOCOL_VERSION_OLD            26
#define PROTOCOL_VERSION_DEFAULT        34
#define PROTOCOL_VERSION_R1Q2           35
#define PROTOCOL_VERSION_Q2PRO          36
#define PROTOCOL_VERSION_MVD            37      // not used for UDP connections
#define PROTOCOL_VERSION_RERELEASE      1038
#define PROTOCOL_VERSION_KEX_DEMOS      2022
#define PROTOCOL_VERSION_KEX            2023

#define PROTOCOL_VERSION_EXTENDED_MINIMUM       3434    // r2894
#define PROTOCOL_VERSION_EXTENDED_LIMITS_2      3435    // r3300
#define PROTOCOL_VERSION_EXTENDED_PLAYERFOG     3436    // r3579
#define PROTOCOL_VERSION_EXTENDED_CURRENT       3436    // r3579

#define PROTOCOL_VERSION_R1Q2_MINIMUM           1903    // b6377
#define PROTOCOL_VERSION_R1Q2_UCMD              1904    // b7387
#define PROTOCOL_VERSION_R1Q2_LONG_SOLID        1905    // b7759
#define PROTOCOL_VERSION_R1Q2_CURRENT           1905    // b7759

#define PROTOCOL_VERSION_Q2PRO_MINIMUM              1015    // r335
#define PROTOCOL_VERSION_Q2PRO_RESERVED             1016    // r364
#define PROTOCOL_VERSION_Q2PRO_BEAM_ORIGIN          1017    // r1037-8
#define PROTOCOL_VERSION_Q2PRO_SHORT_ANGLES         1018    // r1037-44
#define PROTOCOL_VERSION_Q2PRO_SERVER_STATE         1019    // r1302
#define PROTOCOL_VERSION_Q2PRO_EXTENDED_LAYOUT      1020    // r1354
#define PROTOCOL_VERSION_Q2PRO_ZLIB_DOWNLOADS       1021    // r1358
#define PROTOCOL_VERSION_Q2PRO_CLIENTNUM_SHORT      1022    // r2161
#define PROTOCOL_VERSION_Q2PRO_CINEMATICS           1023    // r2263
#define PROTOCOL_VERSION_Q2PRO_EXTENDED_LIMITS      1024    // r2894
#define PROTOCOL_VERSION_Q2PRO_EXTENDED_LIMITS_2    1025    // r3300
#define PROTOCOL_VERSION_Q2PRO_PLAYERFOG            1026    // r3579
#define PROTOCOL_VERSION_Q2PRO_CURRENT              1026    // r3579

#define PROTOCOL_VERSION_MVD_MINIMUM            2009    // r168
#define PROTOCOL_VERSION_MVD_DEFAULT            2010    // r177
#define PROTOCOL_VERSION_MVD_EXTENDED_LIMITS    2011    // r2894
#define PROTOCOL_VERSION_MVD_EXTENDED_LIMITS_2  2012    // r3300
#define PROTOCOL_VERSION_MVD_PLAYERFOG          2013    // r3579
#define PROTOCOL_VERSION_MVD_CURRENT            2013    // r3579
#define PROTOCOL_VERSION_MVD_RERELEASE          3038

#define R1Q2_SUPPORTED(x) \
    ((x) >= PROTOCOL_VERSION_R1Q2_MINIMUM && \
     (x) <= PROTOCOL_VERSION_R1Q2_CURRENT)

#define Q2PRO_SUPPORTED(x) \
    ((x) >= PROTOCOL_VERSION_Q2PRO_MINIMUM && \
     (x) <= PROTOCOL_VERSION_Q2PRO_CURRENT)

#define MVD_SUPPORTED(x) \
    (((x) >= PROTOCOL_VERSION_MVD_MINIMUM && \
      (x) <= PROTOCOL_VERSION_MVD_CURRENT) \
     || ((x) == PROTOCOL_VERSION_MVD_RERELEASE))

#define EXTENDED_SUPPORTED(x) \
    ((x) >= PROTOCOL_VERSION_EXTENDED_MINIMUM && \
     (x) <= PROTOCOL_VERSION_EXTENDED_CURRENT)

#define VALIDATE_CLIENTNUM(csr, x) \
    ((x) >= -1 && (x) < (csr)->max_edicts - 1)

#define Q2PRO_PF_STRAFEJUMP_HACK    BIT(0)
#define Q2PRO_PF_QW_MODE            BIT(1)
#define Q2PRO_PF_WATERJUMP_HACK     BIT(2)
#define Q2PRO_PF_EXTENSIONS         BIT(3)
#define Q2PRO_PF_EXTENSIONS_2       BIT(4)
#define Q2PRO_PF_GAME3_COMPAT       BIT(15) // This indicates the server game library is a version 3 game

//=========================================

#define UPDATE_BACKUP   16  // copies of entity_state_t to keep buffered
                            // must be power of two
#define UPDATE_MASK     (UPDATE_BACKUP - 1)

#define CMD_BACKUP      128 // allow a lot of command backups for very fast systems
                            // increased from 64
#define CMD_MASK        (CMD_BACKUP - 1)

#define SVCMD_BITS              5
#define SVCMD_MASK              MASK(SVCMD_BITS)

#define FRAMENUM_BITS           27
#define FRAMENUM_MASK           MASK(FRAMENUM_BITS)

#define SUPPRESSCOUNT_BITS      4
#define SUPPRESSCOUNT_MASK      MASK(SUPPRESSCOUNT_BITS)

#define MAX_PACKET_ENTITIES_OLD 128

#define MAX_PACKET_ENTITIES     512
#define MAX_PARSE_ENTITIES      (MAX_PACKET_ENTITIES * UPDATE_BACKUP)
#define PARSE_ENTITIES_MASK     (MAX_PARSE_ENTITIES - 1)

#define MAX_PACKET_USERCMDS     32
#define MAX_PACKET_FRAMES       4

#define MAX_PACKET_STRINGCMDS   8
#define MAX_PACKET_USERINFOS    8

#define MVD_MAGIC               MakeRawLong('M','V','D','2')

//
// server to client
//
typedef enum {
    svc_bad,

    // these ops are known to the game dll
    svc_muzzleflash,
    svc_muzzleflash2,
    svc_temp_entity,
    svc_layout,
    svc_inventory,
    // Q2PRO game
    svc_stufftext = 11,         // [string] stuffed into client's console buffer
                                // should be \n terminated
    // Rerelease game
    svc_level_restart = 24,     // [Paril-KEX] level was soft-rebooted
    svc_damage,                 // [Paril-KEX] damage indicators
    svc_locprint,               // [Kex] localized + libfmt version of print
    svc_fog,                    // [Paril-KEX] change current fog values
    svc_waitingforplayers,      // [Kex-Edward] Inform clients that the server is waiting for remaining players
    svc_bot_chat,               // [Kex] bot specific chat
    svc_poi,                    // [Paril-KEX] point of interest
    svc_help_path,              // [Paril-KEX] help path
    svc_muzzleflash3,           // [Paril-KEX] muzzleflashes, but ushort id
    svc_achievement,            // [Paril-KEX]

    // the rest are private to the client and server
    svc_nop = 6,
    svc_disconnect,
    svc_reconnect,
    svc_sound,                  // <see code>
    svc_print,                  // [byte] id [string] null terminated string
    svc_serverdata = 12,        // [long] protocol ...
    svc_configstring,           // [short] [string]
    svc_spawnbaseline,
    svc_centerprint,            // [string] to put in center of the screen
    svc_download,               // [short] size [size bytes]
    svc_playerinfo,             // variable
    svc_packetentities,         // [...]
    svc_deltapacketentities,    // [...]
    svc_frame,

// KEX
    // svc_splitclient,
    // svc_configblast,            // [Kex] A compressed version of svc_configstring
    // svc_spawnbaselineblast,     // [Kex] A compressed version of svc_spawnbaseline
// KEX

    // Same meaning as "R1Q2 specific operations" below, but different values
    svc_rr_zpacket = 34,
    svc_rr_zdownload,
    svc_rr_gamestate,
    svc_rr_setting,

    svc_rr_configstringstream,
    svc_rr_baselinestream,

    // R1Q2 specific operations
    svc_q2pro_zpacket = 21,
    svc_q2pro_zdownload,
    svc_q2pro_gamestate, // q2pro specific, means svc_playerupdate in r1q2
    svc_q2pro_setting,

    // Q2PRO specific operations
    svc_q2pro_configstringstream,
    svc_q2pro_baselinestream,

    svc_num_types
} svc_ops_t;

// MVD protocol specific operations
typedef enum {
    mvd_bad,
    mvd_nop,
    mvd_disconnect,     // reserved
    mvd_reconnect,      // reserved
    mvd_serverdata,
    mvd_configstring,
    mvd_frame,
    mvd_frame_nodelta,  // reserved
    mvd_unicast,
    mvd_unicast_r,

    // must match multicast_t order!!!
    mvd_multicast_all,
    mvd_multicast_phs,
    mvd_multicast_pvs,
    mvd_multicast_all_r,
    mvd_multicast_phs_r,
    mvd_multicast_pvs_r,

    mvd_sound,
    mvd_print,
    mvd_stufftext,      // reserved

    mvd_num_types
} mvd_ops_t;

// MVD stream flags
typedef enum {
    MVF_NOMSGS      = BIT(0),
    MVF_SINGLEPOV   = BIT(1),
    MVF_EXTLIMITS   = BIT(2),
    MVF_EXTLIMITS_2 = BIT(3),
} mvd_flags_t;

//==============================================

//
// client to server
//
typedef enum {
    clc_bad,
    clc_nop,
    clc_move,               // [usercmd_t]
    clc_userinfo,           // [userinfo string]
    clc_stringcmd,          // [string] message

    // R1Q2 specific operations
    clc_setting,

    // Q2PRO specific operations
    clc_move_nodelta = 10,
    clc_move_batched,
    clc_userinfo_delta
} clc_ops_t;

//==============================================

typedef enum {
    Q2PRO_FOG_BIT_COLOR               = BIT(0),
    Q2PRO_FOG_BIT_DENSITY             = BIT(1),
    Q2PRO_FOG_BIT_HEIGHT_DENSITY      = BIT(2),
    Q2PRO_FOG_BIT_HEIGHT_FALLOFF      = BIT(3),
    Q2PRO_FOG_BIT_HEIGHT_START_COLOR  = BIT(4),
    Q2PRO_FOG_BIT_HEIGHT_END_COLOR    = BIT(5),
    Q2PRO_FOG_BIT_HEIGHT_START_DIST   = BIT(6),
    Q2PRO_FOG_BIT_HEIGHT_END_DIST     = BIT(7),
} q2pro_fog_bits_t;

// player_state_t communication

#define PS_M_TYPE           BIT(0)
#define PS_M_ORIGIN         BIT(1)
#define PS_M_VELOCITY       BIT(2)
#define PS_M_TIME           BIT(3)
#define PS_M_FLAGS          BIT(4)
#define PS_M_GRAVITY        BIT(5)
#define PS_M_DELTA_ANGLES   BIT(6)

#define PS_VIEWOFFSET       BIT(7)
#define PS_VIEWANGLES       BIT(8)
#define PS_KICKANGLES       BIT(9)
#define PS_BLEND            BIT(10)
#define PS_FOV              BIT(11)
#define PS_WEAPONINDEX      BIT(12)
#define PS_WEAPONFRAME      BIT(13)
#define PS_RDFLAGS          BIT(14)
#define PS_RR_VIEWHEIGHT    BIT(15) // re-release
#define PS_MOREBITS         BIT(15)     // read one additional byte (q2pro)

#define PS_FOG              BIT(16)

// R1Q2 protocol specific extra flags
#define EPS_GUNOFFSET       BIT(0)
#define EPS_GUNANGLES       BIT(1)
#define EPS_M_VELOCITY2     BIT(2)
#define EPS_M_ORIGIN2       BIT(3)
#define EPS_VIEWANGLE2      BIT(4)
#define EPS_STATS           BIT(5)

// Q2PRO protocol specific extra flags
#define EPS_CLIENTNUM       BIT(6)
// KEX
#define EPS_GUNRATE         BIT(7)

#define BLENDBITS_SCREEN_R  BIT(0)
#define BLENDBITS_SCREEN_G  BIT(1)
#define BLENDBITS_SCREEN_B  BIT(2)
#define BLENDBITS_SCREEN_A  BIT(3)
#define BLENDBITS_DAMAGE_R  BIT(4)
#define BLENDBITS_DAMAGE_G  BIT(5)
#define BLENDBITS_DAMAGE_B  BIT(6)
#define BLENDBITS_DAMAGE_A  BIT(7)
// KEX

//==============================================

// packetized player_state_t communication (MVD specific)

#define PPS_M_TYPE          BIT(0)
#define PPS_M_ORIGIN        BIT(1)
#define PPS_M_ORIGIN2       BIT(2)

#define PPS_VIEWOFFSET      BIT(3)
#define PPS_VIEWANGLES      BIT(4)
#define PPS_VIEWANGLE2      BIT(5)
#define PPS_KICKANGLES      BIT(6)
#define PPS_BLEND           BIT(7)
#define PPS_FOV             BIT(8)
#define PPS_WEAPONINDEX     BIT(9)
#define PPS_WEAPONFRAME     BIT(10)
#define PPS_GUNOFFSET       BIT(11)
#define PPS_GUNANGLES       BIT(12)
#define PPS_RDFLAGS         BIT(13)
#define PPS_STATS           BIT(14)
#define PPS_MOREBITS        BIT(15)     // read one additional byte
                                        // same as PPS_REMOVE for old demos!!!

#define PPS_REMOVE          BIT(16)
#define PPS_FOG             BIT(17)

// this is just a small hack to store inuse flag
// in a field left otherwise unused by MVD code
#define PPS_INUSE(ps)       (ps)->pmove.pm_time

//==============================================

// user_cmd_t communication

// ms and light always sent, the others are optional
#define CM_ANGLE1       BIT(0)
#define CM_ANGLE2       BIT(1)
#define CM_ANGLE3       BIT(2)
#define CM_FORWARD      BIT(3)
#define CM_SIDE         BIT(4)
#define CM_UP           BIT(5)
#define CM_BUTTONS      BIT(6)
#define CM_IMPULSE      BIT(7)

// R1Q2 button byte hacks
#define BUTTON_MASK     (BUTTON_ATTACK|BUTTON_USE|BUTTON_ANY)
#define BUTTON_FORWARD  BIT(2)
#define BUTTON_SIDE     BIT(3)
#define BUTTON_UP       BIT(4)
#define BUTTON_ANGLE1   BIT(5)
#define BUTTON_ANGLE2   BIT(6)

//==============================================

// a sound without an ent or pos will be a local only sound
#define SND_VOLUME          BIT(0)  // a byte
#define SND_ATTENUATION     BIT(1)  // a byte
#define SND_POS             BIT(2)  // three coordinates
#define SND_ENT             BIT(3)  // a short 0-2: channel, 3-15: entity
#define SND_OFFSET          BIT(4)  // a byte, msec offset from frame start
#define SND_INDEX16         BIT(5)  // index is 16-bit

#define DEFAULT_SOUND_PACKET_VOLUME         1.0f
#define DEFAULT_SOUND_PACKET_ATTENUATION    1.0f

//==============================================

// entity_state_t communication

// try to pack the common update flags into the first byte
#define U_ORIGIN1       BIT_ULL(0)
#define U_ORIGIN2       BIT_ULL(1)
#define U_ANGLE2        BIT_ULL(2)
#define U_ANGLE3        BIT_ULL(3)
#define U_FRAME8        BIT_ULL(4)      // frame is a byte
#define U_EVENT         BIT_ULL(5)
#define U_REMOVE        BIT_ULL(6)      // REMOVE this entity, don't add it
#define U_MOREBITS1     BIT_ULL(7)      // read one additional byte

// second byte
#define U_NUMBER16      BIT_ULL(8)      // NUMBER8 is implicit if not set
#define U_ORIGIN3       BIT_ULL(9)
#define U_ANGLE1        BIT_ULL(10)
#define U_MODEL         BIT_ULL(11)
#define U_RENDERFX8     BIT_ULL(12)     // fullbright, etc
#define U_ANGLE16       BIT_ULL(13)
#define U_EFFECTS8      BIT_ULL(14)     // autorotate, trails, etc
#define U_MOREBITS2     BIT_ULL(15)     // read one additional byte

// third byte
#define U_SKIN8         BIT_ULL(16)
#define U_FRAME16       BIT_ULL(17)     // frame is a short
#define U_RENDERFX16    BIT_ULL(18)     // 8 + 16 = 32
#define U_EFFECTS16     BIT_ULL(19)     // 8 + 16 = 32
#define U_MODEL2        BIT_ULL(20)     // weapons, flags, etc
#define U_MODEL3        BIT_ULL(21)
#define U_MODEL4        BIT_ULL(22)
#define U_MOREBITS3     BIT_ULL(23)     // read one additional byte

// fourth byte
#define U_OLDORIGIN     BIT_ULL(24)     // FIXME: get rid of this
#define U_SKIN16        BIT_ULL(25)
#define U_SOUND         BIT_ULL(26)
#define U_SOLID         BIT_ULL(27)
#define U_MODEL16       BIT_ULL(28)
#define U_MOREFX8       BIT_ULL(29)
#define U_ALPHA         BIT_ULL(30)
#define U_MOREBITS4     BIT_ULL(31)     // read one additional byte

// fifth byte
#define U_SCALE         BIT_ULL(32)
#define U_MOREFX16      BIT_ULL(33)

#define U_SKIN32        (U_SKIN8 | U_SKIN16)        // used for laser colors
#define U_EFFECTS32     (U_EFFECTS8 | U_EFFECTS16)
#define U_RENDERFX32    (U_RENDERFX8 | U_RENDERFX16)
#define U_MOREFX32      (U_MOREFX8 | U_MOREFX16)

// ==============================================================

// a client with this number will never be included in MVD stream
#define CLIENTNUM_NONE      (MAX_CLIENTS - 1)

// a SOLID_BBOX will never create this value
#define PACKED_BSP      31

typedef enum {
    // R1Q2 specific
    CLS_NOGUN,
    CLS_NOBLEND,
    CLS_RECORDING,
    CLS_PLAYERUPDATES,
    CLS_FPS,

    // Q2PRO specific
    CLS_NOGIBS            = 10,
    CLS_NOFOOTSTEPS,
    CLS_NOPREDICT,
    CLS_NOFLARES,

    CLS_MAX
} clientSetting_t;

typedef enum {
    // R1Q2 specific
    SVS_PLAYERUPDATES,
    SVS_FPS,

    SVS_MAX
} serverSetting_t;

// Q2PRO frame flags sent by the server
// only SUPPRESSCOUNT_BITS can be used
#define FF_SUPPRESSED   BIT(0)
#define FF_CLIENTDROP   BIT(1)
#define FF_CLIENTPRED   BIT(2)
#define FF_RESERVED     BIT(3)
