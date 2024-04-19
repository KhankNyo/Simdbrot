
#include <immintrin.h>
#include "Common.h"

INLINE void RenderMandelbrotSet64_AVX256(
    color_buffer *ColorBuffer,
    coordmap64 Map,
    int IterationCount,
    double MaxValue
)
{
    /* processing 4 pixels at a time */
    u32 *Buffer = ColorBuffer->Ptr;
    const __m256d DeltaX4 = _mm256_set1_pd(Map.Delta*4.0);
    const __m256d DeltaY4 = _mm256_set1_pd(Map.Delta);
    const __m256d Two4 = _mm256_set1_pd(2.0);
    const __m256i One4 = _mm256_set1_epi64x(1);
    const __m256i IterationCount4 = _mm256_set1_epi64x(IterationCount);
    const __m256i ColorPaletteSizeMask4 = _mm256_set1_epi64x(STATIC_ARRAY_SIZE(ColorBuffer->Palette) - 1);
    const __m256d MaxValueSquared4 = _mm256_set1_pd(MaxValue*MaxValue);
    const __m256d ZixResetValue4 = _mm256_set_pd(
        Map.Left, 
        Map.Left + Map.Delta, 
        Map.Left + 2*Map.Delta, 
        Map.Left + 3*Map.Delta
    );
    __m256d Zix4 = ZixResetValue4;
    __m256d Ziy4 = _mm256_set1_pd(Map.Top);
    int AlignedWidth = ColorBuffer->Width - (ColorBuffer->Width % 4);
    int Remain = ColorBuffer->Width - AlignedWidth;

    for (int y = 0; 
             y < ColorBuffer->Height;
             y++, 
             Buffer += Remain)
    {

        for (int x = 0; 
                 x < AlignedWidth; 
                 x += 4)
        {
            /* the compiler will optimize a _mm256_set1_pd(0) to a dedicated vxorpd reg, reg, reg 
             * so we're good */
            __m256d Zx4 = _mm256_set1_pd(0);
            __m256d Zy4 = _mm256_set1_pd(0);

            /* registers that store conditions */
            __m256i BoundedValue4,
                    UnderIterCount4;

            /* initialize counter for each pixel */
            __m256i Counter4 = _mm256_set1_epi64x(0);
LoopHead:
            {
                /* x = x*x - y*y + x0 */
                /* y*y */
                __m256d yy4 = _mm256_mul_pd(Zy4, Zy4);

                /* x*x + x0 */
                __m256d xx_ix4 = _mm256_fmadd_pd(Zx4, Zx4, Zix4);

                /* y = 2*x*y + y0 */
                Zy4 = _mm256_fmadd_pd(
                        _mm256_mul_pd(Two4, Zx4), 
                        Zy4,
                    Ziy4
                );

                /* x = (x*x + x0) - (y*y) */
                Zx4 = _mm256_sub_pd(xx_ix4, yy4);


                /* we have finished calculating the values for Zx and Zy, 
                 * now check iteration counter and bound */
                /*
                 * FirstCond = x*x + y*y < MaxValueSquared
                 * SecondCond = Counter < IterationCount
                 * IncrementMask = FirstCond & SecondCond & 1
                 * Counter += IncrementMask
                 */
                __m256d xx4 = _mm256_mul_pd(Zx4, Zx4);
                __m256d TestValue4 = _mm256_fmadd_pd(Zy4, Zy4, xx4);

                /* First = TestValue < MaxValueSquared4 */
                BoundedValue4 = _mm256_castpd_si256(
                    _mm256_cmp_pd(TestValue4, MaxValueSquared4, 1) /* compare less than */
                );
                /* Second = IterationCount > Counter */
                UnderIterCount4 = _mm256_cmpgt_epi64(IterationCount4, Counter4);

                /* First & Second */
                __m256i FirstAndSecond4 = _mm256_and_si256(BoundedValue4, UnderIterCount4);

                /* increment */
                __m256i IncrementMask4 = _mm256_and_si256(FirstAndSecond4, One4);
                Counter4 = _mm256_add_epi64(Counter4, IncrementMask4);

                /* when increment mask is NOT zero, we still need to increment and continue the loop */
                if (_mm256_movemask_epi8(FirstAndSecond4))
                    goto LoopHead;
            }

            /* we have finished calculating the iteration count for each pixel */
            /* now it's time to determine the color */

            /* if a particular pixel stays bounded, it will recieve a color value, 
             * otherwise its color is 0 (black) */

            /* split the Counter of each pixel */
            union {
                __m256i FatMan;
                u64 LittleBoy[4];
            } ColorIndex4 = { 
                .FatMan = _mm256_and_si256(Counter4, ColorPaletteSizeMask4) 
            };
            /* load the corresponding color value of each pixel */
            __m128i Color4 = _mm_set_epi32(
                ColorBuffer->Palette[ColorIndex4.LittleBoy[0]],
                ColorBuffer->Palette[ColorIndex4.LittleBoy[1]],
                ColorBuffer->Palette[ColorIndex4.LittleBoy[2]],
                ColorBuffer->Palette[ColorIndex4.LittleBoy[3]]
            );

            /* mask out black color */
            union {
                __m256i FatMan;
                u64 LittleBoy[4];
            } UnderIterCount64_4 = {
                .FatMan = UnderIterCount4
            };
            __m128i ColorMask4 = _mm_set_epi32(
                UnderIterCount64_4.LittleBoy[0],
                UnderIterCount64_4.LittleBoy[1],
                UnderIterCount64_4.LittleBoy[2],
                UnderIterCount64_4.LittleBoy[3]
            );
            Color4 = _mm_and_si128(Color4, ColorMask4);

            /* finally store the color and continue */
            _mm_storeu_si128((void*)Buffer, Color4);
            Buffer += 4;

            Zix4 = _mm256_add_pd(Zix4, DeltaX4);
        }

        Ziy4 = _mm256_sub_pd(Ziy4, DeltaY4);
        Zix4 = ZixResetValue4;
    }
}


