/*
Inter-Quake Model (IQM) format definitions
*/

#pragma once

#define IQM_MAGIC       "INTERQUAKEMODEL"
#define IQM_VERSION     2

#define IQM_POSITION        0
#define IQM_TEXCOORD        1
#define IQM_NORMAL          2
#define IQM_TANGENT         3
#define IQM_BLENDINDEXES    4
#define IQM_BLENDWEIGHTS    5
#define IQM_COLOR           6

#define IQM_BYTE        0
#define IQM_UBYTE       1
#define IQM_SHORT       2
#define IQM_USHORT      3
#define IQM_INT         4
#define IQM_UINT        5
#define IQM_HALF        6
#define IQM_FLOAT       7
#define IQM_DOUBLE      8

#define IQM_LOOP        1

typedef struct {
    char        magic[16];
    uint32_t    version;
    uint32_t    filesize;
    uint32_t    flags;
    uint32_t    num_text, ofs_text;
    uint32_t    num_meshes, ofs_meshes;
    uint32_t    num_vertexarrays, num_vertexes, ofs_vertexarrays;
    uint32_t    num_triangles, ofs_triangles, ofs_adjacency;
    uint32_t    num_joints, ofs_joints;
    uint32_t    num_poses, ofs_poses;
    uint32_t    num_anims, ofs_anims;
    uint32_t    num_frames, num_framechannels, ofs_frames;
    uint32_t    num_bounds, ofs_bounds;
    uint32_t    num_comment, ofs_comment;
    uint32_t    num_extensions, ofs_extensions;
} iqmheader_t;

typedef struct {
    uint32_t    name;
    uint32_t    material;
    uint32_t    first_vertex, num_vertexes;
    uint32_t    first_triangle, num_triangles;
} iqmmesh_t;

typedef struct {
    uint32_t    type;
    uint32_t    flags;
    uint32_t    format;
    uint32_t    size;
    uint32_t    offset;
} iqmvertexarray_t;

typedef struct {
    uint32_t    vertex[3];
} iqmtriangle_t;

typedef struct {
    int32_t     name;
    int32_t     parent;
    float       translate[3];
    float       rotate[4];
    float       scale[3];
} iqmjoint_t;

typedef struct {
    int32_t     parent;
    uint32_t    mask;
    float       channeloffset[10];
    float       channelscale[10];
} iqmpose_t;

typedef struct {
    uint32_t    name;
    uint32_t    first_frame;
    uint32_t    num_frames;
    float       framerate;
    uint32_t    flags;
} iqmanim_t;
