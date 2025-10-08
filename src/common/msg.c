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

#include "shared/shared.h"
#include "shared/game3_shared.h"
#include "common/msg.h"
#include "common/protocol.h"
#include "common/sizebuf.h"
#include "common/math.h"
#include "common/intreadwrite.h"

#include "q2proto/q2proto_sound.h"

/*
==============================================================================

            MESSAGE IO FUNCTIONS

Handles byte ordering and avoids alignment errors
==============================================================================
*/

sizebuf_t   msg_write;
byte        msg_write_buffer[MAX_MSGLEN];

sizebuf_t   msg_read;
byte        msg_read_buffer[MAX_MSGLEN];

const entity_packed_t   nullEntityState;
const player_packed_t   nullPlayerState;
const usercmd_t         nullUserCmd;

/*
=============
MSG_Init

Initialize default buffers (also called from Com_Error).
This is the only place where writing buffer is initialized.
=============
*/
void MSG_Init(void)
{
    SZ_Init(&msg_read, msg_read_buffer, MAX_MSGLEN, "msg_read");
    SZ_Init(&msg_write, msg_write_buffer, MAX_MSGLEN, "msg_write");
    msg_read.allowunderflow = true;
    msg_write.allowoverflow = true;
}


/*
==============================================================================

            WRITING

==============================================================================
*/

/*
=============
MSG_BeginWriting
=============
*/
void MSG_BeginWriting(void)
{
    msg_write.cursize = 0;
    msg_write.bits_buf = 0;
    msg_write.bits_left = 32;
    msg_write.overflowed = false;
}

/*
=============
MSG_WriteChar
=============
*/
void MSG_WriteChar(int c)
{
    byte    *buf;

#ifdef PARANOID
    Q_assert(c >= -128 && c <= 127);
#endif

    buf = SZ_GetSpace(&msg_write, 1);
    buf[0] = c;
}

/*
=============
MSG_WriteByte
=============
*/
void MSG_WriteByte(int c)
{
    byte    *buf;

#ifdef PARANOID
    Q_assert(c >= 0 && c <= 255);
#endif

    buf = SZ_GetSpace(&msg_write, 1);
    buf[0] = c;
}

/*
=============
MSG_WriteShort
=============
*/
void MSG_WriteShort(int c)
{
    byte    *buf;

#ifdef PARANOID
    Q_assert(c >= -0x8000 && c <= 0x7fff);
#endif

    buf = SZ_GetSpace(&msg_write, 2);
    WL16(buf, c);
}

/*
=============
MSG_WriteLong
=============
*/
void MSG_WriteLong(int c)
{
    byte    *buf;

    buf = SZ_GetSpace(&msg_write, 4);
    WL32(buf, c);
}

/*
=============
MSG_WriteLong64
=============
*/
void MSG_WriteLong64(int64_t c)
{
    byte    *buf;

    buf = SZ_GetSpace(&msg_write, 8);
    WL64(buf, c);
}

/*
=============
MSG_WriteString
=============
*/
void MSG_WriteString(const char *string)
{
    SZ_WriteString(&msg_write, string);
}

/*
=============
MSG_WriteDeltaInt23
=============
*/

static inline void MSG_WriteCoord(float to)
{
    MSG_WriteFloat(to);
}

/*
=============
MSG_WritePos
=============
*/
void MSG_WritePos(const vec3_t pos)
{
    MSG_WriteFloat(pos[0]);
    MSG_WriteFloat(pos[1]);
    MSG_WriteFloat(pos[2]);
}

/*
=============
MSG_WriteAngle
=============
*/

#define ANGLE2BYTE(x)   ((int)((x)*256.0f/360)&255)
#define BYTE2ANGLE(x)   ((x)*(360.0f/256))

void MSG_WriteAngle(float f)
{
    MSG_WriteByte(ANGLE2BYTE(f));
}

void MSG_WriteFloat(float f)
{
    union {
        float _f;
        int32_t _i;
    } v = { f };
    MSG_WriteLong(v._i);
}

static inline void MSG_WriteAngle16(float f)
{
    MSG_WriteShort(ANGLE2SHORT(f));
}

#if USE_CLIENT

static void compute_buttons_upmove(int *buttons, short *upmove)
{
    int prev_buttons = *buttons;
    *buttons = prev_buttons & ~(BUTTON_CROUCH | BUTTON_JUMP);
    *upmove = 0;
    if (prev_buttons & BUTTON_JUMP)
        *upmove += 200; /* cl_upspeed */
    if (prev_buttons & BUTTON_CROUCH)
        *upmove -= 200; /* cl_upspeed */
}

/*
=============
MSG_WriteDeltaUsercmd
=============
*/
int MSG_WriteDeltaUsercmd(const usercmd_t *from, const usercmd_t *cmd, int serverProtocol, int version)
{
    int     bits, buttons;

    Q_assert(cmd);

    if (!from) {
        from = &nullUserCmd;
    }

    bool is_rerelease = serverProtocol == PROTOCOL_VERSION_RERELEASE;

    int from_buttons = from->buttons;
    short from_upmove = 0;
    int new_buttons = cmd->buttons;
    short new_upmove = 0;
    if (!is_rerelease) {
        compute_buttons_upmove(&from_buttons, &from_upmove);
        compute_buttons_upmove(&new_buttons, &new_upmove);
    }
    // else: for is_rerelease, always transmit full 'buttons' byte below

//
// send the movement message
//
    bits = 0;
    if (cmd->angles[0] != from->angles[0])
        bits |= CM_ANGLE1;
    if (cmd->angles[1] != from->angles[1])
        bits |= CM_ANGLE2;
    if (cmd->angles[2] != from->angles[2])
        bits |= CM_ANGLE3;
    if (cmd->forwardmove != from->forwardmove)
        bits |= CM_FORWARD;
    if (cmd->sidemove != from->sidemove)
        bits |= CM_SIDE;
    if (new_buttons != from_buttons)
        bits |= CM_BUTTONS;
    // The next one can only happen when is_rerelease == false
    if (new_upmove != from_upmove)
        bits |= CM_UP;

    MSG_WriteByte(bits);

    buttons = new_buttons & BUTTON_MASK;

    // See if movement values can be "compressed" by dividing by 5.
    // If so, set BUTTON_xxx flag
    if (version >= PROTOCOL_VERSION_R1Q2_UCMD && (bits & CM_BUTTONS)) {
        if ((bits & CM_FORWARD) && !((int)cmd->forwardmove % 5)) {
            buttons |= BUTTON_FORWARD;
        }
        if ((bits & CM_SIDE) && !((int)cmd->sidemove % 5)) {
            buttons |= BUTTON_SIDE;
        }
        if ((bits & CM_UP) && !(new_upmove % 5)) {
            buttons |= BUTTON_UP;
        }
        MSG_WriteByte(buttons);
    }

    if (bits & CM_ANGLE1)
        MSG_WriteAngle16(cmd->angles[0]);
    if (bits & CM_ANGLE2)
        MSG_WriteAngle16(cmd->angles[1]);
    if (bits & CM_ANGLE3)
        MSG_WriteAngle16(cmd->angles[2]);

    if (bits & CM_FORWARD) {
        if (buttons & BUTTON_FORWARD) {
            MSG_WriteChar(cmd->forwardmove * 0.2f);
        } else {
            MSG_WriteShort(cmd->forwardmove);
        }
    }
    if (bits & CM_SIDE) {
        if (buttons & BUTTON_SIDE) {
            MSG_WriteChar(cmd->sidemove * 0.2f);
        } else {
            MSG_WriteShort(cmd->sidemove);
        }
    }
    if (bits & CM_UP) {
        if (buttons & BUTTON_UP) {
            MSG_WriteChar(new_upmove / 5);
        } else {
            MSG_WriteShort(new_upmove);
        }
    }

    /* For rerelease, we want to write the full 'buttons' byte,
     * due to the new button bits that have been added. */
    if ((version < PROTOCOL_VERSION_R1Q2_UCMD || is_rerelease) && (bits & CM_BUTTONS))
        MSG_WriteByte(cmd->buttons);

    MSG_WriteByte(cmd->msec);

    return bits;
}

/*
=============
MSG_WriteBits
=============
*/
void MSG_WriteBits(int value, int bits)
{
    Q_assert(!(bits == 0 || bits < -31 || bits > 31));

    if (bits < 0) {
        bits = -bits;
    }

    uint32_t bits_buf  = msg_write.bits_buf;
    uint32_t bits_left = msg_write.bits_left;
    uint32_t v = value & ((1U << bits) - 1);

    bits_buf |= v << (32 - bits_left);
    if (bits >= bits_left) {
        MSG_WriteLong(bits_buf);
        bits_buf   = v >> bits_left;
        bits_left += 32;
    }
    bits_left -= bits;

    msg_write.bits_buf  = bits_buf;
    msg_write.bits_left = bits_left;
}

/*
=============
MSG_FlushBits
=============
*/
void MSG_FlushBits(void)
{
    uint32_t bits_buf  = msg_write.bits_buf;
    uint32_t bits_left = msg_write.bits_left;

    while (bits_left < 32) {
        MSG_WriteByte(bits_buf & 255);
        bits_buf >>= 8;
        bits_left += 8;
    }

    msg_write.bits_buf  = 0;
    msg_write.bits_left = 32;
}

/*
=============
MSG_WriteDeltaUsercmd_Enhanced
=============
*/
int MSG_WriteDeltaUsercmd_Enhanced(const usercmd_t *from,
                                   const usercmd_t *cmd,
                                   int serverProtocol)
{
    int     bits, delta;

    Q_assert(cmd);

    if (!from) {
        from = &nullUserCmd;
    }

    bool is_rerelease = serverProtocol == PROTOCOL_VERSION_RERELEASE;

    int from_buttons = from->buttons;
    short from_upmove = 0;
    int new_buttons = cmd->buttons;
    short new_upmove = 0;
    if (!is_rerelease) {
        compute_buttons_upmove(&from_buttons, &from_upmove);
        compute_buttons_upmove(&new_buttons, &new_upmove);
    }

//
// send the movement message
//
    bits = 0;
    if (cmd->angles[0] != from->angles[0])
        bits |= CM_ANGLE1;
    if (cmd->angles[1] != from->angles[1])
        bits |= CM_ANGLE2;
    if (cmd->angles[2] != from->angles[2])
        bits |= CM_ANGLE3;
    if (cmd->forwardmove != from->forwardmove)
        bits |= CM_FORWARD;
    if (cmd->sidemove != from->sidemove)
        bits |= CM_SIDE;
    if (new_buttons != from_buttons)
        bits |= CM_BUTTONS;
    if (cmd->msec != from->msec)
        bits |= CM_IMPULSE;
    // The next one can only happen when is_rerelease == false
    if (new_upmove != from_upmove)
        bits |= CM_UP;

    if (!bits) {
        MSG_WriteBits(0, 1);
        return 0;
    }

    MSG_WriteBits(1, 1);
    MSG_WriteBits(bits, 8);

    if (bits & CM_ANGLE1) {
        delta = ANGLE2SHORT(cmd->angles[0]) - ANGLE2SHORT(from->angles[0]);
        if (delta >= -128 && delta <= 127) {
            MSG_WriteBits(1, 1);
            MSG_WriteBits(delta, -8);
        } else {
            MSG_WriteBits(0, 1);
            MSG_WriteBits(ANGLE2SHORT(cmd->angles[0]), -16);
        }
    }
    if (bits & CM_ANGLE2) {
        delta = ANGLE2SHORT(cmd->angles[1]) - ANGLE2SHORT(from->angles[1]);
        if (delta >= -128 && delta <= 127) {
            MSG_WriteBits(1, 1);
            MSG_WriteBits(delta, -8);
        } else {
            MSG_WriteBits(0, 1);
            MSG_WriteBits(ANGLE2SHORT(cmd->angles[1]), -16);
        }
    }
    if (bits & CM_ANGLE3) {
        MSG_WriteBits(ANGLE2SHORT(cmd->angles[2]), -16);
    }

    if (bits & CM_FORWARD) {
        MSG_WriteBits(cmd->forwardmove, -10);
    }
    if (bits & CM_SIDE) {
        MSG_WriteBits(cmd->sidemove, -10);
    }
    if (bits & CM_UP) {
        MSG_WriteBits(new_upmove, -10);
    }

    if (bits & CM_BUTTONS) {
        if (is_rerelease) {
            // Write full button byte to account for new buttons
            MSG_WriteBits(new_buttons, 8);
        } else {
            int buttons = (new_buttons & 3) | (new_buttons >> 5);
            MSG_WriteBits(buttons, 3);
        }
    }
    if (bits & CM_IMPULSE) {
        MSG_WriteBits(cmd->msec, 8);
    }

    return bits;
}

#endif // USE_CLIENT

void MSG_WriteDir(const vec3_t dir)
{
    int     best;

    best = DirToByte(dir);
    MSG_WriteByte(best);
}

#define PACK_COORDS(out, in)        \
    (out[0] = COORD2SHORT(in[0]),   \
     out[1] = COORD2SHORT(in[1]),   \
     out[2] = COORD2SHORT(in[2]))

#define PACK_ANGLES(out, in)        \
    (out[0] = ANGLE2SHORT(in[0]),   \
     out[1] = ANGLE2SHORT(in[1]),   \
     out[2] = ANGLE2SHORT(in[2]))

// as per re-release spec, values of 0 for alpha
// and scale should infer a default; as a result,
// any value that is non-zero should be considered
// explicitly specified.
// for alpha, a value of 1.0 (255) *must*
// still be synced, because it may override another
// effect that has an implicit alpha assigned to it.

static uint8_t Q_clip_alpha(float in)
{
    return in ? max(1, Q_clip_uint8(in * 255.0f)) : in;
}

static uint8_t Q_clip_scale(float in)
{
    return in ? max(1, Q_clip_uint8(in * 16.0f)) : in;
}

void MSG_PackEntity(entity_packed_t *out, const entity_state_t *in, bool ext)
{
    // allow 0 to accommodate empty baselines
    Q_assert(in->number >= 0 && in->number < MAX_EDICTS);
    out->number = in->number;

    VectorCopy(in->origin, out->origin);
    VectorCopy(in->old_origin, out->old_origin);
    PACK_ANGLES(out->angles, in->angles);
    out->modelindex = in->modelindex;
    out->modelindex2 = in->modelindex2;
    out->modelindex3 = in->modelindex3;
    out->modelindex4 = in->modelindex4;
    out->skinnum = in->skinnum;
    out->effects = in->effects;
    out->renderfx = in->renderfx;
    out->solid = in->solid;
    out->frame = in->frame;
    out->sound = in->sound;
    out->event = in->event;

    if (ext) {
        out->alpha = Q_clip_alpha(in->alpha);
        out->scale = Q_clip_scale(in->scale);
        out->loop_volume = Q_clip_uint8(in->loop_volume * 255.0f);
        out->loop_attenuation = q2proto_sound_encode_loop_attenuation(in->loop_attenuation);
        // save network bandwidth
        if (out->scale == 16) out->scale = 0;
        if (out->loop_volume == 255) out->loop_volume = 0;
    }
}

void MSG_WriteDeltaEntity(const entity_packed_t *from,
                          const entity_packed_t *to,
                          msgEsFlags_t          flags)
{
    uint64_t    bits;
    uint32_t    mask;

    if (!to) {
        Q_assert(from);
        Q_assert(from->number > 0 && from->number < MAX_EDICTS);

        bits = U_REMOVE;
        if (from->number & 0xff00)
            bits |= U_NUMBER16 | U_MOREBITS1;

        MSG_WriteByte(bits & 255);
        if (bits & 0x0000ff00)
            MSG_WriteByte((bits >> 8) & 255);

        if (bits & U_NUMBER16)
            MSG_WriteShort(from->number);
        else
            MSG_WriteByte(from->number);

        return; // remove entity
    }

    Q_assert(to->number > 0 && to->number < MAX_EDICTS);

    if (!from)
        from = &nullEntityState;

// send an update
    bits = 0;

    if (!(flags & MSG_ES_FIRSTPERSON)) {
        if (to->origin[0] != from->origin[0])
            bits |= U_ORIGIN1;
        if (to->origin[1] != from->origin[1])
            bits |= U_ORIGIN2;
        if (to->origin[2] != from->origin[2])
            bits |= U_ORIGIN3;

        if (flags & MSG_ES_SHORTANGLES && to->solid == PACKED_BSP) {
            if (to->angles[0] != from->angles[0])
                bits |= U_ANGLE1 | U_ANGLE16;
            if (to->angles[1] != from->angles[1])
                bits |= U_ANGLE2 | U_ANGLE16;
            if (to->angles[2] != from->angles[2])
                bits |= U_ANGLE3 | U_ANGLE16;
        } else {
            if ((to->angles[0] ^ from->angles[0]) & 0xff00)
                bits |= U_ANGLE1;
            if ((to->angles[1] ^ from->angles[1]) & 0xff00)
                bits |= U_ANGLE2;
            if ((to->angles[2] ^ from->angles[2]) & 0xff00)
                bits |= U_ANGLE3;
        }

        if ((flags & MSG_ES_NEWENTITY) && !VectorCompare(to->old_origin, from->origin))
            bits |= U_OLDORIGIN;
    }

    if (flags & MSG_ES_UMASK)
        mask = 0xffff0000;
    else
        mask = 0xffff8000;  // don't confuse old clients

    if (to->skinnum != from->skinnum) {
        if (to->skinnum & mask)
            bits |= U_SKIN32;
        else if (to->skinnum & 0x0000ff00)
            bits |= U_SKIN16;
        else
            bits |= U_SKIN8;
    }

    if (to->frame != from->frame) {
        if (to->frame & 0xff00)
            bits |= U_FRAME16;
        else
            bits |= U_FRAME8;
    }

    if (to->effects != from->effects) {
        if (to->effects & mask)
            bits |= U_EFFECTS32;
        else if (to->effects & 0x0000ff00)
            bits |= U_EFFECTS16;
        else
            bits |= U_EFFECTS8;
    }

    if (to->renderfx != from->renderfx) {
        if (to->renderfx & mask)
            bits |= U_RENDERFX32;
        else if (to->renderfx & 0x0000ff00)
            bits |= U_RENDERFX16;
        else
            bits |= U_RENDERFX8;
    }

    if (to->solid != from->solid)
        bits |= U_SOLID;

    // event is not delta compressed, just 0 compressed
    if (to->event)
        bits |= U_EVENT;

    if (to->modelindex != from->modelindex)
        bits |= U_MODEL;
    if (to->modelindex2 != from->modelindex2)
        bits |= U_MODEL2;
    if (to->modelindex3 != from->modelindex3)
        bits |= U_MODEL3;
    if (to->modelindex4 != from->modelindex4)
        bits |= U_MODEL4;

    if (flags & MSG_ES_EXTENSIONS) {
        if (bits & (U_MODEL | U_MODEL2 | U_MODEL3 | U_MODEL4) &&
            (to->modelindex | to->modelindex2 | to->modelindex3 | to->modelindex4) & 0xff00)
            bits |= U_MODEL16;
        if (to->loop_volume != from->loop_volume || to->loop_attenuation != from->loop_attenuation)
            bits |= U_SOUND;
        uint32_t from_morefx = from->effects >> 32;
        uint32_t to_morefx = to->effects >> 32;
        if (to_morefx != from_morefx) {
            if (to_morefx & mask)
                bits |= U_MOREFX32;
            else if (to_morefx & 0x0000ff00)
                bits |= U_MOREFX16;
            else
                bits |= U_MOREFX8;
        }
        if (to->alpha != from->alpha)
            bits |= U_ALPHA;
        if (to->scale != from->scale)
            bits |= U_SCALE;
    }

    if (to->sound != from->sound)
        bits |= U_SOUND;

    if (to->renderfx & RF_FRAMELERP) {
        if (!VectorCompare(to->old_origin, from->origin))
            bits |= U_OLDORIGIN;
    } else if (to->renderfx & RF_BEAM) {
        if (!(flags & MSG_ES_BEAMORIGIN) || !VectorCompare(to->old_origin, from->old_origin))
            bits |= U_OLDORIGIN;
    }

    //
    // write the message
    //
    if (!bits && !(flags & MSG_ES_FORCE))
        return;     // nothing to send!

    if (flags & MSG_ES_REMOVE)
        bits |= U_REMOVE; // used for MVD stream only

    //----------

    if (to->number & 0xff00)
        bits |= U_NUMBER16;     // number8 is implicit otherwise

    if (bits & 0xff00000000ULL)
        bits |= U_MOREBITS4 | U_MOREBITS3 | U_MOREBITS2 | U_MOREBITS1;
    else if (bits & 0xff000000)
        bits |= U_MOREBITS3 | U_MOREBITS2 | U_MOREBITS1;
    else if (bits & 0x00ff0000)
        bits |= U_MOREBITS2 | U_MOREBITS1;
    else if (bits & 0x0000ff00)
        bits |= U_MOREBITS1;

    MSG_WriteByte(bits & 255);
    if (bits & U_MOREBITS1) MSG_WriteByte((bits >>  8) & 255);
    if (bits & U_MOREBITS2) MSG_WriteByte((bits >> 16) & 255);
    if (bits & U_MOREBITS3) MSG_WriteByte((bits >> 24) & 255);
    if (bits & U_MOREBITS4) MSG_WriteByte((bits >> 32) & 255);

    //----------

    if (bits & U_NUMBER16)
        MSG_WriteShort(to->number);
    else
        MSG_WriteByte(to->number);

    if (bits & U_MODEL16) {
        if (bits & U_MODEL ) MSG_WriteShort(to->modelindex );
        if (bits & U_MODEL2) MSG_WriteShort(to->modelindex2);
        if (bits & U_MODEL3) MSG_WriteShort(to->modelindex3);
        if (bits & U_MODEL4) MSG_WriteShort(to->modelindex4);
    } else {
        if (bits & U_MODEL ) MSG_WriteByte(to->modelindex );
        if (bits & U_MODEL2) MSG_WriteByte(to->modelindex2);
        if (bits & U_MODEL3) MSG_WriteByte(to->modelindex3);
        if (bits & U_MODEL4) MSG_WriteByte(to->modelindex4);
    }

    if (bits & U_FRAME8)
        MSG_WriteByte(to->frame);
    else if (bits & U_FRAME16)
        MSG_WriteShort(to->frame);

    if ((bits & U_SKIN32) == U_SKIN32)
        MSG_WriteLong(to->skinnum);
    else if (bits & U_SKIN8)
        MSG_WriteByte(to->skinnum);
    else if (bits & U_SKIN16)
        MSG_WriteShort(to->skinnum);

    if ((bits & U_EFFECTS32) == U_EFFECTS32)
        MSG_WriteLong(to->effects);
    else if (bits & U_EFFECTS8)
        MSG_WriteByte(to->effects);
    else if (bits & U_EFFECTS16)
        MSG_WriteShort(to->effects);

    if ((bits & U_RENDERFX32) == U_RENDERFX32)
        MSG_WriteLong(to->renderfx);
    else if (bits & U_RENDERFX8)
        MSG_WriteByte(to->renderfx);
    else if (bits & U_RENDERFX16)
        MSG_WriteShort(to->renderfx);

    if (bits & U_ORIGIN1) MSG_WriteCoord(to->origin[0]);
    if (bits & U_ORIGIN2) MSG_WriteCoord(to->origin[1]);
    if (bits & U_ORIGIN3) MSG_WriteCoord(to->origin[2]);

    if (bits & U_ANGLE16) {
        if (bits & U_ANGLE1) MSG_WriteShort(to->angles[0]);
        if (bits & U_ANGLE2) MSG_WriteShort(to->angles[1]);
        if (bits & U_ANGLE3) MSG_WriteShort(to->angles[2]);
    } else {
        if (bits & U_ANGLE1) MSG_WriteChar(to->angles[0] >> 8);
        if (bits & U_ANGLE2) MSG_WriteChar(to->angles[1] >> 8);
        if (bits & U_ANGLE3) MSG_WriteChar(to->angles[2] >> 8);
    }

    if (bits & U_OLDORIGIN) {
        MSG_WriteCoord(to->old_origin[0]);
        MSG_WriteCoord(to->old_origin[1]);
        MSG_WriteCoord(to->old_origin[2]);
    }

    if (bits & U_SOUND) {
        if (flags & MSG_ES_EXTENSIONS) {
            int w = to->sound & 0x3fff;

            if (to->loop_volume != from->loop_volume)
                w |= 0x4000;
            if (to->loop_attenuation != from->loop_attenuation)
                w |= 0x8000;

            MSG_WriteShort(w);
            if (w & 0x4000)
                MSG_WriteByte(to->loop_volume);
            if (w & 0x8000)
                MSG_WriteByte(to->loop_attenuation);
        } else {
            MSG_WriteByte(to->sound);
        }
    }

    if (bits & U_EVENT)
        MSG_WriteByte(to->event);

    if (bits & U_SOLID) {
        if (flags & MSG_ES_LONGSOLID)
            MSG_WriteLong(to->solid);
        else
            MSG_WriteShort(to->solid);
    }

    if (flags & MSG_ES_EXTENSIONS) {
        uint32_t to_morefx = to->effects >> 32;
        if ((bits & U_MOREFX32) == U_MOREFX32)
            MSG_WriteLong(to_morefx);
        else if (bits & U_MOREFX8)
            MSG_WriteByte(to_morefx);
        else if (bits & U_MOREFX16)
            MSG_WriteShort(to_morefx);

        if (bits & U_ALPHA)
            MSG_WriteByte(to->alpha);

        if (bits & U_SCALE)
            MSG_WriteByte(to->scale);
    }

}

#define OFFSET2CHAR(x)  Q_clip_int8((x) * 4)
#define BLEND2BYTE(x)   Q_clip_uint8((x) * 255)
#define FRAC2SHORT(x)   Q_clip_uint16((x) * 65535)

#define PACK_OFFSET(out, in)        \
    (out[0] = OFFSET2CHAR(in[0]),   \
     out[1] = OFFSET2CHAR(in[1]),   \
     out[2] = OFFSET2CHAR(in[2]))

#define PACK_COLOR(out, in)        \
    (out[0] = BLEND2BYTE(in[0]),   \
     out[1] = BLEND2BYTE(in[1]),   \
     out[2] = BLEND2BYTE(in[2]))

#define PACK_BLEND(out, in)        \
    (out[0] = BLEND2BYTE(in[0]),   \
     out[1] = BLEND2BYTE(in[1]),   \
     out[2] = BLEND2BYTE(in[2]),   \
     out[3] = BLEND2BYTE(in[3]))

static inline int16_t scaled_short(float x, int scale)
{
    return Q_clip(x * scale, -32768, 32767);
}

void MSG_PackPlayer(player_packed_t *out, const player_state_t *in, msgPsFlags_t flags)
{
    out->pmove = in->pmove;

    for (int i = 0; i < 3; i++) out->viewangles[i] = ANGLE2SHORT(in->viewangles[i]);
    if (flags & MSG_PS_RERELEASE) {
        for (int i = 0; i < 3; i++) out->viewoffset[i] = scaled_short(in->viewoffset[i], 16);
        for (int i = 0; i < 3; i++) out->kick_angles[i] = scaled_short(in->kick_angles[i], 1024);
        for (int i = 0; i < 3; i++) out->gunoffset[i] = scaled_short(in->gunoffset[i], 512);
        for (int i = 0; i < 3; i++) out->gunangles[i] = scaled_short(in->gunangles[i], 4096);
    } else {
        PACK_OFFSET(out->viewoffset, in->viewoffset);
        PACK_OFFSET(out->kick_angles, in->kick_angles);
        PACK_OFFSET(out->gunoffset, in->gunoffset);
        PACK_OFFSET(out->gunangles, in->gunangles);
    }

    out->gunindex = in->gunindex | in->gunskin << GUNINDEX_BITS;
    out->gunframe = in->gunframe;
    PACK_BLEND(out->screen_blend, in->screen_blend);
    PACK_BLEND(out->damage_blend, in->damage_blend);

    out->fov = Q_clip_uint8(in->fov);
    out->rdflags = in->rdflags;

    for (int i = 0; i < MAX_STATS_NEW; i++)
        out->stats[i] = in->stats[i];
// KEX
    out->gunrate = (in->gunrate == 10) ? 0 : in->gunrate;
// KEX
}

static uint64_t MSG_CalcStatBits(const player_packed_t *from, const player_packed_t *to, msgPsFlags_t flags)
{
    int numstats = (flags & (MSG_PS_EXTENSIONS_2 | MSG_PS_RERELEASE)) ? MAX_STATS_NEW : MAX_STATS_OLD;
    uint64_t statbits = 0;

    for (int i = 0; i < numstats; i++)
        if (to->stats[i] != from->stats[i])
            statbits |= BIT_ULL(i);

    return statbits;
}

static void MSG_WriteVarInt64(uint64_t v)
{
    do {
        int c = v & 0x7f;
        v >>= 7;
        if (v)
            c |= 0x80;
        MSG_WriteByte(c);
    } while (v);
}

static void MSG_WriteStats(const player_packed_t *to, uint64_t statbits, msgPsFlags_t flags)
{
    int numstats;

    if (flags & MSG_PS_RERELEASE) {
        MSG_WriteLong64(statbits);
        numstats = MAX_STATS_NEW;
    } else if (flags & MSG_PS_EXTENSIONS_2) {
        MSG_WriteVarInt64(statbits);
        numstats = MAX_STATS_NEW;
    } else {
        MSG_WriteLong(statbits);
        numstats = MAX_STATS_OLD;
    }

    for (int i = 0; i < numstats; i++)
        if (statbits & BIT_ULL(i))
            MSG_WriteShort(to->stats[i]);
}

static void MSG_WriteDeltaBlend(const player_packed_t *from, const player_packed_t *to)
{
    int i, bflags = 0;

    for (i = 0; i < 4; i++) {
        if (to->screen_blend[i] != from->screen_blend[i])
            bflags |= BIT(i);
        if (to->damage_blend[i] != from->damage_blend[i])
            bflags |= BIT(4 + i);
    }

    MSG_WriteByte(bflags);

    for (i = 0; i < 4; i++)
        if (bflags & BIT(i))
            MSG_WriteByte(to->screen_blend[i]);

    for (i = 0; i < 4; i++)
        if (bflags & BIT(4 + i))
            MSG_WriteByte(to->damage_blend[i]);
}

void MSG_WriteDeltaPlayerstate_Default(const player_packed_t *from, const player_packed_t *to, msgPsFlags_t flags)
{
    int         pflags = 0;
    uint64_t    statbits;

    Q_assert(to);

    if (!from)
        from = &nullPlayerState;

    //
    // determine what needs to be sent
    //
    if (to->pmove.pm_type != from->pmove.pm_type)
        pflags |= PS_M_TYPE;

    if (!VectorCompare(to->pmove.origin, from->pmove.origin))
        pflags |= PS_M_ORIGIN;

    if (!VectorCompare(to->pmove.velocity, from->pmove.velocity))
        pflags |= PS_M_VELOCITY;

    if (to->pmove.pm_time != from->pmove.pm_time)
        pflags |= PS_M_TIME;

    if (to->pmove.pm_flags != from->pmove.pm_flags)
        pflags |= PS_M_FLAGS;

    if (to->pmove.gravity != from->pmove.gravity)
        pflags |= PS_M_GRAVITY;

    if (!VectorCompare(to->pmove.delta_angles, from->pmove.delta_angles))
        pflags |= PS_M_DELTA_ANGLES;

    if (!VectorCompare(to->viewoffset, from->viewoffset))
        pflags |= PS_VIEWOFFSET;

    if (!VectorCompare(to->viewangles, from->viewangles))
        pflags |= PS_VIEWANGLES;

    if (!VectorCompare(to->kick_angles, from->kick_angles))
        pflags |= PS_KICKANGLES;

    if (!Vector4Compare(to->screen_blend, from->screen_blend))
        pflags |= PS_BLEND;
    else if (flags & MSG_PS_EXTENSIONS_2 &&
        !Vector4Compare(to->damage_blend, from->damage_blend))
        pflags |= PS_BLEND;

    if (to->fov != from->fov)
        pflags |= PS_FOV;

    if (to->rdflags != from->rdflags)
        pflags |= PS_RDFLAGS;

    if (to->gunframe != from->gunframe ||
        !VectorCompare(to->gunoffset, from->gunoffset) ||
        !VectorCompare(to->gunangles, from->gunangles))
        pflags |= PS_WEAPONFRAME;

    if (to->gunindex != from->gunindex)
        pflags |= PS_WEAPONINDEX;

    //
    // write it
    //
    MSG_WriteShort(pflags & 0xffff);

    //
    // write the pmove_state_t
    //
    if (pflags & PS_M_TYPE) {
        if(flags & MSG_PS_RERELEASE)
            MSG_WriteByte(to->pmove.pm_type);
        else
            MSG_WriteByte(pmtype_to_game3(to->pmove.pm_type));
    }

    if (pflags & PS_M_ORIGIN) {
        MSG_WriteCoord(to->pmove.origin[0]);
        MSG_WriteCoord(to->pmove.origin[1]);
        MSG_WriteCoord(to->pmove.origin[2]);
    }

    if (pflags & PS_M_VELOCITY) {
        MSG_WriteCoord(to->pmove.velocity[0]);
        MSG_WriteCoord(to->pmove.velocity[1]);
        MSG_WriteCoord(to->pmove.velocity[2]);
    }

    if (pflags & PS_M_TIME) {
        if(flags & (MSG_PS_RERELEASE | MSG_PS_EXTENSIONS_2))
            MSG_WriteShort(to->pmove.pm_time);
        else
            MSG_WriteByte(to->pmove.pm_time);
    }

    if (pflags & PS_M_FLAGS) {
        if(flags & MSG_PS_RERELEASE)
            MSG_WriteShort(to->pmove.pm_flags);
        else if(flags & MSG_PS_EXTENSIONS_2)
            MSG_WriteShort(pmflags_to_game3(to->pmove.pm_flags, true));
        else
            MSG_WriteByte(pmflags_to_game3(to->pmove.pm_flags, false));
    }

    if (pflags & PS_M_GRAVITY)
        MSG_WriteShort(to->pmove.gravity);

    if (pflags & PS_M_DELTA_ANGLES) {
        MSG_WriteAngle16(to->pmove.delta_angles[0]);
        MSG_WriteAngle16(to->pmove.delta_angles[1]);
        MSG_WriteAngle16(to->pmove.delta_angles[2]);
    }

    //
    // write the rest of the player_state_t
    //
    if (pflags & PS_VIEWOFFSET) {
        if (flags & MSG_PS_RERELEASE) {
            MSG_WriteShort(to->viewoffset[0]);
            MSG_WriteShort(to->viewoffset[1]);
            MSG_WriteShort(to->viewoffset[2]);
        } else {
            MSG_WriteChar(to->viewoffset[0]);
            MSG_WriteChar(to->viewoffset[1]);
            MSG_WriteChar(to->viewoffset[2]);
        }
    }

    if (pflags & PS_VIEWANGLES) {
        MSG_WriteShort(to->viewangles[0]);
        MSG_WriteShort(to->viewangles[1]);
        MSG_WriteShort(to->viewangles[2]);
    }

    if (pflags & PS_KICKANGLES) {
        if (flags & MSG_PS_RERELEASE) {
            MSG_WriteShort(to->kick_angles[0]);
            MSG_WriteShort(to->kick_angles[1]);
            MSG_WriteShort(to->kick_angles[2]);
        } else {
            MSG_WriteChar(to->kick_angles[0]);
            MSG_WriteChar(to->kick_angles[1]);
            MSG_WriteChar(to->kick_angles[2]);
        }
    }

    if (pflags & PS_WEAPONINDEX) {
        if (flags & MSG_PS_EXTENSIONS)
            MSG_WriteShort(to->gunindex);
        else
            MSG_WriteByte(to->gunindex);
    }

    if (pflags & PS_WEAPONFRAME) {
        if(flags & MSG_PS_RERELEASE) {
            MSG_WriteShort(to->gunframe);
            MSG_WriteShort(to->gunoffset[0]);
            MSG_WriteShort(to->gunoffset[1]);
            MSG_WriteShort(to->gunoffset[2]);
            MSG_WriteShort(to->gunangles[0]);
            MSG_WriteShort(to->gunangles[1]);
            MSG_WriteShort(to->gunangles[2]);
        } else {
            MSG_WriteByte(to->gunframe);
            MSG_WriteChar(to->gunoffset[0]);
            MSG_WriteChar(to->gunoffset[1]);
            MSG_WriteChar(to->gunoffset[2]);
            MSG_WriteChar(to->gunangles[0]);
            MSG_WriteChar(to->gunangles[1]);
            MSG_WriteChar(to->gunangles[2]);
        }
    }

    if (pflags & PS_BLEND) {
        if (flags & MSG_PS_EXTENSIONS_2)
            MSG_WriteDeltaBlend(from, to);
        else
            MSG_WriteData(to->screen_blend, sizeof(to->screen_blend));
    }

    if (pflags & PS_FOV)
        MSG_WriteByte(to->fov);

    if (pflags & PS_RDFLAGS)
        MSG_WriteByte(to->rdflags);

    // send stats
    statbits = MSG_CalcStatBits(from, to, flags);
    MSG_WriteStats(to, statbits, flags);
}

int MSG_WriteDeltaPlayerstate_Enhanced(const player_packed_t    *from,
                                             player_packed_t    *to,
                                             msgPsFlags_t       flags)
{
    int         pflags = 0, eflags = 0;
    uint64_t    statbits;

    Q_assert(to);

    if (!from)
        from = &nullPlayerState;

    //
    // determine what needs to be sent
    //
    if (to->pmove.pm_type != from->pmove.pm_type)
        pflags |= PS_M_TYPE;

    if (to->pmove.origin[0] != from->pmove.origin[0] ||
        to->pmove.origin[1] != from->pmove.origin[1])
        pflags |= PS_M_ORIGIN;

    if (to->pmove.origin[2] != from->pmove.origin[2])
        eflags |= EPS_M_ORIGIN2;

    if (!(flags & MSG_PS_IGNORE_PREDICTION)) {
        if (to->pmove.velocity[0] != from->pmove.velocity[0] ||
            to->pmove.velocity[1] != from->pmove.velocity[1])
            pflags |= PS_M_VELOCITY;

        if (to->pmove.velocity[2] != from->pmove.velocity[2])
            eflags |= EPS_M_VELOCITY2;

        if (to->pmove.pm_time != from->pmove.pm_time)
            pflags |= PS_M_TIME;

        if (to->pmove.pm_flags != from->pmove.pm_flags)
            pflags |= PS_M_FLAGS;

        if (to->pmove.gravity != from->pmove.gravity)
            pflags |= PS_M_GRAVITY;
    } else {
        // save previous state
        VectorCopy(from->pmove.velocity, to->pmove.velocity);
        to->pmove.pm_time = from->pmove.pm_time;
        to->pmove.pm_flags = from->pmove.pm_flags;
        to->pmove.gravity = from->pmove.gravity;
    }

    if (!(flags & MSG_PS_IGNORE_DELTAANGLES)) {
        if (!VectorCompare(from->pmove.delta_angles, to->pmove.delta_angles))
            pflags |= PS_M_DELTA_ANGLES;
    } else {
        // save previous state
        VectorCopy(from->pmove.delta_angles, to->pmove.delta_angles);
    }

    if (!VectorCompare(from->viewoffset, to->viewoffset))
        pflags |= PS_VIEWOFFSET;

    if (!(flags & MSG_PS_IGNORE_VIEWANGLES)) {
        if (from->viewangles[0] != to->viewangles[0] ||
            from->viewangles[1] != to->viewangles[1])
            pflags |= PS_VIEWANGLES;

        if (from->viewangles[2] != to->viewangles[2])
            eflags |= EPS_VIEWANGLE2;
    } else {
        // save previous state
        VectorCopy(from->viewangles, to->viewangles);
    }

    if (!VectorCompare(from->kick_angles, to->kick_angles))
        pflags |= PS_KICKANGLES;

    if (!(flags & MSG_PS_IGNORE_BLEND)) {
        if (!Vector4Compare(from->screen_blend, to->screen_blend) ||
            ((flags & MSG_PS_RERELEASE) && !Vector4Compare(from->damage_blend, to->damage_blend)))
            pflags |= PS_BLEND;
        else if (flags & MSG_PS_EXTENSIONS_2 &&
            !Vector4Compare(to->damage_blend, from->damage_blend))
            pflags |= PS_BLEND;
    } else {
        // save previous state
        Vector4Copy(from->screen_blend, to->screen_blend);
        Vector4Copy(from->damage_blend, to->damage_blend);
    }

    if (from->fov != to->fov)
        pflags |= PS_FOV;

    if (to->rdflags != from->rdflags)
        pflags |= PS_RDFLAGS;

    if (!(flags & MSG_PS_IGNORE_GUNINDEX)) {
        if (to->gunindex != from->gunindex)
            pflags |= PS_WEAPONINDEX;
    } else {
        // save previous state
        to->gunindex = from->gunindex;
    }

    if (!(flags & MSG_PS_IGNORE_GUNFRAMES)) {
        if (to->gunframe != from->gunframe)
            pflags |= PS_WEAPONFRAME;

        if (!VectorCompare(from->gunoffset, to->gunoffset))
            eflags |= EPS_GUNOFFSET;

        if (!VectorCompare(from->gunangles, to->gunangles))
            eflags |= EPS_GUNANGLES;
    } else {
        // save previous state
        to->gunframe = from->gunframe;
        VectorCopy(from->gunoffset, to->gunoffset);
        VectorCopy(from->gunangles, to->gunangles);
    }

    statbits = MSG_CalcStatBits(from, to, flags);
    if (statbits)
        eflags |= EPS_STATS;

// KEX
    if (!(flags & MSG_PS_IGNORE_GUNFRAMES)) {
        if (to->gunrate != from->gunrate)
            eflags |= EPS_GUNRATE;
    } else {
        // save previous state
        to->gunrate = from->gunrate;
    }

    if (to->pmove.viewheight != from->pmove.viewheight) {
        pflags |= PS_RR_VIEWHEIGHT;
    }
// KEX

    //
    // write it
    //
    MSG_WriteShort(pflags & 0xffff);

    //
    // write the pmove_state_t
    //
    if (pflags & PS_M_TYPE) {
        if(flags & MSG_PS_RERELEASE)
            MSG_WriteByte(to->pmove.pm_type);
        else
            MSG_WriteByte(pmtype_to_game3(to->pmove.pm_type));
    }

    if (pflags & PS_M_ORIGIN) {
        MSG_WriteCoord(to->pmove.origin[0]);
        MSG_WriteCoord(to->pmove.origin[1]);
    }

    if (eflags & EPS_M_ORIGIN2)
        MSG_WriteCoord(to->pmove.origin[2]);

    if (pflags & PS_M_VELOCITY) {
        MSG_WriteCoord(to->pmove.velocity[0]);
        MSG_WriteCoord(to->pmove.velocity[1]);
    }

    if (eflags & EPS_M_VELOCITY2)
        MSG_WriteCoord(to->pmove.velocity[2]);

    if (pflags & PS_M_TIME) {
        if(flags & (MSG_PS_RERELEASE | MSG_PS_EXTENSIONS_2))
            MSG_WriteShort(to->pmove.pm_time);
        else
            MSG_WriteByte(to->pmove.pm_time);
    }

    if (pflags & PS_M_FLAGS) {
        if(flags & MSG_PS_RERELEASE)
            MSG_WriteShort(to->pmove.pm_flags);
        else if(flags & MSG_PS_EXTENSIONS_2)
            MSG_WriteShort(pmflags_to_game3(to->pmove.pm_flags, true));
        else
            MSG_WriteByte(pmflags_to_game3(to->pmove.pm_flags, false));
    }

    if (pflags & PS_M_GRAVITY)
        MSG_WriteShort(to->pmove.gravity);

    if (pflags & PS_M_DELTA_ANGLES) {
        MSG_WriteAngle16(to->pmove.delta_angles[0]);
        MSG_WriteAngle16(to->pmove.delta_angles[1]);
        MSG_WriteAngle16(to->pmove.delta_angles[2]);
    }

    //
    // write the rest of the player_state_t
    //
    if (pflags & PS_VIEWOFFSET) {
        if (flags & MSG_PS_EXTENSIONS) {
            MSG_WriteShort(to->viewoffset[0]);
            MSG_WriteShort(to->viewoffset[1]);
            MSG_WriteShort(to->viewoffset[2]);
        } else {
            MSG_WriteChar(to->viewoffset[0]);
            MSG_WriteChar(to->viewoffset[1]);
            MSG_WriteChar(to->viewoffset[2]);
        }
    }

    if (pflags & PS_VIEWANGLES) {
        MSG_WriteShort(to->viewangles[0]);
        MSG_WriteShort(to->viewangles[1]);
    }

    if (eflags & EPS_VIEWANGLE2)
        MSG_WriteShort(to->viewangles[2]);

    if (pflags & PS_KICKANGLES) {
        if (flags & MSG_PS_EXTENSIONS) {
            MSG_WriteShort(to->kick_angles[0]);
            MSG_WriteShort(to->kick_angles[1]);
            MSG_WriteShort(to->kick_angles[2]);
        } else {
            MSG_WriteChar(to->kick_angles[0]);
            MSG_WriteChar(to->kick_angles[1]);
            MSG_WriteChar(to->kick_angles[2]);
        }
    }

    if (pflags & PS_WEAPONINDEX) {
        if (flags & MSG_PS_EXTENSIONS)
            MSG_WriteShort(to->gunindex);
        else
            MSG_WriteByte(to->gunindex);
    }

    if (pflags & PS_WEAPONFRAME) {
        if(flags & MSG_PS_RERELEASE) {
            MSG_WriteShort(to->gunframe);
        } else {
            MSG_WriteByte(to->gunframe);
        }
    }

    if (eflags & EPS_GUNOFFSET) {
        if(flags & MSG_PS_RERELEASE) {
            MSG_WriteShort(to->gunoffset[0]);
            MSG_WriteShort(to->gunoffset[1]);
            MSG_WriteShort(to->gunoffset[2]);
        } else {
            MSG_WriteChar(to->gunoffset[0]);
            MSG_WriteChar(to->gunoffset[1]);
            MSG_WriteChar(to->gunoffset[2]);
        }
    }

    if (eflags & EPS_GUNANGLES) {
        if(flags & MSG_PS_RERELEASE) {
            MSG_WriteShort(to->gunangles[0]);
            MSG_WriteShort(to->gunangles[1]);
            MSG_WriteShort(to->gunangles[2]);
        } else {
            MSG_WriteChar(to->gunangles[0]);
            MSG_WriteChar(to->gunangles[1]);
            MSG_WriteChar(to->gunangles[2]);
        }
    }

    if (pflags & PS_BLEND) {
        if (flags & (MSG_PS_EXTENSIONS_2 | MSG_PS_RERELEASE))
            MSG_WriteDeltaBlend(from, to);
        else
            MSG_WriteData(to->screen_blend, sizeof(to->screen_blend));
    }

    if (pflags & PS_FOV)
        MSG_WriteByte(to->fov);

    if (pflags & PS_RDFLAGS)
        MSG_WriteByte(to->rdflags);

    // send stats
    if (eflags & EPS_STATS)
        MSG_WriteStats(to, statbits, flags);

// KEX
    if (eflags & EPS_GUNRATE) {
        MSG_WriteByte(to->gunrate);
    }

    if (pflags & PS_RR_VIEWHEIGHT) {
        MSG_WriteByte(to->pmove.viewheight);
    }
// KEX

    return eflags;
}

#if USE_MVD_SERVER || USE_MVD_CLIENT || USE_CLIENT_GTV

/*
==================
MSG_WriteDeltaPlayerstate_Packet

Throws away most of the pmove_state_t fields as they are used only
for client prediction, and are not needed in MVDs.
==================
*/
void MSG_WriteDeltaPlayerstate_Packet(const player_packed_t *from,
                                      const player_packed_t *to,
                                      int                   number,
                                      msgPsFlags_t          flags)
{
    int         pflags = 0;
    uint64_t    statbits;

    // this can happen with client GTV
    if (number < 0 || number >= CLIENTNUM_NONE)
        Com_Error(ERR_DROP, "%s: bad number: %d", __func__, number);

    if (!to) {
        MSG_WriteByte(number);
        MSG_WriteShort(PPS_MOREBITS);   // MOREBITS == REMOVE for old demos
        if (flags & MSG_PS_MOREBITS)
            MSG_WriteByte(PPS_REMOVE >> 16);
        return;
    }

    if (!from)
        from = &nullPlayerState;

    //
    // determine what needs to be sent
    //
    if (to->pmove.pm_type != from->pmove.pm_type)
        pflags |= PPS_M_TYPE;

    if (to->pmove.origin[0] != from->pmove.origin[0] ||
        to->pmove.origin[1] != from->pmove.origin[1])
        pflags |= PPS_M_ORIGIN;

    if (to->pmove.origin[2] != from->pmove.origin[2])
        pflags |= PPS_M_ORIGIN2;

    if (!VectorCompare(from->viewoffset, to->viewoffset))
        pflags |= PPS_VIEWOFFSET;

    if (from->viewangles[0] != to->viewangles[0] ||
        from->viewangles[1] != to->viewangles[1])
        pflags |= PPS_VIEWANGLES;

    if (from->viewangles[2] != to->viewangles[2])
        pflags |= PPS_VIEWANGLE2;

    if (!VectorCompare(from->kick_angles, to->kick_angles))
        pflags |= PPS_KICKANGLES;

    if (!(flags & MSG_PS_IGNORE_BLEND)) {
        if (!Vector4Compare(from->screen_blend, to->screen_blend))
            pflags |= PPS_BLEND;
        else if (flags & (MSG_PS_EXTENSIONS_2 | MSG_PS_RERELEASE) &&
            !Vector4Compare(to->damage_blend, from->damage_blend))
            pflags |= PPS_BLEND;
    }

    if (from->fov != to->fov)
        pflags |= PPS_FOV;

    if (to->rdflags != from->rdflags)
        pflags |= PPS_RDFLAGS;

    if (!(flags & MSG_PS_IGNORE_GUNINDEX) && to->gunindex != from->gunindex)
        pflags |= PPS_WEAPONINDEX;

    if (!(flags & MSG_PS_IGNORE_GUNFRAMES)) {
        if (to->gunframe != from->gunframe)
            pflags |= PPS_WEAPONFRAME;

        if (!VectorCompare(from->gunoffset, to->gunoffset))
            pflags |= PPS_GUNOFFSET;

        if (!VectorCompare(from->gunangles, to->gunangles))
            pflags |= PPS_GUNANGLES;
    }

    statbits = MSG_CalcStatBits(from, to, flags);
    if (statbits)
        pflags |= PPS_STATS;

    if (!pflags && !(flags & MSG_PS_FORCE))
        return;

    if (flags & MSG_PS_REMOVE)
        pflags |= PPS_REMOVE; // used for MVD stream only

    //
    // write it
    //
    MSG_WriteByte(number);
    MSG_WriteShort(pflags & 0xffff);
    //
    // write some part of the pmove_state_t
    //
    if (pflags & PPS_M_TYPE) {
        if (flags & MSG_PS_RERELEASE)
            MSG_WriteByte(to->pmove.pm_type);
        else
            MSG_WriteByte(pmtype_to_game3(to->pmove.pm_type));
    }

    if (pflags & PPS_M_ORIGIN) {
        MSG_WriteCoord(to->pmove.origin[0]);
        MSG_WriteCoord(to->pmove.origin[1]);
    }

    if (pflags & PPS_M_ORIGIN2)
        MSG_WriteCoord(to->pmove.origin[2]);

    //
    // write the rest of the player_state_t
    //
    if (pflags & PPS_VIEWOFFSET) {
        if (flags & MSG_PS_EXTENSIONS) {
            MSG_WriteShort(to->viewoffset[0]);
            MSG_WriteShort(to->viewoffset[1]);
            MSG_WriteShort(to->viewoffset[2]);
        } else {
            MSG_WriteChar(to->viewoffset[0]);
            MSG_WriteChar(to->viewoffset[1]);
            MSG_WriteChar(to->viewoffset[2]);
        }
    }

    if (pflags & PPS_VIEWANGLES) {
        MSG_WriteShort(to->viewangles[0]);
        MSG_WriteShort(to->viewangles[1]);
    }

    if (pflags & PPS_VIEWANGLE2)
        MSG_WriteShort(to->viewangles[2]);

    if (pflags & PPS_KICKANGLES) {
        if (flags & MSG_PS_EXTENSIONS) {
            MSG_WriteShort(to->kick_angles[0]);
            MSG_WriteShort(to->kick_angles[1]);
            MSG_WriteShort(to->kick_angles[2]);
        } else {
            MSG_WriteChar(to->kick_angles[0]);
            MSG_WriteChar(to->kick_angles[1]);
            MSG_WriteChar(to->kick_angles[2]);
        }
    }

    if (pflags & PPS_WEAPONINDEX) {
        if (flags & MSG_PS_EXTENSIONS)
            MSG_WriteShort(to->gunindex);
        else
            MSG_WriteByte(to->gunindex);
    }

    if (pflags & PPS_WEAPONFRAME) {
        if(flags & MSG_PS_RERELEASE) {
            MSG_WriteShort(to->gunframe);
        } else {
            MSG_WriteByte(to->gunframe);
        }
    }

    if (pflags & PPS_GUNOFFSET) {
        if(flags & MSG_PS_RERELEASE) {
            MSG_WriteShort(to->gunoffset[0]);
            MSG_WriteShort(to->gunoffset[1]);
            MSG_WriteShort(to->gunoffset[2]);
        } else {
            MSG_WriteChar(to->gunoffset[0]);
            MSG_WriteChar(to->gunoffset[1]);
            MSG_WriteChar(to->gunoffset[2]);
        }
    }

    if (pflags & PPS_GUNANGLES) {
        if(flags & MSG_PS_RERELEASE) {
            MSG_WriteShort(to->gunangles[0]);
            MSG_WriteShort(to->gunangles[1]);
            MSG_WriteShort(to->gunangles[2]);
        } else {
            MSG_WriteChar(to->gunangles[0]);
            MSG_WriteChar(to->gunangles[1]);
            MSG_WriteChar(to->gunangles[2]);
        }
    }

    if (pflags & PPS_BLEND) {
        if (flags & (MSG_PS_EXTENSIONS_2 | MSG_PS_RERELEASE))
            MSG_WriteDeltaBlend(from, to);
        else
            MSG_WriteData(to->screen_blend, sizeof(to->screen_blend));
    }

    if (pflags & PPS_FOV)
        MSG_WriteByte(to->fov);

    if (pflags & PPS_RDFLAGS)
        MSG_WriteByte(to->rdflags);

    // send stats
    if (pflags & PPS_STATS)
        MSG_WriteStats(to, statbits, flags);
}

#endif // USE_MVD_SERVER || USE_MVD_CLIENT || USE_CLIENT_GTV


/*
==============================================================================

            READING

==============================================================================
*/

void MSG_BeginReading(void)
{
    msg_read.readcount = 0;
    msg_read.bits_buf  = 0;
    msg_read.bits_left = 0;
}

byte *MSG_ReadData(size_t len)
{
    return SZ_ReadData(&msg_read, len);
}

// returns -1 if no more characters are available
int MSG_ReadChar(void)
{
    byte *buf = MSG_ReadData(1);
    int c;

    if (!buf) {
        c = -1;
    } else {
        c = (int8_t)buf[0];
    }

    return c;
}

int MSG_ReadByte(void)
{
    byte *buf = MSG_ReadData(1);
    int c;

    if (!buf) {
        c = -1;
    } else {
        c = (uint8_t)buf[0];
    }

    return c;
}

int MSG_ReadShort(void)
{
    byte *buf = MSG_ReadData(2);
    int c;

    if (!buf) {
        c = -1;
    } else {
        c = (int16_t)RL16(buf);
    }

    return c;
}

int MSG_ReadWord(void)
{
    byte *buf = MSG_ReadData(2);
    int c;

    if (!buf) {
        c = -1;
    } else {
        c = (uint16_t)RL16(buf);
    }

    return c;
}

int MSG_ReadLong(void)
{
    byte *buf = MSG_ReadData(4);
    int c;

    if (!buf) {
        c = -1;
    } else {
        c = (int32_t)RL32(buf);
    }

    return c;
}

float MSG_ReadFloat(void)
{
    byte *buf = MSG_ReadData(4);
    union {
        float _f;
        int32_t _i;
    } v;

    if (!buf) {
        v._f = -1;
    } else {
        v._i = (int32_t)RL32(buf);
    }

    return v._f;
}

int64_t MSG_ReadLong64(void)
{
    byte *buf = MSG_ReadData(8);
    int64_t c;

    if (!buf) {
        c = -1;
    } else {
        c = RL64(buf);
    }

    return c;
}

// Paril: uint versions
uint32_t MSG_ReadULong(void)
{
    byte *buf = MSG_ReadData(4);
    uint32_t c;

    if (!buf) {
        c = -1;
    } else {
        c = RL32(buf);
    }

    return c;
}

uint64_t MSG_ReadULong64(void)
{
    byte *buf = MSG_ReadData(8);
    uint64_t c;

    if (!buf) {
        c = -1;
    } else {
        c = RL64(buf);
    }

    return c;
}

size_t MSG_ReadString(char *dest, size_t size)
{
    int     c;
    size_t  len = 0;

    while (1) {
        c = MSG_ReadByte();
        if (c == -1 || c == 0) {
            break;
        }
        if (len + 1 < size) {
            *dest++ = c;
        }
        len++;
    }
    if (size) {
        *dest = 0;
    }

    return len;
}

size_t MSG_ReadStringLine(char *dest, size_t size)
{
    int     c;
    size_t  len = 0;

    while (1) {
        c = MSG_ReadByte();
        if (c == -1 || c == 0 || c == '\n') {
            break;
        }
        if (len + 1 < size) {
            *dest++ = c;
        }
        len++;
    }
    if (size) {
        *dest = 0;
    }

    return len;
}

#if USE_CLIENT || USE_MVD_CLIENT

static inline float MSG_ReadAngle(void)
{
    return BYTE2ANGLE(MSG_ReadChar());
}

static inline float MSG_ReadAngle16(void)
{
    return SHORT2ANGLE(MSG_ReadShort());
}

static void MSG_ReadDeltaCoord(float *to)
{
    uint32_t v = MSG_ReadWord();
    if (v & 1) {
        v |= (uint32_t)MSG_ReadByte() << 16;
        *to = SHORT2COORD(SignExtend(v >> 1, 23));
    } else {
        *to += SHORT2COORD(SignExtend(v >> 1, 15));
    }
}

static float MSG_ReadExtCoord(void)
{
    uint32_t v = MSG_ReadWord();
    if (v & 1) {
        v |= (uint32_t)MSG_ReadByte() << 16;
        return SHORT2COORD(SignExtend(v >> 1, 23));
    } else {
        return SHORT2COORD(SignExtend(v >> 1, 15));
    }
}

static void MSG_ReadCoordP(float *to, msgPsFlags_t flags)
{
    if (flags & MSG_PS_RERELEASE)
        *to = MSG_ReadFloat();
    else if (flags & MSG_PS_EXTENSIONS_2)
        MSG_ReadDeltaCoord(to);
    else
        *to = SHORT2COORD(MSG_ReadShort());
}

static void MSG_ReadCoordE(float *to, msgEsFlags_t flags)
{
    if (flags & MSG_ES_RERELEASE)
        *to = MSG_ReadFloat();
    else if (flags & MSG_ES_EXTENSIONS_2)
        MSG_ReadDeltaCoord(to);
    else
        *to = SHORT2COORD(MSG_ReadShort());
}

#endif

#if USE_SERVER
static inline
#endif
void MSG_ReadPos(vec3_t pos, msgEsFlags_t flags)
{
    if (flags & MSG_ES_RERELEASE) {
        pos[0] = MSG_ReadFloat();
        pos[1] = MSG_ReadFloat();
        pos[2] = MSG_ReadFloat();
    } else if (flags & MSG_ES_EXTENSIONS_2) {
        pos[0] = MSG_ReadExtCoord();
        pos[1] = MSG_ReadExtCoord();
        pos[2] = MSG_ReadExtCoord();
    } else {
        pos[0] = SHORT2COORD(MSG_ReadShort());
        pos[1] = SHORT2COORD(MSG_ReadShort());
        pos[2] = SHORT2COORD(MSG_ReadShort());
    }
}

#if USE_CLIENT
static inline void MSG_ReadPosP(vec3_t pos, msgPsFlags_t flags)
{
    if (flags & MSG_PS_RERELEASE) {
        pos[0] = MSG_ReadFloat();
        pos[1] = MSG_ReadFloat();
        pos[2] = MSG_ReadFloat();
    } else if (flags & MSG_PS_EXTENSIONS_2) {
        VectorClear(pos);
        MSG_ReadDeltaCoord(&pos[0]);
        MSG_ReadDeltaCoord(&pos[1]);
        MSG_ReadDeltaCoord(&pos[2]);
    } else {
        pos[0] = SHORT2COORD(MSG_ReadShort());
        pos[1] = SHORT2COORD(MSG_ReadShort());
        pos[2] = SHORT2COORD(MSG_ReadShort());
    }
}

void MSG_ReadDir(vec3_t dir)
{
    int     b;

    b = MSG_ReadByte();
    if (b < 0 || b >= NUMVERTEXNORMALS)
        Com_Error(ERR_DROP, "MSG_ReadDir: out of range");
    VectorCopy(bytedirs[b], dir);
}
#endif

void MSG_ReadDeltaUsercmd(const usercmd_t *from, usercmd_t *to)
{
    int bits;

    Q_assert(to);

    if (from) {
        memcpy(to, from, sizeof(*to));
    } else {
        memset(to, 0, sizeof(*to));
    }

    bits = MSG_ReadByte();

// read current angles
    if (bits & CM_ANGLE1)
        to->angles[0] = MSG_ReadAngle16();
    if (bits & CM_ANGLE2)
        to->angles[1] = MSG_ReadAngle16();
    if (bits & CM_ANGLE3)
        to->angles[2] = MSG_ReadAngle16();

// read movement
    if (bits & CM_FORWARD)
        to->forwardmove = MSG_ReadShort();
    if (bits & CM_SIDE)
        to->sidemove = MSG_ReadShort();
    Q_assert((bits & CM_UP) == 0); // should not be set in rerelease protocol

// read buttons
    if (bits & CM_BUTTONS) {
        to->buttons = MSG_ReadByte();
    }

    if (bits & CM_IMPULSE)
        MSG_ReadByte(); // skip impulse

// read time to run command
    to->msec = MSG_ReadByte();

// skip the light level
    MSG_ReadByte();
}

int MSG_ReadBits(int bits)
{
    bool sgn = false;

    Q_assert(!(bits == 0 || bits < -25 || bits > 25));

    if (bits < 0) {
        bits = -bits;
        sgn = true;
    }

    uint32_t bits_buf  = msg_read.bits_buf;
    uint32_t bits_left = msg_read.bits_left;

    while (bits > bits_left) {
        bits_buf  |= (uint32_t)MSG_ReadByte() << bits_left;
        bits_left += 8;
    }

    uint32_t value = bits_buf & ((1U << bits) - 1);

    msg_read.bits_buf  = bits_buf >> bits;
    msg_read.bits_left = bits_left - bits;

    if (sgn) {
        return SignExtend(value, bits);
    }

    return value;
}

void MSG_ReadDeltaUsercmd_Enhanced(const usercmd_t *from, usercmd_t *to)
{
    int bits;

    Q_assert(to);

    if (from) {
        memcpy(to, from, sizeof(*to));
    } else {
        memset(to, 0, sizeof(*to));
    }

    if (!MSG_ReadBits(1)) {
        return;
    }

    bits = MSG_ReadBits(8);

// read current angles
    if (bits & CM_ANGLE1) {
        if (MSG_ReadBits(1)) {
            to->angles[0] = to->angles[0] + SHORT2ANGLE(MSG_ReadBits(-8));
        } else {
            to->angles[0] = SHORT2ANGLE(MSG_ReadBits(-16));
        }
    }
    if (bits & CM_ANGLE2) {
        if (MSG_ReadBits(1)) {
            to->angles[1] = to->angles[1] + SHORT2ANGLE(MSG_ReadBits(-8));
        } else {
            to->angles[1] = SHORT2ANGLE(MSG_ReadBits(-16));
        }
    }
    if (bits & CM_ANGLE3) {
        to->angles[2] = SHORT2ANGLE(MSG_ReadBits(-16));
    }

// read movement
    if (bits & CM_FORWARD) {
        to->forwardmove = MSG_ReadBits(-10);
    }
    if (bits & CM_SIDE) {
        to->sidemove = MSG_ReadBits(-10);
    }
    Q_assert((bits & CM_UP) == 0); // should not be set in rerelease protocol

// read buttons
    if (bits & CM_BUTTONS) {
        to->buttons = MSG_ReadBits(8);
    }

// read time to run command
    if (bits & CM_IMPULSE) {
        to->msec = MSG_ReadBits(8);
    }
}

#if USE_CLIENT || USE_MVD_CLIENT

/*
=================
MSG_ParseEntityBits

Returns the entity number and the header bits
=================
*/
int MSG_ParseEntityBits(uint64_t *bits, msgEsFlags_t flags)
{
    uint64_t    b, total;
    int         number;

    total = MSG_ReadByte();
    if (total & U_MOREBITS1) {
        b = MSG_ReadByte();
        total |= b << 8;
    }
    if (total & U_MOREBITS2) {
        b = MSG_ReadByte();
        total |= b << 16;
    }
    if (total & U_MOREBITS3) {
        b = MSG_ReadByte();
        total |= b << 24;
    }
    if (flags & MSG_ES_EXTENSIONS && total & U_MOREBITS4) {
        b = MSG_ReadByte();
        total |= b << 32;
    }

    if (total & U_NUMBER16)
        number = MSG_ReadWord();
    else
        number = MSG_ReadByte();

    *bits = total;

    return number;
}

/*
==================
MSG_ParseDeltaEntity

Can go from either a baseline or a previous packet_entity
==================
*/
void MSG_ParseDeltaEntity(entity_state_t            *to,
                          int                       number,
                          uint64_t                  bits,
                          msgEsFlags_t              flags)
{
    Q_assert(to);
    Q_assert(number > 0 && number < MAX_EDICTS);

    to->number = number;
    to->event = 0;

    if (!bits) {
        return;
    }

    if (flags & MSG_ES_EXTENSIONS && bits & U_MODEL16) {
        if (bits & U_MODEL ) to->modelindex  = MSG_ReadWord();
        if (bits & U_MODEL2) to->modelindex2 = MSG_ReadWord();
        if (bits & U_MODEL3) to->modelindex3 = MSG_ReadWord();
        if (bits & U_MODEL4) to->modelindex4 = MSG_ReadWord();
    } else {
        if (bits & U_MODEL ) to->modelindex  = MSG_ReadByte();
        if (bits & U_MODEL2) to->modelindex2 = MSG_ReadByte();
        if (bits & U_MODEL3) to->modelindex3 = MSG_ReadByte();
        if (bits & U_MODEL4) to->modelindex4 = MSG_ReadByte();
    }

    if (bits & U_FRAME8)
        to->frame = MSG_ReadByte();
    if (bits & U_FRAME16)
        to->frame = MSG_ReadWord();

    if ((bits & U_SKIN32) == U_SKIN32)
        to->skinnum = MSG_ReadLong();
    else if (bits & U_SKIN8)
        to->skinnum = MSG_ReadByte();
    else if (bits & U_SKIN16)
        to->skinnum = MSG_ReadWord();

    if ((bits & U_EFFECTS32) == U_EFFECTS32)
        to->effects = MSG_ReadULong();
    else if (bits & U_EFFECTS8)
        to->effects = MSG_ReadByte();
    else if (bits & U_EFFECTS16)
        to->effects = MSG_ReadWord();

    if ((bits & U_RENDERFX32) == U_RENDERFX32)
        to->renderfx = MSG_ReadULong();
    else if (bits & U_RENDERFX8)
        to->renderfx = MSG_ReadByte();
    else if (bits & U_RENDERFX16)
        to->renderfx = MSG_ReadWord();

    if (bits & U_ORIGIN1) MSG_ReadCoordE(&to->origin[0], flags);
    if (bits & U_ORIGIN2) MSG_ReadCoordE(&to->origin[1], flags);
    if (bits & U_ORIGIN3) MSG_ReadCoordE(&to->origin[2], flags);

    if (flags & MSG_ES_SHORTANGLES && bits & U_ANGLE16) {
        if (bits & U_ANGLE1) to->angles[0] = MSG_ReadAngle16();
        if (bits & U_ANGLE2) to->angles[1] = MSG_ReadAngle16();
        if (bits & U_ANGLE3) to->angles[2] = MSG_ReadAngle16();
    } else {
        if (bits & U_ANGLE1) to->angles[0] = MSG_ReadAngle();
        if (bits & U_ANGLE2) to->angles[1] = MSG_ReadAngle();
        if (bits & U_ANGLE3) to->angles[2] = MSG_ReadAngle();
    }

    if (bits & U_OLDORIGIN)
        MSG_ReadPos(to->old_origin, flags);

    if (bits & U_SOUND) {
        if (flags & MSG_ES_EXTENSIONS) {
            int w = MSG_ReadWord();
            to->sound = w & 0x3fff;
            if (w & 0x4000)
                to->loop_volume = MSG_ReadByte() / 255.0f;
            if (w & 0x8000) {
                to->loop_attenuation = q2proto_sound_decode_loop_attenuation(MSG_ReadByte());
            }
        } else {
            to->sound = MSG_ReadByte();
        }
    }

    if (bits & U_EVENT)
        to->event = MSG_ReadByte();

    if (bits & U_SOLID) {
        if (flags & MSG_ES_LONGSOLID)
            to->solid = MSG_ReadLong();
        else
            to->solid = MSG_ReadWord();
    }

    if (flags & MSG_ES_EXTENSIONS) {
        uint32_t to_morefx = 0;
        if ((bits & U_MOREFX32) == U_MOREFX32)
            to_morefx = MSG_ReadULong();
        else if (bits & U_MOREFX8)
            to_morefx = MSG_ReadByte();
        else if (bits & U_MOREFX16)
            to_morefx = MSG_ReadWord();
        to->effects |= ((effects_t)to_morefx) << 32;

        if (bits & U_ALPHA)
            to->alpha = MSG_ReadByte() / 255.0f;

        if (bits & U_SCALE)
            to->scale = MSG_ReadByte() / 16.0f;
    }
}

#endif // USE_CLIENT || USE_MVD_CLIENT

static uint64_t MSG_ReadVarInt64(void)
{
    uint64_t v = 0;
    int c, bits = 0;

    do {
        c = MSG_ReadByte();
        if (c == -1)
            break;
        v |= (c & UINT64_C(0x7f)) << bits;
        bits += 7;
    } while (c & 0x80 && bits < 64);

    return v;
}

static void MSG_ReadStats(player_state_t *to, msgPsFlags_t flags)
{
    uint64_t statbits;
    int numstats;

    if (flags & MSG_PS_RERELEASE) {
        statbits = MSG_ReadLong64();
        numstats = MAX_STATS_NEW;
    } else if (flags & MSG_PS_EXTENSIONS_2) {
        statbits = MSG_ReadVarInt64();
        numstats = MAX_STATS_NEW;
    } else {
        statbits = MSG_ReadLong();
        numstats = MAX_STATS_OLD;
    }

    if (!statbits)
        return;

    for (int i = 0; i < numstats; i++)
        if (statbits & BIT_ULL(i))
            to->stats[i] = MSG_ReadShort();
}

static void MSG_ReadBlend(player_state_t *to, msgPsFlags_t psflags)
{
    if (psflags & (MSG_PS_RERELEASE | MSG_PS_EXTENSIONS_2)) {
        int bflags = MSG_ReadByte();

        for (int i = 0; i < 4; i++)
            if (bflags & BIT(i))
                to->screen_blend[i] = MSG_ReadByte() / 255.0f;

        for (int i = 0; i < 4; i++)
            if (bflags & BIT(4 + i))
                to->damage_blend[i] = MSG_ReadByte() / 255.0f;
    } else {
        to->screen_blend[0] = MSG_ReadByte() / 255.0f;
        to->screen_blend[1] = MSG_ReadByte() / 255.0f;
        to->screen_blend[2] = MSG_ReadByte() / 255.0f;
        to->screen_blend[3] = MSG_ReadByte() / 255.0f;
    }
}

static void MSG_ReadColor(vec3_t color)
{
    color[0] = MSG_ReadByte() / 255.0f;
    color[1] = MSG_ReadByte() / 255.0f;
    color[2] = MSG_ReadByte() / 255.0f;
}

static void MSG_ReadFog(player_fogchange_t *fog_change)
{
    q2pro_fog_bits_t bits = MSG_ReadByte();

    if (bits & Q2PRO_FOG_BIT_COLOR) {
        MSG_ReadColor(fog_change->linear.color);
        fog_change->bits |= FOG_BIT_R | FOG_BIT_G | FOG_BIT_B;
    }
    if (bits & Q2PRO_FOG_BIT_DENSITY) {
        fog_change->linear.density    = MSG_ReadWord() / 65535.0f;
        fog_change->linear.sky_factor = MSG_ReadWord() / 65535.0f;
        fog_change->bits |= FOG_BIT_DENSITY;
    }

    if (bits & Q2PRO_FOG_BIT_HEIGHT_DENSITY) {
        fog_change->height.density = MSG_ReadWord() / 65535.0f;
        fog_change->bits |= FOG_BIT_HEIGHTFOG_DENSITY;
    }
    if (bits & Q2PRO_FOG_BIT_HEIGHT_FALLOFF) {
        fog_change->height.falloff = MSG_ReadWord() / 65535.0f;
        fog_change->bits |= FOG_BIT_HEIGHTFOG_FALLOFF;
    }

    if (bits & Q2PRO_FOG_BIT_HEIGHT_START_COLOR) {
        MSG_ReadColor(fog_change->height.start.color);
        fog_change->bits |= FOG_BIT_HEIGHTFOG_START_R | FOG_BIT_HEIGHTFOG_START_G | FOG_BIT_HEIGHTFOG_START_B;
    }
    if (bits & Q2PRO_FOG_BIT_HEIGHT_END_COLOR) {
        MSG_ReadColor(fog_change->height.end.color);
        fog_change->bits |= FOG_BIT_HEIGHTFOG_END_R | FOG_BIT_HEIGHTFOG_END_G | FOG_BIT_HEIGHTFOG_END_B;
    }

    if (bits & Q2PRO_FOG_BIT_HEIGHT_START_DIST) {
        fog_change->height.start.dist = MSG_ReadExtCoord();
        fog_change->bits |= FOG_BIT_HEIGHTFOG_START_DIST;
    }
    if (bits & Q2PRO_FOG_BIT_HEIGHT_END_DIST) {
        fog_change->height.end.dist = MSG_ReadExtCoord();
        fog_change->bits |= FOG_BIT_HEIGHTFOG_END_DIST;
    }
}

#if USE_CLIENT

/*
===================
MSG_ParseDeltaPlayerstate_Default
===================
*/
void MSG_ParseDeltaPlayerstate_Default(const player_state_t *from,
                                       player_state_t       *to,
                                       player_fogchange_t   *fog_change,
                                       int                  flags,
                                       msgPsFlags_t         psflags)
{
    Q_assert(to);

    // clear to old value before delta parsing
    if (!from) {
        memset(to, 0, sizeof(*to));
    } else if (to != from) {
        memcpy(to, from, sizeof(*to));
    }

    //
    // parse the pmove_state_t
    //
    if (flags & PS_M_TYPE) {
        if (flags & MSG_PS_RERELEASE)
            to->pmove.pm_type = MSG_ReadByte();
        else
            to->pmove.pm_type = pmtype_from_game3(MSG_ReadByte());
    }

    if (flags & PS_M_ORIGIN) {
        MSG_ReadPosP(to->pmove.origin, psflags);
    }

    if (flags & PS_M_VELOCITY) {
        MSG_ReadPosP(to->pmove.velocity, psflags);
    }

    if (flags & PS_M_TIME) {
        if (psflags & (MSG_PS_RERELEASE | MSG_PS_EXTENSIONS_2))
            to->pmove.pm_time = MSG_ReadShort();
        else
            to->pmove.pm_time = MSG_ReadByte();
    }

    if (flags & PS_M_FLAGS) {
        if (psflags & MSG_PS_RERELEASE)
            to->pmove.pm_flags = MSG_ReadShort();
        else if (psflags & MSG_PS_EXTENSIONS_2)
            to->pmove.pm_flags = pmflags_from_game3(MSG_ReadShort(), true);
        else
            to->pmove.pm_flags = pmflags_from_game3(MSG_ReadByte(), false);
    }

    if (flags & PS_M_GRAVITY)
        to->pmove.gravity = MSG_ReadShort();

    if (flags & PS_M_DELTA_ANGLES) {
        to->pmove.delta_angles[0] = MSG_ReadAngle16();
        to->pmove.delta_angles[1] = MSG_ReadAngle16();
        to->pmove.delta_angles[2] = MSG_ReadAngle16();
    }

    //
    // parse the rest of the player_state_t
    //
    if (flags & PS_VIEWOFFSET) {
        if (psflags & MSG_PS_EXTENSIONS) {
            to->viewoffset[0] = MSG_ReadShort() / 16.f;
            to->viewoffset[1] = MSG_ReadShort() / 16.f;
            to->viewoffset[2] = MSG_ReadShort() / 16.f;
        } else {
            to->viewoffset[0] = MSG_ReadChar() * 0.25f;
            to->viewoffset[1] = MSG_ReadChar() * 0.25f;
            to->viewoffset[2] = MSG_ReadChar() * 0.25f;
        }
    }

    if (flags & PS_VIEWANGLES) {
        to->viewangles[0] = MSG_ReadAngle16();
        to->viewangles[1] = MSG_ReadAngle16();
        to->viewangles[2] = MSG_ReadAngle16();
    }

    if (flags & PS_KICKANGLES) {
        if (psflags & MSG_PS_EXTENSIONS) {
            to->kick_angles[0] = MSG_ReadShort() / 1024.f;
            to->kick_angles[1] = MSG_ReadShort() / 1024.f;
            to->kick_angles[2] = MSG_ReadShort() / 1024.f;
        } else {
            to->kick_angles[0] = MSG_ReadChar() * 0.25f;
            to->kick_angles[1] = MSG_ReadChar() * 0.25f;
            to->kick_angles[2] = MSG_ReadChar() * 0.25f;
        }
    }

    if (flags & PS_WEAPONINDEX) {
        if (psflags & MSG_PS_EXTENSIONS)
            to->gunindex = MSG_ReadWord();
        else
            to->gunindex = MSG_ReadByte();
        to->gunskin = to->gunindex >> GUNINDEX_BITS;
        to->gunindex &= GUNINDEX_MASK;
    }

    if (flags & PS_WEAPONFRAME) {
        if (psflags & MSG_PS_EXTENSIONS) {
            to->gunframe = MSG_ReadWord();
            to->gunoffset[0] = SHORT2COORD(MSG_ReadShort());
            to->gunoffset[1] = SHORT2COORD(MSG_ReadShort());
            to->gunoffset[2] = SHORT2COORD(MSG_ReadShort());
            to->gunangles[0] = MSG_ReadAngle16();
            to->gunangles[1] = MSG_ReadAngle16();
            to->gunangles[2] = MSG_ReadAngle16();
        } else {
            to->gunframe = MSG_ReadByte();
            to->gunoffset[0] = MSG_ReadChar() * 0.25f;
            to->gunoffset[1] = MSG_ReadChar() * 0.25f;
            to->gunoffset[2] = MSG_ReadChar() * 0.25f;
            to->gunangles[0] = MSG_ReadChar() * 0.25f;
            to->gunangles[1] = MSG_ReadChar() * 0.25f;
            to->gunangles[2] = MSG_ReadChar() * 0.25f;
        }
    }

    if (flags & PS_BLEND)
        MSG_ReadBlend(to, psflags);

    if (flags & PS_FOG)
        MSG_ReadFog(fog_change);

    if (flags & PS_FOV)
        to->fov = MSG_ReadByte();

    if (flags & PS_RDFLAGS)
        to->rdflags = MSG_ReadByte();

    // parse stats
    MSG_ReadStats(to, psflags);
}

/*
===================
MSG_ParseDeltaPlayerstate_Default
===================
*/
void MSG_ParseDeltaPlayerstate_Enhanced(const player_state_t    *from,
                                        player_state_t          *to,
                                        player_fogchange_t   *fog_change,
                                        int                     flags,
                                        int                     extraflags,
                                        msgPsFlags_t            psflags)
{
    Q_assert(to);

    // clear to old value before delta parsing
    if (!from) {
        memset(to, 0, sizeof(*to));
    } else if (to != from) {
        memcpy(to, from, sizeof(*to));
    }

    //
    // parse the pmove_state_t
    //
    if (flags & PS_M_TYPE) {
        if (psflags & MSG_PS_RERELEASE)
            to->pmove.pm_type = MSG_ReadByte();
        else
            to->pmove.pm_type = pmtype_from_game3(MSG_ReadByte());
    }

    if (flags & PS_M_ORIGIN) {
        MSG_ReadCoordP(&to->pmove.origin[0], psflags);
        MSG_ReadCoordP(&to->pmove.origin[1], psflags);
    }

    if (extraflags & EPS_M_ORIGIN2)
        MSG_ReadCoordP(&to->pmove.origin[2], psflags);

    if (flags & PS_M_VELOCITY) {
        MSG_ReadCoordP(&to->pmove.velocity[0], psflags);
        MSG_ReadCoordP(&to->pmove.velocity[1], psflags);
    }

    if (extraflags & EPS_M_VELOCITY2)
        MSG_ReadCoordP(&to->pmove.velocity[2], psflags);

    if (flags & PS_M_TIME) {
        if (psflags & (MSG_PS_RERELEASE | MSG_PS_EXTENSIONS_2))
            to->pmove.pm_time = MSG_ReadShort();
        else
            to->pmove.pm_time = MSG_ReadByte();
    }

    if (flags & PS_M_FLAGS) {
        if (psflags & MSG_PS_RERELEASE)
            to->pmove.pm_flags = MSG_ReadShort();
        else if (psflags & MSG_PS_EXTENSIONS_2)
            to->pmove.pm_flags = pmflags_from_game3(MSG_ReadShort(), true);
        else
            to->pmove.pm_flags = pmflags_from_game3(MSG_ReadByte(), false);
    }

    if (flags & PS_M_GRAVITY)
        to->pmove.gravity = MSG_ReadShort();

    if (flags & PS_M_DELTA_ANGLES) {
        to->pmove.delta_angles[0] = MSG_ReadAngle16();
        to->pmove.delta_angles[1] = MSG_ReadAngle16();
        to->pmove.delta_angles[2] = MSG_ReadAngle16();
    }

    //
    // parse the rest of the player_state_t
    //
    if (flags & PS_VIEWOFFSET) {
        if (psflags & MSG_PS_RERELEASE) {
            to->viewoffset[0] = MSG_ReadShort() / 16.f;
            to->viewoffset[1] = MSG_ReadShort() / 16.f;
            to->viewoffset[2] = MSG_ReadShort() / 16.f;
        } else {
            to->viewoffset[0] = MSG_ReadChar() * 0.25f;
            to->viewoffset[1] = MSG_ReadChar() * 0.25f;
            to->viewoffset[2] = MSG_ReadChar() * 0.25f;
        }
    }

    if (flags & PS_VIEWANGLES) {
        to->viewangles[0] = MSG_ReadAngle16();
        to->viewangles[1] = MSG_ReadAngle16();
    }

    if (extraflags & EPS_VIEWANGLE2)
        to->viewangles[2] = MSG_ReadAngle16();

    if (flags & PS_KICKANGLES) {
        if (psflags & MSG_PS_RERELEASE) {
            to->kick_angles[0] = MSG_ReadShort() / 1024.f;
            to->kick_angles[1] = MSG_ReadShort() / 1024.f;
            to->kick_angles[2] = MSG_ReadShort() / 1024.f;
        } else {
            to->kick_angles[0] = MSG_ReadChar() * 0.25f;
            to->kick_angles[1] = MSG_ReadChar() * 0.25f;
            to->kick_angles[2] = MSG_ReadChar() * 0.25f;
        }
    }

    if (flags & PS_WEAPONINDEX) {
        if (psflags & MSG_PS_EXTENSIONS)
            to->gunindex = MSG_ReadWord();
        else
            to->gunindex = MSG_ReadByte();
        to->gunskin = to->gunindex >> GUNINDEX_BITS;
        to->gunindex &= GUNINDEX_MASK;
    }

    if (flags & PS_WEAPONFRAME) {
        if (psflags & MSG_PS_RERELEASE)
            to->gunframe = MSG_ReadWord();
        else
            to->gunframe = MSG_ReadByte();
    }

    if (extraflags & EPS_GUNOFFSET) {
        if (psflags & MSG_PS_RERELEASE) {
            to->gunoffset[0] = MSG_ReadShort() / 512.f;
            to->gunoffset[1] = MSG_ReadShort() / 512.f;
            to->gunoffset[2] = MSG_ReadShort() / 512.f;
        } else {
            to->gunoffset[0] = MSG_ReadChar() * 0.25f;
            to->gunoffset[1] = MSG_ReadChar() * 0.25f;
            to->gunoffset[2] = MSG_ReadChar() * 0.25f;
        }
    }

    if (extraflags & EPS_GUNANGLES) {
        if (psflags & MSG_PS_RERELEASE) {
            to->gunangles[0] = MSG_ReadShort() / 4096.f;
            to->gunangles[1] = MSG_ReadShort() / 4096.f;
            to->gunangles[2] = MSG_ReadShort() / 4096.f;
        } else {
            to->gunangles[0] = MSG_ReadChar() * 0.25f;
            to->gunangles[1] = MSG_ReadChar() * 0.25f;
            to->gunangles[2] = MSG_ReadChar() * 0.25f;
        }
    }

    if (flags & PS_BLEND)
        MSG_ReadBlend(to, psflags);

    if (flags & PS_FOG)
        MSG_ReadFog(fog_change);

    if (flags & PS_FOV)
        to->fov = MSG_ReadByte();

    if (flags & PS_RDFLAGS)
        to->rdflags = MSG_ReadByte();

    // parse stats
    if (extraflags & EPS_STATS)
        MSG_ReadStats(to, psflags);

// KEX
    if (extraflags & EPS_GUNRATE) {
        to->gunrate = MSG_ReadByte();
    }

    if (psflags & MSG_PS_RERELEASE && flags & PS_RR_VIEWHEIGHT) {
        to->pmove.viewheight = MSG_ReadByte();
    }
// KEX
}

#endif // USE_CLIENT

#if USE_MVD_CLIENT

/*
===================
MSG_ParseDeltaPlayerstate_Packet
===================
*/
void MSG_ParseDeltaPlayerstate_Packet(player_state_t        *to,
                                      player_fogchange_t   *fog_change,
                                      int                   flags,
                                      msgPsFlags_t          psflags)
{
    Q_assert(to);

    //
    // parse the pmove_state_t
    //
    if (flags & PPS_M_TYPE) {
        if (psflags & MSG_PS_RERELEASE)
            to->pmove.pm_type = MSG_ReadByte();
        else
            to->pmove.pm_type = pmtype_from_game3(MSG_ReadByte());
    }

    if (flags & PPS_M_ORIGIN) {
        MSG_ReadCoordP(&to->pmove.origin[0], psflags);
        MSG_ReadCoordP(&to->pmove.origin[1], psflags);
    }

    if (flags & PPS_M_ORIGIN2)
        MSG_ReadCoordP(&to->pmove.origin[2], psflags);

    //
    // parse the rest of the player_state_t
    //
    if (flags & PPS_VIEWOFFSET) {
        if (psflags & MSG_PS_RERELEASE) {
            to->viewoffset[0] = MSG_ReadShort() / 16.f;
            to->viewoffset[1] = MSG_ReadShort() / 16.f;
            to->viewoffset[2] = MSG_ReadShort() / 16.f;
        } else {
            to->viewoffset[0] = MSG_ReadChar() * 0.25f;
            to->viewoffset[1] = MSG_ReadChar() * 0.25f;
            to->viewoffset[2] = MSG_ReadChar() * 0.25f;
        }
    }

    if (flags & PPS_VIEWANGLES) {
        to->viewangles[0] = MSG_ReadAngle16();
        to->viewangles[1] = MSG_ReadAngle16();
    }

    if (flags & PPS_VIEWANGLE2)
        to->viewangles[2] = MSG_ReadAngle16();

    if (flags & PPS_KICKANGLES) {
        if (psflags & MSG_PS_RERELEASE) {
            to->kick_angles[0] = MSG_ReadShort() / 1024.f;
            to->kick_angles[1] = MSG_ReadShort() / 1024.f;
            to->kick_angles[2] = MSG_ReadShort() / 1024.f;
        } else {
            to->kick_angles[0] = MSG_ReadChar() * 0.25f;
            to->kick_angles[1] = MSG_ReadChar() * 0.25f;
            to->kick_angles[2] = MSG_ReadChar() * 0.25f;
        }
    }

    if (flags & PPS_WEAPONINDEX) {
        if (psflags & MSG_PS_EXTENSIONS)
            to->gunindex = MSG_ReadWord();
        else
            to->gunindex = MSG_ReadByte();
        to->gunskin = to->gunindex >> GUNINDEX_BITS;
        to->gunindex &= GUNINDEX_MASK;
    }

    if (flags & PPS_WEAPONFRAME) {
        if (psflags & MSG_PS_RERELEASE)
            to->gunframe = MSG_ReadWord();
        else
            to->gunframe = MSG_ReadByte();
    }

    if (flags & PPS_GUNOFFSET) {
        if (psflags & MSG_PS_RERELEASE) {
            to->gunoffset[0] = MSG_ReadShort() / 512.f;
            to->gunoffset[1] = MSG_ReadShort() / 512.f;
            to->gunoffset[2] = MSG_ReadShort() / 512.f;
        } else if (psflags & MSG_PS_EXTENSIONS) {
            to->gunoffset[0] = SHORT2COORD(MSG_ReadShort());
            to->gunoffset[1] = SHORT2COORD(MSG_ReadShort());
            to->gunoffset[2] = SHORT2COORD(MSG_ReadShort());
        } else {
            to->gunoffset[0] = MSG_ReadChar() * 0.25f;
            to->gunoffset[1] = MSG_ReadChar() * 0.25f;
            to->gunoffset[2] = MSG_ReadChar() * 0.25f;
        }
    }

    if (flags & PPS_GUNANGLES) {
        if (psflags & MSG_PS_RERELEASE) {
            to->gunangles[0] = MSG_ReadShort() / 4096.f;
            to->gunangles[1] = MSG_ReadShort() / 4096.f;
            to->gunangles[2] = MSG_ReadShort() / 4096.f;
        } else if (psflags & MSG_PS_EXTENSIONS) {
            to->gunangles[0] = MSG_ReadAngle16();
            to->gunangles[1] = MSG_ReadAngle16();
            to->gunangles[2] = MSG_ReadAngle16();
        } else {
            to->gunangles[0] = MSG_ReadChar() * 0.25f;
            to->gunangles[1] = MSG_ReadChar() * 0.25f;
            to->gunangles[2] = MSG_ReadChar() * 0.25f;
        }
    }

    if (flags & PPS_BLEND)
        MSG_ReadBlend(to, psflags);

    if (flags & PPS_FOG)
        MSG_ReadFog(fog_change);

    if (flags & PPS_FOV)
        to->fov = MSG_ReadByte();

    if (flags & PPS_RDFLAGS)
        to->rdflags = MSG_ReadByte();

    // parse stats
    if (flags & PPS_STATS)
        MSG_ReadStats(to, psflags);
}

#endif // USE_MVD_CLIENT


/*
==============================================================================

            DEBUGGING STUFF

==============================================================================
*/

#if USE_DEBUG

#define SHOWBITS(x) Com_LPrintf(PRINT_DEVELOPER, x " ")

#if USE_CLIENT

void MSG_ShowDeltaPlayerstateBits_Default(int flags)
{
#define S(b,s) if(flags&PS_##b) SHOWBITS(s)
    S(M_TYPE,           "pmove.pm_type");
    S(M_ORIGIN,         "pmove.origin");
    S(M_VELOCITY,       "pmove.velocity");
    S(M_TIME,           "pmove.pm_time");
    S(M_FLAGS,          "pmove.pm_flags");
    S(M_GRAVITY,        "pmove.gravity");
    S(M_DELTA_ANGLES,   "pmove.delta_angles");
    S(VIEWOFFSET,       "viewoffset");
    S(VIEWANGLES,       "viewangles");
    S(KICKANGLES,       "kick_angles");
    S(WEAPONINDEX,      "gunindex");
    S(WEAPONFRAME,      "gunframe");
    S(BLEND,            "blend");
    S(FOG,              "fog");
    S(FOV,              "fov");
    S(RDFLAGS,          "rdflags");
#undef S
}

void MSG_ShowDeltaPlayerstateBits_Enhanced(int flags, int extraflags)
{
#define SP(b,s) if(flags&PS_##b) SHOWBITS(s)
#define SE(b,s) if(extraflags&EPS_##b) SHOWBITS(s)
    SP(M_TYPE,          "pmove.pm_type");
    SP(M_ORIGIN,        "pmove.origin[0,1]");
    SE(M_ORIGIN2,       "pmove.origin[2]");
    SP(M_VELOCITY,      "pmove.velocity[0,1]");
    SE(M_VELOCITY2,     "pmove.velocity[2]");
    SP(M_TIME,          "pmove.pm_time");
    SP(M_FLAGS,         "pmove.pm_flags");
    SP(M_GRAVITY,       "pmove.gravity");
    SP(M_DELTA_ANGLES,  "pmove.delta_angles");
    SP(VIEWOFFSET,      "viewoffset");
    SP(VIEWANGLES,      "viewangles[0,1]");
    SE(VIEWANGLE2,      "viewangles[2]");
    SP(KICKANGLES,      "kick_angles");
    SP(WEAPONINDEX,     "gunindex");
    SP(WEAPONFRAME,     "gunframe");
    SE(GUNOFFSET,       "gunoffset");
    SE(GUNANGLES,       "gunangles");
    SP(BLEND,           "blend");
    SP(FOG,             "fog");
    SP(FOV,             "fov");
    SP(RDFLAGS,         "rdflags");
    SE(STATS,           "stats");
#undef SP
#undef SE
}

void MSG_ShowDeltaUsercmdBits_Enhanced(int bits)
{
    if (!bits) {
        SHOWBITS("<none>");
        return;
    }

#define S(b,s) if(bits&CM_##b) SHOWBITS(s)
    S(ANGLE1,   "angle1");
    S(ANGLE2,   "angle2");
    S(ANGLE3,   "angle3");
    S(FORWARD,  "forward");
    S(SIDE,     "side");
    S(UP,       "up");
    S(BUTTONS,  "buttons");
    S(IMPULSE,  "msec");
#undef S
}

#endif // USE_CLIENT

#if USE_CLIENT || USE_MVD_CLIENT

void MSG_ShowDeltaEntityBits(uint64_t bits)
{
#define S(b,s) if(bits&U_##b) SHOWBITS(s)
    S(MODEL, "modelindex");
    S(MODEL2, "modelindex2");
    S(MODEL3, "modelindex3");
    S(MODEL4, "modelindex4");

    if (bits & U_FRAME8)
        SHOWBITS("frame8");
    if (bits & U_FRAME16)
        SHOWBITS("frame16");

    if ((bits & U_SKIN32) == U_SKIN32)
        SHOWBITS("skinnum32");
    else if (bits & U_SKIN8)
        SHOWBITS("skinnum8");
    else if (bits & U_SKIN16)
        SHOWBITS("skinnum16");

    if ((bits & U_EFFECTS32) == U_EFFECTS32)
        SHOWBITS("effects32");
    else if (bits & U_EFFECTS8)
        SHOWBITS("effects8");
    else if (bits & U_EFFECTS16)
        SHOWBITS("effects16");

    if ((bits & U_RENDERFX32) == U_RENDERFX32)
        SHOWBITS("renderfx32");
    else if (bits & U_RENDERFX8)
        SHOWBITS("renderfx8");
    else if (bits & U_RENDERFX16)
        SHOWBITS("renderfx16");

    S(ORIGIN1, "origin[0]");
    S(ORIGIN2, "origin[1]");
    S(ORIGIN3, "origin[2]");
    S(ANGLE1, "angles[0]");
    S(ANGLE2, "angles[1]");
    S(ANGLE3, "angles[2]");
    S(OLDORIGIN, "old_origin");
    S(SOUND, "sound");
    S(EVENT, "event");
    S(SOLID, "solid");

    if ((bits & U_MOREFX32) == U_MOREFX32)
        SHOWBITS("morefx32");
    else if (bits & U_MOREFX8)
        SHOWBITS("morefx8");
    else if (bits & U_MOREFX16)
        SHOWBITS("morefx16");

    S(ALPHA, "alpha");
    S(SCALE, "scale");
#undef S
}

void MSG_ShowDeltaPlayerstateBits_Packet(int flags)
{
#define S(b,s) if(flags&PPS_##b) SHOWBITS(s)
    S(M_TYPE,       "pmove.pm_type");
    S(M_ORIGIN,     "pmove.origin[0,1]");
    S(M_ORIGIN2,    "pmove.origin[2]");
    S(VIEWOFFSET,   "viewoffset");
    S(VIEWANGLES,   "viewangles[0,1]");
    S(VIEWANGLE2,   "viewangles[2]");
    S(KICKANGLES,   "kick_angles");
    S(WEAPONINDEX,  "gunindex");
    S(WEAPONFRAME,  "gunframe");
    S(GUNOFFSET,    "gunoffset");
    S(GUNANGLES,    "gunangles");
    S(BLEND,        "blend");
    S(FOG,          "fog");
    S(FOV,          "fov");
    S(RDFLAGS,      "rdflags");
    S(STATS,        "stats");
#undef S
}

const char *MSG_ServerCommandString(int cmd, int version)
{
    switch (cmd) {
    case -1: return "END OF MESSAGE";
    default: break;
#define S(x) case svc_##x: return "svc_" #x;
        S(bad)
        S(muzzleflash)
        S(muzzleflash2)
        S(temp_entity)
        S(layout)
        S(inventory)
        S(nop)
        S(disconnect)
        S(reconnect)
        S(sound)
        S(print)
        S(stufftext)
        S(serverdata)
        S(configstring)
        S(spawnbaseline)
        S(centerprint)
        S(download)
        S(playerinfo)
        S(packetentities)
        S(deltapacketentities)
        S(frame)
    }
    if (version == PROTOCOL_VERSION_RERELEASE) {
        switch (cmd) {
        default: break;
            S(rr_zpacket)
            S(rr_zdownload)
            S(rr_gamestate)
            S(rr_setting)
            S(rr_configstringstream)
            S(rr_baselinestream)
            S(fog)
        }
    } else {
        switch (cmd) {
        default: break;
            S(q2pro_zpacket)
            S(q2pro_zdownload)
            S(q2pro_gamestate)
            S(q2pro_setting)
            S(q2pro_configstringstream)
            S(q2pro_baselinestream)
        }
    }
#undef S

    return "UNKNOWN COMMAND";
}

#endif // USE_CLIENT || USE_MVD_CLIENT

#endif // USE_DEBUG
