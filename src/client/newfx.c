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
// cl_newfx.c -- MORE entity effects parsing and management

#include "client.h"

void CL_Flashlight(int ent, const vec3_t pos)
{
    cdlight_t   *dl;

    dl = CL_AllocDlight(ent);
    VectorCopy(pos, dl->origin);
    dl->radius = 400;
    dl->die = cl.time + 100;
    VectorSet(dl->color, 1, 1, 1);
}

/*
======
CL_ColorFlash - flash of light
======
*/
void CL_ColorFlash(const vec3_t pos, int ent, int intensity, float r, float g, float b)
{
    cdlight_t   *dl;

    dl = CL_AllocDlight(ent);
    VectorCopy(pos, dl->origin);
    dl->radius = intensity;
    dl->die = cl.time + 100;
    VectorSet(dl->color, r, g, b);
}

/*
======
CL_DebugTrail
======
*/
void CL_DebugTrail(const vec3_t start, const vec3_t end)
{
    vec3_t      move;
    vec3_t      vec;
    float       len;
    cparticle_t *p;
    float       dec;

    VectorCopy(start, move);
    VectorSubtract(end, start, vec);
    len = VectorNormalize(vec);

    dec = 3;
    VectorScale(vec, dec, vec);
    VectorCopy(start, move);

    while (len > 0) {
        len -= dec;

        p = CL_AllocParticle();
        if (!p)
            return;

        p->time = cl.time;
        VectorClear(p->accel);
        VectorClear(p->vel);
        p->alpha = 1.0f;
        p->alphavel = -0.1f;
        p->color = 0x74 + (Q_rand() & 7);
        VectorCopy(move, p->org);
        VectorAdd(move, vec, move);
    }
}

void CL_ForceWall(const vec3_t start, const vec3_t end, int color)
{
    vec3_t      move;
    vec3_t      vec;
    float       len;
    int         j;
    cparticle_t *p;

    VectorCopy(start, move);
    VectorSubtract(end, start, vec);
    len = VectorNormalize(vec);

    VectorScale(vec, 4, vec);

    // FIXME: this is a really silly way to have a loop
    while (len > 0) {
        len -= 4;

        if (frand() > 0.3f) {
            p = CL_AllocParticle();
            if (!p)
                return;
            VectorClear(p->accel);

            p->time = cl.time;

            p->alpha = 1.0f;
            p->alphavel =  -1.0f / (3.0f + frand() * 0.5f);
            p->color = color;
            for (j = 0; j < 3; j++)
                p->org[j] = move[j] + crand() * 3;
            p->vel[0] = 0;
            p->vel[1] = 0;
            p->vel[2] = -40 - (crand() * 10);
        }

        VectorAdd(move, vec, move);
    }
}


/*
===============
CL_BubbleTrail2 (lets you control the # of bubbles by setting the distance between the spawns)

===============
*/
void CL_BubbleTrail2(const vec3_t start, const vec3_t end, int dist)
{
    vec3_t      move;
    vec3_t      vec;
    float       len;
    int         i, j;
    cparticle_t *p;
    float       dec;

    VectorCopy(start, move);
    VectorSubtract(end, start, vec);
    len = VectorNormalize(vec);

    dec = dist;
    VectorScale(vec, dec, vec);

    for (i = 0; i < len; i += dec) {
        p = CL_AllocParticle();
        if (!p)
            return;

        VectorClear(p->accel);
        p->time = cl.time;

        p->alpha = 1.0f;
        p->alphavel = -1.0f / (1 + frand() * 0.1f);
        p->color = 4 + (Q_rand() & 7);
        for (j = 0; j < 3; j++) {
            p->org[j] = move[j] + crand() * 2;
            p->vel[j] = crand() * 10;
        }
        p->org[2] -= 4;
        p->vel[2] += 20;

        VectorAdd(move, vec, move);
    }
}

void CL_Heatbeam(const vec3_t start, const vec3_t forward)
{
    vec3_t      move;
    vec3_t      vec;
    float       len;
    int         j;
    cparticle_t *p;
    int         i;
    float       c, s;
    vec3_t      dir;
    float       ltime;
    float       step = 32.0f, rstep;
    float       start_pt;
    float       rot;
    float       variance;

    VectorCopy(start, move);
    len = VectorNormalize2(forward, vec);

    ltime = cl.time * 0.001f;
    start_pt = fmodf(ltime * 96.0f, step);
    VectorMA(move, start_pt, vec, move);

    VectorScale(vec, step, vec);

    rstep = M_PIf / 10.0f;
    for (i = start_pt; i < len; i += step) {
        for (rot = 0; rot < M_PIf * 2; rot += rstep) {
            p = CL_AllocParticle();
            if (!p)
                return;

            p->time = cl.time;
            VectorClear(p->accel);
            variance = 0.5f;
            c = cosf(rot) * variance;
            s = sinf(rot) * variance;

            // trim it so it looks like it's starting at the origin
            if (i < 10) {
                VectorScale(cl.v_right, c * (i / 10.0f), dir);
                VectorMA(dir, s * (i / 10.0f), cl.v_up, dir);
            } else {
                VectorScale(cl.v_right, c, dir);
                VectorMA(dir, s, cl.v_up, dir);
            }

            p->alpha = 0.5f;
            p->alphavel = -1000.0f;
            p->color = 223 - (Q_rand() & 7);
            for (j = 0; j < 3; j++) {
                p->org[j] = move[j] + dir[j] * 3;
                p->vel[j] = 0;
            }
        }

        VectorAdd(move, vec, move);
    }
}


/*
===============
CL_ParticleSteamEffect

Puffs with velocity along direction, with some randomness thrown in
===============
*/
void CL_ParticleSteamEffect(const vec3_t org, const vec3_t dir, int color, int count, int magnitude)
{
    int         i, j;
    cparticle_t *p;
    float       d;
    vec3_t      r, u;

    MakeNormalVectors(dir, r, u);

    for (i = 0; i < count; i++) {
        p = CL_AllocParticle();
        if (!p)
            return;

        p->time = cl.time;
        p->color = color + (Q_rand() & 7);

        for (j = 0; j < 3; j++)
            p->org[j] = org[j] + magnitude * 0.1f * crand();

        VectorScale(dir, magnitude, p->vel);
        d = crand() * magnitude / 3;
        VectorMA(p->vel, d, r, p->vel);
        d = crand() * magnitude / 3;
        VectorMA(p->vel, d, u, p->vel);

        p->accel[0] = p->accel[1] = 0;
        p->accel[2] = -PARTICLE_GRAVITY / 2;
        p->alpha = 1.0f;

        p->alphavel = -1.0f / (0.5f + frand() * 0.3f);
    }
}

void CL_ParticleSteamEffect2(cl_sustain_t *self)
{
    CL_ParticleSteamEffect(self->org, self->dir, self->color, self->count, self->magnitude);
    self->nextthink += 100;
}

/*
===============
CL_TrackerTrail
===============
*/
void CL_TrackerTrail(centity_t *ent, const vec3_t end)
{
    vec3_t      move;
    vec3_t      vec;
    vec3_t      forward, up, angle_dir;
    int         i, count, sign;
    cparticle_t *p;
    const int   dec = 3;
    float       dist;

    VectorSubtract(end, ent->lerp_origin, vec);
    count = VectorNormalize(vec) / dec;
    if (!count)
        return;

    VectorCopy(vec, forward);
    vectoangles2(forward, angle_dir);
    AngleVectors(angle_dir, NULL, NULL, up);

    VectorCopy(ent->lerp_origin, move);
    VectorScale(vec, dec, vec);

    sign = ent->trailcount;
    for (i = 0; i < count; i++) {
        p = CL_AllocParticle();
        if (!p)
            break;
        VectorClear(p->accel);

        p->time = cl.time;

        p->alpha = 1.0f;
        p->alphavel = -2.0f;
        p->color = 0;
        dist = 8 * cosf(DotProduct(move, forward) * M_PIf / 64);
        if (sign & 1)
            dist = -dist;
        VectorMA(move, dist, up, p->org);
        VectorSet(p->vel, 0, 0, 5);

        VectorAdd(move, vec, move);
        sign ^= 1;
    }

    ent->trailcount = sign;
    VectorCopy(move, ent->lerp_origin);
}

// Marsaglia 1972 rejection method
static void RandomDir(vec3_t dir)
{
    float x, y, s, a;

    do {
        x = crand();
        y = crand();
        s = x * x + y * y;
    } while (s > 1);

    a = 2 * sqrtf(1 - s);
    dir[0] = x * a;
    dir[1] = y * a;
    dir[2] = -1 + 2 * s;
}

void CL_Tracker_Shell(const centity_t *ent, const vec3_t origin)
{
    vec3_t          org, dir, mid;
    int             i, count;
    cparticle_t     *p;
    float           radius, scale;

    if (cl.csr.extended) {
        VectorAvg(ent->mins, ent->maxs, mid);
        VectorAdd(origin, mid, org);
        radius = ent->radius;
        scale = Q_clipf(ent->radius / 40.0f, 1, 2);
        count = 300 * scale;
    } else {
        VectorCopy(origin, org);
        radius = 40.0f;
        scale = 1.0f;
        count = 300;
    }

    for (i = 0; i < count; i++) {
        p = CL_AllocParticle();
        if (!p)
            return;
        VectorClear(p->accel);

        p->time = cl.time;

        p->alpha = 1.0f;
        p->alphavel = INSTANT_PARTICLE;
        p->color = 0;
        p->scale = scale;

        RandomDir(dir);
        VectorMA(org, radius, dir, p->org);
    }
}

void CL_MonsterPlasma_Shell(const vec3_t origin)
{
    vec3_t          dir;
    int             i;
    cparticle_t     *p;

    for (i = 0; i < 40; i++) {
        p = CL_AllocParticle();
        if (!p)
            return;
        VectorClear(p->accel);

        p->time = cl.time;

        p->alpha = 1.0f;
        p->alphavel = INSTANT_PARTICLE;
        p->color = 0xe0;

        RandomDir(dir);
        VectorMA(origin, 10, dir, p->org);
    }
}

void CL_Widowbeamout(cl_sustain_t *self)
{
    static const byte   colortable[4] = {2 * 8, 13 * 8, 21 * 8, 18 * 8};
    vec3_t          dir;
    int             i;
    cparticle_t     *p;
    float           ratio;

    ratio = 1.0f - (self->endtime - cl.time) / 2100.0f;

    for (i = 0; i < 300; i++) {
        p = CL_AllocParticle();
        if (!p)
            return;
        VectorClear(p->accel);

        p->time = cl.time;

        p->alpha = 1.0f;
        p->alphavel = INSTANT_PARTICLE;
        p->color = colortable[Q_rand() & 3];

        RandomDir(dir);
        VectorMA(self->org, (45.0f * ratio), dir, p->org);
    }
}

void CL_Nukeblast(cl_sustain_t *self)
{
    static const byte   colortable[4] = {110, 112, 114, 116};
    vec3_t          dir;
    int             i;
    cparticle_t     *p;
    float           ratio;

    ratio = 1.0f - (self->endtime - cl.time) / 1000.0f;

    for (i = 0; i < 700; i++) {
        p = CL_AllocParticle();
        if (!p)
            return;
        VectorClear(p->accel);

        p->time = cl.time;

        p->alpha = 1.0f;
        p->alphavel = INSTANT_PARTICLE;
        p->color = colortable[Q_rand() & 3];

        RandomDir(dir);
        VectorMA(self->org, (200.0f * ratio), dir, p->org);
    }
}

void CL_WidowSplash(void)
{
    static const byte   colortable[4] = {2 * 8, 13 * 8, 21 * 8, 18 * 8};
    int         i;
    cparticle_t *p;
    vec3_t      dir;

    for (i = 0; i < 256; i++) {
        p = CL_AllocParticle();
        if (!p)
            return;

        p->time = cl.time;
        p->color = colortable[Q_rand() & 3];

        RandomDir(dir);
        VectorMA(te.pos1, 45.0f, dir, p->org);
        VectorScale(dir, 40.0f, p->vel);

        VectorClear(p->accel);
        p->alpha = 1.0f;

        p->alphavel = -0.8f / (0.5f + frand() * 0.3f);
    }
}

/*
===============
CL_TagTrail

===============
*/
void CL_TagTrail(centity_t *ent, const vec3_t end, int color)
{
    vec3_t      move;
    vec3_t      vec;
    int         i, j, count;
    cparticle_t *p;
    const int   dec = 5;

    VectorSubtract(end, ent->lerp_origin, vec);
    count = VectorNormalize(vec) / dec;
    if (!count)
        return;

    VectorCopy(ent->lerp_origin, move);
    VectorScale(vec, dec, vec);

    for (i = 0; i < count; i++) {
        p = CL_AllocParticle();
        if (!p)
            break;
        VectorClear(p->accel);

        p->time = cl.time;

        p->alpha = 1.0f;
        p->alphavel = -1.0f / (0.8f + frand() * 0.2f);
        p->color = color;
        for (j = 0; j < 3; j++) {
            p->org[j] = move[j] + crand() * 16;
            p->vel[j] = crand() * 5;
        }

        VectorAdd(move, vec, move);
    }

    VectorCopy(move, ent->lerp_origin);
}

/*
===============
CL_ColorExplosionParticles
===============
*/
void CL_ColorExplosionParticles(const vec3_t org, int color, int run)
{
    int         i, j;
    cparticle_t *p;

    for (i = 0; i < 128; i++) {
        p = CL_AllocParticle();
        if (!p)
            return;

        p->time = cl.time;
        p->color = color + (Q_rand() % run);

        for (j = 0; j < 3; j++) {
            p->org[j] = org[j] + ((int)(Q_rand() % 32) - 16);
            p->vel[j] = (int)(Q_rand() % 256) - 128;
        }

        p->accel[0] = p->accel[1] = 0;
        p->accel[2] = -PARTICLE_GRAVITY;
        p->alpha = 1.0f;

        p->alphavel = -0.4f / (0.6f + frand() * 0.2f);
    }
}

/*
===============
CL_ParticleSmokeEffect - like the steam effect, but unaffected by gravity
===============
*/
void CL_ParticleSmokeEffect(const vec3_t org, const vec3_t dir, int color, int count, int magnitude)
{
    int         i, j;
    cparticle_t *p;
    float       d;
    vec3_t      r, u;

    MakeNormalVectors(dir, r, u);

    for (i = 0; i < count; i++) {
        p = CL_AllocParticle();
        if (!p)
            return;

        p->time = cl.time;
        p->color = color + (Q_rand() & 7);

        for (j = 0; j < 3; j++)
            p->org[j] = org[j] + magnitude * 0.1f * crand();

        VectorScale(dir, magnitude, p->vel);
        d = crand() * magnitude / 3;
        VectorMA(p->vel, d, r, p->vel);
        d = crand() * magnitude / 3;
        VectorMA(p->vel, d, u, p->vel);

        VectorClear(p->accel);
        p->alpha = 1.0f;

        p->alphavel = -1.0f / (0.5f + frand() * 0.3f);
    }
}

/*
===============
CL_BlasterParticles2

Wall impact puffs (Green)
===============
*/
void CL_BlasterParticles2(const vec3_t org, const vec3_t dir, unsigned int color)
{
    int         i, j;
    cparticle_t *p;
    float       d;
    int         count;

    count = 40;
    for (i = 0; i < count; i++) {
        p = CL_AllocParticle();
        if (!p)
            return;

        p->time = cl.time;
        p->color = color + (Q_rand() & 7);

        d = Q_rand() & 15;
        for (j = 0; j < 3; j++) {
            p->org[j] = org[j] + ((int)(Q_rand() & 7) - 4) + d * dir[j];
            p->vel[j] = dir[j] * 30 + crand() * 40;
        }

        p->accel[0] = p->accel[1] = 0;
        p->accel[2] = -PARTICLE_GRAVITY;
        p->alpha = 1.0f;

        p->alphavel = -1.0f / (0.5f + frand() * 0.3f);
    }
}

/*
===============
CL_BlasterTrail2

Green!
===============
*/
void CL_BlasterTrail2(centity_t *ent, const vec3_t end)
{
    vec3_t      move;
    vec3_t      vec;
    int         i, j, count;
    cparticle_t *p;
    const int   dec = 5;

    VectorSubtract(end, ent->lerp_origin, vec);
    count = VectorNormalize(vec) / dec;
    if (!count)
        return;

    VectorCopy(ent->lerp_origin, move);
    VectorScale(vec, dec, vec);

    for (i = 0; i < count; i++) {
        p = CL_AllocParticle();
        if (!p)
            break;
        VectorClear(p->accel);

        p->time = cl.time;

        p->alpha = 1.0f;
        p->alphavel = -1.0f / (0.3f + frand() * 0.2f);
        p->color = 0xd0;
        for (j = 0; j < 3; j++) {
            p->org[j] = move[j] + crand();
            p->vel[j] = crand() * 5;
        }

        VectorAdd(move, vec, move);
    }

    VectorCopy(move, ent->lerp_origin);
}

/*
===============
CL_IonripperTrail
===============
*/
void CL_IonripperTrail(centity_t *ent, const vec3_t end)
{
    vec3_t      move;
    vec3_t      vec;
    cparticle_t *p;
    const int   dec = 5;
    int         i, count, sign;

    VectorSubtract(end, ent->lerp_origin, vec);
    count = VectorNormalize(vec) / dec;
    if (!count)
        return;

    VectorCopy(ent->lerp_origin, move);
    VectorScale(vec, dec, vec);

    sign = ent->trailcount;
    for (i = 0; i < count; i++) {
        p = CL_AllocParticle();
        if (!p)
            break;
        VectorClear(p->accel);

        p->time = cl.time;
        p->alpha = 0.5f;
        p->alphavel = -1.0f / (0.3f + frand() * 0.2f);
        p->color = 0xe4 + (Q_rand() & 3);

        VectorCopy(move, p->org);

        p->vel[0] = (sign & 1) ? 10 : -10;
        p->vel[1] = 0;
        p->vel[2] = 0;

        VectorAdd(move, vec, move);
        sign ^= 1;
    }

    ent->trailcount = sign;
    VectorCopy(move, ent->lerp_origin);
}

/*
===============
CL_TrapParticles
===============
*/
void CL_TrapParticles(centity_t *ent, const vec3_t origin)
{
    vec3_t      move;
    vec3_t      vec;
    vec3_t      start, end;
    float       len;
    int         j;
    cparticle_t *p;
    int         dec;

    if (cl.time - ent->fly_stoptime < 10)
        return;
    ent->fly_stoptime = cl.time;

    VectorCopy(origin, start);
    VectorCopy(origin, end);
    start[2] -= 14;
    end[2] += 50;

    VectorCopy(start, move);
    VectorSubtract(end, start, vec);
    len = VectorNormalize(vec);

    dec = 5;
    VectorScale(vec, 5, vec);

    // FIXME: this is a really silly way to have a loop
    while (len > 0) {
        len -= dec;

        p = CL_AllocParticle();
        if (!p)
            return;
        VectorClear(p->accel);

        p->time = cl.time;

        p->alpha = 1.0f;
        p->alphavel = -1.0f / (0.3f + frand() * 0.2f);
        p->color = 0xe0;
        for (j = 0; j < 3; j++) {
            p->org[j] = move[j] + crand();
            p->vel[j] = crand() * 15;
        }
        p->accel[2] = PARTICLE_GRAVITY;

        VectorAdd(move, vec, move);
    }

    {
        int         i, j, k;
        cparticle_t *p;
        float       vel;
        vec3_t      dir;
        vec3_t      org;

        VectorCopy(origin, org);

        for (i = -2; i <= 2; i += 4)
            for (j = -2; j <= 2; j += 4)
                for (k = -2; k <= 4; k += 4) {
                    p = CL_AllocParticle();
                    if (!p)
                        return;

                    p->time = cl.time;
                    p->color = 0xe0 + (Q_rand() & 3);

                    p->alpha = 1.0f;
                    p->alphavel = -1.0f / (0.3f + (Q_rand() & 7) * 0.02f);

                    p->org[0] = org[0] + i + ((Q_rand() & 23) * crand());
                    p->org[1] = org[1] + j + ((Q_rand() & 23) * crand());
                    p->org[2] = org[2] + k + ((Q_rand() & 23) * crand());

                    dir[0] = j * 8;
                    dir[1] = i * 8;
                    dir[2] = k * 8;

                    VectorNormalize(dir);
                    vel = 50 + (Q_rand() & 63);
                    VectorScale(dir, vel, p->vel);

                    p->accel[0] = p->accel[1] = 0;
                    p->accel[2] = -PARTICLE_GRAVITY;
                }
    }
}

/*
===============
CL_ParticleEffect3
===============
*/
void CL_ParticleEffect3(const vec3_t org, const vec3_t dir, int color, int count)
{
    int         i, j;
    cparticle_t *p;
    float       d;

    for (i = 0; i < count; i++) {
        p = CL_AllocParticle();
        if (!p)
            return;

        p->time = cl.time;
        p->color = color;

        d = Q_rand() & 7;
        for (j = 0; j < 3; j++) {
            p->org[j] = org[j] + ((int)(Q_rand() & 7) - 4) + d * dir[j];
            p->vel[j] = crand() * 20;
        }

        p->accel[0] = p->accel[1] = 0;
        p->accel[2] = PARTICLE_GRAVITY;
        p->alpha = 1.0f;

        p->alphavel = -1.0f / (0.5f + frand() * 0.3f);
    }
}

/*
===============
CL_BerserkSlamParticles
===============
*/
void CL_BerserkSlamParticles(const vec3_t org, const vec3_t dir)
{
    static const byte   colortable[4] = {110, 112, 114, 116};
    int         i;
    cparticle_t *p;
    float       d;
    vec3_t      r, u;

    MakeNormalVectors(dir, r, u);

    for (i = 0; i < 700; i++) {
        p = CL_AllocParticle();
        if (!p)
            return;

        p->time = cl.time;
        p->color = colortable[Q_rand() & 3];

        VectorCopy(org, p->org);

        d = frand() * 192;
        VectorScale(dir, d, p->vel);
        d = crand() * 192;
        VectorMA(p->vel, d, r, p->vel);
        d = crand() * 192;
        VectorMA(p->vel, d, u, p->vel);

        VectorClear(p->accel);
        p->alpha = 1.0f;

        p->alphavel = -1.0f / (0.5f + frand() * 0.3f);
    }
}

/*
===============
CL_PowerSplash

TODO: differentiate screen/shield
===============
*/
void CL_PowerSplash(void)
{
    static const byte   colortable[4] = {208, 209, 210, 211};
    int         i;
    cparticle_t *p;
    vec3_t      org, dir, mid;
    centity_t   *ent;

    if ((unsigned)te.entity1 >= cl.csr.max_edicts)
        Com_Error(ERR_DROP, "%s: bad entity", __func__);

    ent = &cl_entities[te.entity1];
    if (ent->serverframe != cl.frame.number)
        return;

    VectorAvg(ent->mins, ent->maxs, mid);
    VectorAdd(ent->current.origin, mid, org);

    for (i = 0; i < 256; i++) {
        p = CL_AllocParticle();
        if (!p)
            return;

        p->time = cl.time;
        p->color = colortable[Q_rand() & 3];

        RandomDir(dir);
        VectorMA(org, ent->radius, dir, p->org);
        VectorScale(dir, 40.0f, p->vel);

        VectorClear(p->accel);
        p->alpha = 1.0f;

        p->alphavel = -1.0f / (0.5f + frand() * 0.3f);
    }
}

/*
===============
CL_TeleporterParticles
===============
*/
void CL_TeleporterParticles2(const vec3_t org)
{
    int         i;
    cparticle_t *p;
    vec3_t      dir;

    for (i = 0; i < 8; i++) {
        p = CL_AllocParticle();
        if (!p)
            return;

        p->time = cl.time;
        p->color = 0xdb;

        RandomDir(dir);
        VectorMA(org, 30.0f, dir, p->org);
        p->org[2] += 20.0f;
        VectorScale(dir, -25.0f, p->vel);

        VectorClear(p->accel);
        p->alpha = 1.0f;

        p->alphavel = -0.8f;
    }
}

/*
===============
CL_HologramParticles
===============
*/
void CL_HologramParticles(const vec3_t org)
{
    int         i;
    cparticle_t *p;
    vec3_t      dir;
    float       ltime;
    vec3_t      axis[3];

    ltime = cl.time * 0.03f;
    VectorSet(dir, ltime, ltime, 0);
    AnglesToAxis(dir, axis);

    for (i = 0; i < NUMVERTEXNORMALS; i++) {
        p = CL_AllocParticle();
        if (!p)
            return;

        p->time = cl.time;
        p->color = 0xd0;

        VectorRotate(bytedirs[i], axis, dir);
        VectorMA(org, 100.0f, dir, p->org);

        VectorClear(p->vel);
        VectorClear(p->accel);

        p->alpha = 1.0f;
        p->alphavel = INSTANT_PARTICLE;
    }
}

/*
===============
CL_BarrelExplodingParticles
===============
*/
void CL_BarrelExplodingParticles(const vec3_t org)
{
    static const vec3_t ofs[6] = {
        { -10, 0, 40 },
        { 10, 0, 40 },
        { 0, 16, 30 },
        { 16, 0, 25 },
        { 0, -16, 20 },
        { -16, 0, 15 },
    };

    static const vec3_t dir[6] = {
        { 0, 0, 1 },
        { 0, 0, 1 },
        { 0, 1, 0 },
        { 1, 0, 0 },
        { 0, -1, 0 },
        { -1, 0, 0 },
    };

    static const byte color[4] = { 52, 64, 96, 112 };

    for (int i = 0; i < 6; i++) {
        vec3_t p;
        VectorAdd(org, ofs[i], p);
        for (int j = 0; j < 4; j++)
            CL_ParticleSmokeEffect(p, dir[i], color[j], 5, 40);
    }
}
