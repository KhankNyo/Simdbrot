#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef int16_t i16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef uint8_t Bool8;
#define false 0
#define true 1

#ifndef RGB 
#  define RGB(r, g, b) (((u32)(u8)(r) << 16) | ((u32)(u8)(g) << 8) | ((u8)(b)))
#endif /* RGB */

typedef struct color_buffer 
{
    u32 Palette[16];
    u32 *Ptr;
    int Width;
    int Height;
} color_buffer;

typedef struct coordmap
{
    double Left, Top;
    double Width, Height;
    double Delta;
} coordmap;


static inline void GetDefaultPalette(u32 Palette[16])
{
    static const u32 DefaultPalette[16] = {
        RGB(   66,  30,  15 /* brown 3 */),
        RGB(   25,   7,  26 /* dark violett */),
        RGB(   9,   1,  47 /* darkest blue */),
        RGB(   4,   4,  73 /* blue 5 */),
        RGB(   0,   7, 100 /* blue 4 */),
        RGB(   12,  44, 138 /* blue 3 */),
        RGB(   24,  82, 177 /* blue 2 */),
        RGB(   57, 125, 209 /* blue 1 */),
        RGB(   134, 181, 229 /* blue 0 */),
        RGB(   211, 236, 248 /* lightest blue */),
        RGB(   241, 233, 191 /* lightest yellow */),
        RGB(   248, 201,  95 /* light yellow */),
        RGB(   255, 170,   0 /* dirty yellow */),
        RGB(   204, 128,   0 /* brown 0 */),
        RGB(   153,  87,   0 /* brown 1 */),
        RGB(   106,  52,   3 /* brown 2 */),
    };
    for (int i = 0; i < 16; i += 4)
    {
        Palette[i + 0] = DefaultPalette[i + 0];
        Palette[i + 1] = DefaultPalette[i + 1];
        Palette[i + 2] = DefaultPalette[i + 2];
        Palette[i + 3] = DefaultPalette[i + 3];
    }
}


#define INLINE 
#define STATIC_ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

INLINE void RenderMandelbrotSet64_Unopt(
    color_buffer *ColorBuffer,
    coordmap Map,
    int IterationCount, 
    double MaxValue
);

INLINE void RenderMandelbrotSet64_AVX256(
    color_buffer *ColorBuffer,
    coordmap Map,
    int IterationCount,
    double MaxValue
);

INLINE void RenderMandelbrotSet32_AVX256(
    color_buffer *ColorBuffer,
    coordmap Map,
    int IterationCount,
    double MaxValue
);

#endif /* COMMON_H */

