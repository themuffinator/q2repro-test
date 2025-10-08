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

#include "../server.h"
#include "shared/game3_shared.h"
#include "shared/game3.h"
#include "game3_proxy.h"
#include "shared/base85.h"
#include "common/game3_pmove.h"
#include "common/game3_convert.h"

#include <assert.h>
#if HAVE_MALLOC_H
#include <malloc.h>
#endif
#include <stdlib.h>

static const cs_remap_t *game_csr;

static game3_edict_t* translate_edict_to_game(edict_t* ent);
static void game_entity_state_to_server(entity_state_t *server_state, const game3_entity_state_t *game_state);

static void sync_single_edict_server_to_game(int index);
static void sync_edicts_server_to_game(void);
static void sync_single_edict_game_to_server(int index);
static void sync_edicts_game_to_server(void);

static game_import_t game_import;

static game3_export_t *game3_export;
static const game3_export_ex_t *game3_export_ex;
static game_export_t game_export;

static void wrap_RestartFilesystem(void)
{
    game3_export_ex->RestartFilesystem();
}

const char *game_q2pro_restart_filesystem_ext = "q2pro:restart_filesystem";

static const game_q2pro_restart_filesystem_t game_q2pro_restart_filesystem = {
    .api_version = 1,

    .RestartFilesystem = wrap_RestartFilesystem,
};

static qboolean wrap_CustomizeEntityToClient(edict_t *client, edict_t *ent, customize_entity_t *temp)
{
    game3_customize_entity_t new_temp;
    qboolean result = game3_export_ex->CustomizeEntityToClient(translate_edict_to_game(client), translate_edict_to_game(ent), &new_temp);
    if (result)
    {
        game_entity_state_to_server(&temp->s, &new_temp.s);
    }
    return result;
}

static qboolean wrap_EntityVisibleToClient(edict_t *client, edict_t *ent)
{
    return game3_export_ex->EntityVisibleToClient(translate_edict_to_game(client), translate_edict_to_game(ent));
}

const char *game_q2pro_customize_entity_ext = "q2pro:customize_entity";

static const game_q2pro_customize_entity_t game_q2pro_customize_entity = {
    .api_version = 1,

    .CustomizeEntityToClient = wrap_CustomizeEntityToClient,
    .EntityVisibleToClient = wrap_EntityVisibleToClient,
};

// flag indicating if game uses gclient_new_t or gclient_old_t.
// doesn't enable protocol extensions by itself.
#define IS_NEW_GAME_API    (game3_export->apiversion == GAME3_API_VERSION_NEW)

#define GAME_EDICT_NUM(n) ((game3_edict_t *)((byte *)game3_export->edicts + game3_export->edict_size*(n)))
#define NUM_FOR_GAME_EDICT(e) ((int)(((byte *)(e) - (byte *)game3_export->edicts) / game3_export->edict_size))

typedef struct {
    edict_t edict;

    // fog values to deal with fog changes
    struct {
        player_fog_t linear;
        player_heightfog_t height;
    } fog;
} game3_proxy_edict_t;

static game3_proxy_edict_t *server_edicts;

static edict_t *translate_edict_from_game(game3_edict_t *ent)
{
    assert(!ent || (ent >= GAME_EDICT_NUM(0) && ent < GAME_EDICT_NUM(game3_export->num_edicts)));
    return ent ? &server_edicts[NUM_FOR_GAME_EDICT(ent)].edict : NULL;
}

static game3_edict_t* translate_edict_to_game(edict_t* ent)
{
    game3_proxy_edict_t *proxy_edict = (game3_proxy_edict_t *)ent;
    assert(!proxy_edict || (proxy_edict >= server_edicts && proxy_edict < server_edicts + game_export.num_edicts));
    return ent ? GAME_EDICT_NUM(proxy_edict - server_edicts) : NULL;
}

static void wrap_multicast(const vec3_t origin, multicast_t to)
{
    game_import.multicast(origin, to, false);
}

static void wrap_unicast(game3_edict_t *gent, qboolean reliable)
{
    edict_t *ent = translate_edict_from_game(gent);
    game_import.unicast(ent, reliable, 0);
}

static void wrap_bprintf(int printlevel, const char *fmt, ...)
{
    char        msg[MAX_STRING_CHARS];
    va_list     argptr;
    va_start(argptr, fmt);
    size_t len = Q_vsnprintf(msg, sizeof(msg), fmt, argptr);
    va_end(argptr);

    if (len >= sizeof(msg)) {
        Com_WPrintf("%s: overflow\n", __func__);
        return;
    }
    game_import.Broadcast_Print(printlevel, msg);
}

static void wrap_dprintf(const char *fmt, ...)
{
    char        msg[MAX_STRING_CHARS];
    va_list     argptr;
    va_start(argptr, fmt);
    size_t len = Q_vsnprintf(msg, sizeof(msg), fmt, argptr);
    va_end(argptr);

    if (len >= sizeof(msg)) {
        Com_WPrintf("%s: overflow\n", __func__);
        return;
    }

#if USE_SAVEGAMES
    // detect YQ2 game lib by unique first two messages
    if (!svs.gamedetecthack)
        svs.gamedetecthack = 1 + !strcmp(fmt, "Game is starting up.\n");
    else if (svs.gamedetecthack == 2)
        svs.gamedetecthack = 3 + !strcmp(fmt, "Game is %s built on %s.\n");
#endif

    game_import.Com_Print(msg);
}

static void wrap_cprintf(game3_edict_t *gent, int level, const char *fmt, ...)
{
    edict_t *ent = translate_edict_from_game(gent);

    char        msg[MAX_STRING_CHARS];
    va_list     argptr;
    va_start(argptr, fmt);
    size_t len = Q_vsnprintf(msg, sizeof(msg), fmt, argptr);
    va_end(argptr);

    if (len >= sizeof(msg)) {
        Com_WPrintf("%s: overflow\n", __func__);
        return;
    }
    game_import.Client_Print(ent, level, msg);
}

static void wrap_centerprintf(game3_edict_t *gent, const char *fmt, ...)
{
    edict_t *ent = translate_edict_from_game(gent);

    char        msg[MAX_STRING_CHARS];
    va_list     argptr;
    va_start(argptr, fmt);
    size_t len = Q_vsnprintf(msg, sizeof(msg), fmt, argptr);
    va_end(argptr);

    if (len >= sizeof(msg)) {
        Com_WPrintf("%s: overflow\n", __func__);
        return;
    }
    game_import.Center_Print(ent, msg);
}

static q_noreturn void wrap_error(const char *fmt, ...)
{
    char        msg[MAX_STRING_CHARS];
    va_list     argptr;
    va_start(argptr, fmt);
    size_t len = Q_vsnprintf(msg, sizeof(msg), fmt, argptr);
    va_end(argptr);

    if (len >= sizeof(msg)) {
        Com_WPrintf("%s: overflow\n", __func__);
    }
    game_import.Com_Error(msg);
}


static void wrap_setmodel(game3_edict_t *gent, const char *name)
{
    game_export.num_edicts = game3_export->num_edicts;

    int ent_idx = NUM_FOR_GAME_EDICT(gent);
    sync_single_edict_game_to_server(ent_idx);
    game_import.setmodel(translate_edict_from_game(gent), name);
    sync_single_edict_server_to_game(ent_idx);
}

static qboolean wrap_inPVS(const vec3_t p1, const vec3_t p2)
{
    return game_import.inPVS(p1, p2, true);
}

static qboolean wrap_inPHS(const vec3_t p1, const vec3_t p2)
{
    return game_import.inPHS(p1, p2, true);
}

static void wrap_Pmove_import(void *pmove)
{
    const pmoveParams_t *pmp = sv_client ? &sv_client->pmp : &svs.pmp;

    if (IS_NEW_GAME_API)
        game3_PmoveNew(pmove, &((game3_pmove_new_t*)pmove)->groundplane, pmp);
    else
        game3_PmoveOld(pmove, NULL, pmp);
}

static void wrap_sound(game3_edict_t *gent, int channel,
                       int soundindex, float volume,
                       float attenuation, float timeofs)
{
    edict_t *ent = translate_edict_from_game(gent);
    game_import.sound(ent, channel, soundindex, volume, attenuation, timeofs);
}

static void wrap_positioned_sound(const vec3_t origin, game3_edict_t *gent, int channel,
                                  int soundindex, float volume,
                                  float attenuation, float timeofs)
{
    edict_t *ent = translate_edict_from_game(gent);
    game_import.positioned_sound(origin, ent, channel, soundindex, volume, attenuation, timeofs);
}

static void wrap_unlinkentity(game3_edict_t *ent)
{
    int ent_idx = NUM_FOR_GAME_EDICT(ent);
    sync_single_edict_game_to_server(ent_idx);
    game_import.unlinkentity(translate_edict_from_game(ent));
    sync_single_edict_server_to_game(ent_idx);
}

static void wrap_linkentity(game3_edict_t *ent)
{
    game_export.num_edicts = game3_export->num_edicts;

    int ent_idx = NUM_FOR_GAME_EDICT(ent);
    sync_single_edict_game_to_server(ent_idx);
    game_import.linkentity(translate_edict_from_game(ent));
    sync_single_edict_server_to_game(ent_idx);
}

static int wrap_BoxEdicts(const vec3_t mins, const vec3_t maxs, game3_edict_t **glist, int maxcount, int areatype)
{
    edict_t **list = alloca(maxcount * sizeof(edict_t *));
    size_t num_edicts = game_import.BoxEdicts(mins, maxs, list, maxcount, areatype, NULL, NULL);
    for (size_t i = 0; i < min(num_edicts, maxcount); i++) {
        glist[i] = translate_edict_to_game(list[i]);
    }
    return (int)min(num_edicts, maxcount);
}

static void server_trace_to_game(game3_trace_t *tr, const trace_t *str)
{
    tr->allsolid = str->allsolid;
    tr->startsolid = str->startsolid;
    tr->fraction = str->fraction;
    VectorCopy(str->endpos, tr->endpos);
    tr->plane = str->plane;
    tr->surface = &str->surface->surface_v3;
    tr->contents = str->contents;
    tr->ent = translate_edict_to_game(str->ent);
}

static game3_trace_t wrap_trace(const vec3_t start, const vec3_t mins,
                                const vec3_t maxs, const vec3_t end,
                                game3_edict_t *passedict, int contentmask)
{
    trace_t str = game_import.trace(start, mins, maxs, end, translate_edict_from_game(passedict), contentmask);
    game3_trace_t tr;
    server_trace_to_game(&tr, &str);
    return tr;
}

static int wrap_pointcontents(const vec3_t point)
{
    return game_import.pointcontents(point);
}

static void wrap_local_sound(game3_edict_t *target, const vec3_t origin, game3_edict_t *ent, int channel, int soundindex, float volume, float attenuation, float timeofs)
{
    game_import.local_sound(translate_edict_from_game(target), origin, translate_edict_from_game(ent), channel, soundindex, volume, attenuation, timeofs, 0);
}

static void wrap_configstring(int index, const char* str)
{
    game_import.configstring(index, str);
}

static const char *wrap_get_configstring(int index)
{
    return game_import.get_configstring(index);
}

static trace_t wrap_clip(const vec3_t start, const vec3_t mins, const vec3_t maxs, const vec3_t end, game3_edict_t *clip, int contentmask)
{
    return game_import.clip(translate_edict_from_game(clip), start, mins, maxs, end, contentmask);
}

static qboolean wrap_inVIS(const vec3_t p1, const vec3_t p2, vis_t vis)
{
    switch(vis & ~VIS_NOAREAS)
    {
    case VIS_PVS:
        return game_import.inPVS(p1, p2, (vis & VIS_NOAREAS) == 0);
    case VIS_PHS:
        return game_import.inPHS(p1, p2, (vis & VIS_NOAREAS) == 0);
    }
    return false;
}

static void *wrap_GetExtension_import(const char *name)
{
    return game_import.GetExtension(name);
}

static void *wrap_TagMalloc(unsigned size, unsigned tag)
{
    return game_import.TagMalloc(size, tag);
}

static void wrap_FreeTags(unsigned tag)
{
    game_import.FreeTags(tag);
}

static void *PF_TagRealloc(void *ptr, size_t size)
{
    if (!ptr && size) {
        Com_Error(ERR_DROP, "%s: untagged allocation not allowed", __func__);
    }
    return Z_Realloc(ptr, size);
}

static char* wrap_argv(int idx)
{
    // TODO ugly
    return (char *)game_import.argv(idx);
}

static char* wrap_args(void)
{
    // TODO ugly
    return (char *)game_import.args();
}

static void wrap_SetAreaPortalState(int portalnum, qboolean open)
{
    game_import.SetAreaPortalState(portalnum, open);
}

// Macro to check whether a member that wasn't synced has been unexpectedly changed
#define VERIFY_UNCHANGED(pred, a, b, member) \
    if (pred)                                \
    {                                        \
        assert((a).member == (b).member);    \
    }

static void sync_single_edict_server_to_game(int index)
{
    edict_t *server_edict = &server_edicts[index].edict;
    server_entity_t *sent = &sv.entities[index];
    game3_edict_t *game_edict = GAME_EDICT_NUM(index);

    // Skip unused entities
    bool server_edict_inuse = server_edict->inuse || HAS_EFFECTS(server_edict);
    if(!server_edict_inuse && !game_edict->inuse)
        return;

    bool is_new = !(game_edict->inuse || HAS_EFFECTS(game_edict));
    game_edict->inuse = server_edict->inuse;
    // Don't change fields if edict became unused
    if(!game_edict->inuse)
        return;

    // Check that fields not changed by server are, indeed, unchanged
    VERIFY_UNCHANGED(!is_new, server_edict->s, game_edict->s, origin[0]);
    VERIFY_UNCHANGED(!is_new, server_edict->s, game_edict->s, origin[1]);
    VERIFY_UNCHANGED(!is_new, server_edict->s, game_edict->s, origin[2]);
    VERIFY_UNCHANGED(!is_new, server_edict->s, game_edict->s, angles[0]);
    VERIFY_UNCHANGED(!is_new, server_edict->s, game_edict->s, angles[1]);
    VERIFY_UNCHANGED(!is_new, server_edict->s, game_edict->s, angles[2]);
    VERIFY_UNCHANGED(!is_new, server_edict->s, game_edict->s, modelindex2);
    VERIFY_UNCHANGED(!is_new, server_edict->s, game_edict->s, modelindex3);
    VERIFY_UNCHANGED(!is_new, server_edict->s, game_edict->s, modelindex4);
    VERIFY_UNCHANGED(!is_new, server_edict->s, game_edict->s, frame);
    VERIFY_UNCHANGED(!is_new, server_edict->s, game_edict->s, skinnum);
    VERIFY_UNCHANGED(!is_new, server_edict->s, game_edict->s, effects);
    VERIFY_UNCHANGED(!is_new, server_edict->s, game_edict->s, renderfx);
    VERIFY_UNCHANGED(!is_new, server_edict->s, game_edict->s, sound);

    game_edict->s.number = server_edict->s.number;
    VectorCopy(server_edict->s.old_origin, game_edict->s.old_origin);
    game_edict->s.modelindex = server_edict->s.modelindex;
    game_edict->s.solid = server_edict->s.solid;
    game_edict->s.event = server_edict->s.event;

    assert((game_edict->client && server_edict->client) || (!game_edict->client && !server_edict->client));
    if(game_edict->client) {
        if (IS_NEW_GAME_API)
            ((struct game3_gclient_new_s*)game_edict->client)->ping = server_edict->client->ping;
        else
            ((struct game3_gclient_old_s*)game_edict->client)->ping = server_edict->client->ping;
    }
    game_edict->linkcount = server_edict->linkcount;
    game_edict->area.next = game_edict->area.prev = server_edict->linked ? &game_edict->area : NULL; // emulate entity being linked
    game_edict->num_clusters = sent->num_clusters;
    memcpy(&game_edict->clusternums, &sent->clusternums, sizeof(game_edict->clusternums));
    game_edict->headnode = sent->headnode;
    game_edict->areanum = server_edict->areanum;
    game_edict->areanum2 = server_edict->areanum2;
    game_edict->svflags = server_edict->svflags;

    /* Although it's said "If the size, position, or solidity changes, [an edict] must be relinked.",
     * those fields may not only be changed by linkentity. */
    // changed by setmodel
    VectorCopy(server_edict->mins, game_edict->mins);
    VectorCopy(server_edict->maxs, game_edict->maxs);
    // changed by linkentity
    VectorCopy(server_edict->absmin, game_edict->absmin);
    VectorCopy(server_edict->absmax, game_edict->absmax);
    VectorCopy(server_edict->size, game_edict->size);
    VERIFY_UNCHANGED(!is_new, *game_edict, *server_edict, solid);
    VERIFY_UNCHANGED(!is_new, *game_edict, *server_edict, clipmask);

    // We cannot sensibly set 'sv', as this is all game-private data.

    // slightly changed 'translate_edict_to_game', as owners may be beyond num_edicts when loading a game
    game3_proxy_edict_t *proxy_owner = (game3_proxy_edict_t *)server_edict->owner;
    assert(!proxy_owner || (proxy_owner >= server_edicts && proxy_owner < server_edicts + game_export.max_edicts));
    game_edict->owner = proxy_owner ? GAME_EDICT_NUM(proxy_owner - server_edicts) : NULL;
}

// Sync edicts from server to game
static void sync_edicts_server_to_game(void)
{
    for (int i = 0; i < game_export.num_edicts; i++) {
        sync_single_edict_server_to_game(i);
    }

    game3_export->num_edicts = game_export.num_edicts;
}

static void game_client_old_to_server(struct gclient_s *server_client, const struct game3_gclient_old_s *game_client)
{
    ConvertFromGame3_pmove_state_old(&server_client->ps.pmove, &game_client->ps.pmove, game_csr->extended);

    VectorCopy(game_client->ps.viewangles, server_client->ps.viewangles);
    VectorCopy(game_client->ps.viewoffset, server_client->ps.viewoffset);
    VectorCopy(game_client->ps.kick_angles, server_client->ps.kick_angles);
    VectorCopy(game_client->ps.gunangles, server_client->ps.gunangles);
    VectorCopy(game_client->ps.gunoffset, server_client->ps.gunoffset);
    server_client->ps.gunindex = game_client->ps.gunindex & GUNINDEX_MASK;
    server_client->ps.gunskin = game_client->ps.gunindex >> GUNINDEX_BITS;
    server_client->ps.gunframe = game_client->ps.gunframe;
    Vector4Copy(game_client->ps.blend, server_client->ps.screen_blend);
    server_client->ps.fov = game_client->ps.fov;
    server_client->ps.rdflags = game_client->ps.rdflags;
    memset(&server_client->ps.stats, 0, sizeof(server_client->ps.stats));
    memcpy(&server_client->ps.stats, &game_client->ps.stats, sizeof(game_client->ps.stats));

    server_client->ping = game_client->ping;
    // FIXME: Only copy if GMF_CLIENTNUM feature is set
    server_client->clientNum = game_client->clientNum;
}

static void game_client_new_to_server(struct gclient_s *server_client, const struct game3_gclient_new_s *game_client)
{
    ConvertFromGame3_pmove_state_new(&server_client->ps.pmove, &game_client->ps.pmove, game_csr->extended);

    VectorCopy(game_client->ps.viewangles, server_client->ps.viewangles);
    VectorCopy(game_client->ps.viewoffset, server_client->ps.viewoffset);
    VectorCopy(game_client->ps.kick_angles, server_client->ps.kick_angles);
    VectorCopy(game_client->ps.gunangles, server_client->ps.gunangles);
    VectorCopy(game_client->ps.gunoffset, server_client->ps.gunoffset);
    server_client->ps.gunindex = game_client->ps.gunindex & GUNINDEX_MASK;
    server_client->ps.gunskin = game_client->ps.gunindex >> GUNINDEX_BITS;
    server_client->ps.gunframe = game_client->ps.gunframe;
    Vector4Copy(game_client->ps.blend, server_client->ps.screen_blend);
    server_client->ps.fov = game_client->ps.fov;
    server_client->ps.rdflags = game_client->ps.rdflags;
    memcpy(&server_client->ps.stats, &game_client->ps.stats, sizeof(game_client->ps.stats));

    server_client->ping = game_client->ping;
    // FIXME: Only copy if GMF_CLIENTNUM feature is set
    server_client->clientNum = game_client->clientNum;
}

static void game_entity_state_to_server(entity_state_t* server_state, const game3_entity_state_t* game_state)
{
    server_state->number = game_state->number;
    VectorCopy(game_state->origin, server_state->origin);
    VectorCopy(game_state->angles, server_state->angles);
    VectorCopy(game_state->old_origin, server_state->old_origin);
    server_state->modelindex = game_state->modelindex;
    server_state->modelindex2 = game_state->modelindex2;
    server_state->modelindex3 = game_state->modelindex3;
    server_state->modelindex4 = game_state->modelindex4;
    server_state->frame = game_state->frame;
    server_state->skinnum = game_state->skinnum;
    server_state->effects = game_state->effects;
    server_state->renderfx = game_state->renderfx;
    server_state->solid = game_state->solid;
    server_state->sound = game_state->sound;
    server_state->event = game_state->event;
}

static void sync_fog(edict_t *server_edict, const game3_player_state_new_t *ps)
{
    game3_proxy_edict_t *proxy_edict = (game3_proxy_edict_t *)server_edict;

    int fog_bits = 0;
    if (proxy_edict->fog.linear.color[0] != ps->fog.color[0])
        fog_bits |= FOG_BIT_R;
    if (proxy_edict->fog.linear.color[1] != ps->fog.color[1])
        fog_bits |= FOG_BIT_G;
    if (proxy_edict->fog.linear.color[2] != ps->fog.color[2])
        fog_bits |= FOG_BIT_B;
    if (proxy_edict->fog.linear.density != ps->fog.density || proxy_edict->fog.linear.sky_factor != ps->fog.sky_factor)
        fog_bits |= FOG_BIT_DENSITY;

    if (proxy_edict->fog.height.start.color[0] != ps->heightfog.start.color[0])
        fog_bits |= FOG_BIT_HEIGHTFOG_START_R;
    if (proxy_edict->fog.height.start.color[1] != ps->heightfog.start.color[1])
        fog_bits |= FOG_BIT_HEIGHTFOG_START_G;
    if (proxy_edict->fog.height.start.color[2] != ps->heightfog.start.color[2])
        fog_bits |= FOG_BIT_HEIGHTFOG_START_B;
    if (proxy_edict->fog.height.start.dist != ps->heightfog.start.dist)
        fog_bits |= FOG_BIT_HEIGHTFOG_START_DIST;

    if (proxy_edict->fog.height.end.color[0] != ps->heightfog.end.color[0])
        fog_bits |= FOG_BIT_HEIGHTFOG_END_R;
    if (proxy_edict->fog.height.end.color[1] != ps->heightfog.end.color[1])
        fog_bits |= FOG_BIT_HEIGHTFOG_END_G;
    if (proxy_edict->fog.height.end.color[2] != ps->heightfog.end.color[2])
        fog_bits |= FOG_BIT_HEIGHTFOG_END_B;
    if (proxy_edict->fog.height.end.dist != ps->heightfog.end.dist)
        fog_bits |= FOG_BIT_HEIGHTFOG_END_DIST;

    if (fog_bits == 0)
        return;

    fog_bits |= FOG_BIT_TIME; // always transition over the time a single frame takes

    // Write a fog packet
    if (fog_bits & 0xFF00)
        fog_bits |= FOG_BIT_MORE_BITS;

    game_import.WriteByte(svc_fog);

    if (fog_bits & FOG_BIT_MORE_BITS)
        game_import.WriteShort(fog_bits);
    else
        game_import.WriteByte(fog_bits);

    if (fog_bits & FOG_BIT_DENSITY) {
        game_import.WriteFloat(ps->fog.density);
        game_import.WriteByte(ps->fog.sky_factor);
    }
    if (fog_bits & FOG_BIT_R)
        game_import.WriteByte(Q_clip_uint8(ps->fog.color[0] * 255));
    if (fog_bits & FOG_BIT_G)
        game_import.WriteByte(Q_clip_uint8(ps->fog.color[1] * 255));
    if (fog_bits & FOG_BIT_B)
        game_import.WriteByte(Q_clip_uint8(ps->fog.color[2] * 255));
    if (fog_bits & FOG_BIT_TIME)
        game_import.WriteShort(SV_FRAMETIME);

    if (fog_bits & FOG_BIT_HEIGHTFOG_FALLOFF)
        game_import.WriteFloat(ps->heightfog.falloff);
    if (fog_bits & FOG_BIT_HEIGHTFOG_DENSITY)
        game_import.WriteFloat(ps->heightfog.density);

    if (fog_bits & FOG_BIT_HEIGHTFOG_START_R)
        game_import.WriteByte(Q_clip_uint8(ps->heightfog.start.color[0] * 255));
    if (fog_bits & FOG_BIT_HEIGHTFOG_START_G)
        game_import.WriteByte(Q_clip_uint8(ps->heightfog.start.color[1] * 255));
    if (fog_bits & FOG_BIT_HEIGHTFOG_START_B)
        game_import.WriteByte(Q_clip_uint8(ps->heightfog.start.color[2] * 255));
    if (fog_bits & FOG_BIT_HEIGHTFOG_START_DIST)
        game_import.WriteLong(ps->heightfog.start.dist);

    if (fog_bits & FOG_BIT_HEIGHTFOG_END_R)
        game_import.WriteByte(Q_clip_uint8(ps->heightfog.end.color[0] * 255));
    if (fog_bits & FOG_BIT_HEIGHTFOG_END_G)
        game_import.WriteByte(Q_clip_uint8(ps->heightfog.end.color[1] * 255));
    if (fog_bits & FOG_BIT_HEIGHTFOG_END_B)
        game_import.WriteByte(Q_clip_uint8(ps->heightfog.end.color[2] * 255));
    if (fog_bits & FOG_BIT_HEIGHTFOG_END_DIST)
        game_import.WriteLong(ps->heightfog.end.dist);

    game_import.unicast(server_edict, true, 0);

    proxy_edict->fog.linear = ps->fog;
    proxy_edict->fog.height = ps->heightfog;
}

static void sync_single_edict_game_to_server(int index)
{
    edict_t *server_edict = &server_edicts[index].edict;
    server_entity_t *sent = &sv.entities[index];
    game3_edict_t *game_edict = GAME_EDICT_NUM(index);

    if (game_edict->client) {
        if(!server_edict->client) {
            server_edict->client = Z_Mallocz(sizeof(struct gclient_s));
        }
        if (IS_NEW_GAME_API) {
            game_client_new_to_server(server_edict->client, game_edict->client);
            // deal with fog
            sync_fog(server_edict, &((const struct game3_gclient_new_s *)game_edict->client)->ps);
        }
        else
            game_client_old_to_server(server_edict->client, game_edict->client);
    } else if (server_edict->client) {
        Z_Free(server_edict->client);
        server_edict->client = NULL;
    }

    // Skip unused entities
    bool game_edict_inuse = game_edict->inuse || HAS_EFFECTS(game_edict);
    if(!game_edict_inuse && !server_edict->inuse)
        return;

    server_edict->inuse = game_edict->inuse;
    // HAS_EFFECTS() needs correct entity state
    game_entity_state_to_server(&server_edict->s, &game_edict->s);
    // Don't change more fields if edict became unused
    if(!server_edict->inuse)
        return;

    // Check whether game cleared links
    if (!game_edict->area.next && !game_edict->area.prev && (sent->area.next || sent->area.prev))
        PF_UnlinkEdict(server_edict);
    server_edict->svflags = game_edict->svflags;

    VectorCopy(game_edict->mins, server_edict->mins);
    VectorCopy(game_edict->maxs, server_edict->maxs);
    VectorCopy(game_edict->absmin, server_edict->absmin);
    VectorCopy(game_edict->absmax, server_edict->absmax);
    VectorCopy(game_edict->size, server_edict->size);
    server_edict->solid = game_edict->solid;
    server_edict->clipmask = game_edict->clipmask;

    server_edict->owner = game_edict->owner ? &server_edicts[NUM_FOR_GAME_EDICT(game_edict->owner)].edict : NULL;
}

// Sync edicts from game to server
static void sync_edicts_game_to_server(void)
{
    for (int i = 0; i < game3_export->num_edicts; i++) {
        sync_single_edict_game_to_server(i);
    }

    game_export.num_edicts = game3_export->num_edicts;
}

static void wrap_PreInit(void) { }

static void wrap_Init(void)
{
    game3_export->Init();

    // Games with Q2PRO extensions use different limits/configstring IDs
    if (g_features->integer & GMF_PROTOCOL_EXTENSIONS) {
        Com_Printf("Game supports Q2PRO protocol extensions.\n");
        game_csr = &cs_remap_q2pro_new;
    } else
        game_csr = &cs_remap_old;

    if (game_csr->extended && IS_NEW_GAME_API) {
        PmoveEnableExt(&svs.pmp);
        svs.pmp.extended_server_ver = 2;
    } else if (game_csr->extended) {
        svs.pmp.extended_server_ver = 1;
    } else {
        svs.pmp.extended_server_ver = 0;
    }

    server_edicts = Z_Mallocz(sizeof(game3_proxy_edict_t) * game3_export->max_edicts);
    game_export.edicts = (edict_t*)server_edicts;
    game_export.edict_size = sizeof(game3_proxy_edict_t);
    game_export.max_edicts = game3_export->max_edicts;
    game_export.num_edicts = game3_export->num_edicts;

    sync_edicts_game_to_server();
}

static void wrap_Shutdown(void)
{
    game3_export->Shutdown();

    for (int i = 0; i < game3_export->max_edicts; i++) {
        Z_Free(server_edicts[i].edict.client);
    }
    Z_Free(server_edicts);
    server_edicts = NULL;
}

static void wrap_SpawnEntities(const char *mapname, const char *entstring, const char *spawnpoint)
{
    sync_edicts_server_to_game();
    game3_export->SpawnEntities(mapname, entstring, spawnpoint);
    sync_edicts_game_to_server();
}


static char* make_temp_directory(void)
{
#if defined(_WIN32)
    char temp_name[MAX_OSPATH];
    while(true)
    {
        tmpnam_s(temp_name, sizeof(temp_name));
        if (_mkdir(temp_name) == 0)
            break;
        if (errno != EEXIST)
            return NULL;
    }
    return Z_CopyString(temp_name);
#else
    char temp_dir[MAX_OSPATH];
    Q_strlcpy(temp_dir, "/tmp/q2game-XXXXXXXX", sizeof(temp_dir));
    if (!mkdtemp(temp_dir))
        Com_Error(ERR_DROP, "Couldn't get temp directory");
    return Z_CopyString(temp_dir);
#endif
}

static char* read_as_base85(const char* filename, size_t* result_size)
{
    FILE *f = fopen(filename, "rb");
    if (!f)
        Com_Error(ERR_DROP, "Couldn't open %s", filename);

    struct base85_context_t ctx;
    ascii85_context_init(&ctx);

    uint8_t buf[1024];
    while(!feof(f)) {
        size_t num_read = fread(buf, sizeof(char), sizeof(buf) / sizeof(buf[0]), f);
        if (num_read == 0 && ferror(f))
            Com_Error(ERR_DROP, "Error reading %s", filename);
        ascii85_encode(buf, num_read, &ctx);
    }
    fclose(f);

    ascii85_encode_last(&ctx);

    const char *encoded_data = (const char *)ascii85_get_output(&ctx, result_size);
    char *result = Z_CopyString(encoded_data);
    ascii85_context_destroy(&ctx);

    return result;
}

static void write_as_base85(const char* filename, const char* base85)
{
    FILE *f = fopen(filename, "wb");
    if (!f)
        Com_Error(ERR_DROP, "Couldn't open %s", filename);

    struct base85_context_t ctx;
    ascii85_context_init(&ctx);

    ascii85_decode((const uint8_t*)base85, strlen(base85), &ctx);
    ascii85_decode_last(&ctx);

    size_t data_size = 0;
    const uint8_t *data = ascii85_get_output(&ctx, &data_size);
    if (fwrite(data, 1, data_size, f) != data_size)
        Com_Error(ERR_DROP, "Error writing %s", filename);
    fclose(f);

    ascii85_context_destroy(&ctx);
}

static char* wrap_WriteGameJson(bool autosave, size_t* json_size)
{
    sync_edicts_server_to_game();

    /* Game 3 interface only supports writing to files.
     * So have the game write itself to a temp file,
     * and read _that_ file, and encode it into a string... */
    char *save_dir = make_temp_directory();
    if (!save_dir)
        Com_Error(ERR_DROP, "Couldn't create temp dir to save game");

    char game_fn[MAX_OSPATH];
    Q_snprintf(game_fn, sizeof(game_fn), "%s/game.ssv", save_dir);
    game3_export->WriteGame(game_fn, autosave);

    char* result = read_as_base85(game_fn, json_size);

    os_unlink(game_fn);
    os_rmdir(save_dir);
    Z_Free(save_dir);

    return result;
}

static void wrap_ReadGameJson(const char *json)
{
    /* Game 3 interface only supports reading files.
     * So decode the game data into a temp file
     * and read _that_ file... */
    char *save_dir = make_temp_directory();
    if (!save_dir)
        Com_Error(ERR_DROP, "Couldn't create temp dir to load game");

    char game_fn[MAX_OSPATH];
    Q_snprintf(game_fn, sizeof(game_fn), "%s/game.ssv", save_dir);
    write_as_base85(game_fn, json);

    game3_export->ReadGame(game_fn);

    os_unlink(game_fn);
    os_rmdir(save_dir);
    Z_Free(save_dir);

    sync_edicts_game_to_server();
}

static char* wrap_WriteLevelJson(bool transition, size_t* json_size)
{
    sync_edicts_server_to_game();

    /* Game 3 interface only supports writing to files.
     * So have the game write itself to a temp file,
     * and read _that_ file, and encode it into a string... */
    char *save_dir = make_temp_directory();
    if (!save_dir)
        Com_Error(ERR_DROP, "Couldn't create temp dir to save level");

    char level_fn[MAX_OSPATH];
    Q_snprintf(level_fn, sizeof(level_fn), "%s/level.sav", save_dir);
    game3_export->WriteLevel(level_fn);

    char* result = read_as_base85(level_fn, json_size);

    os_unlink(level_fn);
    os_rmdir(save_dir);
    Z_Free(save_dir);

    return result;
}

static void wrap_ReadLevelJson(const char *json)
{
    char *save_dir = make_temp_directory();
    if (!save_dir)
        Com_Error(ERR_DROP, "Couldn't create temp dir to load game");

    char level_fn[MAX_OSPATH];
    Q_snprintf(level_fn, sizeof(level_fn), "%s/level.sav", save_dir);
    write_as_base85(level_fn, json);

    game3_export->ReadLevel(level_fn);

    os_unlink(level_fn);
    os_rmdir(save_dir);
    Z_Free(save_dir);

    sync_edicts_game_to_server();
}

static bool wrap_CanSave(void)
{
    if (sv_maxclients->integer == 1 && svs.client_pool[0].edict->client->ps.stats[STAT_HEALTH] <= 0) {
        Com_Printf("Can't savegame while dead!\n");
        return false;
    }

    return true;
}

static bool edict_ignored(edict_t *ent, edict_t **ignore, size_t num_ignore)
{
    for (size_t i = 0; i < num_ignore; i++) {
        if (ent == ignore[i])
            return true;
    }
    return false;
}

static edict_t *game_ClientChooseSlot (const char *userinfo, const char *social_id, bool isBot, edict_t **ignore, size_t num_ignore, bool cinematic)
{
    client_t *cl;
    int i;

    // find a free client slot
    for (i = 0; i < sv_maxclients->integer; i++) {
        cl = &svs.client_pool[i];
        if (!edict_ignored(cl->edict, ignore, num_ignore) && cl->state == cs_free)
            return cl->edict;
    }

    return NULL;
}

static bool wrap_ClientConnect(edict_t *ent, char *userinfo, const char *social_id, bool isBot)
{
    int ent_idx = NUM_FOR_EDICT(ent);
    sync_single_edict_server_to_game(ent_idx);
    bool result = game3_export->ClientConnect(translate_edict_to_game(ent), userinfo);
    sync_single_edict_game_to_server(ent_idx);
    game_export.num_edicts = game3_export->num_edicts;
    return result;
}

static void wrap_ClientBegin(edict_t *ent)
{
    int ent_idx = NUM_FOR_EDICT(ent);
    sync_single_edict_server_to_game(ent_idx);
    game3_export->ClientBegin(translate_edict_to_game(ent));
    sync_single_edict_game_to_server(ent_idx);
    game_export.num_edicts = game3_export->num_edicts;
}

static void wrap_ClientUserinfoChanged(edict_t *ent, const char *userinfo)
{
    int ent_idx = NUM_FOR_EDICT(ent);
    sync_single_edict_server_to_game(ent_idx);
    game3_export->ClientUserinfoChanged(translate_edict_to_game(ent), (char*)userinfo);
    sync_single_edict_game_to_server(ent_idx);
    game_export.num_edicts = game3_export->num_edicts;
}

static void wrap_ClientDisconnect(edict_t *ent)
{
    int ent_idx = NUM_FOR_EDICT(ent);
    sync_single_edict_server_to_game(ent_idx);
    game3_export->ClientDisconnect(translate_edict_to_game(ent));
    sync_single_edict_game_to_server(ent_idx);
    game_export.num_edicts = game3_export->num_edicts;
}

static void wrap_ClientCommand(edict_t *ent)
{
    // ClientCommand() may spawn new entitities, so sync them all
    sync_edicts_server_to_game();
    game3_export->ClientCommand(translate_edict_to_game(ent));
    sync_edicts_game_to_server();
}

static void wrap_ClientThink(edict_t *ent, usercmd_t *cmd)
{
    // ClientThink() may spawn new entitities, so sync them all
    sync_edicts_server_to_game();
    game3_usercmd_t game_cmd;
    ConvertToGame3_usercmd(&game_cmd, cmd);
    game3_export->ClientThink(translate_edict_to_game(ent), &game_cmd);
    sync_edicts_game_to_server();
}

static void wrap_RunFrame(bool main_loop)
{
    sync_edicts_server_to_game();
    game3_export->RunFrame();
    sync_edicts_game_to_server();
}

static void wrap_PrepFrame(void)
{
    for (int i = 1; i < game3_export->num_edicts; i++) {
        game3_edict_t *gent = GAME_EDICT_NUM(i);
        edict_t *sent = EDICT_NUM(i);

        // events only last for a single keyframe
        gent->s.event = 0;
        sent->s.event = 0;
    }
}

static void wrap_ServerCommand(void)
{
    sync_edicts_server_to_game();
    game3_export->ServerCommand();
    sync_edicts_game_to_server();
}

static void *wrap_GetExtension_export(const char *name)
{
    if (game3_export_ex && strcmp(name, game_q2pro_restart_filesystem_ext) == 0) {
        return (void*)&game_q2pro_restart_filesystem;
    }
    if (game3_export_ex
        && game3_export_ex->apiversion >= GAME3_API_VERSION_EX_ENTITY_VISIBLE
        && game3_export_ex->CustomizeEntityToClient
        && game3_export_ex->EntityVisibleToClient
        && strcmp(name, game_q2pro_customize_entity_ext) == 0) {
        return (void*)&game_q2pro_customize_entity;
    }
    return NULL;
}

static void wrap_Bot_SetWeapon(edict_t *botEdict, const int weaponIndex, const bool instantSwitch) {}
static void wrap_Bot_TriggerEdict(edict_t *botEdict, edict_t *edict) {}
static void wrap_Bot_UseItem(edict_t *botEdict, const int32_t itemID) {}

static int32_t wrap_Bot_GetItemID(const char *classname)
{ /* FIXME: Can we get that info? */
    return 0;
}

static void wrap_Edict_ForceLookAtPoint(edict_t *edict, const vec3_t point)
{
    // "is only used for the in-game nav editor"
}

static bool wrap_Bot_PickedUpItem(edict_t *botEdict, edict_t *itemEdict) { return false; }

static bool wrap_Entity_IsVisibleToPlayer(edict_t *ent, edict_t *player)
{
    // "is only useful for split screen, can always return true"
    return true;
}

static const shadow_light_data_t *wrap_GetShadowLightData(int32_t entity_number)
{
    // currently not supported
    return NULL;
}

static const game3_import_ex_t game3_import_ex = {
    .apiversion = GAME3_API_VERSION_EX,
    .structsize = sizeof(game3_import_ex_t),

    .local_sound = wrap_local_sound,
    .get_configstring = wrap_get_configstring,
    .clip = wrap_clip,
    .inVIS = wrap_inVIS,

    .GetExtension = wrap_GetExtension_import,
    .TagRealloc = PF_TagRealloc,
};

game_export_t *GetGame3Proxy(game_import_t *import, void *game3_entry, void *game3_ex_entry)
{
    game_import = *import;

    game3_import_t import3;
    game3_export_t *(*entry)(game3_import_t *) = game3_entry;
    const game3_export_ex_t *(*entry_ex)(const game3_import_ex_t *) = game3_ex_entry;

    import3.multicast = wrap_multicast;
    import3.unicast = wrap_unicast;
    import3.bprintf = wrap_bprintf;
    import3.dprintf = wrap_dprintf;
    import3.cprintf = wrap_cprintf;
    import3.centerprintf = wrap_centerprintf;
    import3.error = wrap_error;

    import3.linkentity = wrap_linkentity;
    import3.unlinkentity = wrap_unlinkentity;
    import3.BoxEdicts = wrap_BoxEdicts;
    import3.trace = wrap_trace;
    import3.pointcontents = wrap_pointcontents;
    import3.setmodel = wrap_setmodel;
    import3.inPVS = wrap_inPVS;
    import3.inPHS = wrap_inPHS;
    import3.Pmove = wrap_Pmove_import;

    import3.modelindex = import->modelindex;
    import3.soundindex = import->soundindex;
    import3.imageindex = import->imageindex;

    import3.configstring = wrap_configstring;
    import3.sound = wrap_sound;
    import3.positioned_sound = wrap_positioned_sound;

    import3.WriteChar = import->WriteChar;
    import3.WriteByte = import->WriteByte;
    import3.WriteShort = import->WriteShort;
    import3.WriteLong = import->WriteLong;
    import3.WriteFloat = import->WriteFloat;
    import3.WriteString = import->WriteString;
    import3.WritePosition = import->WritePosition;
    import3.WriteDir = import->WriteDir;
    import3.WriteAngle = import->WriteAngle;

    import3.TagMalloc = wrap_TagMalloc;
    import3.TagFree = import->TagFree;
    import3.FreeTags = wrap_FreeTags;

    import3.cvar = import->cvar;
    import3.cvar_set = import->cvar_set;
    import3.cvar_forceset = import->cvar_forceset;

    import3.argc = import->argc;
    import3.argv = wrap_argv;
    import3.args = wrap_args;
    import3.AddCommandString = import->AddCommandString;

    import3.DebugGraph = import->DebugGraph;
    import3.SetAreaPortalState = wrap_SetAreaPortalState;
    import3.AreasConnected = import->AreasConnected;

    game3_export = entry(&import3);
    if (game3_ex_entry)
    {
        game3_export_ex = entry_ex(&game3_import_ex);
        if (game3_export_ex && game3_export_ex->apiversion < GAME3_API_VERSION_EX_MINIMUM)
            game3_export_ex = NULL;
        else
            Com_DPrintf("Game supports Q2PRO extended API version %d.\n", game3_export_ex->apiversion);
    }
    else
        game3_export_ex = NULL;

    game_export.apiversion = GAME_API_VERSION;
    game_export.PreInit = wrap_PreInit;
    game_export.Init = wrap_Init;
    game_export.Shutdown = wrap_Shutdown;
    game_export.SpawnEntities = wrap_SpawnEntities;
    game_export.WriteGameJson = wrap_WriteGameJson;
    game_export.ReadGameJson = wrap_ReadGameJson;
    game_export.WriteLevelJson = wrap_WriteLevelJson;
    game_export.ReadLevelJson = wrap_ReadLevelJson;
    game_export.CanSave = wrap_CanSave;

    game_export.ClientChooseSlot = game_ClientChooseSlot;
    game_export.ClientConnect = wrap_ClientConnect;
    game_export.ClientBegin = wrap_ClientBegin;
    game_export.ClientUserinfoChanged = wrap_ClientUserinfoChanged;
    game_export.ClientDisconnect = wrap_ClientDisconnect;
    game_export.ClientCommand = wrap_ClientCommand;
    game_export.ClientThink = wrap_ClientThink;
    game_export.RunFrame = wrap_RunFrame;
    game_export.PrepFrame = wrap_PrepFrame;
    game_export.ServerCommand = wrap_ServerCommand;
    game_export.Pmove = NULL; // the engine doesn't actually use the game exported Pmove, so don't bother providing it...
    game_export.GetExtension = wrap_GetExtension_export;
    game_export.Bot_SetWeapon = wrap_Bot_SetWeapon;
    game_export.Bot_TriggerEdict = wrap_Bot_TriggerEdict;
    game_export.Bot_UseItem = wrap_Bot_UseItem;
    game_export.Bot_GetItemID = wrap_Bot_GetItemID;
    game_export.Edict_ForceLookAtPoint = wrap_Edict_ForceLookAtPoint;
    game_export.Bot_PickedUpItem = wrap_Bot_PickedUpItem;
    game_export.Entity_IsVisibleToPlayer = wrap_Entity_IsVisibleToPlayer;
    game_export.GetShadowLightData = wrap_GetShadowLightData;

    return &game_export;
}
