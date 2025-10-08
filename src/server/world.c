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
// world.c -- world query functions

#include "server.h"

/*
===============================================================================

ENTITY AREA CHECKING

FIXME: this use of "area" is different from the bsp file use
===============================================================================
*/

typedef struct areanode_s {
    int     axis;       // -1 = leaf node
    float   dist;
    struct areanode_s   *children[2];
    list_t  trigger_edicts;
    list_t  solid_edicts;
} areanode_t;

#define    AREA_DEPTH    4
#define    AREA_NODES    32

static areanode_t   sv_areanodes[AREA_NODES];
static int          sv_numareanodes;

static const vec_t  *area_mins, *area_maxs;
static edict_t      **area_list;
static size_t       area_count, area_maxcount;
static int          area_type;
static BoxEdictsFilter_t area_filter;
static void         *area_filter_data;
static bool         area_bail;

/*
===============
SV_CreateAreaNode

Builds a uniformly subdivided tree for the given world size
===============
*/
static areanode_t *SV_CreateAreaNode(int depth, const vec3_t mins, const vec3_t maxs)
{
    areanode_t  *anode;
    vec3_t      size;
    vec3_t      mins1, maxs1, mins2, maxs2;

    anode = &sv_areanodes[sv_numareanodes];
    sv_numareanodes++;

    List_Init(&anode->trigger_edicts);
    List_Init(&anode->solid_edicts);

    if (depth == AREA_DEPTH) {
        anode->axis = -1;
        anode->children[0] = anode->children[1] = NULL;
        return anode;
    }

    VectorSubtract(maxs, mins, size);
    if (size[0] > size[1])
        anode->axis = 0;
    else
        anode->axis = 1;

    anode->dist = 0.5f * (maxs[anode->axis] + mins[anode->axis]);
    VectorCopy(mins, mins1);
    VectorCopy(mins, mins2);
    VectorCopy(maxs, maxs1);
    VectorCopy(maxs, maxs2);

    maxs1[anode->axis] = mins2[anode->axis] = anode->dist;

    anode->children[0] = SV_CreateAreaNode(depth + 1, mins2, maxs2);
    anode->children[1] = SV_CreateAreaNode(depth + 1, mins1, maxs1);

    return anode;
}

/*
===============
SV_ClearWorld

===============
*/
void SV_ClearWorld(void)
{
    memset(sv_areanodes, 0, sizeof(sv_areanodes));
    sv_numareanodes = 0;

    if (sv.cm.cache) {
        const mmodel_t *cm = &sv.cm.cache->models[0];
        SV_CreateAreaNode(0, cm->mins, cm->maxs);
    }

    // make sure all entities are unlinked
    for (int i = 0; i < ge->max_edicts; i++) {
        server_entity_t *sent = &sv.entities[i];
        sent->area.next = sent->area.prev = NULL;
    }
}

/*
===============
SV_LinkEdict

General purpose routine shared between game DLL and MVD code.
Links entity to PVS leafs.
===============
*/
void SV_LinkEdict(const cm_t *cm, edict_t *ent, server_entity_t* sv_ent)
{
    const mleaf_t   *leafs[MAX_TOTAL_ENT_LEAFS];
    int             clusters[MAX_TOTAL_ENT_LEAFS];
    int             i, j, area, num_leafs;
    const mnode_t   *topnode;

    // set the size
    VectorSubtract(ent->maxs, ent->mins, ent->size);

    // set the abs box
    if (ent->solid == SOLID_BSP && !VectorEmpty(ent->s.angles)) {
        // expand for rotation
        float   max, v;

        max = 0;
        for (i = 0; i < 3; i++) {
            v = fabsf(ent->mins[i]);
            if (v > max)
                max = v;
            v = fabsf(ent->maxs[i]);
            if (v > max)
                max = v;
        }
        for (i = 0; i < 3; i++) {
            ent->absmin[i] = ent->s.origin[i] - max;
            ent->absmax[i] = ent->s.origin[i] + max;
        }
    } else {
        // normal
        VectorAdd(ent->s.origin, ent->mins, ent->absmin);
        VectorAdd(ent->s.origin, ent->maxs, ent->absmax);
    }

    // because movement is clipped an epsilon away from an actual edge,
    // we must fully check even when bounding boxes don't quite touch
    ent->absmin[0] -= 1;
    ent->absmin[1] -= 1;
    ent->absmin[2] -= 1;
    ent->absmax[0] += 1;
    ent->absmax[1] += 1;
    ent->absmax[2] += 1;

// link to PVS leafs
    sv_ent->num_clusters = 0;
    ent->areanum = 0;
    ent->areanum2 = 0;

    // get all leafs, including solids
    num_leafs = CM_BoxLeafs(cm, ent->absmin, ent->absmax,
                            leafs, q_countof(leafs), &topnode);

    // set areas
    for (i = 0; i < num_leafs; i++) {
        clusters[i] = leafs[i]->cluster;
        area = leafs[i]->area;
        if (area) {
            // doors may legally straggle two areas,
            // but nothing should evern need more than that
            if (ent->areanum && ent->areanum != area) {
                if (ent->areanum2 && ent->areanum2 != area && sv.state == ss_loading)
                    Com_DPrintf("Object touching 3 areas at %s\n", vtos(ent->absmin));
                ent->areanum2 = area;
            } else
                ent->areanum = area;
        }
    }

    if (num_leafs == q_countof(leafs)) {
        // assume we missed some leafs, and mark by headnode
        sv_ent->num_clusters = -1;
        sv_ent->headnode = CM_NumNode(cm, topnode);
    } else {
        sv_ent->num_clusters = 0;
        for (i = 0; i < num_leafs; i++) {
            if (clusters[i] == -1)
                continue;        // not a visible leaf
            for (j = 0; j < i; j++)
                if (clusters[j] == clusters[i])
                    break;
            if (j == i) {
                if (sv_ent->num_clusters == MAX_ENT_CLUSTERS) {
                    // assume we missed some leafs, and mark by headnode
                    sv_ent->num_clusters = -1;
                    sv_ent->headnode = CM_NumNode(cm, topnode);
                    break;
                }

                sv_ent->clusternums[sv_ent->num_clusters++] = clusters[i];
            }
        }
    }
}

static void unlink_sent(server_entity_t *sent)
{
    if (!sent->area.prev)
        return;        // not linked in anywhere
    List_Remove(&sent->area);
    sent->area.prev = sent->area.next = NULL;
}

void PF_UnlinkEdict(edict_t *ent)
{
    if (!ent)
        Com_Error(ERR_DROP, "%s: NULL", __func__);
    int entnum = NUM_FOR_EDICT(ent);
    server_entity_t *sent = &sv.entities[entnum];
    unlink_sent(sent);

    ent->linked = sent->area.prev != NULL;
}

static uint32_t SV_PackSolid32(const edict_t *ent)
{
    uint32_t solid32;

    solid32 = q2proto_pack_solid_32_q2pro_v2(ent->mins, ent->maxs); // FIXME: game-dependent

    if (solid32 == PACKED_BSP)
        solid32 = 0;  // can happen in pathological case if z mins > maxs

#if USE_DEBUG
    if (developer->integer) {
        vec3_t mins, maxs;

        q2proto_unpack_solid_32_q2pro_v2(solid32, mins, maxs); // FIXME: game-dependent

        if (!VectorCompare(ent->mins, mins) || !VectorCompare(ent->maxs, maxs))
            Com_LPrintf(PRINT_DEVELOPER, "Bad mins/maxs on entity %d: %s %s\n",
                        NUM_FOR_EDICT(ent), vtos(ent->mins), vtos(ent->maxs));
    }
#endif

    return solid32;
}

void PF_LinkEdict(edict_t *ent)
{
    areanode_t *node;
    server_entity_t *sent;
    int entnum;
#if USE_FPS
    int i;
#endif

    if (!ent)
        Com_Error(ERR_DROP, "%s: NULL", __func__);

    entnum = NUM_FOR_EDICT(ent);
    sent = &sv.entities[entnum];

    if (ent->linked)
        unlink_sent(sent);     // unlink from old position

    if (ent == ge->edicts)
        return;        // don't add the world

    if (!ent->inuse) {
        Com_DPrintf("%s: entity %d is not in use\n", __func__, NUM_FOR_EDICT(ent));
        return;
    }

    if (!sv.cm.cache)
        return;

    // encode the size into the entity_state for client prediction
    switch (ent->solid) {
    case SOLID_BBOX:
        if ((ent->svflags & SVF_DEADMONSTER) || VectorCompare(ent->mins, ent->maxs)) {
            ent->s.solid = 0;
            sent->solid32 = 0;
        } else if (svs.csr.extended) {
            sent->solid32 = ent->s.solid = SV_PackSolid32(ent);
        } else {
            ent->s.solid = q2proto_pack_solid_16(ent->mins, ent->maxs);
            sent->solid32 = SV_PackSolid32(ent);
        }
        break;
    case SOLID_BSP:
        ent->s.solid = PACKED_BSP;      // a SOLID_BBOX will never create this value
        sent->solid32 = PACKED_BSP;     // FIXME: use 255?
        break;
    default:
        ent->s.solid = 0;
        sent->solid32 = 0;
        break;
    }

    SV_LinkEdict(&sv.cm, ent, sent);

    // if first time, make sure old_origin is valid
    if (!ent->linkcount) {
        if (!(ent->s.renderfx & RF_BEAM))
            VectorCopy(ent->s.origin, ent->s.old_origin);
#if USE_FPS
        VectorCopy(ent->s.origin, sent->create_origin);
        sent->create_framenum = sv.framenum;
#endif
    }
    ent->linkcount++;

#if USE_FPS
    // save origin for later recovery
    i = sv.framenum & ENT_HISTORY_MASK;
    VectorCopy(ent->s.origin, sent->history[i].origin);
    sent->history[i].framenum = sv.framenum;
#endif

    if (ent->solid == SOLID_NOT)
        return;

// find the first node that the ent's box crosses
    node = sv_areanodes;
    while (1) {
        if (node->axis == -1)
            break;
        if (ent->absmin[node->axis] > node->dist)
            node = node->children[0];
        else if (ent->absmax[node->axis] < node->dist)
            node = node->children[1];
        else
            break;        // crosses the node
    }

    // link it in
    if (ent->solid == SOLID_TRIGGER)
        List_Append(&node->trigger_edicts, &sent->area);
    else
        List_Append(&node->solid_edicts, &sent->area);

    ent->linked = sent->area.prev != NULL;
}


/*
====================
SV_AreaEdicts_r

====================
*/
static void SV_AreaEdicts_r(areanode_t *node)
{
    list_t      *start;
    server_entity_t *sent;

    if (area_bail)
        return;

    // touch linked edicts
    if (area_type == AREA_SOLID)
        start = &node->solid_edicts;
    else
        start = &node->trigger_edicts;

    LIST_FOR_EACH(server_entity_t, sent, start, area) {
        edict_t *check = EDICT_NUM(sent - sv.entities);
        if (check->solid == SOLID_NOT)
            continue;        // deactivated
        if (check->absmin[0] > area_maxs[0]
            || check->absmin[1] > area_maxs[1]
            || check->absmin[2] > area_maxs[2]
            || check->absmax[0] < area_mins[0]
            || check->absmax[1] < area_mins[1]
            || check->absmax[2] < area_mins[2])
            continue;        // not touching

        if (area_maxcount > 0 && area_count == area_maxcount) {
            Com_WPrintf("SV_AreaEdicts: MAXCOUNT\n");
            return;
        }

        BoxEdictsResult_t filter_result = area_filter ? area_filter(check, area_filter_data) : BoxEdictsResult_Keep;

        if ((filter_result & ~BoxEdictsResult_End) == BoxEdictsResult_Keep) {
            if (area_list)
                area_list[area_count] = check;
            area_count++;
        }
        if ((filter_result & BoxEdictsResult_End) != 0) {
            area_bail = true;
            return;
        }
    }

    if (node->axis == -1)
        return;        // terminal node

    // recurse down both sides
    if (area_maxs[node->axis] > node->dist)
        SV_AreaEdicts_r(node->children[0]);
    if (area_mins[node->axis] < node->dist)
        SV_AreaEdicts_r(node->children[1]);
}

/*
================
SV_AreaEdicts
================
*/
size_t SV_AreaEdicts(const vec3_t mins, const vec3_t maxs,
                     edict_t **list, size_t maxcount, int areatype,
                     BoxEdictsFilter_t filter, void *filter_data)
{
    area_mins = mins;
    area_maxs = maxs;
    area_list = list;
    area_count = 0;
    area_maxcount = maxcount;
    area_type = areatype;
    area_filter = filter;
    area_filter_data = filter_data;
    area_bail = false;

    SV_AreaEdicts_r(sv_areanodes);

    return area_count;
}


//===========================================================================

/*
================
SV_HullForEntity

Returns a headnode that can be used for testing or clipping an
object of mins/maxs size.
================
*/
static const mnode_t *SV_HullForEntity(const edict_t *ent, bool triggers)
{
    if (ent->solid == SOLID_BSP || (triggers && ent->solid == SOLID_TRIGGER)) {
        const bsp_t *bsp = sv.cm.cache;
        int i = ent->s.modelindex - 1;

        if (bsp) {
            // account for "hole" in configstring namespace
            if (i >= MODELINDEX_PLAYER && bsp->nummodels >= MODELINDEX_PLAYER)
                i--;

            // explicit hulls in the BSP model
            if (i > 0 && i < bsp->nummodels)
                return bsp->models[i].headnode;
        }

        if (ent->solid == SOLID_BSP)
            Com_Error(ERR_DROP, "%s: inline model %d out of range", __func__, i);
    }

    // create a temp hull from bounding box sizes
    return CM_HeadnodeForBox(ent->mins, ent->maxs);
}

/*
=============
SV_WorldNodes
=============
*/
static const mnode_t *SV_WorldNodes(void)
{
    return sv.cm.cache ? sv.cm.cache->nodes : NULL;
}

/*
=============
SV_PointContents
=============
*/
contents_t SV_PointContents(const vec3_t p)
{
    edict_t     *touch[MAX_EDICTS_OLD], *hit;
    int         i, num;
    int         contents;

    // get base contents from world
    contents = CM_PointContents(p, SV_WorldNodes(), svs.csr.extended);

    // or in contents from all the other entities
    num = SV_AreaEdicts(p, p, touch, q_countof(touch), AREA_SOLID, NULL, NULL);

    for (i = 0; i < num; i++) {
        hit = touch[i];

        // might intersect, so do an exact clip
        contents |= CM_TransformedPointContents(p, SV_HullForEntity(hit, false),
                                                hit->s.origin, hit->s.angles,
                                                svs.csr.extended);
    }

    return contents;
}

/*
====================
SV_ClipMoveToEntities
====================
*/
static void SV_ClipMoveToEntities(trace_t *tr,
                                  const vec3_t start, const vec3_t end,
                                  const vec3_t mins, const vec3_t maxs,
                                  edict_t *passedict, int contentmask)
{
    vec3_t      boxmins, boxmaxs;
    int         i, num;
    edict_t     *touchlist[MAX_EDICTS], *touch;
    trace_t     trace;

    // create the bounding box of the entire move
    for (i = 0; i < 3; i++) {
        if (end[i] > start[i]) {
            boxmins[i] = start[i] + mins[i] - 1;
            boxmaxs[i] = end[i] + maxs[i] + 1;
        } else {
            boxmins[i] = end[i] + mins[i] - 1;
            boxmaxs[i] = start[i] + maxs[i] + 1;
        }
    }

    num = SV_AreaEdicts(boxmins, boxmaxs, touchlist, q_countof(touchlist), AREA_SOLID, NULL, NULL);

    // be careful, it is possible to have an entity in this
    // list removed before we get to it (killtriggered)
    for (i = 0; i < num; i++) {
        touch = touchlist[i];
        if (touch->solid == SOLID_NOT)
            continue;
        if (tr->allsolid)
            return;
        if (passedict) {
            if (touch == passedict)
                continue;
            if (touch->owner == passedict)
                continue;    // don't clip against own missiles
            if (passedict->owner == touch)
                continue;    // don't clip against owner
        }

        if (!(contentmask & CONTENTS_DEADMONSTER)
            && (touch->svflags & SVF_DEADMONSTER))
            continue;

        if (svs.csr.extended) {
            if (!(contentmask & CONTENTS_PROJECTILE)
                && (touch->svflags & SVF_PROJECTILE))
                continue;
            if (!(contentmask & CONTENTS_PLAYER)
                && (touch->svflags & SVF_PLAYER))
                continue;
        }

        // might intersect, so do an exact clip
        CM_TransformedBoxTrace(&trace, start, end, mins, maxs,
                               SV_HullForEntity(touch, false), contentmask,
                               touch->s.origin, touch->s.angles,
                               svs.csr.extended);

        CM_ClipEntity(tr, &trace, touch);
    }
}

/*
==================
SV_Trace

Moves the given mins/maxs volume through the world from start to end.
Passedict and edicts owned by passedict are explicitly not checked.
==================
*/
trace_t q_gameabi SV_Trace(const vec3_t start, const vec3_t mins,
                           const vec3_t maxs, const vec3_t end,
                           edict_t *passedict, contents_t contentmask)
{
    trace_t     trace;

    if (!mins)
        mins = vec3_origin;
    if (!maxs)
        maxs = vec3_origin;

    // clip to world
    CM_BoxTrace(&trace, start, end, mins, maxs, SV_WorldNodes(), contentmask, svs.csr.extended);
    trace.ent = ge->edicts;
    if (trace.fraction == 0)
        return trace;   // blocked by the world

    // clip to other solid entities
    SV_ClipMoveToEntities(&trace, start, end, mins, maxs, passedict, contentmask);
    return trace;
}

/*
==================
SV_Clip

Like SV_Trace(), but clip to specified entity only.
Can be used to clip to SOLID_TRIGGER by its BSP tree.
==================
*/
trace_t q_gameabi SV_Clip(const vec3_t start, const vec3_t mins,
                          const vec3_t maxs, const vec3_t end,
                          edict_t *clip, contents_t contentmask)
{
    trace_t     trace;

    if (!mins)
        mins = vec3_origin;
    if (!maxs)
        maxs = vec3_origin;

    if (clip == ge->edicts)
        CM_BoxTrace(&trace, start, end, mins, maxs, SV_WorldNodes(), contentmask, svs.csr.extended);
    else
        CM_TransformedBoxTrace(&trace, start, end, mins, maxs,
                               SV_HullForEntity(clip, true), contentmask,
                               clip->s.origin, clip->s.angles,
                               svs.csr.extended);
    trace.ent = clip;
    return trace;
}
