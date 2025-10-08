#include "client.h"

#include <math.h>
#include <string.h>

static cvar_t *cl_modelview;
static cvar_t *cl_modelview_model;
static cvar_t *cl_modelview_skin;
static cvar_t *cl_modelview_fov;

static const float MODELVIEW_DEFAULT_FRAME_INTERVAL = 1.0f / 10.0f;

extern vec3_t listener_origin;
extern vec3_t listener_forward;
extern vec3_t listener_right;
extern vec3_t listener_up;

static unsigned modelview_wrap_frame(int frame)
{
    if (!cl.modelview.num_frames) {
        return 0;
    }

    int total = (int)cl.modelview.num_frames;
    frame %= total;
    if (frame < 0) {
        frame += total;
    }

    return (unsigned)frame;
}

static void modelview_reset_animation(void)
{
    cl.modelview.num_frames = 0;
    cl.modelview.current_frame = 0;
    cl.modelview.next_frame = 0;
    cl.modelview.frame_lerp = 0.0f;
    cl.modelview.animation_paused = false;
    cl.modelview.animation_dir = 0;
    cl.modelview.frame_interval = MODELVIEW_DEFAULT_FRAME_INTERVAL;
}

static void modelview_snap_to_current(void)
{
    if (!cl.modelview.num_frames) {
        cl.modelview.current_frame = 0;
        cl.modelview.next_frame = 0;
        cl.modelview.frame_lerp = 0.0f;
        return;
    }

    if (cl.modelview.frame_lerp >= 0.5f) {
        cl.modelview.current_frame = cl.modelview.next_frame;
    }

    cl.modelview.next_frame = cl.modelview.current_frame;
    cl.modelview.frame_lerp = 0.0f;
}

static void modelview_set_direction(int dir)
{
    if (cl.modelview.num_frames <= 1) {
        cl.modelview.animation_dir = 0;
        cl.modelview.next_frame = cl.modelview.current_frame;
        cl.modelview.frame_lerp = 0.0f;
        return;
    }

    dir = (dir > 0) - (dir < 0);
    if (!dir) {
        cl.modelview.animation_dir = 0;
        cl.modelview.next_frame = cl.modelview.current_frame;
        cl.modelview.frame_lerp = 0.0f;
        return;
    }

    if (cl.modelview.animation_dir == dir && cl.modelview.next_frame != cl.modelview.current_frame) {
        return;
    }

    cl.modelview.animation_dir = dir;
    cl.modelview.next_frame = modelview_wrap_frame((int)cl.modelview.current_frame + dir);
    cl.modelview.frame_lerp = 0.0f;
}

static void modelview_set_num_frames(unsigned frames)
{
    if (cl.modelview.num_frames == frames) {
        return;
    }

    cl.modelview.num_frames = frames;

    if (!frames) {
        cl.modelview.current_frame = 0;
        cl.modelview.next_frame = 0;
        cl.modelview.animation_dir = 0;
        cl.modelview.frame_lerp = 0.0f;
        cl.modelview.animation_paused = true;
        return;
    }

    if (cl.modelview.current_frame >= frames) {
        cl.modelview.current_frame = frames - 1;
    }

    if (frames <= 1) {
        cl.modelview.next_frame = cl.modelview.current_frame;
        cl.modelview.animation_dir = 0;
        cl.modelview.frame_lerp = 0.0f;
        cl.modelview.animation_paused = true;
        return;
    }

    if (cl.modelview.animation_dir == 0) {
        cl.modelview.animation_dir = 1;
    } else {
        cl.modelview.animation_dir = (cl.modelview.animation_dir > 0) ? 1 : -1;
    }

    cl.modelview.next_frame = modelview_wrap_frame((int)cl.modelview.current_frame + cl.modelview.animation_dir);
    cl.modelview.frame_lerp = 0.0f;
}

static void modelview_update_animation(float dt)
{
    if (cl.modelview.num_frames <= 1) {
        cl.modelview.animation_dir = 0;
        cl.modelview.next_frame = cl.modelview.current_frame;
        cl.modelview.frame_lerp = 0.0f;
        return;
    }

    if (cl.modelview.animation_dir == 0) {
        cl.modelview.next_frame = cl.modelview.current_frame;
        cl.modelview.frame_lerp = 0.0f;
        return;
    }

    if (cl.modelview.animation_paused) {
        return;
    }

    float interval = cl.modelview.frame_interval;
    if (interval <= 0.0f) {
        interval = MODELVIEW_DEFAULT_FRAME_INTERVAL;
    }

    cl.modelview.frame_lerp += dt / interval;

    while (cl.modelview.frame_lerp >= 1.0f) {
        cl.modelview.frame_lerp -= 1.0f;
        cl.modelview.current_frame = cl.modelview.next_frame;
        cl.modelview.next_frame = modelview_wrap_frame((int)cl.modelview.current_frame + cl.modelview.animation_dir);
    }
}

static float modelview_calc_fov_y(float fov_x, float width, float height)
{
    if (width <= 0.0f || height <= 0.0f) {
        return fov_x;
    }

    float x = width / tanf(fov_x * (M_PIf / 360.0f));
    float a = atanf(height / x);
    return a * (360.0f / M_PIf);
}

static void modelview_update_activation(void)
{
    bool want_active = cl_modelview && cl_modelview->integer;

    if (want_active && !cl.modelview.active) {
        VectorCopy(cl.viewangles, cl.modelview.saved_viewangles);
        VectorCopy(cl.viewangles, cl.modelview.angles);
        cl.modelview.restore_viewangles = true;
        cl.modelview.active = true;

        modelview_reset_animation();
        cl.modelview.animation_dir = 1;

        if (cl.modelview.distance <= 0.0f) {
            cl.modelview.distance = 120.0f;
        }
    } else if (!want_active && cl.modelview.active) {
        if (cl.modelview.restore_viewangles) {
            VectorCopy(cl.modelview.saved_viewangles, cl.viewangles);
        }

        cl.modelview.restore_viewangles = false;
        cl.modelview.active = false;
    }
}

static void modelview_update_media(void)
{
    if (!cl.modelview.active) {
        return;
    }

    qhandle_t previous_model = cl.modelview.model;

    if (!cl.modelview.model || cl.modelview.model_modified != cl_modelview_model->modified_count) {
        if (cl_modelview_model->string[0]) {
            cl.modelview.model = R_RegisterModel(cl_modelview_model->string);
        } else {
            cl.modelview.model = 0;
        }
        cl.modelview.model_modified = cl_modelview_model->modified_count;

        if (cl.modelview.model != previous_model) {
            modelview_reset_animation();
            cl.modelview.animation_dir = 1;
        }
    }

    if (!cl.modelview.skin || cl.modelview.skin_modified != cl_modelview_skin->modified_count) {
        if (cl_modelview_skin->string[0]) {
            cl.modelview.skin = R_RegisterSkin(cl_modelview_skin->string);
        } else {
            cl.modelview.skin = 0;
        }
        cl.modelview.skin_modified = cl_modelview_skin->modified_count;
    }

    if (cl.modelview.model) {
        unsigned frames = R_ModelNumFrames(cl.modelview.model);
        if (!frames) {
            frames = 1;
        }
        modelview_set_num_frames(frames);
    } else {
        modelview_set_num_frames(0);
    }
}

void CL_ModelView_Init(void)
{
    cl_modelview = Cvar_Get("cl_modelview", "0", 0);
    cl_modelview_model = Cvar_Get("cl_modelview_model", "players/male/tris.md2", 0);
    cl_modelview_skin = Cvar_Get("cl_modelview_skin", "players/male/grunt.pcx", 0);
    cl_modelview_fov = Cvar_Get("cl_modelview_fov", "45", 0);

    modelview_reset_animation();
    VectorClear(cl.modelview.target);

    cl.modelview.model_modified = -1;
    cl.modelview.skin_modified = -1;
    cl.modelview.distance = 120.0f;
}

bool CL_ModelView_Active(void)
{
    return cl.modelview.active;
}

bool CL_ModelView_Frame(int msec, float forward, float side, float vertical)
{
    modelview_update_activation();

    if (!cl.modelview.active) {
        return false;
    }

    float dt = msec * 0.001f;

    VectorCopy(cl.viewangles, cl.modelview.angles);
    cl.modelview.angles[ROLL] = 0.0f;
    cl.modelview.angles[PITCH] = Q_clipf(cl.modelview.angles[PITCH], -89.0f, 89.0f);
    cl.viewangles[PITCH] = cl.modelview.angles[PITCH];
    cl.viewangles[ROLL] = 0.0f;

    vec3_t forward_vec, right_vec, up_vec;
    AngleVectors(cl.modelview.angles, forward_vec, right_vec, up_vec);

    const float zoom_speed = 320.0f;
    const float pan_speed = 200.0f;

    if (forward) {
        cl.modelview.distance -= forward * zoom_speed * dt;
        cl.modelview.distance = Q_clipf(cl.modelview.distance, 16.0f, 2048.0f);
    }

    if (side) {
        VectorMA(cl.modelview.target, pan_speed * side * dt, right_vec, cl.modelview.target);
    }

    if (vertical) {
        VectorMA(cl.modelview.target, pan_speed * vertical * dt, up_vec, cl.modelview.target);
    }

    modelview_update_animation(dt);

    cl.cmd.msec = 0;
    cl.cmd.buttons = 0;
    cl.cmd.forwardmove = 0;
    cl.cmd.sidemove = 0;
    cl.cmd.upmove = 0;
    cl.cmd.angles[0] = cl.viewangles[0];
    cl.cmd.angles[1] = cl.viewangles[1];
    cl.cmd.angles[2] = cl.viewangles[2];

    cl.mousemove[0] = 0;
    cl.mousemove[1] = 0;

    return true;
}

bool CL_ModelView_Render(void)
{
    if (!cl_modelview || !cl_modelview->integer) {
        if (cl.modelview.active) {
            modelview_update_activation();
        }
        return false;
    }

    modelview_update_activation();

    if (!cl.modelview.active) {
        return false;
    }

    modelview_update_media();

    refdef_t *fd = &cl.refdef;

    fd->x = scr.vrect.x;
    fd->y = scr.vrect.y;
    fd->width = scr.vrect.width;
    fd->height = scr.vrect.height;

    float fov_x = Cvar_ClampValue(cl_modelview_fov, 10.0f, 140.0f);
    fd->fov_x = fov_x;
    fd->fov_y = modelview_calc_fov_y(fov_x, fd->width, fd->height);

    vec3_t forward_vec, right_vec, up_vec;
    AngleVectors(cl.modelview.angles, forward_vec, right_vec, up_vec);

    VectorMA(cl.modelview.target, -cl.modelview.distance, forward_vec, fd->vieworg);
    VectorCopy(cl.modelview.angles, fd->viewangles);

    VectorCopy(forward_vec, cl.v_forward);
    VectorCopy(right_vec, cl.v_right);
    VectorCopy(up_vec, cl.v_up);

    VectorCopy(fd->vieworg, listener_origin);
    VectorCopy(forward_vec, listener_forward);
    VectorCopy(right_vec, listener_right);
    VectorCopy(up_vec, listener_up);

    Vector4Clear(fd->screen_blend);
    Vector4Clear(fd->damage_blend);

    memset(&fd->fog, 0, sizeof(fd->fog));
    memset(&fd->heightfog, 0, sizeof(fd->heightfog));

    fd->frametime = cls.frametime;
    fd->time = cls.realtime * 0.001f;
    fd->rdflags = RDF_NOWORLDMODEL;
    fd->extended = false;
    fd->areabits = NULL;
    fd->lightstyles = NULL;

    entity_t ent;
    memset(&ent, 0, sizeof(ent));
    ent.model = cl.modelview.model;
    ent.skin = cl.modelview.skin;
    ent.alpha = 1.0f;
    VectorSet(ent.scale, 1.0f, 1.0f, 1.0f);
    VectorCopy(cl.modelview.target, ent.origin);
    VectorCopy(ent.origin, ent.oldorigin);

    if (cl.modelview.num_frames > 0) {
        if (cl.modelview.animation_dir != 0 && !cl.modelview.animation_paused && cl.modelview.num_frames > 1) {
            ent.frame = cl.modelview.next_frame;
            ent.oldframe = cl.modelview.current_frame;
            ent.backlerp = 1.0f - Q_clipf(cl.modelview.frame_lerp, 0.0f, 1.0f);
        } else {
            ent.frame = cl.modelview.current_frame;
            ent.oldframe = cl.modelview.current_frame;
            ent.backlerp = 0.0f;
        }
    } else {
        ent.frame = 0;
        ent.oldframe = 0;
        ent.backlerp = 0.0f;
    }

    if (ent.model) {
        fd->num_entities = 1;
        fd->entities = &ent;
    } else {
        fd->num_entities = 0;
        fd->entities = NULL;
    }

    dlight_t sun;
    memset(&sun, 0, sizeof(sun));
    VectorCopy(cl.modelview.target, sun.origin);
    sun.origin[0] += 192.0f;
    sun.origin[1] += 128.0f;
    sun.origin[2] += 256.0f;
    VectorCopy(sun.origin, sun.sphere);
    sun.sphere[3] = 600.0f;
    sun.radius = 600.0f;
    sun.intensity = 1.0f;
    VectorSet(sun.color, 1.0f, 0.95f, 0.85f);

    fd->num_dlights = 1;
    fd->dlights = &sun;

    fd->num_particles = 0;
    fd->particles = NULL;

    R_RenderFrame(fd);

    cl.lightlevel = 255;

    return true;
}

bool CL_ModelView_KeyEvent(unsigned key, bool down, unsigned repeats)
{
    if (!cl.modelview.active) {
        return false;
    }

    switch (key) {
    case K_SPACE:
        if (down && repeats == 1) {
            if (cl.modelview.animation_paused) {
                cl.modelview.animation_paused = false;

                if (cl.modelview.animation_dir == 0) {
                    modelview_set_direction(1);
                } else if (cl.modelview.next_frame == cl.modelview.current_frame) {
                    modelview_set_direction(cl.modelview.animation_dir);
                }

                if (cl.modelview.animation_dir == 0) {
                    cl.modelview.animation_paused = true;
                }
            } else {
                modelview_snap_to_current();
                cl.modelview.animation_paused = true;
            }
        }
        return true;

    case K_LEFTARROW:
        if (down && repeats == 1) {
            modelview_set_direction(-1);
            cl.modelview.animation_paused = (cl.modelview.animation_dir == 0);
        }
        return true;

    case K_RIGHTARROW:
        if (down && repeats == 1) {
            modelview_set_direction(1);
            cl.modelview.animation_paused = (cl.modelview.animation_dir == 0);
        }
        return true;

    default:
        break;
    }

    return false;
}

void CL_ModelView_Draw2D(void)
{
    if (!cl.modelview.active) {
        return;
    }

    const int x = 16;
    const int y = 16;

    if (!cl.modelview.model) {
        SCR_DrawString(x, y, UI_DROPSHADOW, COLOR_WHITE, "No model loaded");
        return;
    }

    unsigned total = cl.modelview.num_frames ? cl.modelview.num_frames : 1;
    unsigned frame = cl.modelview.current_frame;

    if (frame >= total) {
        frame = total - 1;
    }

    const char *status = "";

    if (cl.modelview.animation_paused) {
        status = " [Paused]";
    } else if (cl.modelview.animation_dir < 0) {
        status = " [Reverse]";
    }

    SCR_DrawString(x, y, UI_DROPSHADOW, COLOR_WHITE,
                   va("Frame %u / %u%s", frame + 1, total, status));
}
