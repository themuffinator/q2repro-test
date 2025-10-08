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
// sv_send.c

#include "server.h"

/*
=============================================================================

MISC

=============================================================================
*/

char sv_outputbuf[SV_OUTPUTBUF_LENGTH];

void SV_FlushRedirect(int redirected, const char *outputbuf, size_t len)
{
    byte    buffer[MAX_PACKETLEN_DEFAULT];

    if (redirected == RD_PACKET) {
        Q_assert(len <= sizeof(buffer) - 10);
        memcpy(buffer, "\xff\xff\xff\xffprint\n", 10);
        memcpy(buffer + 10, outputbuf, len);
        NET_SendPacket(NS_SERVER, buffer, len + 10, &net_from);
    } else if (redirected == RD_CLIENT) {
        q2proto_svc_message_t message = {.type = Q2P_SVC_PRINT, .print = {0}};
        message.print.level = PRINT_HIGH;
        message.print.string.str = outputbuf;
        message.print.string.len = len;
        q2proto_server_write(&sv_client->q2proto_ctx, (uintptr_t)&sv_client->io_data, &message);
        SV_ClientAddMessage(sv_client, MSG_RELIABLE | MSG_CLEAR);
    }
}

/*
=======================
SV_RateDrop

Returns true if the client is over its current
bandwidth estimation and should not be sent another packet
=======================
*/
static bool SV_RateDrop(client_t *client)
{
    size_t  total;
    int     i;

    // never drop over the loopback
    if (!client->rate) {
        return false;
    }

    total = 0;
    for (i = 0; i < RATE_MESSAGES; i++) {
        total += client->message_size[i];
    }

#if USE_FPS
    total = total * sv.frametime.div / client->framediv;
#endif

    if (total > client->rate) {
        SV_DPrintf(1, "Frame %d suppressed for %s (total = %zu)\n",
                   client->framenum, client->name, total);
        client->frameflags |= FF_SUPPRESSED;
        client->suppress_count++;
        client->message_size[client->framenum % RATE_MESSAGES] = 0;
        return true;
    }

    return false;
}

static void SV_CalcSendTime(client_t *client, unsigned size)
{
    // never drop over the loopback
    if (!client->rate) {
        client->send_time = svs.realtime;
        client->send_delta = 0;
        return;
    }

    if (client->state == cs_spawned)
        client->message_size[client->framenum % RATE_MESSAGES] = size;

    client->send_time = svs.realtime;
    client->send_delta = size * 1000 / client->rate;
}

/*
=============================================================================

EVENT MESSAGES

=============================================================================
*/


/*
=================
SV_ClientPrintf

Sends text across to be displayed if the level passes.
NOT archived in MVD stream.
=================
*/
void SV_ClientPrintf(client_t *client, int level, const char *fmt, ...)
{
    va_list     argptr;
    char        string[MAX_STRING_CHARS];
    size_t      len;

    if (level < client->messagelevel)
        return;

    va_start(argptr, fmt);
    len = Q_vsnprintf(string, sizeof(string), fmt, argptr);
    va_end(argptr);

    if (len >= sizeof(string)) {
        Com_WPrintf("%s: overflow\n", __func__);
        return;
    }

    q2proto_svc_message_t message = {.type = Q2P_SVC_PRINT, .print = {0}};
    message.print.level = level;
    message.print.string = q2proto_make_string(string);
    q2proto_server_write(&client->q2proto_ctx, (uintptr_t)&client->io_data, &message);

    SV_ClientAddMessage(client, MSG_RELIABLE | MSG_CLEAR);
}

/*
=================
SV_BroadcastPrintf

Sends text to all active clients.
NOT archived in MVD stream.
=================
*/
void SV_BroadcastPrintf(int level, const char *fmt, ...)
{
    va_list     argptr;
    char        string[MAX_STRING_CHARS];
    client_t    *client;
    size_t      len;

    va_start(argptr, fmt);
    len = Q_vsnprintf(string, sizeof(string), fmt, argptr);
    va_end(argptr);

    if (len >= sizeof(string)) {
        Com_WPrintf("%s: overflow\n", __func__);
        return;
    }

    q2proto_svc_message_t message = {.type = Q2P_SVC_PRINT, .print = {0}};
    message.print.level = level;
    message.print.string = q2proto_make_string(string);
    q2proto_server_multicast_write(Q2P_PROTOCOL_MULTICAST_FLOAT, Q2PROTO_IOARG_SERVER_WRITE_MULTICAST, &message);

    FOR_EACH_CLIENT(client) {
        if (client->state != cs_spawned)
            continue;
        if (level < client->messagelevel)
            continue;
        SV_ClientAddMessage(client, MSG_RELIABLE);
    }

    SZ_Clear(&msg_write);
}

void SV_ClientCommand(client_t *client, const char *fmt, ...)
{
    va_list     argptr;
    char        string[MAX_STRING_CHARS];
    size_t      len;

    va_start(argptr, fmt);
    len = Q_vsnprintf(string, sizeof(string), fmt, argptr);
    va_end(argptr);

    if (len >= sizeof(string)) {
        Com_WPrintf("%s: overflow\n", __func__);
        return;
    }

    MSG_WriteByte(svc_stufftext);
    MSG_WriteData(string, len + 1);

    SV_ClientAddMessage(client, MSG_RELIABLE | MSG_CLEAR);
}

/*
=================
SV_BroadcastCommand

Sends command to all active clients.
NOT archived in MVD stream.
=================
*/
void SV_BroadcastCommand(const char *fmt, ...)
{
    va_list     argptr;
    char        string[MAX_STRING_CHARS];
    client_t    *client;
    size_t      len;

    va_start(argptr, fmt);
    len = Q_vsnprintf(string, sizeof(string), fmt, argptr);
    va_end(argptr);

    if (len >= sizeof(string)) {
        Com_WPrintf("%s: overflow\n", __func__);
        return;
    }

    MSG_WriteByte(svc_stufftext);
    MSG_WriteData(string, len + 1);

    FOR_EACH_CLIENT(client) {
        SV_ClientAddMessage(client, MSG_RELIABLE);
    }

    SZ_Clear(&msg_write);
}


/*
=================
SV_Multicast

Sends the contents of the write buffer to a subset of the clients,
then clears the write buffer.

Archived in MVD stream.

MULTICAST_ALL    same as broadcast (origin can be NULL)
MULTICAST_PVS    send to clients potentially visible from org
MULTICAST_PHS    send to clients potentially hearable from org
=================
*/
void SV_Multicast(const vec3_t origin, multicast_t to, bool reliable)
{
    client_t        *client;
    visrow_t        mask;
    const mleaf_t   *leaf1 = NULL;
    int             flags = 0;

    if (to < MULTICAST_ALL || to > MULTICAST_PVS)
        Com_Error(ERR_DROP, "%s: bad to: %d", __func__, to);

    if (to && !origin)
        Com_Error(ERR_DROP, "%s: NULL origin", __func__);

    if (msg_write.overflowed)
        Com_Error(ERR_DROP, "%s: message buffer overflowed", __func__);

    if (!msg_write.cursize) {
        Com_DPrintf("%s with empty data\n", __func__);
        return;
    }

    if (to) {
        leaf1 = CM_PointLeaf(&sv.cm, origin);
        BSP_ClusterVis(sv.cm.cache, &mask, leaf1->cluster, MULTICAST_PVS - to);
    }
    if (reliable)
        flags |= MSG_RELIABLE;

    // send the data to all relevant clients
    FOR_EACH_CLIENT(client) {
        if (client->state < cs_primed) {
            continue;
        }
        // do not send unreliables to connecting clients
        if (!(flags & MSG_RELIABLE) && !CLIENT_ACTIVE(client)) {
            continue;
        }

        if (to) {
            const mleaf_t *leaf2 = CM_PointLeaf(&sv.cm, client->edict->s.origin);
            if (!CM_AreasConnected(&sv.cm, leaf1->area, leaf2->area))
                continue;
            if (leaf2->cluster == -1)
                continue;
            if (!Q_IsBitSet(mask.b, leaf2->cluster))
                continue;
        }

        SV_ClientAddMessage(client, flags);
    }

    // add to MVD datagram
    SV_MvdMulticast(leaf1, to, flags & MSG_RELIABLE);

    // clear the buffer
    SZ_Clear(&msg_write);
}

#if USE_ZLIB
static bool can_auto_compress(const client_t *client)
{
    if (!client->q2proto_ctx.features.enable_deflate)
        return false;

    // compress only sufficiently large layouts
    if (msg_write.cursize < client->netchan.maxpacketlen / 2)
        return false;

    return true;
}

static int compress_message(client_t *client)
{
    size_t uncompressed_size = msg_write.cursize;
    msg_write.cursize = 0;
    q2proto_error_t err = q2proto_server_write_zpacket(&client->q2proto_ctx, &client->q2proto_deflate, (uintptr_t)&client->io_data, msg_write.data, uncompressed_size);
    if (err != Q2P_ERR_SUCCESS)
    {
        if (err != Q2P_ERR_ALREADY_COMPRESSED)
            Com_WPrintf("Error %s compressing %zu bytes message for %s\n",
                        q2proto_error_string(err), uncompressed_size, client->name);
        msg_write.cursize = uncompressed_size;
        return 0;
    }
    return msg_write.cursize;
}

static byte *get_compressed_data(void)
{
    return msg_write.data;
}
#else
#define can_auto_compress(c)    false
#define compress_message(c)     0
#define get_compressed_data()   NULL
#endif

/*
=======================
SV_ClientAddMessage

Adds contents of the current write buffer to client's message list.
Does NOT clean the buffer for multicast delivery purpose,
unless told otherwise.
=======================
*/
void SV_ClientAddMessage(client_t *client, int flags)
{
    int len;

    Q_assert(!msg_write.overflowed);

    if (!msg_write.cursize) {
        return;
    }

    bool compress = (flags & MSG_COMPRESS_AUTO) && can_auto_compress(client);

    if (compress && (len = compress_message(client)) && len < msg_write.cursize) {
        client->AddMessage(client, get_compressed_data(), len, flags & MSG_RELIABLE);
        SV_DPrintf(1, "Compressed %sreliable message to %s: %u into %d\n",
                   (flags & MSG_RELIABLE) ? "" : "un", client->name, msg_write.cursize, len);
    } else {
        client->AddMessage(client, msg_write.data, msg_write.cursize, flags & MSG_RELIABLE);
        SV_DPrintf(2, "Added %sreliable message to %s: %u bytes\n",
                   (flags & MSG_RELIABLE) ? "" : "un", client->name, msg_write.cursize);
    }

    if (flags & MSG_CLEAR) {
        SZ_Clear(&msg_write);
    }
}

/*
===============================================================================

FRAME UPDATES - COMMON

===============================================================================
*/

static inline void free_msg_packet(client_t *client, message_packet_t *msg)
{
    List_Remove(&msg->entry);

    if (msg->cursize > MSG_TRESHOLD) {
        Q_assert(msg->cursize <= client->msg_dynamic_bytes);
        client->msg_dynamic_bytes -= msg->cursize;
        Z_Free(msg);
    } else {
        List_Insert(&client->msg_free_list, &msg->entry);
    }
}

#define FOR_EACH_MSG_SAFE(list) \
    LIST_FOR_EACH_SAFE(message_packet_t, msg, next, list, entry)
#define MSG_FIRST(list) \
    LIST_FIRST(message_packet_t, list, entry)

static void free_all_messages(client_t *client)
{
    message_packet_t *msg, *next;

    FOR_EACH_MSG_SAFE(&client->msg_unreliable_list) {
        free_msg_packet(client, msg);
    }
    FOR_EACH_MSG_SAFE(&client->msg_reliable_list) {
        free_msg_packet(client, msg);
    }
    client->msg_unreliable_bytes = 0;
    client->msg_dynamic_bytes = 0;
}

static void add_msg_packet(client_t *client, const byte *data,
                           size_t len, bool reliable)
{
    message_packet_t    *msg;

    if (!client->msg_pool) {
        return; // already dropped
    }

    Q_assert(len <= MAX_MSGLEN);

    if (len > MSG_TRESHOLD) {
        if (client->msg_dynamic_bytes > MAX_MSGLEN - len) {
            Com_DWPrintf("%s to %s: out of dynamic memory\n",
                         __func__, client->name);
            goto overflowed;
        }
        msg = SV_Malloc(sizeof(*msg) + len - MSG_TRESHOLD);
        client->msg_dynamic_bytes += len;
    } else {
        if (LIST_EMPTY(&client->msg_free_list)) {
            Com_DWPrintf("%s to %s: out of message slots\n",
                         __func__, client->name);
            goto overflowed;
        }
        msg = MSG_FIRST(&client->msg_free_list);
        List_Remove(&msg->entry);
    }

    memcpy(msg->data, data, len);
    msg->cursize = (uint16_t)len;

    if (reliable) {
        List_Append(&client->msg_reliable_list, &msg->entry);
    } else {
        List_Append(&client->msg_unreliable_list, &msg->entry);
        client->msg_unreliable_bytes += len;
    }

    return;

overflowed:
    if (reliable) {
        free_all_messages(client);
        SV_DropClient(client, "reliable queue overflowed");
    }
}

// check if this entity is present in current client frame
static bool check_entity(const client_t *client, int entnum)
{
    const client_frame_t *frame;
    int left, right;

    frame = &client->frames[client->framenum & UPDATE_MASK];

    left = 0;
    right = frame->num_entities - 1;
    while (left <= right) {
        int i, j;

        i = (left + right) / 2;
        j = (frame->first_entity + i) & (client->num_entities - 1);
        j = client->entities[j].number;
        if (j < entnum)
            left = i + 1;
        else if (j > entnum)
            right = i - 1;
        else
            return true;
    }

    return false;
}

// sounds relative to entities are handled specially
static void emit_snd(client_t *client, const message_packet_t *msg)
{
    int entnum = msg->sound.entity;
    int flags = msg->sound.flags;

    // check if position needs to be explicitly sent
    if (!(flags & SND_POS) && !check_entity(client, entnum)) {
        SV_DPrintf(2, "Forcing position on entity %d for %s\n",
                   entnum, client->name);
        flags |= SND_POS;   // entity is not present in frame
    }

    q2proto_svc_message_t message = {.type = Q2P_SVC_SOUND, .sound = msg->sound};
    message.sound.flags = flags;
    q2proto_server_write(&client->q2proto_ctx, (uintptr_t)&client->io_data, &message);
}

static inline void write_snd(client_t *client, message_packet_t *msg, unsigned maxsize)
{
    // if this msg fits, write it
    if (msg_write.cursize + MAX_SOUND_PACKET <= maxsize) {
        emit_snd(client, msg);
    }
    List_Remove(&msg->entry);
    List_Insert(&client->msg_free_list, &msg->entry);
}

static inline void write_msg(client_t *client, message_packet_t *msg, unsigned maxsize)
{
    // if this msg fits, write it
    if (msg_write.cursize + msg->cursize <= maxsize) {
        MSG_WriteData(msg->data, msg->cursize);
    }
    free_msg_packet(client, msg);
}

static inline void write_unreliables(client_t *client, unsigned maxsize)
{
    message_packet_t    *msg, *next;

    FOR_EACH_MSG_SAFE(&client->msg_unreliable_list) {
        if (msg->cursize == SOUND_PACKET) {
            write_snd(client, msg, maxsize);
        } else {
            write_msg(client, msg, maxsize);
        }
    }
}

/*
===============================================================================

FRAME UPDATES - OLD NETCHAN

===============================================================================
*/

static void add_message_old(client_t *client, const byte *data,
                            size_t len, bool reliable)
{
    if (len > client->netchan.maxpacketlen) {
        if (reliable) {
            SV_DropClient(client, "oversize reliable message");
        } else {
            Com_DPrintf("Dumped oversize unreliable for %s\n", client->name);
        }
        return;
    }

    add_msg_packet(client, data, len, reliable);
}

// this should be the only place data is ever written to netchan message for old clients
static void write_reliables_old(client_t *client, unsigned maxsize)
{
    message_packet_t *msg, *next;
    int count;

    if (client->netchan.reliable_length) {
        SV_DPrintf(2, "%s to %s: unacked\n", __func__, client->name);
        return;    // there is still outgoing reliable message pending
    }

    // find at least one reliable message to send
    count = 0;
    FOR_EACH_MSG_SAFE(&client->msg_reliable_list) {
        // stop if this msg doesn't fit (reliables must be delivered in order)
        if (client->netchan.message.cursize + msg->cursize > maxsize) {
            if (!count) {
                // this should never happen
                Com_WPrintf("%s to %s: overflow on the first message\n",
                            __func__, client->name);
            }
            break;
        }

        SV_DPrintf(2, "%s to %s: writing msg %d: %d bytes\n",
                   __func__, client->name, count, msg->cursize);

        SZ_Write(&client->netchan.message, msg->data, msg->cursize);
        free_msg_packet(client, msg);
        count++;
    }
}

// unreliable portion doesn't fit, then throw out low priority effects
static void repack_unreliables(client_t *client, unsigned maxsize)
{
    message_packet_t *msg, *next;

    if (msg_write.cursize + 4 > maxsize) {
        return;
    }

    // temp entities first
    FOR_EACH_MSG_SAFE(&client->msg_unreliable_list) {
        if (msg->cursize == SOUND_PACKET || msg->data[0] != svc_temp_entity) {
            continue;
        }
        // ignore some low-priority effects, these checks come from R1Q2
        if (msg->data[1] == TE_BLOOD || msg->data[1] == TE_SPLASH ||
            msg->data[1] == TE_GUNSHOT || msg->data[1] == TE_BULLET_SPARKS ||
            msg->data[1] == TE_SHOTGUN) {
            continue;
        }
        write_msg(client, msg, maxsize);
    }

    if (msg_write.cursize + 4 > maxsize) {
        return;
    }

    // then entity sounds
    FOR_EACH_MSG_SAFE(&client->msg_unreliable_list) {
        if (msg->cursize == SOUND_PACKET) {
            write_snd(client, msg, maxsize);
        }
    }

    if (msg_write.cursize + 4 > maxsize) {
        return;
    }

    // then positioned sounds
    FOR_EACH_MSG_SAFE(&client->msg_unreliable_list) {
        if (msg->cursize != SOUND_PACKET && msg->data[0] == svc_sound) {
            write_msg(client, msg, maxsize);
        }
    }

    if (msg_write.cursize + 4 > maxsize) {
        return;
    }

    // then everything else left
    FOR_EACH_MSG_SAFE(&client->msg_unreliable_list) {
        if (msg->cursize != SOUND_PACKET) {
            write_msg(client, msg, maxsize);
        }
    }
}

static void write_datagram_old(client_t *client)
{
    message_packet_t *msg;
    unsigned maxsize, cursize;
    bool ret;

    // determine how much space is left for unreliable data
    maxsize = client->netchan.maxpacketlen;
    if (client->netchan.reliable_length) {
        // there is still unacked reliable message pending
        maxsize -= client->netchan.reliable_length;
    } else {
        // find at least one reliable message to send
        // and make sure to reserve space for it
        if (!LIST_EMPTY(&client->msg_reliable_list)) {
            msg = MSG_FIRST(&client->msg_reliable_list);
            maxsize -= msg->cursize;
        }
    }
    Q_assert(maxsize <= client->netchan.maxpacketlen);

    // send over all the relevant entity_state_t
    // and the player_state_t
    ret = SV_WriteFrameToClient_Enhanced(client, maxsize);
    if (!ret) {
        SV_DPrintf(1, "Frame %d overflowed for %s\n", client->framenum, client->name);
        SZ_Clear(&msg_write);
    }

    // now write unreliable messages
    // it is necessary for this to be after the WriteFrame
    // so that entity references will be current
    if (msg_write.cursize + client->msg_unreliable_bytes > maxsize) {
        // throw out some low priority effects
        repack_unreliables(client, maxsize);
    } else {
        // all messages fit, write them in order
        write_unreliables(client, maxsize);
    }

    // write at least one reliable message
    write_reliables_old(client, client->netchan.maxpacketlen - msg_write.cursize);

#if USE_DEBUG
    if (sv_pad_packets->integer > 0) {
        int pad = min(MAX_PACKETLEN - 8, sv_pad_packets->integer);

        while (msg_write.cursize < pad) {
            q2proto_svc_message_t message = {.type = Q2P_SVC_NOP};
            q2proto_server_write(&client->q2proto_ctx, (uintptr_t)&client->io_data, &message);
        }
    }
#endif

    Q_assert(!msg_write.overflowed);

    // send the datagram
    cursize = Netchan_Transmit(&client->netchan,
                               msg_write.cursize,
                               msg_write.data,
                               client->numpackets);

    // record the size for rate estimation
    SV_CalcSendTime(client, cursize);

    // clear the write buffer
    SZ_Clear(&msg_write);
}

/*
===============================================================================

FRAME UPDATES - NEW NETCHAN

===============================================================================
*/

static void add_message_new(client_t *client, const byte *data,
                            size_t len, bool reliable)
{
    if (reliable) {
        // don't packetize, netchan level will do fragmentation as needed
        SZ_Write(&client->netchan.message, data, len);
    } else {
        // still have to packetize, relative sounds need special processing
        add_msg_packet(client, data, len, false);
    }
}

static void write_datagram_new(client_t *client)
{
    int cursize;

    // send over all the relevant entity_state_t
    // and the player_state_t
    if (!SV_WriteFrameToClient_Enhanced(client, msg_write.maxsize)) {
        // should never really happen
        Com_WPrintf("Frame overflowed for %s\n", client->name);
        SZ_Clear(&msg_write);
    }

    // now write unreliable messages
    // for this client out to the message
    // it is necessary for this to be after the WriteFrame
    // so that entity references will be current
    if (msg_write.cursize + client->msg_unreliable_bytes > msg_write.maxsize) {
        Com_WPrintf("Dumping datagram for %s\n", client->name);
    } else {
        write_unreliables(client, msg_write.maxsize);
    }

#if USE_DEBUG
    if (sv_pad_packets->integer > 0) {
        int pad = min(msg_write.maxsize, sv_pad_packets->integer);

        while (msg_write.cursize < pad) {
            q2proto_svc_message_t message = {.type = Q2P_SVC_NOP};
            q2proto_server_write(&client->q2proto_ctx, (uintptr_t)&client->io_data, &message);
        }
    }
#endif

    Q_assert(!msg_write.overflowed);

    // send the datagram
    cursize = Netchan_Transmit(&client->netchan,
                               msg_write.cursize,
                               msg_write.data,
                               client->numpackets);

    // record the size for rate estimation
    SV_CalcSendTime(client, cursize);

    // clear the write buffer
    SZ_Clear(&msg_write);
}


/*
===============================================================================

COMMON STUFF

===============================================================================
*/

static void finish_frame(client_t *client)
{
    message_packet_t *msg, *next;

    FOR_EACH_MSG_SAFE(&client->msg_unreliable_list) {
        free_msg_packet(client, msg);
    }
    client->msg_unreliable_bytes = 0;
}

#if USE_DEBUG && USE_FPS
static void check_key_sync(const client_t *client)
{
    int div = sv.frametime.div / client->framediv;
    int key1 = !(sv.framenum % sv.frametime.div);
    int key2 = !(client->framenum % div);

    if (key1 != key2) {
        Com_LPrintf(PRINT_DEVELOPER,
                    "[%d] frame %d for %s not synced (%d != %d)\n",
                    sv.framenum, client->framenum, client->name, key1, key2);
    }
}
#endif

/*
=======================
SV_SendClientMessages

Called each game frame, sends svc_frame messages to spawned clients only.
Clients in earlier connection state are handled in SV_SendAsyncPackets.
=======================
*/
void SV_SendClientMessages(void)
{
    client_t    *client;
    int         cursize;

    // send a message to each connected client
    FOR_EACH_CLIENT(client) {
        if (!CLIENT_ACTIVE(client))
            goto finish;

        if (!SV_CLIENTSYNC(client))
            continue;

#if USE_DEBUG && USE_FPS
        if (developer->integer)
            check_key_sync(client);
#endif

        // if the reliable message overflowed,
        // drop the client (should never happen)
        if (client->netchan.message.overflowed) {
            SZ_Clear(&client->netchan.message);
            SV_DropClient(client, "reliable message overflowed");
            goto finish;
        }

        // don't overrun bandwidth
        if (SV_RateDrop(client))
            goto advance;

        // don't write any frame data until all fragments are sent
        if (client->netchan.fragment_pending) {
            client->frameflags |= FF_SUPPRESSED;
            cursize = Netchan_TransmitNextFragment(&client->netchan);
            SV_CalcSendTime(client, cursize);
            goto advance;
        }

        // build the new frame and write it
        SV_BuildClientFrame(client);

        if (client->netchan.type == NETCHAN_NEW)
            write_datagram_new(client);
        else
            write_datagram_old(client);

advance:
        // advance for next frame
        client->framenum++;

finish:
        // clear all unreliable messages still left
        finish_frame(client);
    }
}

static void write_pending_download(client_t *client)
{
    sizebuf_t   *buf = &client->netchan.message;
    int         chunk;

    if (!client->download)
        return;

    if (!client->downloadpending)
        return;

    if (client->netchan.reliable_length)
        return;

    if (buf->cursize >= client->netchan.maxpacketlen)
        return;

    chunk = client->netchan.maxpacketlen - buf->cursize;

    client->downloadpending = false;

    q2proto_svc_message_t message = {.type = Q2P_SVC_DOWNLOAD};
    int download_err = q2proto_server_download_data(&client->download_state, &client->download_ptr, &client->download_remaining, chunk, &message.download);
    if (download_err == Q2P_ERR_SUCCESS || download_err == Q2P_ERR_DOWNLOAD_COMPLETE) {
        q2proto_server_write(&client->q2proto_ctx, (uintptr_t)&client->io_data, &message);
        MSG_FlushTo(buf);
    } else if (download_err != Q2P_ERR_NOT_ENOUGH_PACKET_SPACE) {
        Com_WPrintf("%s: failed downloading data to %s: %s\n", __func__, client->name, q2proto_error_string(download_err));
    }

    if (download_err == Q2P_ERR_DOWNLOAD_COMPLETE) {
        SV_CloseDownload(client);
        SV_AlignKeyFrames(client);
    }
}

/*
==================
SV_SendAsyncPackets

If the client is just connecting, it is pointless to wait another 100ms
before sending next command and/or reliable acknowledge, send it as soon
as client rate limit allows.

For spawned clients, this is not used, as we are forced to send svc_frame
packets synchronously with game DLL ticks.
==================
*/
void SV_SendAsyncPackets(void)
{
    bool        retransmit;
    client_t    *client;
    netchan_t   *netchan;
    int         cursize;

    FOR_EACH_CLIENT(client) {
        // don't overrun bandwidth
        if (svs.realtime - client->send_time < client->send_delta) {
            continue;
        }

        netchan = &client->netchan;

        // make sure all fragments are transmitted first
        if (netchan->fragment_pending) {
            cursize = Netchan_TransmitNextFragment(netchan);
            SV_DPrintf(2, "%s: frag: %d\n", client->name, cursize);
            goto calctime;
        }

        // spawned clients are handled elsewhere
        if (CLIENT_ACTIVE(client) && !SV_PAUSED) {
            continue;
        }

        // see if it's time to resend a (possibly dropped) packet
        retransmit = (com_localTime - netchan->last_sent > 1000);

        // don't write new reliables if not yet acknowledged
        if (netchan->reliable_length && !retransmit && client->state != cs_zombie) {
            continue;
        }

        // just update reliable if needed
        if (netchan->type == NETCHAN_OLD) {
            write_reliables_old(client, netchan->maxpacketlen);
        }

        // now fill up remaining buffer space with download
        write_pending_download(client);

        if (netchan->message.cursize || netchan->reliable_ack_pending ||
            netchan->reliable_length || retransmit) {
            cursize = Netchan_Transmit(netchan, 0, NULL, 1);
            SV_DPrintf(2, "%s: send: %d\n", client->name, cursize);
calctime:
            SV_CalcSendTime(client, cursize);
        }
    }
}

void SV_InitClientSend(client_t *newcl)
{
    List_Init(&newcl->msg_free_list);
    List_Init(&newcl->msg_unreliable_list);
    List_Init(&newcl->msg_reliable_list);

    newcl->msg_pool = SV_Malloc(sizeof(newcl->msg_pool[0]) * MSG_POOLSIZE);
    for (int i = 0; i < MSG_POOLSIZE; i++) {
        List_Append(&newcl->msg_free_list, &newcl->msg_pool[i].entry);
    }

    // setup protocol
    if (newcl->netchan.type == NETCHAN_NEW) {
        newcl->AddMessage = add_message_new;
    } else {
        newcl->AddMessage = add_message_old;
    }
}

void SV_ShutdownClientSend(client_t *client)
{
    free_all_messages(client);

    Z_Freep(&client->msg_pool);
    List_Init(&client->msg_free_list);
}
