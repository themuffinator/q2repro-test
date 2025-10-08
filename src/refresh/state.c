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

#include "gl.h"

glState_t gls;

const glbackend_t *gl_backend;

const mat4_t gl_identity = { [0] = 1, [5] = 1, [10] = 1, [15] = 1 };

// for uploading
void GL_ForceTexture(glTmu_t tmu, GLuint texnum)
{
    GL_ActiveTexture(tmu);

    if (gls.texnums[tmu] == texnum)
        return;

    qglBindTexture(GL_TEXTURE_2D, texnum);
    gls.texnums[tmu] = texnum;

    c.texSwitches++;
}

// for drawing
void GL_BindTexture(glTmu_t tmu, GLuint texnum)
{
#if USE_DEBUG
    if (gl_nobind->integer && tmu == TMU_TEXTURE)
        texnum = TEXNUM_DEFAULT;
#endif

    if (gls.texnums[tmu] == texnum)
        return;

    if (qglBindTextureUnit) {
        qglBindTextureUnit(tmu, texnum);
    } else {
        GL_ActiveTexture(tmu);
        qglBindTexture(GL_TEXTURE_2D, texnum);
    }
    gls.texnums[tmu] = texnum;

    c.texSwitches++;
}

void GL_ForceCubemap(GLuint texnum)
{
    GL_ActiveTexture(TMU_TEXTURE);

    if (gls.texnumcube == texnum)
        return;

    qglBindTexture(GL_TEXTURE_CUBE_MAP, texnum);
    gls.texnumcube = texnum;

    c.texSwitches++;
}

void GL_BindCubemap(GLuint texnum)
{
    if (!gl_drawsky->integer)
        texnum = TEXNUM_CUBEMAP_BLACK;

    if (gls.texnumcube == texnum)
        return;

    if (qglBindTextureUnit) {
        qglBindTextureUnit(TMU_TEXTURE, texnum);
    } else {
        GL_ActiveTexture(TMU_TEXTURE);
        qglBindTexture(GL_TEXTURE_CUBE_MAP, texnum);
    }
    gls.texnumcube = texnum;

    c.texSwitches++;
}

void GL_DeleteBuffers(GLsizei n, const GLuint *buffers)
{
    int i, j;

    for (i = 0; i < n; i++)
        if (buffers[i])
            break;
    if (i == n)
        return;

    Q_assert(qglDeleteBuffers);
    qglDeleteBuffers(n, buffers);

    // invalidate bindings
    for (i = 0; i < n; i++)
        for (j = 0; j < GLB_COUNT; j++)
            if (gls.currentbuffer[j] == buffers[i])
                gls.currentbuffer[j] = 0;
}

void GL_CommonStateBits(glStateBits_t bits)
{
    glStateBits_t diff = bits ^ gls.state_bits;

    if (diff & GLS_BLEND_MASK) {
        if (bits & GLS_BLEND_MASK) {
            qglEnable(GL_BLEND);
            if (bits & GLS_BLEND_BLEND)
                qglBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            else if (bits & GLS_BLEND_ADD)
                qglBlendFunc(GL_SRC_ALPHA, GL_ONE);
            else if (bits & GLS_BLEND_MODULATE)
                qglBlendFunc(GL_DST_COLOR, GL_ONE);
        } else {
            qglDisable(GL_BLEND);
        }
    }

    if (diff & GLS_DEPTHMASK_FALSE) {
        if (bits & GLS_DEPTHMASK_FALSE)
            qglDepthMask(GL_FALSE);
        else
            qglDepthMask(GL_TRUE);
    }

    if (diff & GLS_DEPTHTEST_DISABLE) {
        if (bits & GLS_DEPTHTEST_DISABLE)
            qglDisable(GL_DEPTH_TEST);
        else
            qglEnable(GL_DEPTH_TEST);
    }

    if (diff & GLS_CULL_DISABLE) {
        if (bits & GLS_CULL_DISABLE)
            qglDisable(GL_CULL_FACE);
        else
            qglEnable(GL_CULL_FACE);
    }
}

void GL_ScrollPos(vec2_t scroll, glStateBits_t bits)
{
    float speed = 1.6f;

    if (bits & (GLS_SCROLL_X | GLS_SCROLL_Y))
        speed = 0.78125f;
    else if (bits & GLS_SCROLL_SLOW)
        speed = 0.5f;

    if (bits & GLS_SCROLL_FLIP)
        speed = -speed;

    speed *= glr.fd.time;

    if (bits & GLS_SCROLL_Y) {
        scroll[0] = 0;
        scroll[1] = speed;
    } else {
        scroll[0] = -speed;
        scroll[1] = 0;
    }
}

void GL_Ortho(GLfloat xmin, GLfloat xmax, GLfloat ymin, GLfloat ymax, GLfloat znear, GLfloat zfar)
{
    GLfloat width, height, depth;
    mat4_t matrix;

    width  = xmax - xmin;
    height = ymax - ymin;
    depth  = zfar - znear;

    matrix[ 0] = 2 / width;
    matrix[ 4] = 0;
    matrix[ 8] = 0;
    matrix[12] = -(xmax + xmin) / width;

    matrix[ 1] = 0;
    matrix[ 5] = 2 / height;
    matrix[ 9] = 0;
    matrix[13] = -(ymax + ymin) / height;

    matrix[ 2] = 0;
    matrix[ 6] = 0;
    matrix[10] = -2 / depth;
    matrix[14] = -(zfar + znear) / depth;

    matrix[ 3] = 0;
    matrix[ 7] = 0;
    matrix[11] = 0;
    matrix[15] = 1;

    gl_backend->load_matrix(GL_PROJECTION, matrix, gl_identity);
}

void GL_Setup2D(void)
{
    qglViewport(0, 0, r_config.width, r_config.height);

    GL_Ortho(0, r_config.width, r_config.height, 0, -1, 1);
    draw.scale = 1;

    if (draw.scissor) {
        qglDisable(GL_SCISSOR_TEST);
        draw.scissor = false;
    }

    if (gl_backend->setup_2d)
        gl_backend->setup_2d();

    gl_backend->load_matrix(GL_MODELVIEW, gl_identity, gl_identity);
}

void GL_Frustum(GLfloat fov_x, GLfloat fov_y, GLfloat reflect_x)
{
    mat4_t matrix;

    float znear = gl_znear->value, zfar;

    if (glr.fd.rdflags & RDF_NOWORLDMODEL)
        zfar = 2048;
    else
        zfar = gl_static.world.size * 2;

    Matrix_Frustum(fov_x, fov_y, reflect_x, znear, zfar, matrix);
    gl_backend->load_matrix(GL_PROJECTION, matrix, gl_identity);
}

static void GL_RotateForViewer(void)
{
    GLfloat *matrix = glr.viewmatrix;

    AnglesToAxis(glr.fd.viewangles, glr.viewaxis);

    Matrix_FromOriginAxis(glr.fd.vieworg, glr.viewaxis, matrix);

    GL_ForceMatrix(glr.entmatrix, matrix);
}

void GL_Setup3D(void)
{
    if (glr.framebuffer_bound)
        qglViewport(0, 0, glr.fd.width, glr.fd.height);
    else
        qglViewport(glr.fd.x, r_config.height - (glr.fd.y + glr.fd.height),
                    glr.fd.width, glr.fd.height);

    if (gl_backend->setup_3d)
        gl_backend->setup_3d();

    GL_Frustum(glr.fd.fov_x, glr.fd.fov_y, 1.0f);

    GL_RotateForViewer();

    // enable depth writes before clearing
    GL_StateBits(GLS_DEFAULT);

    // clear both wanted & active dlight bits
    gls.dlight_bits = glr.ppl_dlight_bits = 0;

    qglClear(GL_DEPTH_BUFFER_BIT | gl_static.stencil_buffer_bit);
}

void GL_DrawOutlines(GLsizei count, GLenum type, const void *indices)
{
    GL_BindTexture(TMU_TEXTURE, TEXNUM_WHITE);
    GL_StateBits(GLS_DEPTHMASK_FALSE | GLS_TEXTURE_REPLACE | (gls.state_bits & GLS_MESH_MASK));
    if (gls.currentva)
        GL_ArrayBits(GLA_VERTEX);
    GL_DepthRange(0, 0);

    if (qglPolygonMode) {
        qglPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

        if (type)
            qglDrawElements(GL_TRIANGLES, count, type, indices);
        else
            qglDrawArrays(GL_TRIANGLES, 0, count);

        qglPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    } else if (type) {
        uintptr_t base = (uintptr_t)indices;
        uintptr_t size = 0;

        switch (type) {
        case GL_UNSIGNED_INT:
            size = 4 * 3;
            break;
        case GL_UNSIGNED_SHORT:
            size = 2 * 3;
            break;
        default:
            Q_assert(!"bad type");
        }

        for (int i = 0; i < count / 3; i++, base += size)
            qglDrawElements(GL_LINE_LOOP, 3, type, VBO_OFS(base));
    } else {
        for (int i = 0; i < count / 3; i++)
            qglDrawArrays(GL_LINE_LOOP, i * 3, 3);
    }

    GL_DepthRange(0, 1);
}

void GL_ClearState(void)
{
    qglClearColor(Vector4Unpack(gl_static.clearcolor));
    GL_ClearDepth(1);
    qglClearStencil(0);

    qglEnable(GL_DEPTH_TEST);
    qglDepthFunc(GL_LEQUAL);
    GL_DepthRange(0, 1);
    qglDepthMask(GL_TRUE);
    qglDisable(GL_BLEND);
    qglFrontFace(GL_CW);
    qglCullFace(GL_BACK);
    qglEnable(GL_CULL_FACE);

    // unbind buffers
    if (qglBindBuffer) {
        qglBindBuffer(GL_ARRAY_BUFFER, 0);
        qglBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    }

    gl_backend->clear_state();

    qglClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | gl_static.stencil_buffer_bit);

    memset(&gls, 0, sizeof(gls));
    GL_ShowErrors(__func__);
}

extern const glbackend_t backend_legacy;
extern const glbackend_t backend_shader;

void GL_InitState(void)
{
    gl_static.use_shaders = gl_shaders->integer > 0;

    if (gl_static.use_shaders) {
        if (!(gl_config.caps & QGL_CAP_SHADER)) {
            Com_WPrintf("GLSL rendering backend not available.\n");
            gl_static.use_shaders = false;
            Cvar_Set("gl_shaders", "0");
        }
    } else {
        if (!(gl_config.caps & QGL_CAP_LEGACY)) {
            Com_WPrintf("Legacy rendering backend not available.\n");
            gl_static.use_shaders = true;
            Cvar_Set("gl_shaders", "1");
        }
    }

    gl_shaders_modified = gl_shaders->modified_count;

    gl_backend = gl_static.use_shaders ? &backend_shader : &backend_legacy;
    gl_backend->init();

    Com_Printf("Using %s rendering backend.\n", gl_backend->name);
}

void GL_ShutdownState(void)
{
    if (gl_backend) {
        gl_backend->shutdown();
        gl_backend = NULL;
    }
}
