/*
Copyright (C) 1997-2001 Id Software, Inc.
Copyright (C) 2003-2008 Andrey Nazarov

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

//
// images.c -- image reading and writing functions
//

#include "shared/shared.h"
#include "common/async.h"
#include "common/common.h"
#include "common/cvar.h"
#include "common/files.h"
#include "common/intreadwrite.h"
#include "common/sizebuf.h"
#include "system/system.h"
#include "format/pcx.h"
#include "format/wal.h"
#include "images.h"

#if USE_PNG
#define PNG_SKIP_SETJMP_CHECK
#include <png.h>
#endif // USE_PNG

#if USE_JPG
#include <jpeglib.h>
#endif

#include <setjmp.h>

#define R_COLORMAP_PCX    "pics/colormap.pcx"

#define IMG_LOAD(x) \
    static int IMG_Load##x(const byte *rawdata, size_t rawlen, \
        image_t *image, byte **pic)

static bool check_image_size(unsigned w, unsigned h)
{
    return (w < 1 || h < 1 || w > MAX_TEXTURE_SIZE || h > MAX_TEXTURE_SIZE);
}

/*
====================================================================

IMAGE FLOOD FILLING

====================================================================
*/

typedef struct {
    uint16_t    x, y;
} floodfill_t;

// must be a power of 2
#define FLOODFILL_FIFO_SIZE 0x1000
#define FLOODFILL_FIFO_MASK (FLOODFILL_FIFO_SIZE - 1)

#define FLOODFILL_STEP(off, dx, dy)                     \
    do {                                                \
        if (pos[off] == fillcolor) {                    \
            pos[off] = 255;                             \
            fifo[inpt].x = x + (dx);                    \
            fifo[inpt].y = y + (dy);                    \
            inpt = (inpt + 1) & FLOODFILL_FIFO_MASK;    \
        } else if (pos[off] != 255) {                   \
            fdc = pos[off];                             \
        }                                               \
    } while (0)

/*
=================
IMG_FloodFill

Fill background pixels so mipmapping doesn't have haloes
=================
*/
static void IMG_FloodFill(byte *skin, int skinwidth, int skinheight)
{
    byte                fillcolor = *skin; // assume this is the pixel to fill
    floodfill_t         fifo[FLOODFILL_FIFO_SIZE];
    int                 inpt = 0, outpt = 0;
    int                 filledcolor = 0; // FIXME: fixed black

    // can't fill to filled color or to transparent color
    // (used as visited marker)
    if (fillcolor == filledcolor || fillcolor == 255)
        return;

    fifo[inpt].x = 0, fifo[inpt].y = 0;
    inpt = (inpt + 1) & FLOODFILL_FIFO_MASK;

    while (outpt != inpt) {
        int         x = fifo[outpt].x, y = fifo[outpt].y;
        int         fdc = filledcolor;
        byte        *pos = &skin[x + skinwidth * y];

        outpt = (outpt + 1) & FLOODFILL_FIFO_MASK;

        if (x > 0) FLOODFILL_STEP(-1, -1, 0);
        if (x < skinwidth - 1) FLOODFILL_STEP(1, 1, 0);
        if (y > 0) FLOODFILL_STEP(-skinwidth, 0, -1);
        if (y < skinheight - 1) FLOODFILL_STEP(skinwidth, 0, 1);

        skin[x + skinwidth * y] = fdc;
    }
}

/*
===============
IMG_Unpack8
===============
*/
static int IMG_Unpack8(uint32_t *out, const uint8_t *in, int width, int height)
{
    int         x, y, p;
    bool        has_alpha = false;

    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            p = *in;
            if (p == 255) {
                has_alpha = true;
                // transparent, so scan around for another color
                // to avoid alpha fringes
                if (y > 0 && *(in - width) != 255)
                    p = *(in - width);
                else if (y < height - 1 && *(in + width) != 255)
                    p = *(in + width);
                else if (x > 0 && *(in - 1) != 255)
                    p = *(in - 1);
                else if (x < width - 1 && *(in + 1) != 255)
                    p = *(in + 1);
                else if (y > 0 && x > 0 && *(in - width - 1) != 255)
                    p = *(in - width - 1);
                else if (y > 0 && x < width - 1 && *(in - width + 1) != 255)
                    p = *(in - width + 1);
                else if (y < height - 1 && x > 0 && *(in + width - 1) != 255)
                    p = *(in + width - 1);
                else if (y < height - 1 && x < width - 1 && *(in + width + 1) != 255)
                    p = *(in + width + 1);
                else
                    p = 0;
                // copy rgb components
                *out = COLOR_SETA_U8(COLOR_U32(d_8to24table[p]), 0).u32;
            } else {
                *out = d_8to24table[p];
            }
            in++;
            out++;
        }
    }

    if (has_alpha)
        return IF_PALETTED | IF_TRANSPARENT;

    return IF_PALETTED | IF_OPAQUE;
}

/*
=================================================================

PCX LOADING

=================================================================
*/

#define PCX_PALETTE_SIZE    768

static int uncompress_pcx(sizebuf_t *s, int scan, byte *pix)
{
    for (int x = 0; x < scan;) {
        int dataByte, runLength;

        if ((dataByte = SZ_ReadByte(s)) == -1)
            return Q_ERR_UNEXPECTED_EOF;

        if ((dataByte & 0xC0) == 0xC0) {
            runLength = dataByte & 0x3F;
            if (x + runLength > scan)
                return Q_ERR_OVERRUN;
            if ((dataByte = SZ_ReadByte(s)) == -1)
                return Q_ERR_UNEXPECTED_EOF;
        } else {
            runLength = 1;
        }

        while (runLength--)
            pix[x++] = dataByte;
    }

    return Q_ERR_SUCCESS;
}

static int load_pcx(const byte *rawdata, size_t rawlen,
                    image_t *image, byte *palette, byte **pic)
{
    const dpcx_t    *pcx;
    int             w, h, bytes_per_line;

    //
    // parse the PCX file
    //
    if (rawlen < sizeof(dpcx_t))
        return Q_ERR_FILE_TOO_SMALL;

    pcx = (const dpcx_t *)rawdata;

    if (pcx->manufacturer != 10 || pcx->version != 5)
        return Q_ERR_UNKNOWN_FORMAT;

    if (pcx->encoding != 1 || pcx->bits_per_pixel != 8) {
        Com_SetLastError("Unsupported encoding or bits per pixel");
        return Q_ERR_INVALID_FORMAT;
    }

    w = (LittleShort(pcx->xmax) - LittleShort(pcx->xmin)) + 1;
    h = (LittleShort(pcx->ymax) - LittleShort(pcx->ymin)) + 1;
    if (check_image_size(w, h)) {
        Com_SetLastError("Invalid image dimensions");
        return Q_ERR_INVALID_FORMAT;
    }

    if (pcx->color_planes != 1 && (palette || pcx->color_planes != 3)) {
        Com_SetLastError("Unsupported number of color planes");
        return Q_ERR_INVALID_FORMAT;
    }

    bytes_per_line = LittleShort(pcx->bytes_per_line);
    if (bytes_per_line < w) {
        Com_SetLastError("Invalid number of bytes per line");
        return Q_ERR_INVALID_FORMAT;
    }

    //
    // get palette
    //
    if (palette) {
        if (rawlen < PCX_PALETTE_SIZE)
            return Q_ERR_FILE_TOO_SMALL;
        memcpy(palette, rawdata + rawlen - PCX_PALETTE_SIZE, PCX_PALETTE_SIZE);
    }

    //
    // get pixels
    //
    if (image) {
        int bytes_per_scanline = bytes_per_line * pcx->color_planes;
        bool is_pal = pcx->color_planes == 1;
        byte *scanline, *pixels, *out;
        sizebuf_t s;
        int ret = 0;

        SZ_InitRead(&s, rawdata, rawlen);
        s.readcount = offsetof(dpcx_t, data);

        out = pixels = IMG_AllocPixels(w * h * (is_pal ? 1 : 4));
        scanline = IMG_AllocPixels(bytes_per_scanline);

        for (int y = 0; y < h; y++) {
            ret = uncompress_pcx(&s, bytes_per_scanline, scanline);
            if (ret < 0)
                break;
            if (is_pal) {
                memcpy(out, scanline, w);
                out += w;
            } else {
                for (int x = 0; x < w; x++, out += 4) {
                    out[0] = scanline[x];
                    out[1] = scanline[x + bytes_per_line];
                    out[2] = scanline[x + bytes_per_line * 2];
                    out[3] = 255;
                }
            }
        }

        IMG_FreePixels(scanline);

        if (ret < 0) {
            IMG_FreePixels(pixels);
            return ret;
        }

        if (is_pal) {
            if (SZ_Remaining(&s) < PCX_PALETTE_SIZE)
                Com_WPrintf("PCX file %s possibly corrupted\n", image->name);

            if (image->type == IT_SKIN)
                IMG_FloodFill(pixels, w, h);

            *pic = IMG_AllocPixels(w * h * 4);

            image->flags |= IMG_Unpack8((uint32_t *)*pic, pixels, w, h);

            IMG_FreePixels(pixels);
        } else {
            Com_DWPrintf("%s is a 24-bit PCX file. This is not portable.\n", image->name);
            *pic = pixels;
            image->flags |= IF_OPAQUE;
        }

        image->upload_width = image->width = w;
        image->upload_height = image->height = h;
    }

    return Q_ERR_SUCCESS;
}

IMG_LOAD(PCX)
{
    return load_pcx(rawdata, rawlen, image, NULL, pic);
}

/*
=================================================================

WAL LOADING

=================================================================
*/

IMG_LOAD(WAL)
{
    const miptex_t  *mt;
    unsigned        w, h, offset;

    if (rawlen < sizeof(miptex_t))
        return Q_ERR_FILE_TOO_SMALL;

    mt = (const miptex_t *)rawdata;

    w = LittleLong(mt->width);
    h = LittleLong(mt->height);
    if (check_image_size(w, h))  {
        Com_SetLastError("Invalid image dimensions");
        return Q_ERR_INVALID_FORMAT;
    }

    offset = LittleLong(mt->offsets[0]);
    if ((uint64_t)offset + w * h > rawlen) {
        Com_SetLastError("Data out of bounds");
        return Q_ERR_INVALID_FORMAT;
    }

    *pic = IMG_AllocPixels(w * h * 4);

    image->upload_width = image->width = w;
    image->upload_height = image->height = h;
    image->flags |= IMG_Unpack8((uint32_t *)*pic, rawdata + offset, w, h);

    return Q_ERR_SUCCESS;
}

/*
=========================================================

TARGA IMAGES

=========================================================
*/

#if USE_TGA

#define TARGA_HEADER_SIZE  18

enum {
    TGA_Colormap = 1,
    TGA_RGB = 2,
    TGA_Mono = 3,
    TGA_RLE = 8
};

enum {
    TGA_RIGHTTOLEFT     = BIT(4),
    TGA_TOPTOBOTTOM     = BIT(5),
    TGA_INTERLEAVE_2    = BIT(6),
    TGA_INTERLEAVE_4    = BIT(7),
};

static uint32_t tga_unpack_pixel(const byte *in, int bpp)
{
    int r, g, b;

    switch (bpp) {
    case 1:
        r = in[0];
        return COLOR_RGB(r, r, r).u32;
    case 2:
        r = (in[1] & 0x7C) >> 2;
        g = ((in[1] & 0x03) << 3) | ((in[0] & 0xE0) >> 5);
        b = (in[0] & 0x1F);
        return COLOR_RGB(r << 3, g << 3, b << 3).u32;
    case 3:
        return COLOR_RGB(in[2], in[1], in[0]).u32;
    case 4:
        return COLOR_RGBA(in[2], in[1], in[0], in[3]).u32;
    default:
        q_unreachable();
    }
}

static int tga_decode_raw(sizebuf_t *s, uint32_t **row_pointers,
                          int cols, int rows, int bpp,
                          const uint32_t *palette)
{
    int col, row;
    uint32_t *out_row;
    const byte *in;

    in = SZ_ReadData(s, cols * rows * bpp);
    if (!in)
        return Q_ERR_UNEXPECTED_EOF;

    for (row = 0; row < rows; row++) {
        out_row = row_pointers[row];
        if (palette) {
            for (col = 0; col < cols; col++, in++)
                *out_row++ = palette[*in];
        } else {
            for (col = 0; col < cols; col++, in += bpp)
                *out_row++ = tga_unpack_pixel(in, bpp);
        }
    }

    return Q_ERR_SUCCESS;
}

static int tga_decode_rle(sizebuf_t *s, uint32_t **row_pointers,
                          int cols, int rows, int bpp,
                          const uint32_t *palette)
{
    int col, row;
    uint32_t *out_row;
    uint32_t color;
    const byte *in;
    int packet_header, packet_size;

    col = row = 0;
    out_row = row_pointers[row];

    while (1) {
        packet_header = SZ_ReadByte(s);
        packet_size = 1 + (packet_header & 0x7f);

        if (packet_header & 0x80) {
            in = SZ_ReadData(s, bpp);
            if (!in)
                return Q_ERR_UNEXPECTED_EOF;

            if (palette)
                color = palette[*in];
            else
                color = tga_unpack_pixel(in, bpp);
            do {
                *out_row++ = color;
                packet_size--;

                if (++col == cols) {
                    col = 0;
                    if (++row == rows)
                        goto done;
                    out_row = row_pointers[row];
                }
            } while (packet_size);
        } else {
            in = SZ_ReadData(s, bpp * packet_size);
            if (!in)
                return Q_ERR_UNEXPECTED_EOF;

            do {
                if (palette)
                    color = palette[*in];
                else
                    color = tga_unpack_pixel(in, bpp);
                *out_row++ = color;
                in += bpp;
                packet_size--;

                if (++col == cols) {
                    col = 0;
                    if (++row == rows)
                        goto done;
                    out_row = row_pointers[row];
                }
            } while (packet_size);
        }
    }

done:
    if (packet_size)
        return Q_ERR_OVERRUN;

    return Q_ERR_SUCCESS;
}

IMG_LOAD(TGA)
{
    byte *pixels, *start;
    uint32_t *row_pointers[MAX_TEXTURE_SIZE];
    uint32_t colormap[256];
    const uint32_t *pal;
    sizebuf_t s;
    unsigned id_length, colormap_type, image_type, colormap_start,
             colormap_length, colormap_size, w, h, pixel_size, attributes;
    bool rle;
    int i, j, ret, bpp, stride, interleave;

    if (rawlen < TARGA_HEADER_SIZE)
        return Q_ERR_FILE_TOO_SMALL;

    SZ_InitRead(&s, rawdata, rawlen);

    id_length       = SZ_ReadByte(&s);
    colormap_type   = SZ_ReadByte(&s);
    image_type      = SZ_ReadByte(&s);
    colormap_start  = SZ_ReadWord(&s);
    colormap_length = SZ_ReadWord(&s);
    colormap_size   = SZ_ReadByte(&s);
    s.readcount     += 4;
    w               = SZ_ReadWord(&s);
    h               = SZ_ReadWord(&s);
    pixel_size      = SZ_ReadByte(&s);
    attributes      = SZ_ReadByte(&s);

    rle = image_type & TGA_RLE;
    image_type &= ~TGA_RLE;

    switch (image_type) {
    case TGA_Colormap:
    case TGA_RGB:
    case TGA_Mono:
        break;
    default:
        Com_SetLastError("Unsupported targa image type");
        return Q_ERR_INVALID_FORMAT;
    }

    if (check_image_size(w, h)) {
        Com_SetLastError("Invalid image dimensions");
        return Q_ERR_INVALID_FORMAT;
    }

    switch (pixel_size) {
    case 8:
    case 15:
    case 16:
    case 24:
    case 32:
        break;
    default:
        Com_SetLastError("Unsupported number of bits per pixel");
        return Q_ERR_INVALID_FORMAT;
    }

    switch (attributes & (TGA_INTERLEAVE_2 | TGA_INTERLEAVE_4)) {
    case 0:
        interleave = 1;
        break;
    case TGA_INTERLEAVE_2:
        interleave = 2;
        break;
    case TGA_INTERLEAVE_4:
        interleave = 4;
        break;
    default:
        Com_SetLastError("Unsupported interleaving flag");
        return Q_ERR_INVALID_FORMAT;
    }

    if (image_type == TGA_Colormap) {
        if (!colormap_type) {
            Com_SetLastError("Colormapped image but no colormap present");
            return Q_ERR_INVALID_FORMAT;
        }
        if (pixel_size != 8) {
            Com_SetLastError("Only 8-bit colormaps are supported");
            return Q_ERR_INVALID_FORMAT;
        }
    }

    // skip TARGA image comment
    if (!SZ_ReadData(&s, id_length))
        return Q_ERR_UNEXPECTED_EOF;

    // read the colormap
    if (colormap_type) {
        int colormap_bpp = (colormap_size + 1) / 8;
        const byte *in;

        switch (colormap_size) {
        case 15:
        case 16:
        case 24:
        case 32:
            break;
        default:
            Com_SetLastError("Unsupported number of bits per colormap pixel");
            return Q_ERR_INVALID_FORMAT;
        }

        if (colormap_start + colormap_length > 256) {
            Com_SetLastError("Too many colormap entries");
            return Q_ERR_INVALID_FORMAT;
        }

        in = SZ_ReadData(&s, colormap_length * colormap_bpp);
        if (!in)
            return Q_ERR_UNEXPECTED_EOF;

        // don't bother unpacking palette unless we need it
        if (image_type == TGA_Colormap) {
            if (colormap_length != 256)
                memset(colormap, 0, sizeof(colormap));

            for (i = 0; i < colormap_length; i++) {
                colormap[colormap_start + i] = tga_unpack_pixel(in, colormap_bpp);
                in += colormap_bpp;
            }
        }
    }

    stride = w * 4;
    start = pixels = IMG_AllocPixels(h * stride);

    if (!(attributes & TGA_TOPTOBOTTOM)) {
        start += (h - 1) * stride;
        stride = -stride;
    }

    for (i = j = 0; i < h; i++) {
        row_pointers[i] = (uint32_t *)(start + j * stride);
        j += interleave;
        if (j >= h)
            j = (j + 1) & (interleave - 1);
    }

    bpp = (pixel_size + 1) / 8;
    pal = image_type == TGA_Colormap ? colormap : NULL;

    if (rle)
        ret = tga_decode_rle(&s, row_pointers, w, h, bpp, pal);
    else
        ret = tga_decode_raw(&s, row_pointers, w, h, bpp, pal);
    if (ret < 0) {
        IMG_FreePixels(pixels);
        return ret;
    }

    if (attributes & TGA_RIGHTTOLEFT)
        for (i = 0; i < h; i++)
            for (j = 0; j < w / 2; j++)
                SWAP(uint32_t, row_pointers[i][j], row_pointers[i][w - j - 1]);

    *pic = pixels;

    image->upload_width = image->width = w;
    image->upload_height = image->height = h;

    if (image_type == TGA_Colormap) {
        image->flags |= IF_PALETTED;
        pixel_size = colormap_size;
    }

    if (pixel_size == 8)
        image->flags |= IF_TRANSPARENT;
    else if (pixel_size != 32)
        image->flags |= IF_OPAQUE;

    return Q_ERR_SUCCESS;
}

static int IMG_SaveTGA(const screenshot_t *s)
{
    byte header[TARGA_HEADER_SIZE] = { 0 };

    header[ 2] = 2;     // uncompressed type
    WL16(&header[12], s->width);
    WL16(&header[14], s->height);
    header[16] = 24;    // pixel size

    if (!fwrite(&header, sizeof(header), 1, s->fp))
        return Q_ERR_FAILURE;

    byte *row = malloc(s->width * 3);
    if (!row)
        return Q_ERR(ENOMEM);

    int ret = Q_ERR_SUCCESS;
    for (int i = 0; i < s->height; i++) {
        byte *src = s->pixels + i * s->rowbytes;
        byte *dst = row;
        for (int j = 0; j < s->width; j++, src += s->bpp, dst += 3) {
            dst[0] = src[2];
            dst[1] = src[1];
            dst[2] = src[0];
        }
        if (!fwrite(row, s->width * 3, 1, s->fp)) {
            ret = Q_ERR_FAILURE;
            break;
        }
    }

    free(row);
    return ret;
}

#endif // USE_TGA

/*
=========================================================

JPEG IMAGES

=========================================================
*/

#if USE_JPG

typedef struct my_error_mgr {
    struct jpeg_error_mgr   pub;
    jmp_buf                 setjmp_buffer;
    const char              *filename;
} *my_error_ptr;

static void my_output_message_2(j_common_ptr cinfo, bool err_exit)
{
    char buffer[JMSG_LENGTH_MAX];
    my_error_ptr jerr = (my_error_ptr)cinfo->err;

    if (!jerr->filename)
        return;

    (*cinfo->err->format_message)(cinfo, buffer);

    if (err_exit)
        Com_SetLastError(buffer);
    else
        Com_WPrintf("libjpeg: %s: %s\n", jerr->filename, buffer);
}

static void my_output_message(j_common_ptr cinfo)
{
    my_output_message_2(cinfo, false);
}

static void my_error_exit(j_common_ptr cinfo)
{
    my_error_ptr jerr = (my_error_ptr)cinfo->err;

    my_output_message_2(cinfo, true);

    longjmp(jerr->setjmp_buffer, 1);
}

static int my_jpeg_start_decompress(j_decompress_ptr cinfo, const byte *rawdata, size_t rawlen)
{
    my_error_ptr jerr = (my_error_ptr)cinfo->err;

    if (setjmp(jerr->setjmp_buffer))
        return Q_ERR_LIBRARY_ERROR;

    jpeg_create_decompress(cinfo);
    jpeg_mem_src(cinfo, rawdata, rawlen);
    jpeg_read_header(cinfo, TRUE);

    if (cinfo->out_color_space != JCS_RGB && cinfo->out_color_space != JCS_GRAYSCALE) {
        Com_SetLastError("Invalid image color space");
        return Q_ERR_INVALID_FORMAT;
    }

    cinfo->out_color_space = JCS_EXT_RGBA;

    jpeg_start_decompress(cinfo);

    if (cinfo->output_components != 4) {
        Com_SetLastError("Invalid number of color components");
        return Q_ERR_INVALID_FORMAT;
    }

    if (check_image_size(cinfo->output_width, cinfo->output_height)) {
        Com_SetLastError("Invalid image dimensions");
        return Q_ERR_INVALID_FORMAT;
    }

    return Q_ERR_SUCCESS;
}

static int my_jpeg_finish_decompress(j_decompress_ptr cinfo, JSAMPARRAY row_pointers)
{
    my_error_ptr jerr = (my_error_ptr)cinfo->err;

    if (setjmp(jerr->setjmp_buffer))
        return Q_ERR_LIBRARY_ERROR;

    while (cinfo->output_scanline < cinfo->output_height)
        jpeg_read_scanlines(cinfo, &row_pointers[cinfo->output_scanline], cinfo->output_height - cinfo->output_scanline);

    jpeg_finish_decompress(cinfo);
    return Q_ERR_SUCCESS;
}

IMG_LOAD(JPG)
{
    JSAMPROW row_pointers[MAX_TEXTURE_SIZE];
    struct jpeg_decompress_struct cinfo;
    struct my_error_mgr jerr;
    byte *pixels;
    int ret, row, rowbytes;

    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = my_error_exit;
    jerr.pub.output_message = my_output_message;
    jerr.filename = image->name;

    ret = my_jpeg_start_decompress(&cinfo, rawdata, rawlen);
    if (ret < 0)
        goto fail;

    image->upload_width = image->width = cinfo.output_width;
    image->upload_height = image->height = cinfo.output_height;
    image->flags |= IF_OPAQUE;

    rowbytes = cinfo.output_width * 4;
    pixels = IMG_AllocPixels(cinfo.output_height * rowbytes);

    for (row = 0; row < cinfo.output_height; row++)
        row_pointers[row] = (JSAMPROW)(pixels + row * rowbytes);

    ret = my_jpeg_finish_decompress(&cinfo, row_pointers);
    if (ret < 0) {
        IMG_FreePixels(pixels);
        goto fail;
    }

    *pic = pixels;
fail:
    jpeg_destroy_decompress(&cinfo);
    return ret;
}

static int my_jpeg_compress(j_compress_ptr cinfo, JSAMPARRAY row_pointers, const screenshot_t *s)
{
    my_error_ptr jerr = (my_error_ptr)cinfo->err;

    if (setjmp(jerr->setjmp_buffer))
        return Q_ERR_LIBRARY_ERROR;

    jpeg_create_compress(cinfo);
    jpeg_stdio_dest(cinfo, s->fp);

    cinfo->image_width = s->width;
    cinfo->image_height = s->height;
    cinfo->input_components = s->bpp;
    cinfo->in_color_space = s->bpp == 4 ? JCS_EXT_RGBA : JCS_RGB;

    jpeg_set_defaults(cinfo);
    jpeg_set_quality(cinfo, Q_clip(s->param, 0, 100), TRUE);

    jpeg_start_compress(cinfo, TRUE);
    jpeg_write_scanlines(cinfo, row_pointers, s->height);
    jpeg_finish_compress(cinfo);

    return Q_ERR_SUCCESS;
}

static int IMG_SaveJPG(const screenshot_t *s)
{
    struct jpeg_compress_struct cinfo;
    struct my_error_mgr jerr;
    JSAMPARRAY row_pointers;
    int i, ret;

    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = my_error_exit;
    jerr.pub.output_message = my_output_message;
    jerr.filename = s->async ? NULL : s->filename;

    row_pointers = malloc(sizeof(JSAMPROW) * s->height);
    if (!row_pointers)
        return Q_ERR(ENOMEM);

    for (i = 0; i < s->height; i++)
        row_pointers[i] = (JSAMPROW)(s->pixels + (s->height - i - 1) * s->rowbytes);

    ret = my_jpeg_compress(&cinfo, row_pointers, s);
    free(row_pointers);
    jpeg_destroy_compress(&cinfo);
    return ret;
}

#endif // USE_JPG

/*
=========================================================

PNG IMAGES

=========================================================
*/

#if USE_PNG

typedef struct {
    png_const_bytep next_in;
    png_size_t avail_in;
} my_png_io;

typedef struct {
    jmp_buf setjmp_buffer;
    png_const_charp filename;
} my_png_error;

static void my_png_read_fn(png_structp png_ptr, png_bytep buf, png_size_t size)
{
    my_png_io *io = png_get_io_ptr(png_ptr);

    if (size > io->avail_in) {
        png_error(png_ptr, "read error");
    } else {
        memcpy(buf, io->next_in, size);
        io->next_in += size;
        io->avail_in -= size;
    }
}

static void my_png_error_fn(png_structp png_ptr, png_const_charp error_msg)
{
    my_png_error *err = png_get_error_ptr(png_ptr);

    if (err->filename)
        Com_SetLastError(error_msg);

    longjmp(err->setjmp_buffer, -1);
}

static void my_png_warning_fn(png_structp png_ptr, png_const_charp warning_msg)
{
    my_png_error *err = png_get_error_ptr(png_ptr);

    if (err->filename)
        Com_WPrintf("libpng: %s: %s\n", err->filename, warning_msg);
}

static int my_png_read_header(png_structp png_ptr, png_infop info_ptr,
                              png_voidp io_ptr, image_t *image)
{
    my_png_error *err = png_get_error_ptr(png_ptr);
    png_uint_32 w, h, has_tRNS;
    int bitdepth, colortype;

    if (setjmp(err->setjmp_buffer))
        return Q_ERR_LIBRARY_ERROR;

    png_set_read_fn(png_ptr, io_ptr, my_png_read_fn);

    png_read_info(png_ptr, info_ptr);

    if (!png_get_IHDR(png_ptr, info_ptr, &w, &h, &bitdepth, &colortype, NULL, NULL, NULL))
        return Q_ERR_FAILURE;

    if (check_image_size(w, h)) {
        Com_SetLastError("Invalid image dimensions");
        return Q_ERR_INVALID_FORMAT;
    }

    switch (colortype) {
    case PNG_COLOR_TYPE_PALETTE:
        png_set_palette_to_rgb(png_ptr);
        break;
    case PNG_COLOR_TYPE_GRAY:
        if (bitdepth < 8)
            png_set_expand_gray_1_2_4_to_8(png_ptr);
        // fall through
    case PNG_COLOR_TYPE_GRAY_ALPHA:
        png_set_gray_to_rgb(png_ptr);
        break;
    }

    if (bitdepth < 8)
        png_set_packing(png_ptr);
    else if (bitdepth == 16)
        png_set_strip_16(png_ptr);

    has_tRNS = png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS);
    if (has_tRNS)
        png_set_tRNS_to_alpha(png_ptr);

    png_set_filler(png_ptr, 0xff, PNG_FILLER_AFTER);

    png_set_interlace_handling(png_ptr);

    png_read_update_info(png_ptr, info_ptr);

    image->upload_width = image->width = w;
    image->upload_height = image->height = h;

    if (colortype == PNG_COLOR_TYPE_PALETTE)
        image->flags |= IF_PALETTED;

    if (!has_tRNS && !(colortype & PNG_COLOR_MASK_ALPHA))
        image->flags |= IF_OPAQUE;

    return Q_ERR_SUCCESS;
}

static int my_png_read_image(png_structp png_ptr, png_infop info_ptr, png_bytepp row_pointers)
{
    my_png_error *err = png_get_error_ptr(png_ptr);

    if (setjmp(err->setjmp_buffer))
        return Q_ERR_LIBRARY_ERROR;

    png_read_image(png_ptr, row_pointers);
    png_read_end(png_ptr, info_ptr);
    return Q_ERR_SUCCESS;
}

IMG_LOAD(PNG)
{
    byte *pixels;
    png_bytep row_pointers[MAX_TEXTURE_SIZE];
    png_structp png_ptr;
    png_infop info_ptr;
    my_png_error my_err;
    my_png_io my_io;
    int h, ret, row, rowbytes;

    if (rawlen < 8)
        return Q_ERR_FILE_TOO_SMALL;

    if (png_sig_cmp(rawdata, 0, 8))
        return Q_ERR_UNKNOWN_FORMAT;

    my_err.filename = image->name;
    png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING,
                                     (png_voidp)&my_err, my_png_error_fn, my_png_warning_fn);
    if (!png_ptr) {
        Com_SetLastError("png_create_read_struct failed");
        return Q_ERR_LIBRARY_ERROR;
    }

    png_set_option(png_ptr, PNG_SKIP_sRGB_CHECK_PROFILE, PNG_OPTION_ON);

    info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) {
        ret = Q_ERR_FAILURE;
        goto fail;
    }

    my_io.next_in = rawdata;
    my_io.avail_in = rawlen;
    ret = my_png_read_header(png_ptr, info_ptr, (png_voidp)&my_io, image);
    if (ret < 0)
        goto fail;

    h = image->height;
    rowbytes = image->width * 4;
    pixels = IMG_AllocPixels(h * rowbytes);

    for (row = 0; row < h; row++)
        row_pointers[row] = (png_bytep)(pixels + row * rowbytes);

    ret = my_png_read_image(png_ptr, info_ptr, row_pointers);
    if (ret < 0) {
        IMG_FreePixels(pixels);
        goto fail;
    }

    *pic = pixels;
fail:
    png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
    return ret;
}

static int my_png_write_image(png_structp png_ptr, png_infop info_ptr,
                              png_bytepp row_pointers, const screenshot_t *s)
{
    my_png_error *err = png_get_error_ptr(png_ptr);

    if (setjmp(err->setjmp_buffer))
        return Q_ERR_LIBRARY_ERROR;

    png_init_io(png_ptr, s->fp);
    png_set_IHDR(png_ptr, info_ptr, s->width, s->height, 8, PNG_COLOR_TYPE_RGB,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_set_compression_level(png_ptr, Q_clip(s->param, 0, 9));
    png_set_rows(png_ptr, info_ptr, row_pointers);
    png_write_png(png_ptr, info_ptr, s->bpp == 4 ? PNG_TRANSFORM_STRIP_FILLER_AFTER : 0, NULL);
    return Q_ERR_SUCCESS;
}

static int IMG_SavePNG(const screenshot_t *s)
{
    png_structp png_ptr;
    png_infop info_ptr;
    png_bytepp row_pointers;
    my_png_error my_err;
    int i, ret;

    my_err.filename = s->async ? NULL : s->filename;
    png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING,
                                      (png_voidp)&my_err, my_png_error_fn, my_png_warning_fn);
    if (!png_ptr) {
        if (!s->async)
            Com_SetLastError("png_create_write_struct failed");
        return Q_ERR_LIBRARY_ERROR;
    }

    info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) {
        ret = Q_ERR_FAILURE;
        goto fail;
    }

    row_pointers = malloc(sizeof(png_bytep) * s->height);
    if (!row_pointers) {
        ret = Q_ERR(ENOMEM);
        goto fail;
    }

    for (i = 0; i < s->height; i++)
        row_pointers[i] = (png_bytep)(s->pixels + (s->height - i - 1) * s->rowbytes);

    ret = my_png_write_image(png_ptr, info_ptr, row_pointers, s);
    free(row_pointers);
fail:
    png_destroy_write_struct(&png_ptr, &info_ptr);
    return ret;
}

#endif // USE_PNG

/*
=========================================================

SCREEN SHOTS

=========================================================
*/

#if USE_JPG || USE_PNG
static cvar_t *r_screenshot_format;
static cvar_t *r_screenshot_async;
#endif
#if USE_JPG
static cvar_t *r_screenshot_quality;
#endif
#if USE_PNG
static cvar_t *r_screenshot_compression;
#endif

#if USE_TGA || USE_JPG || USE_PNG
static cvar_t *r_screenshot_template;

static int suffix_pos(const char *s, int ch)
{
    int pos = strlen(s);
    while (pos > 0 && s[pos - 1] == ch)
        pos--;
    return pos;
}

static int parse_template(cvar_t *var, char *buffer, size_t size)
{
    if (FS_NormalizePathBuffer(buffer, var->string, size) < size) {
        FS_CleanupPath(buffer);
        int start = suffix_pos(buffer, 'X');
        int width = strlen(buffer) - start;
        buffer[start] = 0;
        if (width >= 3 && width <= 9)
            return width;
    }

    Com_WPrintf("Bad value '%s' for '%s'. Falling back to '%s'.\n",
                var->string, var->name, var->default_string);
    Cvar_Reset(var);
    Q_strlcpy(buffer, "quake", size);
    return 3;
}

static int create_screenshot(char *buffer, size_t size, FILE **f,
                             const char *name, const char *ext)
{
    char temp[MAX_OSPATH];
    int i, ret, width, count;

    if (name && *name) {
        // save to user supplied name
        if (FS_NormalizePathBuffer(temp, name, sizeof(temp)) >= sizeof(temp))
            return Q_ERR(ENAMETOOLONG);

        FS_CleanupPath(temp);

        if (Q_snprintf(buffer, size, "%s/screenshots/%s%s", fs_gamedir, temp, ext) >= size)
            return Q_ERR(ENAMETOOLONG);

        if ((ret = FS_CreatePath(buffer)) < 0)
            return ret;

        if (!(*f = fopen(buffer, "wb")))
            return Q_ERRNO;

        return Q_ERR_SUCCESS;
    }

    width = parse_template(r_screenshot_template, temp, sizeof(temp));

    // create the directory
    if (Q_snprintf(buffer, size, "%s/screenshots/%s", fs_gamedir, temp) >= size)
        return Q_ERR(ENAMETOOLONG);

    if ((ret = FS_CreatePath(buffer)) < 0)
        return ret;

    count = 1;
    for (i = 0; i < width; i++)
        count *= 10;

    // find a file name to save it to
    for (i = 0; i < count; i++) {
        if (Q_snprintf(buffer, size, "%s/screenshots/%s%0*d%s", fs_gamedir, temp, width, i, ext) >= size)
            return Q_ERR(ENAMETOOLONG);

        if ((*f = Q_fopen(buffer, "wxb")))
            return Q_ERR_SUCCESS;

        ret = Q_ERRNO;
        if (ret != Q_ERR(EEXIST))
            return ret;
    }

    return Q_ERR_OUT_OF_SLOTS;
}

static void screenshot_work_cb(void *arg)
{
    screenshot_t *s = arg;
    s->status = s->save_cb(s);
}

static void screenshot_done_cb(void *arg)
{
    screenshot_t *s = arg;

    if (fclose(s->fp) && !s->status)
        s->status = Q_ERRNO;
    Z_Free(s->pixels);

    if (s->status < 0) {
        const char *msg;

        if (s->status == Q_ERR_LIBRARY_ERROR && !s->async)
            msg = Com_GetLastError();
        else
            msg = Q_ErrorString(s->status);

        Com_EPrintf("Couldn't write %s: %s\n", s->filename, msg);
        remove(s->filename);
    } else {
        Com_Printf("Wrote %s\n", s->filename);
    }

    if (s->async) {
        Z_Free(s->filename);
        Z_Free(s);
    }
}

static void make_screenshot(const char *name, const char *ext,
                            save_cb_t save_cb, bool async, int param)
{
    char        buffer[MAX_OSPATH];
    FILE        *fp;
    int         ret;

    ret = create_screenshot(buffer, sizeof(buffer), &fp, name, ext);
    if (ret < 0) {
        Com_EPrintf("Couldn't create screenshot: %s\n", Q_ErrorString(ret));
        return;
    }

    screenshot_t s = {
        .save_cb = save_cb,
        .fp = fp,
        .filename = async ? Z_CopyString(buffer) : buffer,
        .status = -1,
        .param = param,
        .async = async,
    };

    ret = IMG_ReadPixels(&s);
    if (ret < 0) {
        s.status = ret;
        screenshot_done_cb(&s);
        return;
    }

    if (async) {
        asyncwork_t work = {
            .work_cb = screenshot_work_cb,
            .done_cb = screenshot_done_cb,
            .cb_arg = Z_CopyStruct(&s),
        };
        Com_QueueAsyncWork(&work);
    } else {
        screenshot_work_cb(&s);
        screenshot_done_cb(&s);
    }
}

#endif // USE_TGA || USE_JPG || USE_PNG

/*
==================
IMG_ScreenShot_f

Standard function to take a screenshot. Saves in default format unless user
overrides format with a second argument. Screenshot name can't be
specified. This function is always compiled in to give a meaningful warning
if no formats are available.
==================
*/
static void IMG_ScreenShot_f(void)
{
#if USE_JPG || USE_PNG
    const char *s;

    if (Cmd_Argc() > 2) {
        Com_Printf("Usage: %s [format]\n", Cmd_Argv(0));
        return;
    }

    if (Cmd_Argc() > 1)
        s = Cmd_Argv(1);
    else
        s = r_screenshot_format->string;
#endif

#if USE_JPG
    if (*s == 'j') {
        make_screenshot(NULL, ".jpg", IMG_SaveJPG,
                        r_screenshot_async->integer > 1,
                        r_screenshot_quality->integer);
        return;
    }
#endif

#if USE_PNG
    if (*s == 'p') {
        make_screenshot(NULL, ".png", IMG_SavePNG,
                        r_screenshot_async->integer > 0,
                        r_screenshot_compression->integer);
        return;
    }
#endif

#if USE_TGA
    make_screenshot(NULL, ".tga", IMG_SaveTGA, false, 0);
#else
    Com_Printf("Can't take screenshot, TGA format not available.\n");
#endif
}

/*
==================
IMG_ScreenShotXXX_f

Specialized function to take a screenshot in specified format. Screenshot name
can be also specified, as well as quality and compression options.
==================
*/

#if USE_TGA
static void IMG_ScreenShotTGA_f(void)
{
    if (Cmd_Argc() > 2) {
        Com_Printf("Usage: %s [name]\n", Cmd_Argv(0));
        return;
    }

    make_screenshot(Cmd_Argv(1), ".tga", IMG_SaveTGA, false, 0);
}
#endif

#if USE_JPG
static void IMG_ScreenShotJPG_f(void)
{
    int quality;

    if (Cmd_Argc() > 3) {
        Com_Printf("Usage: %s [name] [quality]\n", Cmd_Argv(0));
        return;
    }

    if (Cmd_Argc() > 2)
        quality = Q_atoi(Cmd_Argv(2));
    else
        quality = r_screenshot_quality->integer;

    make_screenshot(Cmd_Argv(1), ".jpg", IMG_SaveJPG,
                    r_screenshot_async->integer > 1, quality);
}
#endif

#if USE_PNG
static void IMG_ScreenShotPNG_f(void)
{
    int compression;

    if (Cmd_Argc() > 3) {
        Com_Printf("Usage: %s [name] [compression]\n", Cmd_Argv(0));
        return;
    }

    if (Cmd_Argc() > 2)
        compression = Q_atoi(Cmd_Argv(2));
    else
        compression = r_screenshot_compression->integer;

    make_screenshot(Cmd_Argv(1), ".png", IMG_SavePNG,
                    r_screenshot_async->integer > 0, compression);
}
#endif

/*
=========================================================

IMAGE MANAGER

=========================================================
*/

#define RIMAGES_HASH    256

static list_t   r_imageHash[RIMAGES_HASH];

image_t     r_images[MAX_RIMAGES];
int         r_numImages;

uint32_t    d_8to24table[256];

static const struct {
    char    ext[4];
    int     (*load)(const byte *, size_t, image_t *, byte **);
} img_loaders[IM_MAX] = {
    { "pcx", IMG_LoadPCX },
    { "wal", IMG_LoadWAL },
#if USE_TGA
    { "tga", IMG_LoadTGA },
#endif
#if USE_JPG
    { "jpg", IMG_LoadJPG },
#endif
#if USE_PNG
    { "png", IMG_LoadPNG }
#endif
};

#if USE_PNG || USE_JPG || USE_TGA
static imageformat_t    img_search[IM_MAX];
static int              img_total;

static cvar_t   *r_override_textures;
static cvar_t   *r_texture_formats;
static cvar_t   *r_texture_overrides;
#endif

static cvar_t   *r_glowmaps;

static const cmd_option_t o_imagelist[] = {
    { "8", "pal", "list paletted images" },
    { "f", "fonts", "list fonts" },
    { "h", "help", "display this help message" },
    { "m", "skins", "list skins" },
    { "p", "pics", "list pics" },
    { "r", "rgb", "list rgb images" },
    { "s", "sprites", "list sprites" },
    { "w", "walls", "list walls" },
    { "x", "missing", "list missing images" },
    { "y", "skies", "list skies" },
    { NULL }
};

static void IMG_List_c(genctx_t *ctx, int argnum)
{
    Cmd_Option_c(o_imagelist, NULL, ctx, argnum);
}

/*
===============
IMG_List_f
===============
*/
static void IMG_List_f(void)
{
    static const char types[8] = "PFMSWY??";
    const image_t   *image;
    const char      *wildcard = NULL;
    bool            missing = false;
    int             paletted = 0;
    int             i, c, mask = 0, count;
    size_t          texels;

    while ((c = Cmd_ParseOptions(o_imagelist)) != -1) {
        switch (c) {
        case 'p': mask |= BIT(IT_PIC);      break;
        case 'f': mask |= BIT(IT_FONT);     break;
        case 'm': mask |= BIT(IT_SKIN);     break;
        case 's': mask |= BIT(IT_SPRITE);   break;
        case 'w': mask |= BIT(IT_WALL);     break;
        case 'y': mask |= BIT(IT_SKY);      break;
        case '8': paletted = 1;             break;
        case 'r': paletted = -1;            break;
        case 'x': missing = true;           break;
        case 'h':
            Cmd_PrintUsage(o_imagelist, "[wildcard]");
            Com_Printf("List registered images.\n");
            Cmd_PrintHelp(o_imagelist);
            Com_Printf(
                "Types legend:\n"
                "P: pics\n"
                "F: fonts\n"
                "M: skins\n"
                "S: sprites\n"
                "W: walls\n"
                "Y: skies\n"
                "\nFlags legend:\n"
                "T: transparent\n"
                "S: scrap\n"
                "G: glowmap\n"
                "*: permanent\n"
            );
            return;
        default:
            return;
        }
    }

    if (cmd_optind < Cmd_Argc())
        wildcard = Cmd_Argv(cmd_optind);

    Com_Printf("------------------\n");
    texels = count = 0;

    for (i = R_NUM_AUTO_IMG, image = r_images + i; i < r_numImages; i++, image++) {
        if (!image->name[0])
            continue;
        if (mask && !(mask & BIT(image->type)))
            continue;
        if (wildcard && !Com_WildCmp(wildcard, image->name))
            continue;
        if ((image->width && image->height) == missing)
            continue;
        if (paletted == 1 && !(image->flags & IF_PALETTED))
            continue;
        if (paletted == -1 && (image->flags & IF_PALETTED))
            continue;

        Com_Printf("%c%c%c%c %4i %4i %s: %s\n",
                   types[image->type > IT_MAX ? IT_MAX : image->type],
                   (image->flags & IF_TRANSPARENT) ? 'T' : ' ',
                   (image->flags & IF_SCRAP) ? 'S' : image->texnum2 ? 'G' : ' ',
                   (image->flags & IF_PERMANENT) ? '*' : ' ',
                   image->upload_width,
                   image->upload_height,
                   (image->flags & IF_PALETTED) ? "PAL" : "RGB",
                   image->name);

        texels += image->upload_width * image->upload_height;
        count++;
    }

    Com_Printf("Total images: %d (out of %d slots)\n", count, r_numImages);
    Com_Printf("Total texels: %zu (not counting mipmaps)\n", texels);
}

static image_t *alloc_image(void)
{
    int i;
    image_t *image, *placeholder = NULL;

    // find a free image_t slot
    for (i = R_NUM_AUTO_IMG, image = r_images + i; i < r_numImages; i++, image++) {
        if (!image->name[0])
            return image;
        if (!image->upload_width && !image->upload_height && !placeholder)
            placeholder = image;
    }

    // allocate new slot if possible
    if (r_numImages < MAX_RIMAGES) {
        r_numImages++;
        return image;
    }

    // reuse placeholder image if available
    if (placeholder) {
        List_Remove(&placeholder->entry);
        memset(placeholder, 0, sizeof(*placeholder));
        return placeholder;
    }

    return NULL;
}

// finds the given image of the given type.
// case and extension insensitive.
static image_t *lookup_image(const char *name,
                             imagetype_t type, unsigned hash, size_t baselen)
{
    image_t *image;

    // look for it
    LIST_FOR_EACH(image_t, image, &r_imageHash[hash], entry) {
        if (image->type != type)
            continue;
        if (image->baselen != baselen)
            continue;
        if (!FS_pathcmpn(image->name, name, baselen))
            return image;
    }

    return NULL;
}

static int try_image_format(imageformat_t fmt, image_t *image, byte **pic)
{
    void    *data;
    int     ret;

    // load the file
    ret = FS_LoadFile(image->name, &data);
    if (!data)
        return ret;

    // decompress the image
    ret = img_loaders[fmt].load(data, ret, image, pic);

    FS_FreeFile(data);

    return ret < 0 ? ret : fmt;
}

#if USE_PNG || USE_JPG || USE_TGA

static int try_replace_ext(imageformat_t fmt, image_t *image, byte **pic)
{
    // replace the extension
    memcpy(image->name + image->baselen + 1, img_loaders[fmt].ext, 4);
    return try_image_format(fmt, image, pic);
}

// tries to load the image with a different extension
static int try_other_formats(imageformat_t orig, image_t *image, byte **pic)
{
    imageformat_t   fmt;
    int             i, ret;

    // search through all the 32-bit formats
    for (i = 0; i < img_total; i++) {
        fmt = img_search[i];
        if (fmt == orig)
            continue;   // don't retry twice

        ret = try_replace_ext(fmt, image, pic);
        if (ret != Q_ERR(ENOENT))
            return ret; // found something
    }

    // fall back to 8-bit formats
    fmt = (image->type == IT_WALL) ? IM_WAL : IM_PCX;
    if (fmt == orig)
        return Q_ERR(ENOENT); // don't retry twice

    return try_replace_ext(fmt, image, pic);
}

static void get_image_dimensions(imageformat_t fmt, image_t *image)
{
    char        buffer[MAX_QPATH];
    qhandle_t   f;
    unsigned    w, h;

    memcpy(buffer, image->name, image->baselen + 1);
    memcpy(buffer + image->baselen + 1, img_loaders[fmt].ext, 4);

    FS_OpenFile(buffer, &f, FS_MODE_READ | FS_FLAG_LOADFILE);
    if (!f)
        return;

    w = h = 0;
    if (fmt == IM_WAL) {
        miptex_t mt;
        if (FS_Read(&mt, sizeof(mt), f) == sizeof(mt)) {
            w = LittleLong(mt.width);
            h = LittleLong(mt.height);
        }
    } else {
        dpcx_t pcx;
        if (FS_Read(&pcx, sizeof(pcx), f) == sizeof(pcx)) {
            w = (LittleShort(pcx.xmax) - LittleShort(pcx.xmin)) + 1;
            h = (LittleShort(pcx.ymax) - LittleShort(pcx.ymin)) + 1;
        }
    }

    FS_CloseFile(f);

    if (check_image_size(w, h))
        return;

    image->width = w;
    image->height = h;
}

static void add_texture_format(imageformat_t fmt)
{
    // don't let format to be specified more than once
    for (int i = 0; i < img_total; i++)
        if (img_search[i] == fmt)
            return;

    Q_assert(img_total < IM_MAX);
    img_search[img_total++] = fmt;
}

static void r_texture_formats_changed(cvar_t *self)
{
    const char *s;

    // reset the search order
    img_total = 0;

    // parse the string
    s = self->string;
    while (s) {
        char *tok = COM_Parse(&s);
        int i;

        // handle "png jpg tga" format
        for (i = IM_WAL + 1; i < IM_MAX; i++) {
            if (!Q_stricmp(tok, img_loaders[i].ext)) {
                add_texture_format(i);
                break;
            }
        }
        if (i != IM_MAX)
            continue;

        // handle legacy "pjt" format
        while (*tok) {
            for (i = IM_WAL + 1; i < IM_MAX; i++) {
                if (Q_tolower(*tok) == img_loaders[i].ext[0]) {
                    add_texture_format(i);
                    break;
                }
            }
            tok++;
        }
    }
}

static bool need_override_image(imagetype_t type, imageformat_t fmt)
{
    if (r_override_textures->integer < 1)
        return false;
    if (r_override_textures->integer == 1 && fmt > IM_WAL)
        return false;
    return r_texture_overrides->integer & (1 << type);
}

#endif // USE_PNG || USE_JPG || USE_TGA

static void print_error(const char *name, imageflags_t flags, int err)
{
    const char *msg;
    int level = PRINT_ERROR;

    switch (err) {
    case Q_ERR_INVALID_FORMAT:
    case Q_ERR_LIBRARY_ERROR:
        msg = Com_GetLastError();
        break;
    case Q_ERR(ENOENT):
        if (flags == -1) {
            return;
        } else if ((flags & (IF_PERMANENT | IF_OPTIONAL)) == IF_PERMANENT) {
            // ugly hack for console code
            if (strcmp(name, "pics/conchars.pcx"))
                level = PRINT_WARNING;
        } else if (COM_DEVELOPER >= 2) {
            level = PRINT_DEVELOPER;
        } else {
            return;
        }
        // fall through
    default:
        msg = Q_ErrorString(err);
        break;
    }

    Com_LPrintf(level, "Couldn't load %s: %s\n", Com_MakePrintable(name), msg);
}

static int load_image_data(image_t *image, imageformat_t fmt, bool need_dimensions, byte **pic)
{
    int ret;

#if USE_PNG || USE_JPG || USE_TGA
    if (fmt == IM_MAX) {
        // unknown extension, but give it a chance to load anyway
        ret = try_other_formats(IM_MAX, image, pic);
        if (ret == Q_ERR(ENOENT)) {
            // not found, change error to invalid path
            ret = Q_ERR_INVALID_PATH;
        }
    } else if (need_override_image(image->type, fmt)) {
        // forcibly replace the extension
        ret = try_other_formats(IM_MAX, image, pic);
    } else {
        // first try with original extension
        ret = try_image_format(fmt, image, pic);
        if (ret == Q_ERR(ENOENT)) {
            // retry with remaining extensions
            ret = try_other_formats(fmt, image, pic);
        }
    }

    // if we are replacing 8-bit texture with a higher resolution 32-bit
    // texture, we need to recover original image dimensions
    if (need_dimensions && fmt <= IM_WAL && ret > IM_WAL)
        get_image_dimensions(fmt, image);
#else
    if (fmt == IM_MAX)
        ret = Q_ERR_INVALID_PATH;
    else
        ret = try_image_format(fmt, image, pic);
#endif

    return ret;
}

static void check_for_glow_map(image_t *image)
{
    extern cvar_t *gl_shaders;
    imagetype_t type = image->type;
    byte *glow_pic;
    size_t len;
    int ret;

    // glow maps are not supported in legacy mode due to
    // various corner cases that are not worth taking care of
    if (!gl_shaders->integer)
        return;

    // use a temporary image_t to hold glow map stuff.
    // it doesn't need to be registered.
    image_t temporary = {
        .type = type,
        .flags = IF_TURBULENT,  // avoid post-processing
    };

    COM_StripExtension(temporary.name, image->name, sizeof(temporary.name));
    len = Q_strlcat(temporary.name, "_glow.pcx", sizeof(temporary.name));
    if (len >= sizeof(temporary.name))
        return;
    temporary.baselen = len - 4;

    // load the pic from disk
    glow_pic = NULL;

    ret = load_image_data(&temporary, IM_PCX, false, &glow_pic);
    if (ret < 0) {
        print_error(temporary.name, -1, ret);
        return;
    }

    // post-process data;
    // - model glowmaps should be premultiplied
    // - wal glowmaps just use the alpha, so the RGB channels are ignored
    if (type == IT_SKIN) {
        int size = temporary.upload_width * temporary.upload_height;
        byte *dst = glow_pic;

        for (int i = 0; i < size; i++, dst += 4) {
            float alpha = dst[3] / 255.0f;
            dst[0] *= alpha;
            dst[1] *= alpha;
            dst[2] *= alpha;
        }
    }

    IMG_Load(&temporary, glow_pic);
    image->texnum2 = temporary.texnum;

    Z_Free(glow_pic);
}

static void load_special_image(image_t *image, byte **pic) {
    image->width = image->height = image->upload_width = image->upload_height = 1;
    byte *p = *pic = Z_Malloc(4);
    p[0] = p[1] = p[2] = p[3] = 0xFF;
}

// finds or loads the given image, adding it to the hash table.
static image_t *find_or_load_image(const char *name, size_t len,
                                   imagetype_t type, imageflags_t flags)
{
    image_t         *image;
    byte            *pic;
    unsigned        hash;
    size_t          baselen;
    imageformat_t   fmt;
    int             ret;

    Q_assert(len < MAX_QPATH);
    baselen = COM_FileExtension(name) - name;

    // must have an extension and at least 1 char of base name
    if (baselen < 1 || name[baselen] != '.') {
        ret = Q_ERR_INVALID_PATH;
        goto fail;
    }

    hash = FS_HashPathLen(name, baselen, RIMAGES_HASH);

    // look for it
    if ((image = lookup_image(name, type, hash, baselen)) != NULL) {
        image->registration_sequence = r_registration_sequence;
        if (image->upload_width && image->upload_height) {
            image->flags |= flags & IF_PERMANENT;
            return image;
        }
        return NULL;
    }

    // allocate image slot
    image = alloc_image();
    if (!image) {
        ret = Q_ERR_OUT_OF_SLOTS;
        goto fail;
    }

    // fill in some basic info
    memcpy(image->name, name, len + 1);
    image->baselen = baselen;
    image->type = type;
    image->flags = flags;
    image->registration_sequence = r_registration_sequence;

    if (!(flags & IF_SPECIAL)) {
        // find out original extension
        for (fmt = 0; fmt < IM_MAX; fmt++)
            if (!Q_stricmp(image->name + image->baselen + 1, img_loaders[fmt].ext))
                break;

        // load the pic from disk
        pic = NULL;

        if (flags & IF_KEEP_EXTENSION) {
            // direct load requested (for testing code)
            if (fmt == IM_MAX)
                ret = Q_ERR_INVALID_PATH;
            else
                ret = try_image_format(fmt, image, &pic);
        } else {
            ret = load_image_data(image, fmt, true, &pic);
        }

        if (ret < 0) {
            print_error(image->name, flags, ret);
            if (flags & IF_PERMANENT) {
                memset(image, 0, sizeof(*image));
            } else {
                // don't reload temp pics every frame
                image->upload_width = image->upload_height = 0;
                List_Append(&r_imageHash[hash], &image->entry);
            }
            return NULL;
        }

        image->aspect = (float)image->upload_width / image->upload_height;
    } else {
        load_special_image(image, &pic);
    }

    List_Append(&r_imageHash[hash], &image->entry);
    
    if (!(flags & IF_SPECIAL)) {
        // check for glow maps
        if (r_glowmaps->integer && (type == IT_SKIN || type == IT_WALL))
            check_for_glow_map(image);
    }

    if (type == IT_SKY && flags & IF_CLASSIC_SKY) {
        // upload the top half of the image (solid)
        image->height /= 2;
        image->upload_height /= 2;

        IMG_Load(image, pic + (image->width * image->height * 4));

        // upload the bottom half (alpha)
        image_t temporary = {
            .type = type,
            .flags = flags,
            .width = image->width,
            .height = image->height,
            .upload_width = image->upload_width,
            .upload_height = image->upload_height
        };

        IMG_Load(&temporary, pic);
        image->texnum2 = temporary.texnum;
    } else {
        // upload the image
        IMG_Load(image, pic);
    }

    // don't need pics in memory after GL upload
    Z_Free(pic);

    return image;

fail:
    print_error(name, flags, ret);
    return NULL;
}

image_t *IMG_Find(const char *name, imagetype_t type, imageflags_t flags)
{
    char buffer[MAX_QPATH];
    image_t *image;
    size_t len;

    Q_assert(name);

    // path MUST never overflow
    len = FS_NormalizePathBuffer(buffer, name, sizeof(buffer));
    image = find_or_load_image(buffer, len, type, flags);

    // missing (or invalid) sky texture will use default sky
    if (type == IT_SKY) {
        if (!image)
            return R_SKYTEXTURE;
        if (~image->flags & flags & IF_CUBEMAP)
            return R_SKYTEXTURE;
    }

    if (!image)
        return R_NOTEXTURE;
    return image;
}

/*
===============
IMG_ForHandle
===============
*/
image_t *IMG_ForHandle(qhandle_t h)
{
    Q_assert(h >= 0 && h < r_numImages);
    return &r_images[h];
}

/*
===============
R_RegisterImage
===============
*/
qhandle_t R_RegisterImage(const char *name, imagetype_t type, imageflags_t flags)
{
    image_t     *image;
    char        fullname[MAX_QPATH];
    size_t      len;

    Q_assert(name);

    // empty names are legal, silently ignore them
    if (!*name)
        return 0;

    // no images = not initialized
    if (!r_numImages)
        return 0;

    if (type == IT_SKIN || type == IT_SPRITE) {
        len = FS_NormalizePathBuffer(fullname, name, sizeof(fullname));
    } else if (*name == '/' || *name == '\\') {
        len = FS_NormalizePathBuffer(fullname, name + 1, sizeof(fullname));
    } else {
        len = Q_concat(fullname, sizeof(fullname), "pics/", name);
        if (len < sizeof(fullname)) {
            FS_NormalizePath(fullname);
            len = COM_DefaultExtension(fullname, ".pcx", sizeof(fullname));
        }
    }

    if (len >= sizeof(fullname)) {
        print_error(fullname, flags, Q_ERR(ENAMETOOLONG));
        return 0;
    }

    if ((image = find_or_load_image(fullname, len, type, flags)))
        return image - r_images;

    return 0;
}

/*
=============
R_GetPicSize
=============
*/
bool R_GetPicSize(int *w, int *h, qhandle_t pic)
{
    const image_t *image = IMG_ForHandle(pic);

    if (w)
        *w = image->width;
    if (h)
        *h = image->height;

    return image->flags & IF_TRANSPARENT;
}

/*
================
IMG_FreeUnused

Any image that was not touched on this registration sequence
will be freed.
================
*/
void IMG_FreeUnused(void)
{
    image_t *image;
    int i, count = 0;

    for (i = R_NUM_AUTO_IMG, image = r_images + i; i < r_numImages; i++, image++) {
        if (!image->name[0])
            continue;        // free image_t slot
        if (image->registration_sequence == r_registration_sequence)
            continue;        // used this sequence
        if (image->flags & (IF_PERMANENT | IF_SCRAP))
            continue;        // don't free pics

        // delete it from hash table
        List_Remove(&image->entry);

        // free it
        IMG_Unload(image);

        memset(image, 0, sizeof(*image));
        count++;
    }

    if (count)
        Com_DPrintf("%s: %i images freed\n", __func__, count);
}

void IMG_FreeAll(void)
{
    image_t *image;
    int i, count = 0;

    for (i = R_NUM_AUTO_IMG, image = r_images + i; i < r_numImages; i++, image++) {
        if (!image->name[0])
            continue;        // free image_t slot
        // free it
        IMG_Unload(image);

        memset(image, 0, sizeof(*image));
        count++;
    }

    if (count)
        Com_DPrintf("%s: %i images freed\n", __func__, count);

    for (i = 0; i < RIMAGES_HASH; i++)
        List_Init(&r_imageHash[i]);

    // &r_images[0] == R_NOTEXTURE
    r_numImages = R_NUM_AUTO_IMG;
}

/*
===============
R_GetPalette

===============
*/
void IMG_GetPalette(void)
{
    byte        pal[PCX_PALETTE_SIZE], *src, *data;
    int         i, ret;

    // get the palette
    ret = FS_LoadFile(R_COLORMAP_PCX, (void **)&data);
    if (!data)
        goto fail;

    ret = load_pcx(data, ret, NULL, pal, NULL);

    FS_FreeFile(data);

    if (ret < 0)
        goto fail;

    for (i = 0, src = pal; i < 255; i++, src += 3)
        d_8to24table[i] = COLOR_RGB(src[0], src[1], src[2]).u32;

    // 255 is transparent
    d_8to24table[i] = COLOR_RGBA(src[0], src[1], src[2], 0).u32;
    return;

fail:
    Com_Error(ERR_FATAL, "Couldn't load %s: %s", R_COLORMAP_PCX, Q_ErrorString(ret));
}

static const cmdreg_t img_cmd[] = {
    { "imagelist", IMG_List_f, IMG_List_c },
    { "screenshot", IMG_ScreenShot_f },
#if USE_TGA
    { "screenshottga", IMG_ScreenShotTGA_f },
#endif
#if USE_JPG
    { "screenshotjpg", IMG_ScreenShotJPG_f },
#endif
#if USE_PNG
    { "screenshotpng", IMG_ScreenShotPNG_f },
#endif

    { NULL }
};

void IMG_Init(void)
{
    int i;

    Q_assert(!r_numImages);

#if USE_PNG || USE_JPG || USE_TGA
    r_override_textures = Cvar_Get("r_override_textures", "1", CVAR_FILES);
    r_texture_formats = Cvar_Get("r_texture_formats", R_TEXTURE_FORMATS, 0);
    r_texture_formats->changed = r_texture_formats_changed;
    r_texture_formats_changed(r_texture_formats);
    r_texture_overrides = Cvar_Get("r_texture_overrides", "-1", CVAR_FILES);

#if USE_JPG
    r_screenshot_format = Cvar_Get("gl_screenshot_format", "jpg", 0);
#elif USE_PNG
    r_screenshot_format = Cvar_Get("gl_screenshot_format", "png", 0);
#endif
#if USE_JPG || USE_PNG
    r_screenshot_async = Cvar_Get("gl_screenshot_async", "1", 0);
#endif
#if USE_JPG
    r_screenshot_quality = Cvar_Get("gl_screenshot_quality", "90", 0);
#endif
#if USE_PNG
    r_screenshot_compression = Cvar_Get("gl_screenshot_compression", "6", 0);
#endif
    r_screenshot_template = Cvar_Get("gl_screenshot_template", "quakeXXX", 0);
#endif // USE_PNG || USE_JPG || USE_TGA

    r_glowmaps = Cvar_Get("r_glowmaps", "1", CVAR_FILES);

    Cmd_Register(img_cmd);

    for (i = 0; i < RIMAGES_HASH; i++)
        List_Init(&r_imageHash[i]);

    // &r_images[0] == R_NOTEXTURE
    r_numImages = R_NUM_AUTO_IMG;
    
    // &r_images[R_NUM_AUTO_IMG] == white pic
    R_RegisterImage("_white", IT_PIC, IF_PERMANENT | IF_REPEAT | IF_SPECIAL);
}

void IMG_Shutdown(void)
{
    Cmd_Deregister(img_cmd);
    memset(r_images, 0, R_NUM_AUTO_IMG * sizeof(r_images[0]));   // clear R_NOTEXTURE
    r_numImages = 0;
}
