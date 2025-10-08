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
// cl_view.c -- player rendering positioning

#include "client.h"

//=============
//
// development tools for weapons
//
int         gun_frame;
qhandle_t   gun_model;

//=============

static cvar_t   *cl_add_particles;
static cvar_t   *cl_add_lights;
static cvar_t   *cl_add_entities;
static cvar_t   *cl_add_blend;

#if USE_DEBUG
static cvar_t   *cl_testparticles;
static cvar_t   *cl_testentities;
static cvar_t   *cl_testlights;
static cvar_t   *cl_testblend;

static cvar_t   *cl_stats;
#endif

cvar_t   *cl_adjustfov;

int         r_numdlights;
dlight_t    r_dlights[MAX_DLIGHTS];

int         r_numentities;
entity_t    r_entities[MAX_ENTITIES];

int         r_numparticles;
particle_t  r_particles[MAX_PARTICLES];

lightstyle_t    r_lightstyles[MAX_LIGHTSTYLES];

/*
====================
V_ClearScene

Specifies the model that will be used as the world
====================
*/
static void V_ClearScene(void)
{
    r_numdlights = 0;
    r_numentities = 0;
    r_numparticles = 0;
}

/*
=====================
V_AddEntity

=====================
*/
void V_AddEntity(const entity_t *ent)
{
    if (r_numentities >= MAX_ENTITIES)
    {
        if (ent->flags & RF_LOW_PRIORITY)
            return;

        for (size_t i = 0; i < r_numentities; i++) {
            if (r_entities[i].flags & RF_LOW_PRIORITY) {
                r_entities[i] = *ent;
                return;
            }
        }

        return;
    }

    r_entities[r_numentities++] = *ent;
}

/*
=====================
V_AddParticle

=====================
*/
void V_AddParticle(const particle_t *p)
{
    if (r_numparticles >= MAX_PARTICLES)
        return;
    r_particles[r_numparticles++] = *p;
}

static void cone_to_bounding_sphere(const vec3_t origin, const vec3_t forward, float size, float angle_radians, float c, float s, vec4_t out)
{
    if(angle_radians > M_PI/4.0f)
    {
        VectorMA(origin, c * size, forward, out);
        out[3]   = s * size;
    }
    else
    {
        VectorMA(origin, size / (2.0f * c), forward, out);
        out[3]   = size / (2.0f * c);
    }
}

// calculate the fade distance from screen to light
static inline float fade_distance_to_light(const vec2_t fade, const vec3_t light_origin, const vec3_t org)
{
    if (fade[0] <= 1.0f && fade[1] <= 1.0f)
        return 1.0f;
    else if (fade[0] > fade[1])
        return 1.0f;

    float dist_to_light = VectorDistance(org, light_origin);
    float frac_to_end = Q_clipf(dist_to_light / fade[1], 0.0f, 1.0f);
    float min_frag_dist = fade[0] / fade[1];
    
    if (min_frag_dist > 1.0f)
        return 1.0f;
    else if (min_frag_dist <= 0)
        return frac_to_end;

    return 1.0f - smoothstep(min_frag_dist, 1.0f, frac_to_end);
}

/*
=====================
V_AddLightEx

=====================
*/
void V_AddLightEx(cl_shadow_light_t *light)
{
    dlight_t    *dl;

    if (r_numdlights >= MAX_DLIGHTS)
        return;
    
    float fade = fade_distance_to_light((const vec2_t) { light->fade_start, light->fade_end }, light->origin, cl.refdef.vieworg);

    if (fade <= 0.0f)
        return;

    dl = &r_dlights[r_numdlights++];
    VectorCopy(light->origin, dl->origin);
    dl->radius = light->radius;
    dl->intensity = light->intensity * (light->lightstyle == -1 ? 1.0f : r_lightstyles[light->lightstyle].white) * fade;
    dl->color[0] = light->color.r / 255.f;
    dl->color[1] = light->color.g / 255.f;
    dl->color[2] = light->color.b / 255.f;

    if (light->coneangle) {
        VectorCopy(light->conedirection, dl->cone);
        dl->cone[3] = DEG2RAD(light->coneangle);
        dl->conecos = cosf(dl->cone[3]);
        cone_to_bounding_sphere(dl->origin, dl->cone, dl->radius, dl->cone[3], dl->conecos, sinf(dl->cone[3]), dl->sphere);
    } else {
        dl->conecos = 0;
        VectorCopy(dl->origin, dl->sphere);
        dl->sphere[3] = dl->radius;
    }

    dl->fade[0] = light->fade_start;
    dl->fade[1] = light->fade_end;
}

/*
=====================
V_AddLight

=====================
*/
void V_AddLight(const vec3_t org, float intensity, float r, float g, float b)
{
    dlight_t    *dl;

    if (r_numdlights >= MAX_DLIGHTS)
        return;
    dl = &r_dlights[r_numdlights++];
    VectorCopy(org, dl->origin);
    dl->radius = intensity;
    dl->intensity = 1.0f;
    dl->color[0] = r;
    dl->color[1] = g;
    dl->color[2] = b;
    dl->conecos = 0;
    dl->fade[0] = dl->fade[1] = 0.0f;
    VectorCopy(dl->origin, dl->sphere);
    dl->sphere[3] = dl->radius;
}

/*
=====================
V_AddLightStyle

=====================
*/
void V_AddLightStyle(int style, float value)
{
    lightstyle_t    *ls;

    Q_assert(style >= 0 && style < MAX_LIGHTSTYLES);
    ls = &r_lightstyles[style];
    ls->white = value;
}

#if USE_DEBUG

/*
================
V_TestParticles

If cl_testparticles is set, create 4096 particles in the view
================
*/
static void V_TestParticles(void)
{
    particle_t  *p;
    int         i, j;
    float       d, r, u;

    r_numparticles = MAX_PARTICLES;
    for (i = 0; i < r_numparticles; i++) {
        d = i * 0.25f;
        r = 4 * ((i & 7) - 3.5f);
        u = 4 * (((i >> 3) & 7) - 3.5f);
        p = &r_particles[i];

        for (j = 0; j < 3; j++)
            p->origin[j] = cl.refdef.vieworg[j] + cl.v_forward[j] * d +
                           cl.v_right[j] * r + cl.v_up[j] * u;

        p->color = 8;
        p->alpha = 1;
    }
}

/*
================
V_TestEntities

If cl_testentities is set, create 32 player models
================
*/
static void V_TestEntities(void)
{
    int         i, j;
    float       f, r;
    entity_t    *ent;

    r_numentities = 32;
    memset(r_entities, 0, sizeof(r_entities));

    for (i = 0; i < r_numentities; i++) {
        ent = &r_entities[i];

        r = 64 * ((i % 4) - 1.5f);
        f = 64 * (i / 4) + 128;

        for (j = 0; j < 3; j++)
            ent->origin[j] = cl.refdef.vieworg[j] + cl.v_forward[j] * f +
                             cl.v_right[j] * r;

        ent->model = cl.baseclientinfo.model;
        ent->skin = cl.baseclientinfo.skin;
    }
}

/*
================
V_TestLights

If cl_testlights is set, create 32 lights models
================
*/
static void V_TestLights(void)
{
    int         i, j;
    float       f, r;
    dlight_t    *dl;

    if (cl_testlights->integer != 1) {
        dl = &r_dlights[0];
        r_numdlights = 1;

        VectorMA(cl.refdef.vieworg, 256, cl.v_forward, dl->origin);
        if (cl_testlights->integer == -1)
            VectorSet(dl->color, -1, -1, -1);
        else
            VectorSet(dl->color, 1, 1, 1);
        dl->radius = 256;
        dl->intensity = 1.0f;
        return;
    }

    r_numdlights = MAX_DLIGHTS;
    memset(r_dlights, 0, sizeof(r_dlights));

    for (i = 0; i < r_numdlights; i++) {
        dl = &r_dlights[i];

        r = 64 * ((i % 4) - 1.5f);
        f = 64 * (i / 4) + 128;

        for (j = 0; j < 3; j++)
            dl->origin[j] = cl.refdef.vieworg[j] + cl.v_forward[j] * f +
                            cl.v_right[j] * r;
        dl->color[0] = ((i % 6) + 1) & 1;
        dl->color[1] = (((i % 6) + 1) & 2) >> 1;
        dl->color[2] = (((i % 6) + 1) & 4) >> 2;
        dl->radius = 200;
        dl->intensity = 1.0f;
    }
}

#endif

//===================================================================

void CL_UpdateBlendSetting(void)
{
    if (cls.netchan.protocol < PROTOCOL_VERSION_R1Q2) {
        return;
    }

    q2proto_clc_message_t message = {.type = Q2P_CLC_SETTING, .setting = {0}};
    message.setting.index = CLS_NOBLEND;
    message.setting.value = !cl_add_blend->integer;
    q2proto_client_write(&cls.q2proto_ctx, Q2PROTO_IOARG_CLIENT_WRITE, &message);
    MSG_FlushTo(&cls.netchan.message);
}

//============================================================================

// gun frame debugging functions
static void V_Gun_Next_f(void)
{
    gun_frame++;
    Com_Printf("frame %i\n", gun_frame);
}

static void V_Gun_Prev_f(void)
{
    gun_frame--;
    if (gun_frame < 0)
        gun_frame = 0;
    Com_Printf("frame %i\n", gun_frame);
}

static void V_Gun_Model_f(void)
{
    char    name[MAX_QPATH];

    if (Cmd_Argc() != 2) {
        gun_model = 0;
        return;
    }
    Q_concat(name, sizeof(name), "models/", Cmd_Argv(1), "/tris.md2");
    gun_model = R_RegisterModel(name);
}

//============================================================================

// renderer will iterate the list backwards, so sorting order must be reversed
static int entitycmpfnc(const void *_a, const void *_b)
{
    const entity_t *a = (const entity_t *)_a;
    const entity_t *b = (const entity_t *)_b;

    bool a_trans = a->flags & RF_TRANSLUCENT;
    bool b_trans = b->flags & RF_TRANSLUCENT;
    if (a_trans != b_trans)
        return b_trans - a_trans;
    if (a_trans) {
        float dist_a = DistanceSquared(a->origin, cl.refdef.vieworg);
        float dist_b = DistanceSquared(b->origin, cl.refdef.vieworg);
        if (dist_a > dist_b)
            return 1;
        if (dist_a < dist_b)
            return -1;
    }

    bool a_shell = a->flags & RF_SHELL_MASK;
    bool b_shell = b->flags & RF_SHELL_MASK;
    if (a_shell != b_shell)
        return b_shell - a_shell;

    // all other models are sorted by model then skin
    if (a->model > b->model)
        return -1;
    if (a->model < b->model)
        return 1;

    if (a->skin > b->skin)
        return -1;
    if (a->skin < b->skin)
        return 1;

    return 0;
}

static void V_SetLightLevel(void)
{
    vec3_t shadelight;

    // save off light value for server to look at (BIG HACK!)
    R_LightPoint(cl.refdef.vieworg, shadelight);

    // pick the greatest component, which should be the same
    // as the mono value returned by software
    if (shadelight[0] > shadelight[1]) {
        if (shadelight[0] > shadelight[2]) {
            cl.lightlevel = 150.0f * shadelight[0];
        } else {
            cl.lightlevel = 150.0f * shadelight[2];
        }
    } else {
        if (shadelight[1] > shadelight[2]) {
            cl.lightlevel = 150.0f * shadelight[1];
        } else {
            cl.lightlevel = 150.0f * shadelight[2];
        }
    }
}

/*
====================
V_CalcFov
====================
*/
float V_CalcFov(float fov_x, float width, float height)
{
    float    a;
    float    x;

    if (fov_x < 0.75f || fov_x > 179)
        Com_Error(ERR_DROP, "%s: bad fov: %f", __func__, fov_x);

    x = width / tanf(fov_x * (M_PIf / 360));

    a = atanf(height / x);
    a = a * (360 / M_PIf);

    return a;
}

/*
====================
CL_ServerTime
====================
*/
int CL_ServerTime(void)
{
    return cl.servertime;
}

/*
==================
V_RenderView

==================
*/
void V_FogParamsChanged(unsigned bits, unsigned color_bits, unsigned hf_start_color_bits, unsigned hf_end_color_bits, const cl_fog_params_t *params, int time)
{
    if (time != 0) {
        // shift the current fog values back to start
        cl.fog.start = cl.fog.end;
        cl.fog.lerp_time = time;
        cl.fog.lerp_time_start = cl.time;
    } else {
        // no lerp, just disable lerp entirely
        cl.fog.lerp_time = 0;
    }

    cl_fog_params_t *cur = &cl.fog.end;

    // fill in updated values in end
    if (bits & Q2P_FOG_DENSITY_SKYFACTOR) {
        cur->linear.density = params->linear.density; // Kex divides the density by 64, prob because of exp2
        cur->linear.sky_factor = params->linear.sky_factor;
    }

    if (color_bits & BIT(0))
        cur->linear.color[0] = params->linear.color[0];
    if (color_bits & BIT(1))
        cur->linear.color[1] = params->linear.color[1];
    if (color_bits & BIT(2))
        cur->linear.color[2] = params->linear.color[2];
    
    if (bits & Q2P_HEIGHTFOG_FALLOFF)
        cur->height.falloff = params->height.falloff;
    if (bits & Q2P_HEIGHTFOG_DENSITY)
        cur->height.density = params->height.density;
    if (hf_start_color_bits & BIT(0))
        cur->height.start.color[0] = params->height.start.color[0];
    if (hf_start_color_bits & BIT(1))
        cur->height.start.color[1] = params->height.start.color[1];
    if (hf_start_color_bits & BIT(2))
        cur->height.start.color[2] = params->height.start.color[2];
    if (bits & Q2P_HEIGHTFOG_START_DIST)
        cur->height.start.dist = params->height.start.dist;
    if (hf_end_color_bits & BIT(0))
        cur->height.end.color[0] = params->height.end.color[0];
    if (hf_end_color_bits & BIT(1))
        cur->height.end.color[1] = params->height.end.color[1];
    if (hf_end_color_bits & BIT(2))
        cur->height.end.color[2] = params->height.end.color[2];
    if (bits & Q2P_HEIGHTFOG_END_DIST)
        cur->height.end.dist = params->height.end.dist;
}

/*
==================
V_RenderView

==================
*/
void V_RenderView(void)
{
    // an invalid frame will just use the exact previous refdef
    // we can't use the old frame if the video mode has changed, though...
    if (cl.frame.valid) {
        V_ClearScene();

        // build a refresh entity list and calc cl.sim*
        // this also calls CL_CalcViewValues which loads
        // v_forward, etc.
        CL_AddEntities();

#if USE_DEBUG
        if (cl_testparticles->integer)
            V_TestParticles();
        if (cl_testentities->integer)
            V_TestEntities();
        if (cl_testlights->integer)
            V_TestLights();
        if (cl_testblend->integer & 1)
            Vector4Set(cl.refdef.screen_blend, 1, 0.5f, 0.25f, 0.5f);
        if (cl_testblend->integer & 2)
            Vector4Set(cl.refdef.damage_blend, 0.25f, 0.5f, 0.7f, 0.5f);
#endif

        // never let it sit exactly on a node line, because a water plane can
        // disappear when viewed with the eye exactly on it.
        // the server protocol only specifies to 1/8 pixel, so add 1/16 in each axis
        cl.refdef.vieworg[0] += 1.0f / 16;
        cl.refdef.vieworg[1] += 1.0f / 16;
        cl.refdef.vieworg[2] += 1.0f / 16;

        cl.refdef.x = scr.vrect.x;
        cl.refdef.y = scr.vrect.y;
        cl.refdef.width = scr.vrect.width;
        cl.refdef.height = scr.vrect.height;

        // adjust for non-4/3 screens
        if (cl_adjustfov->integer) {
            cl.refdef.fov_y = cl.fov_y;
            cl.refdef.fov_x = V_CalcFov(cl.refdef.fov_y, cl.refdef.height, cl.refdef.width);
        } else {
            cl.refdef.fov_x = cl.fov_x;
            cl.refdef.fov_y = V_CalcFov(cl.refdef.fov_x, cl.refdef.width, cl.refdef.height);
        }

        cl.refdef.frametime = cls.frametime;
        cl.refdef.time = cl.time * 0.001f;

        if (cl.frame.areabytes) {
            cl.refdef.areabits = cl.frame.areabits;
        } else {
            cl.refdef.areabits = NULL;
        }

        if (!cl_add_entities->integer)
            r_numentities = 0;
        if (!cl_add_particles->integer)
            r_numparticles = 0;
        if (!cl_add_lights->integer)
            r_numdlights = 0;
        if (!cl_add_blend->integer) {
            Vector4Clear(cl.refdef.screen_blend);
            Vector4Clear(cl.refdef.damage_blend);
        }

        cl.refdef.num_entities = r_numentities;
        cl.refdef.entities = r_entities;
        cl.refdef.num_particles = r_numparticles;
        cl.refdef.particles = r_particles;
        cl.refdef.num_dlights = r_numdlights;
        cl.refdef.dlights = r_dlights;
        cl.refdef.lightstyles = r_lightstyles;
        cl.refdef.rdflags = cl.frame.ps.rdflags | cl.predicted_rdflags;
        cl.refdef.extended = cl.csr.extended;

        // sort entities for better cache locality
        qsort(cl.refdef.entities, cl.refdef.num_entities, sizeof(cl.refdef.entities[0]), entitycmpfnc);

        if (cl.fog.lerp_time == 0 || cl.time > cl.fog.lerp_time_start + cl.fog.lerp_time) {
            cl.refdef.fog = cl.fog.end.linear;
            cl.refdef.heightfog = cl.fog.end.height;
        } else {
            float fog_frontlerp = (cl.time - cl.fog.lerp_time_start) / (float) cl.fog.lerp_time;
            float fog_backlerp = 1.0f - fog_frontlerp;
            
#define Q_FP(p) \
                cl.refdef.fog.p = LERP2(cl.fog.start.linear.p, cl.fog.end.linear.p, fog_backlerp, fog_frontlerp)
#define Q_HFP(p) \
                cl.refdef.heightfog.p = LERP2(cl.fog.start.height.p, cl.fog.end.height.p, fog_backlerp, fog_frontlerp)

            Q_FP(color[0]);
            Q_FP(color[1]);
            Q_FP(color[2]);
            Q_FP(density);
            Q_FP(sky_factor);

            Q_HFP(start.color[0]);
            Q_HFP(start.color[1]);
            Q_HFP(start.color[2]);
            Q_HFP(start.dist);

            Q_HFP(end.color[0]);
            Q_HFP(end.color[1]);
            Q_HFP(end.color[2]);
            Q_HFP(end.dist);
            
            Q_HFP(density);
            Q_HFP(falloff);

#undef Q_FP
        }
    }

    R_RenderFrame(&cl.refdef);
#if USE_DEBUG
    if (cl_stats->integer)
        Com_Printf("ent:%i  lt:%i  part:%i\n", r_numentities, r_numdlights, r_numparticles);
#endif

    V_SetLightLevel();
}


/*
=============
V_Viewpos_f
=============
*/
static void V_Viewpos_f(void)
{
    Com_Printf("%s : %.f\n", vtos(cl.refdef.vieworg), cl.refdef.viewangles[YAW]);
}

static void V_Fog_f(void)
{
    cl_fog_params_t p;
    p.linear.color[0] = atof(Cmd_Argv(1));
    p.linear.color[1] = atof(Cmd_Argv(2));
    p.linear.color[2] = atof(Cmd_Argv(3));
    p.linear.density = atof(Cmd_Argv(4));
    p.linear.sky_factor = atof(Cmd_Argv(5));
    int time = atoi(Cmd_Argv(6));

    V_FogParamsChanged(Q2P_FOG_DENSITY_SKYFACTOR, BIT(0) | BIT(1) | BIT(2), 0, 0, &p, time);
}

static const cmdreg_t v_cmds[] = {
    { "gun_next", V_Gun_Next_f },
    { "gun_prev", V_Gun_Prev_f },
    { "gun_model", V_Gun_Model_f },
    { "viewpos", V_Viewpos_f },
    { "fog", V_Fog_f },
    { NULL }
};

static void cl_add_blend_changed(cvar_t *self)
{
    CL_UpdateBlendSetting();
}

/*
=============
V_Init
=============
*/
void V_Init(void)
{
    Cmd_Register(v_cmds);

#if USE_DEBUG
    cl_testblend = Cvar_Get("cl_testblend", "0", 0);
    cl_testparticles = Cvar_Get("cl_testparticles", "0", 0);
    cl_testentities = Cvar_Get("cl_testentities", "0", 0);
    cl_testlights = Cvar_Get("cl_testlights", "0", CVAR_CHEAT);

    cl_stats = Cvar_Get("cl_stats", "0", 0);
#endif

    cl_add_lights = Cvar_Get("cl_lights", "1", 0);
    cl_add_particles = Cvar_Get("cl_particles", "1", 0);
    cl_add_entities = Cvar_Get("cl_entities", "1", 0);
    cl_add_blend = Cvar_Get("cl_blend", "1", 0);
    cl_add_blend->changed = cl_add_blend_changed;

    cl_adjustfov = Cvar_Get("cl_adjustfov", "1", 0);
}

void V_Shutdown(void)
{
    Cmd_Deregister(v_cmds);
}
