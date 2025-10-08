/*
Copyright (C) 2003-2006 Andrey Nazarov

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

#include "shared/shared.h"
#include "common/bsp.h"
#include "common/cmd.h"
#include "common/common.h"
#include "common/cvar.h"
#include "common/files.h"
#include "common/hash_map.h"
#include "common/intreadwrite.h"
#include "common/math.h"
#include "client/video.h"
#include "client/client.h"
#include "refresh/refresh.h"
#include "system/hunk.h"
#include "images.h"
#include "qgl.h"

/*
 * gl_main.c
 *
 */

#define R_Malloc(size)  Z_TagMalloc(size, TAG_RENDERER)
#define R_Mallocz(size) Z_TagMallocz(size, TAG_RENDERER)

#if USE_GLES
#define QGL_INDEX_TYPE  GL_UNSIGNED_SHORT
typedef GLushort glIndex_t;
#else
#define QGL_INDEX_TYPE  GL_UNSIGNED_INT
typedef GLuint glIndex_t;
#endif

typedef uint64_t glStateBits_t;

#define TAB_SIN(x)  gl_static.sintab[(x) & 255]
#define TAB_COS(x)  gl_static.sintab[((x) + 64) & 255]

// auto textures
#define NUM_AUTO_TEXTURES       13
#define AUTO_TEX(n)             gl_static.texnums[n]

#define TEXNUM_DEFAULT          AUTO_TEX(0)
#define TEXNUM_SCRAP            AUTO_TEX(1)
#define TEXNUM_PARTICLE         AUTO_TEX(2)
#define TEXNUM_BEAM             AUTO_TEX(3)
#define TEXNUM_WHITE            AUTO_TEX(4)
#define TEXNUM_BLACK            AUTO_TEX(5)
#define TEXNUM_RAW              AUTO_TEX(6)
#define TEXNUM_CUBEMAP_DEFAULT  AUTO_TEX(7)
#define TEXNUM_CUBEMAP_BLACK    AUTO_TEX(8)
#define TEXNUM_PP_SCENE         AUTO_TEX(9)
#define TEXNUM_PP_BLOOM         AUTO_TEX(10)
#define TEXNUM_PP_BLUR_0        AUTO_TEX(11)
#define TEXNUM_PP_BLUR_1        AUTO_TEX(12)

// framebuffers
#define FBO_COUNT   3
#define FBO_SCENE   gl_static.framebuffers[0]
#define FBO_BLUR_0  gl_static.framebuffers[1]
#define FBO_BLUR_1  gl_static.framebuffers[2]

typedef struct {
    GLuint query;
    float frac;
    unsigned timestamp;
    bool pending;
    bool visible;
} glquery_t;

typedef struct {
    bool            registering;
    bool            use_shaders;
    bool            use_cubemaps;
    bool            use_bmodel_skies;
    bool            use_gpu_lerp;
    struct {
        bsp_t       *cache;
        vec_t       *vertices;
        GLuint      buffer;
        size_t      buffer_size;
        vec_t       size;
    } world;
    GLuint          renderbuffer;
    GLuint          framebuffers[FBO_COUNT];
    GLuint          uniform_buffer;
    GLuint          dlight_buffer;
#if USE_MD5
    GLuint          skeleton_buffer;
    GLuint          skeleton_tex[2];
#endif
    GLuint          array_object;
    GLuint          index_buffer;
    GLuint          vertex_buffer;
    GLuint          warp_program;
    GLuint          texnums[NUM_AUTO_TEXTURES];
    GLenum          samples_passed;
    GLbitfield      stencil_buffer_bit;
    GLsync          sync;
    float           entity_modulate;
    float           bloom_sigma;
    uint32_t        inverse_intensity_33;
    uint32_t        inverse_intensity_66;
    uint32_t        inverse_intensity_100;
    int             nolm_mask;
    int             hunk_align;
    float           sintab[256];
    byte            latlngtab[NUMVERTEXNORMALS][2];
    byte            lightstylemap[MAX_LIGHTSTYLES];
    vec4_t          clearcolor;
    hash_map_t      *queries;
    hash_map_t      *programs;
} glStatic_t;

typedef struct {
    refdef_t        fd;
    vec3_t          viewaxis[3];
    mat4_t          viewmatrix;
    unsigned        visframe;
    unsigned        drawframe;
    unsigned        dlightframe;
    int             viewcluster1;
    int             viewcluster2;
    int             nodes_visible;
    cplane_t        frustumPlanes[4];
    entity_t        *ent;
    bool            entrotated;
    float           entscale;
    vec3_t          entaxis[3];
    mat4_t          entmatrix;
    mat4_t          skymatrix[2];
    lightpoint_t    lightpoint;
    struct {
        entity_t    *opaque;
        entity_t    *beams;
        entity_t    *flares;
        entity_t    *bmodels;
        entity_t    *alpha_back;
        entity_t    *alpha_front;
    } ents;
    glStateBits_t   fog_bits, fog_bits_sky;
    glStateBits_t   ppl_bits;
    uint64_t        ppl_dlight_bits;
    int             framebuffer_width;
    int             framebuffer_height;
    bool            framebuffer_ok;
    bool            framebuffer_bound;
} glRefdef_t;

typedef enum {
    QGL_CAP_LEGACY                      = BIT(0),
    QGL_CAP_SHADER                      = BIT(1),
    QGL_CAP_TEXTURE_BITS                = BIT(2),
    QGL_CAP_TEXTURE_CLAMP_TO_EDGE       = BIT(3),
    QGL_CAP_TEXTURE_MAX_LEVEL           = BIT(4),
    QGL_CAP_TEXTURE_LOD_BIAS            = BIT(5),
    QGL_CAP_TEXTURE_NON_POWER_OF_TWO    = BIT(6),
    QGL_CAP_TEXTURE_ANISOTROPY          = BIT(7),
    QGL_CAP_UNPACK_SUBIMAGE             = BIT(8),
    QGL_CAP_ELEMENT_INDEX_UINT          = BIT(9),
    QGL_CAP_QUERY_RESULT_NO_WAIT        = BIT(10),
    QGL_CAP_CLIENT_VA                   = BIT(11),
    QGL_CAP_LINE_SMOOTH                 = BIT(12),
    QGL_CAP_BUFFER_TEXTURE              = BIT(13),
    QGL_CAP_SHADER_STORAGE              = BIT(14),
    QGL_CAP_SKELETON_MASK               = QGL_CAP_BUFFER_TEXTURE | QGL_CAP_SHADER_STORAGE,
} glcap_t;

#define QGL_VER(major, minor)   ((major) * 100 + (minor))
#define QGL_UNPACK_VER(ver)     (ver) / 100, (ver) % 100

typedef struct {
    int     ver_gl;
    int     ver_es;
    int     ver_sl;
    glcap_t caps;
    int     colorbits;
    int     depthbits;
    int     stencilbits;
    int     max_texture_size_log2;
    int     max_texture_size;
    int     ssbo_align;
    float   max_anisotropy;
    bool    webgl;
} glConfig_t;

extern glStatic_t gl_static;
extern glConfig_t gl_config;
extern glRefdef_t glr;

extern entity_t gl_world;

extern unsigned r_registration_sequence;

typedef struct {
    int nodesDrawn;
    int leavesDrawn;
    int facesMarked;
    int facesDrawn;
    int facesTris;
    int texSwitches;
    int texUploads;
    int lightTexels;
    int trisDrawn;
    int batchesDrawn;
    int nodesCulled;
    int facesCulled;
    int boxesCulled;
    int spheresCulled;
    int rotatedBoxesCulled;
    int shadowsCulled;
    int batchesDrawn2D;
    int uniformUploads;
    int vertexArrayBinds;
    int occlusionQueries;
    int dlightsTotal;
    int dlightUploads;
    int dlightsCulled;
    int dlightsNotUsed;
    int dlightsUsed;
    int dlightsEntCulled;
} statCounters_t;

extern statCounters_t c;

// regular variables
extern cvar_t *gl_partscale;
extern cvar_t *gl_partstyle;
extern cvar_t *gl_beamstyle;
extern cvar_t *gl_celshading;
extern cvar_t *gl_dotshading;
extern cvar_t *gl_shadows;
extern cvar_t *gl_modulate;
extern cvar_t *gl_modulate_world;
extern cvar_t *gl_coloredlightmaps;
extern cvar_t *gl_lightmap_bits;
extern cvar_t *gl_brightness;
extern cvar_t *gl_dynamic;
extern cvar_t *gl_dlight_falloff;
extern cvar_t *gl_modulate_entities;
extern cvar_t *gl_glowmap_intensity;
extern cvar_t *gl_flarespeed;
extern cvar_t *gl_fontshadow;
extern cvar_t *gl_shaders;
#if USE_MD5
extern cvar_t *gl_md5_load;
extern cvar_t *gl_md5_use;
extern cvar_t *gl_md5_distance;
#endif
extern cvar_t *gl_damageblend_frac;
extern cvar_t *gl_waterwarp;
extern cvar_t *gl_bloom;
extern cvar_t *gl_bloom_height;

// development variables
extern cvar_t *gl_znear;
extern cvar_t *gl_drawsky;
extern cvar_t *gl_showtris;
#if USE_DEBUG
extern cvar_t *gl_nobind;
extern cvar_t *gl_novbo;
extern cvar_t *gl_test;
#endif
extern cvar_t *gl_cull_nodes;
extern cvar_t *gl_cull_models;
extern cvar_t *gl_showcull;
extern cvar_t *gl_clear;
extern cvar_t *gl_novis;
extern cvar_t *gl_lockpvs;
extern cvar_t *gl_lightmap;
extern cvar_t *gl_fullbright;
extern cvar_t *gl_vertexlight;
extern cvar_t *gl_lightgrid;
extern cvar_t *gl_showerrors;
extern cvar_t *gl_per_pixel_lighting; // use_shaders only

extern int32_t gl_shaders_modified;

typedef enum {
    CULL_OUT,
    CULL_IN,
    CULL_CLIP
} glCullResult_t;

glCullResult_t GL_CullBox(const vec3_t bounds[2]);
glCullResult_t GL_CullSphere(const vec3_t origin, float radius);
glCullResult_t GL_CullLocalBox(const vec3_t origin, const vec3_t bounds[2]);

bool GL_AllocBlock(int width, int height, uint16_t *inuse,
                   int w, int h, int *s, int *t);

void GL_MultMatrix(GLfloat *restrict out, const GLfloat *restrict a, const GLfloat *restrict b);
void GL_SetEntityAxis(void);
void GL_RotationMatrix(GLfloat *matrix);
void GL_RotateForEntity(void);

void GL_ClearErrors(void);
bool GL_ShowErrors(const char *func);

void GL_InitQueries(void);
void GL_DeleteQueries(void);

static inline void GL_AdvanceValue(float *restrict val, float target, float speed)
{
    if (speed <= 0) {
        *val = target;
    } else if (*val < target) {
        *val += speed * glr.fd.frametime;
        if (*val > target)
            *val = target;
    } else if (*val > target) {
        *val -= speed * glr.fd.frametime;
        if (*val < target)
            *val = target;
    }
}

/*
 * gl_model.c
 *
 */

#define MOD_MAXSIZE_GPU     0x1000000

#if (defined _WIN32) && !(defined _WIN64)
#define MOD_MAXSIZE_CPU     0x400000
#else
#define MOD_MAXSIZE_CPU     0x800000
#endif

typedef struct {
    float   st[2];
} maliastc_t;

typedef struct {
    int16_t     pos[3];
    uint8_t     norm[2]; // lat, lng
} maliasvert_t;

typedef struct {
    vec3_t  scale;
    vec3_t  translate;
    vec3_t  bounds[2];
    vec_t   radius;
} maliasframe_t;

typedef char maliasskinname_t[MAX_QPATH];

typedef struct {
    int             numverts;
    int             numtris;
    int             numindices;
    int             numskins;
    uint16_t        *indices;
    maliasvert_t    *verts;
    maliastc_t      *tcoords;
#if USE_MD5
    maliasskinname_t *skinnames;
#endif
    image_t         **skins;
} maliasmesh_t;

typedef struct {
    int             width;
    int             height;
    int             origin_x;
    int             origin_y;
    image_t         *image;
} mspriteframe_t;

#if USE_MD5

// the total amount of joints the renderer will bother handling
#define MD5_MAX_JOINTS      256
#define MD5_MAX_JOINTNAME   48
#define MD5_MAX_MESHES      32
#define MD5_MAX_WEIGHTS     8192
#define MD5_MAX_FRAMES      1024

/* Joint */
typedef struct {
    vec4_t pos;
    vec4_t axis[3];
} glJoint_t;

typedef struct {
    vec3_t pos;
    float scale;
    quat_t orient;
    vec3_t axis[3];
} md5_joint_t;

/* Vertex */
typedef struct {
    vec3_t normal;

    uint16_t start; /* start weight */
    uint16_t count; /* weight count */
} md5_vertex_t;

/* Weight */
typedef struct {
    vec3_t pos;
    float bias;
} md5_weight_t;

/* Mesh */
typedef struct {
    int num_verts;
    int num_indices;
    int num_weights;

    md5_vertex_t *vertices;
    maliastc_t *tcoords;
    uint16_t *indices;
    md5_weight_t *weights;
    uint8_t *jointnums;
} md5_mesh_t;

/* MD5 model + animation structure */
typedef struct {
    int num_meshes;
    int num_joints;
    int num_frames; // may not match model_t::numframes, but not fatal
    int num_skins;

    md5_mesh_t *meshes;
    md5_joint_t *skeleton_frames; // [num_joints][num_frames]
    image_t **skins;
} md5_model_t;

#endif

typedef struct {
    enum {
        MOD_FREE,
        MOD_ALIAS,
        MOD_SPRITE,
        MOD_EMPTY
    } type;

    char name[MAX_QPATH];
    unsigned registration_sequence;
    memhunk_t hunk;

    int nummeshes;
    int numframes;

    maliasmesh_t *meshes; // MD2 / MD3
#if USE_MD5
    md5_model_t *skeleton; // MD5
#endif
    union {
        maliasframe_t *frames;
        mspriteframe_t *spriteframes;
    };

    GLuint buffers[2];
} model_t;

// 2d:    xy[2]  | st[2]     | color[1]
// world: xyz[3] | color[1]  | st[2]    | lmst[2]   | normal[3] | unused[1]
// model: xyz[3] | unused[1] | color[4]             | normal[3] | unused[1]
#define VERTEX_SIZE 12

void MOD_FreeUnused(void);
void MOD_FreeAll(void);
void MOD_Init(void);
void MOD_Shutdown(void);

model_t *MOD_ForHandle(qhandle_t h);
qhandle_t R_RegisterModel(const char *name);

/*
 * gl_surf.c
 *
 */
#define LIGHT_STYLE(i) \
    &glr.fd.lightstyles[gl_static.lightstylemap[(i)]]

#define LM_MAX_LIGHTMAPS    128
#define LM_MAX_BLOCK_WIDTH  (1 << 10)

typedef struct lightmap_s {
    uint16_t    mins[2];
    uint16_t    maxs[2];
    byte        *buffer;
} lightmap_t;

typedef struct {
    bool        dirty;
    int         comp, block_size, block_shift;
    float       add, modulate, scale;
    int         nummaps, maxmaps, block_bytes;
    uint16_t    inuse[LM_MAX_BLOCK_WIDTH];
    GLuint      texnums[LM_MAX_LIGHTMAPS];
    lightmap_t  lightmaps[LM_MAX_LIGHTMAPS];
} lightmap_builder_t;

extern lightmap_builder_t lm;

void GL_AdjustColor(vec3_t color);
void GL_PushLights(mface_t *surf);
void GL_UploadLightmaps(void);

void GL_RebuildLighting(void);
void GL_FreeWorld(void);
void GL_LoadWorld(const char *name);

/*
 * gl_state.c
 *
 */
#define GLS_DEFAULT             0ULL

#define GLS_DEPTHMASK_FALSE     BIT_ULL(0)
#define GLS_DEPTHTEST_DISABLE   BIT_ULL(1)
#define GLS_CULL_DISABLE        BIT_ULL(2)
#define GLS_BLEND_BLEND         BIT_ULL(3)
#define GLS_BLEND_ADD           BIT_ULL(4)
#define GLS_BLEND_MODULATE      BIT_ULL(5)

#define GLS_ALPHATEST_ENABLE    BIT_ULL(6)
#define GLS_TEXTURE_REPLACE     BIT_ULL(7)
#define GLS_SCROLL_ENABLE       BIT_ULL(8)
#define GLS_LIGHTMAP_ENABLE     BIT_ULL(9)
#define GLS_WARP_ENABLE         BIT_ULL(10)
#define GLS_INTENSITY_ENABLE    BIT_ULL(11)
#define GLS_GLOWMAP_ENABLE      BIT_ULL(12)
#define GLS_CLASSIC_SKY         BIT_ULL(13)
#define GLS_DEFAULT_SKY         BIT_ULL(14)
#define GLS_DEFAULT_FLARE       BIT_ULL(15)

#define GLS_MESH_MD2            BIT_ULL(16)
#define GLS_MESH_MD5            BIT_ULL(17)
#define GLS_MESH_LERP           BIT_ULL(18)
#define GLS_MESH_SHELL          BIT_ULL(19)
#define GLS_MESH_SHADE          BIT_ULL(20)

#define GLS_SHADE_SMOOTH        BIT_ULL(21)
#define GLS_SCROLL_X            BIT_ULL(22)
#define GLS_SCROLL_Y            BIT_ULL(23)
#define GLS_SCROLL_FLIP         BIT_ULL(24)
#define GLS_SCROLL_SLOW         BIT_ULL(25)

#define GLS_FOG_GLOBAL          BIT_ULL(26)
#define GLS_FOG_HEIGHT          BIT_ULL(27)
#define GLS_FOG_SKY             BIT_ULL(28)

#define GLS_BLOOM_GENERATE      BIT_ULL(29)
#define GLS_BLOOM_OUTPUT        BIT_ULL(30)
#define GLS_BLOOM_SHELL         BIT_ULL(31)

#define GLS_BLUR_GAUSS          BIT_ULL(32)
#define GLS_BLUR_BOX            BIT_ULL(33)

#define GLS_DYNAMIC_LIGHTS      BIT_ULL(34)

#define GLS_BLEND_MASK          (GLS_BLEND_BLEND | GLS_BLEND_ADD | GLS_BLEND_MODULATE)
#define GLS_COMMON_MASK         (GLS_DEPTHMASK_FALSE | GLS_DEPTHTEST_DISABLE | GLS_CULL_DISABLE | GLS_BLEND_MASK)
#define GLS_SKY_MASK            (GLS_CLASSIC_SKY | GLS_DEFAULT_SKY)
#define GLS_FOG_MASK            (GLS_FOG_GLOBAL | GLS_FOG_HEIGHT | GLS_FOG_SKY)
#define GLS_MESH_ANY            (GLS_MESH_MD2 | GLS_MESH_MD5)
#define GLS_MESH_MASK           (GLS_MESH_ANY | GLS_MESH_LERP | GLS_MESH_SHELL | GLS_MESH_SHADE)
#define GLS_BLOOM_MASK          (GLS_BLOOM_GENERATE | GLS_BLOOM_OUTPUT | GLS_BLOOM_SHELL)
#define GLS_BLUR_MASK           (GLS_BLUR_GAUSS | GLS_BLUR_BOX)
#define GLS_SHADER_MASK         (GLS_ALPHATEST_ENABLE | GLS_TEXTURE_REPLACE | GLS_SCROLL_ENABLE | \
                                 GLS_LIGHTMAP_ENABLE | GLS_WARP_ENABLE | GLS_INTENSITY_ENABLE | \
                                 GLS_GLOWMAP_ENABLE | GLS_SKY_MASK | GLS_DEFAULT_FLARE | GLS_MESH_MASK | \
                                 GLS_FOG_MASK | GLS_BLOOM_MASK | GLS_BLUR_MASK | GLS_DYNAMIC_LIGHTS)
#define GLS_UNIFORM_MASK        (GLS_WARP_ENABLE | GLS_LIGHTMAP_ENABLE | GLS_INTENSITY_ENABLE | \
                                 GLS_SKY_MASK | GLS_FOG_MASK | GLS_BLUR_MASK | GLS_DYNAMIC_LIGHTS)
#define GLS_SCROLL_MASK         (GLS_SCROLL_ENABLE | GLS_SCROLL_X | GLS_SCROLL_Y | GLS_SCROLL_FLIP | GLS_SCROLL_SLOW)

typedef enum {
    VERT_ATTR_POS,
    VERT_ATTR_TC,
    VERT_ATTR_LMTC,
    VERT_ATTR_COLOR,
    VERT_ATTR_NORMAL,
    VERT_ATTR_COUNT,

    // MD2
    VERT_ATTR_MESH_TC = 0,
    VERT_ATTR_MESH_NEW_POS = 1,
    VERT_ATTR_MESH_OLD_POS = 2,

    // MD5
    VERT_ATTR_MESH_NORM = VERT_ATTR_NORMAL,
    VERT_ATTR_MESH_VERT = 1,
} glVertexAttr_t;

typedef enum {
    GLA_NONE        = 0,
    GLA_VERTEX      = BIT(VERT_ATTR_POS),
    GLA_TC          = BIT(VERT_ATTR_TC),
    GLA_LMTC        = BIT(VERT_ATTR_LMTC),
    GLA_COLOR       = BIT(VERT_ATTR_COLOR),
    GLA_NORMAL      = BIT(VERT_ATTR_NORMAL),
    GLA_MESH_STATIC = MASK(2),
    GLA_MESH_LERP   = MASK(3),
} glArrayBits_t;

typedef enum {
    VA_NONE,

    VA_SPRITE,
    VA_EFFECT,
    VA_NULLMODEL,
    VA_OCCLUDE,
    VA_POSTPROCESS,
    VA_MESH_SHADE,
    VA_MESH_FLAT,  // also used for per-pixel lighting
    VA_2D,
    VA_3D,

    VA_TOTAL,

    VA_DEBUG = VA_OCCLUDE // VA_OCCLUDE happens to have the layout needed to debug lines
} glVertexArray_t;

// VA_2D
typedef struct {
    vec2_t      xy;
    vec2_t      st;
    uint32_t    c;
} glVertexDesc2D_t;

typedef enum {
    TMU_TEXTURE,
    TMU_LIGHTMAP,
    TMU_GLOWMAP,
    MAX_TMUS,

    // MD5
    TMU_SKEL_WEIGHTS,
    TMU_SKEL_JOINTNUMS,
} glTmu_t;

typedef enum {
    GLB_VBO,
    GLB_EBO,
    GLB_UBO,

    GLB_COUNT
} glBufferBinding_t;

enum { UBO_UNIFORMS, UBO_SKELETON, UBO_DLIGHTS };
enum { SSBO_WEIGHTS, SSBO_JOINTNUMS };

typedef struct {
    vec3_t    position;
    float     radius;
    vec4_t    color; // a = intensity
    vec4_t    cone; // a = angle
} glDlight_t;

typedef struct {
    vec4_t      oldscale;
    vec4_t      newscale;
    vec4_t      translate;
    vec4_t      shadedir;
    vec4_t      color;
    vec4_t      pad_0;
    GLfloat     pad_1;
    GLfloat     pad_2;
    GLfloat     pad_3;
    GLuint      weight_ofs;
    GLuint      jointnum_ofs;
    GLfloat     shellscale;
    GLfloat     backlerp;
    GLfloat     frontlerp;
} glMeshBlock_t;

typedef struct {
    mat4_t     m_model;
    mat4_t     m_view;
    mat4_t     m_proj;
    union {
        mat4_t          m_sky[2];
        glMeshBlock_t   mesh;
    };
    GLfloat     time;
    GLfloat     modulate;
    GLfloat     add;
    GLfloat     intensity;
    GLfloat     intensity2;
    GLfloat     fog_sky_factor;
    vec2_t      w_amp;
    vec2_t      w_phase;
    vec2_t      scroll;
    vec4_t      fog_color;
    vec4_t      heightfog_start;
    vec4_t      heightfog_end;
    GLfloat     heightfog_density;
    GLfloat     heightfog_falloff;
    GLfloat     pad;
    GLfloat     pad2;
    vec4_t      vieworg;
} glUniformBlock_t;

typedef struct {
    GLint          num_dlights;
    GLint          pad[3];
    glDlight_t     lights[MAX_DLIGHTS];
} glUniformDlights_t;

typedef struct {
    glTmu_t             client_tmu;
    glTmu_t             server_tmu;
    GLuint              texnums[MAX_TMUS];
    GLuint              texnumcube;
    glStateBits_t       state_bits;
    glArrayBits_t       array_bits;
    GLuint              currentbuffer[GLB_COUNT];
    glVertexArray_t     currentva;
    const GLfloat      *currentmodelmatrix;
    const GLfloat      *currentviewmatrix;
    mat4_t              view_matrix;
    mat4_t              proj_matrix;
    glUniformBlock_t    u_block;
    bool                u_block_dirty;
    glUniformDlights_t  u_dlights;
    uint64_t            dlight_bits;
} glState_t;

extern glState_t gls;

#define VBO_OFS(n)  ((void *)(n))

typedef struct {
    uint8_t size;
    bool type;
    uint8_t stride;
    uint8_t offset;
} glVaDesc_t;

typedef struct {
    const char *name;

    void (*init)(void);
    void (*shutdown)(void);
    void (*clear_state)(void);
    void (*setup_2d)(void);
    void (*setup_3d)(void);

    void (*load_matrix)(GLenum mode, const GLfloat *matrix, const GLfloat *view);
    void (*load_uniforms)(void);
    void (*update_blur)(void);

    void (*state_bits)(glStateBits_t bits);
    void (*array_bits)(glArrayBits_t bits);

    void (*array_pointers)(const glVaDesc_t *desc, const GLfloat *ptr);
    void (*tex_coord_pointer)(const GLfloat *ptr);

    void (*color)(GLfloat r, GLfloat g, GLfloat b, GLfloat a);

    bool (*use_per_pixel_lighting)(void);
    void (*load_lights)(void);
} glbackend_t;

extern const glbackend_t *gl_backend;

extern const mat4_t gl_identity;

static inline void GL_ActiveTexture(glTmu_t tmu)
{
    if (gls.server_tmu != tmu) {
        qglActiveTexture(GL_TEXTURE0 + tmu);
        gls.server_tmu = tmu;
    }
}

static inline void GL_ClientActiveTexture(glTmu_t tmu)
{
    if (gls.client_tmu != tmu) {
        qglClientActiveTexture(GL_TEXTURE0 + tmu);
        gls.client_tmu = tmu;
    }
}

static inline void GL_StateBits(glStateBits_t bits)
{
    if (gls.state_bits != bits) {
        gl_backend->state_bits(bits);
        gls.state_bits = bits;
    }
}

static inline void GL_ArrayBits(glArrayBits_t bits)
{
    if (gls.array_bits != bits) {
        gl_backend->array_bits(bits);
        gls.array_bits = bits;
    }
}

static inline void GL_ForceMatrix(const GLfloat *model, const GLfloat *view)
{
    gl_backend->load_matrix(GL_MODELVIEW, model, view);
    gls.currentmodelmatrix = model;
    gls.currentviewmatrix = view;
}

static inline void GL_LoadMatrix(const GLfloat *model, const GLfloat *view)
{
    if (gls.currentmodelmatrix != model ||
        gls.currentviewmatrix != view) {
        gl_backend->load_matrix(GL_MODELVIEW, model, view);
        gls.currentmodelmatrix = model;
        gls.currentviewmatrix = view;
    }
}

static inline void GL_LoadUniforms(void)
{
    if (gls.u_block_dirty && gl_backend->load_uniforms) {
        gl_backend->load_uniforms();
        gls.u_block_dirty = false;
    }
}

static inline void GL_LoadLights(void)
{
    if (gls.dlight_bits != glr.ppl_dlight_bits && gl_backend->load_lights) {
        gl_backend->load_lights();
        gls.dlight_bits = glr.ppl_dlight_bits;
    }
}

static inline glBufferBinding_t GL_BindingForTarget(GLenum target)
{
    switch (target) {
    case GL_ARRAY_BUFFER:
        return GLB_VBO;
    case GL_ELEMENT_ARRAY_BUFFER:
        return GLB_EBO;
    case GL_UNIFORM_BUFFER:
        return GLB_UBO;
    default:
        q_unreachable();
    }
}

static inline void GL_BindBuffer(GLenum target, GLuint buffer)
{
    glBufferBinding_t i = GL_BindingForTarget(target);
    if (gls.currentbuffer[i] != buffer) {
        qglBindBuffer(target, buffer);
        gls.currentbuffer[i] = buffer;
    }
}

static inline void GL_BindBufferBase(GLenum target, GLuint index, GLuint buffer)
{
    glBufferBinding_t i = GL_BindingForTarget(target);
    qglBindBufferBase(target, index, buffer);
    gls.currentbuffer[i] = buffer;
}

static inline void GL_ClearDepth(GLfloat d)
{
    if (qglClearDepthf)
        qglClearDepthf(d);
    else
        qglClearDepth(d);
}

static inline void GL_DepthRange(GLfloat n, GLfloat f)
{
    if (qglDepthRangef)
        qglDepthRangef(n, f);
    else
        qglDepthRange(n, f);
}

#define GL_Color(r, g, b, a) gl_backend->color(r, g, b, a)

typedef enum {
    SHOWTRIS_NONE   = 0,
    SHOWTRIS_WORLD  = BIT(0),
    SHOWTRIS_MESH   = BIT(1),
    SHOWTRIS_PIC    = BIT(2),
    SHOWTRIS_FX     = BIT(3),
} showtris_t;

void GL_ForceTexture(glTmu_t tmu, GLuint texnum);
void GL_BindTexture(glTmu_t tmu, GLuint texnum);
void GL_ForceCubemap(GLuint texnum);
void GL_BindCubemap(GLuint texnum);
void GL_DeleteBuffers(GLsizei n, const GLuint *buffers);
void GL_CommonStateBits(glStateBits_t bits);
void GL_ScrollPos(vec2_t scroll, glStateBits_t bits);
void GL_DrawOutlines(GLsizei count, GLenum type, const void *indices);
void GL_Ortho(GLfloat xmin, GLfloat xmax, GLfloat ymin, GLfloat ymax, GLfloat znear, GLfloat zfar);
void GL_Frustum(GLfloat fov_x, GLfloat fov_y, GLfloat reflect_x);
void GL_Setup2D(void);
void GL_Setup3D(void);
void GL_ClearState(void);
void GL_InitState(void);
void GL_ShutdownState(void);

/*
 * gl_draw.c
 *
 */
typedef struct {
    bool        scissor;
    float       scale;
} drawStatic_t;

extern drawStatic_t draw;

extern qhandle_t r_charset;

#if USE_DEBUG
void Draw_Lightmaps(void);
void Draw_Scrap(void);
#endif

void GL_Blend(void);


/*
 * gl_images.c
 *
 */

void Scrap_Upload(void);

void GL_InitImages(void);
void GL_ShutdownImages(void);

bool GL_InitFramebuffers(void);

extern cvar_t *gl_intensity;

/*
 * gl_tess.c
 *
 */
#define TESS_MAX_VERTICES   6144
#define TESS_MAX_INDICES    (3 * TESS_MAX_VERTICES)

typedef struct {
    GLfloat         vertices[VERTEX_SIZE * TESS_MAX_VERTICES];
    glIndex_t       indices[TESS_MAX_INDICES];
    GLuint          texnum[MAX_TMUS];
    int             numverts;
    int             numindices;
    glStateBits_t   flags;
    uint64_t        dlight_bits;
} tesselator_t;

extern tesselator_t tess;

void GL_Flush2D(void);
void GL_DrawParticles(void);
void GL_DrawBeams(void);
void GL_DrawFlares(void);

void GL_BindArrays(glVertexArray_t va);
void GL_LockArrays(GLsizei count);
void GL_UnlockArrays(void);
void GL_DrawIndexed(showtris_t showtris);
void GL_InitArrays(void);
void GL_ShutdownArrays(void);

void GL_Flush3D(void);

void GL_AddAlphaFace(mface_t *face);
void GL_AddSolidFace(mface_t *face);
void GL_DrawAlphaFaces(void);
void GL_DrawSolidFaces(void);
void GL_ClearSolidFaces(void);

/*
 * gl_world.c
 *
 */
void GL_DrawBspModel(mmodel_t *model);
void GL_DrawWorld(void);
void GL_SampleLightPoint(vec3_t color);
void GL_LightPoint(const vec3_t origin, vec3_t color);

/*
 * gl_sky.c
 *
 */
void R_AddSkySurface(const mface_t *surf);
void R_ClearSkyBox(void);
void R_DrawSkyBox(void);
void R_RotateForSky(void);
void R_SetSky(const char *name, float rotate, bool autorotate, const vec3_t axis);

/*
 * gl_mesh.c
 *
 */
void GL_DrawAliasModel(const model_t *model);

/*
 * hq2x.c
 *
 */
void HQ2x_Render(uint32_t *output, const uint32_t *input, int width, int height);
void HQ4x_Render(uint32_t *output, const uint32_t *input, int width, int height);
void HQ2x_Init(void);

/*
 * debug.c
 *
 */
void GL_InitDebugDraw(void);
void GL_ShutdownDebugDraw(void);
void GL_DrawDebugObjects(void);
void GL_ExpireDebugObjects(void);

/*
 * debug_text.c
 *
 */
void GL_InitDebugTextLines(void);
void GL_AddDebugTextLines(const vec3_t origin, const vec3_t angles, const char *text, float size, color_t color, uint32_t time, qboolean depth_test);
