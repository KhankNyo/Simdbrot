
#include <immintrin.h>
#include "Common.h"


typedef union avx256_access
{
    __m256i m256i;
    __m256d m256d;
    __m256 m256;
    u32 DWord[8];
    u64 QWord[4];
} avx256_access;

INLINE void RenderMandelbrotSet32_AVX256(
    color_buffer *ColorBuffer,
    coordmap Map,
    int IterationCount,
    double MaxValue
)
{
    /* processing 8 pixels at a time */
    int BitsPerIteration = 8;

    u32 *Buffer = ColorBuffer->Ptr;
    const __m256  DeltaX8 = _mm256_set1_ps(Map.Delta*BitsPerIteration);
    const __m256  DeltaY8 = _mm256_set1_ps(Map.Delta);
    const __m256  Two8 = _mm256_set1_ps(2.0);
    const __m256i One8 = _mm256_set1_epi32(1);
    const __m256i IterationCount8 = _mm256_set1_epi32(IterationCount);
    const __m256i ColorPaletteSizeMask8 = _mm256_set1_epi32(STATIC_ARRAY_SIZE(ColorBuffer->Palette) - 1);
    const __m256  MaxValueSquared8 = _mm256_set1_ps(MaxValue*MaxValue);
    const __m256  ZixResetValue8 = _mm256_set_ps(
        -Map.Left + 7*Map.Delta,
        -Map.Left + 6*Map.Delta, 
        -Map.Left + 5*Map.Delta, 
        -Map.Left + 4*Map.Delta, 
        -Map.Left + 3*Map.Delta,
        -Map.Left + 2*Map.Delta, 
        -Map.Left + Map.Delta, 
        -Map.Left
    );
    __m256 Zix8 = ZixResetValue8;
    __m256 Ziy8 = _mm256_set1_ps(Map.Top);
    int AlignedWidth = ColorBuffer->Width - (ColorBuffer->Width % BitsPerIteration);
    int Remain = ColorBuffer->Width - AlignedWidth;
    for (int y = 0; 
             y < ColorBuffer->Height; 
             y++, 
             Buffer += Remain)
    {
        for (int x = 0; 
                 x < AlignedWidth; 
                 x += BitsPerIteration)
        {
            /* the compiler will optimize a _mm256_set1_ps(0) to a dedicated vxorpd reg, reg, reg 
             * so we're good */
            __m256 Zx8 = _mm256_set1_ps(0);
            __m256 Zy8 = _mm256_set1_ps(0);

            /* registers that store conditions */
            __m256i BoundedValue8,
                    UnderIterCount8;

            /* initialize counter for each pixel */
            __m256i Counter8 = _mm256_set1_epi32(0);
LoopHead:
            {
                /* x = x*x - y*y + x0 */
                /* y*y */
                __m256 yy8 = _mm256_mul_ps(Zy8, Zy8);

                /* x*x + x0 */
                __m256 xx_ix8 = _mm256_fmadd_ps(Zx8, Zx8, Zix8);

                /* y = 2*x*y + y0 */
                Zy8 = _mm256_fmadd_ps(
                        _mm256_mul_ps(Two8, Zx8), 
                        Zy8,
                    Ziy8
                );

                /* x = (x*x + x0) - (y*y) */
                Zx8 = _mm256_sub_ps(xx_ix8, yy8);


                /* we have finished calculating the values for Zx and Zy, 
                 * now check iteration counter and bound */
                /*
                 * FirstCond = x*x + y*y < MaxValueSquared
                 * SecondCond = Counter < IterationCount
                 * IncrementMask = FirstCond & SecondCond & 1
                 * Counter += IncrementMask
                 */
                __m256 xx8 = _mm256_mul_ps(Zx8, Zx8);
                __m256 TestValue8 = _mm256_fmadd_ps(Zy8, Zy8, xx8);

                /* First = TestValue < MaxValueSquared4 */
                BoundedValue8 = _mm256_castps_si256(
                    _mm256_cmp_ps(TestValue8, MaxValueSquared8, 1) /* compare less than */
                );
                /* Second = IterationCount > Counter */
                UnderIterCount8 = _mm256_cmpgt_epi32(IterationCount8, Counter8);

                /* First & Second */
                __m256i FirstAndSecond8 = _mm256_and_si256(BoundedValue8, UnderIterCount8);

                /* increment */
                __m256i IncrementMask8 = _mm256_and_si256(FirstAndSecond8, One8);
                Counter8 = _mm256_add_epi32(Counter8, IncrementMask8);

                /* when increment mask is NOT zero, we still need to increment and continue the loop */
                if (_mm256_movemask_epi8(FirstAndSecond8))
                    goto LoopHead;
            }

            /* we have finished calculating the iteration count for each pixel */
            /* now it's time to determine the color */

            /* if a particular pixel stays bounded, it will recieve a color value, 
             * otherwise its color is 0 (black) */

            /* split the Counter of each pixel */
            avx256_access ColorIndexAccess = {
                .m256i = _mm256_and_si256(Counter8, ColorPaletteSizeMask8),
            };
            /* load the corresponding color value of each pixel */
            __m256i Color8 = _mm256_set_epi32(
                ColorBuffer->Palette[ColorIndexAccess.DWord[7]],
                ColorBuffer->Palette[ColorIndexAccess.DWord[6]],
                ColorBuffer->Palette[ColorIndexAccess.DWord[5]],
                ColorBuffer->Palette[ColorIndexAccess.DWord[4]],
                ColorBuffer->Palette[ColorIndexAccess.DWord[3]],
                ColorBuffer->Palette[ColorIndexAccess.DWord[2]],
                ColorBuffer->Palette[ColorIndexAccess.DWord[1]],
                ColorBuffer->Palette[ColorIndexAccess.DWord[0]]
            );

            /* mask out black color */
            Color8 = _mm256_and_si256(Color8, UnderIterCount8);

            /* finally store the color and continue */
            _mm256_storeu_si256((void*)Buffer, Color8);
            Buffer += BitsPerIteration;

            Zix8 = _mm256_add_ps(Zix8, DeltaX8);
        }

        Ziy8 = _mm256_sub_ps(Ziy8, DeltaY8);
        Zix8 = ZixResetValue8;
    }
}



INLINE void RenderMandelbrotSet64_AVX256(
    color_buffer *ColorBuffer,
    coordmap Map,
    int IterationCount,
    double MaxValue
)
{
    /* processing 4 pixels at a time */
    int BitsPerIteration = 4;

    u32 *Buffer = ColorBuffer->Ptr;
    const __m256d DeltaX4 = _mm256_set1_pd(Map.Delta*BitsPerIteration);
    const __m256d DeltaY4 = _mm256_set1_pd(Map.Delta);
    const __m256d Two4 = _mm256_set1_pd(2.0);
    const __m256i One4 = _mm256_set1_epi64x(1);
    const __m256i IterationCount4 = _mm256_set1_epi64x(IterationCount);
    const __m256i ColorPaletteSizeMask4 = _mm256_set1_epi64x(STATIC_ARRAY_SIZE(ColorBuffer->Palette) - 1);
    const __m256d MaxValueSquared4 = _mm256_set1_pd(MaxValue*MaxValue);
    const __m256d ZixResetValue4 = _mm256_set_pd(
        -Map.Left + 3*Map.Delta,
        -Map.Left + 2*Map.Delta, 
        -Map.Left + Map.Delta, 
        -Map.Left
    );
    __m256d Zix4 = ZixResetValue4;
    __m256d Ziy4 = _mm256_set1_pd(Map.Top);
    int AlignedWidth = ColorBuffer->Width - (ColorBuffer->Width % BitsPerIteration);
    int Remain = ColorBuffer->Width - AlignedWidth;

    for (int y = 0; 
             y < ColorBuffer->Height;
             y++, 
             Buffer += Remain)
    {

        for (int x = 0; 
                 x < AlignedWidth; 
                 x += BitsPerIteration)
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
            avx256_access ColorIndexAccess = {
                .m256i = _mm256_and_si256(Counter4, ColorPaletteSizeMask4),
            };
            /* load the corresponding color value of each pixel */
            __m128i Color4 = _mm_set_epi32(
                ColorBuffer->Palette[ColorIndexAccess.QWord[3]],
                ColorBuffer->Palette[ColorIndexAccess.QWord[2]],
                ColorBuffer->Palette[ColorIndexAccess.QWord[1]],
                ColorBuffer->Palette[ColorIndexAccess.QWord[0]]
            );

            /* mask out black color */
            avx256_access UnderIterCountAccess = {
                .m256i = UnderIterCount4,
            };
            __m128i ColorMask4 = _mm_set_epi32(
                UnderIterCountAccess.QWord[3],
                UnderIterCountAccess.QWord[2],
                UnderIterCountAccess.QWord[1],
                UnderIterCountAccess.QWord[0]
            );
            Color4 = _mm_and_si128(Color4, ColorMask4);

            /* finally store the color and continue */
            _mm_storeu_si128((void*)Buffer, Color4);
            Buffer += BitsPerIteration;

            Zix4 = _mm256_add_pd(Zix4, DeltaX4);
        }

        Ziy4 = _mm256_sub_pd(Ziy4, DeltaY4);
        Zix4 = ZixResetValue4;
    }
}


INLINE void RenderMandelbrotSet64_AVX256x2(
    color_buffer *ColorBuffer,
    coordmap Map,
    int IterationCount,
    double MaxValue
)
{
    /* processing 4 pixels at a time */
    int BitsPerIteration = 8;

    u32 *Buffer = ColorBuffer->Ptr;
    const __m256d DeltaX4 = _mm256_set1_pd(Map.Delta*BitsPerIteration);
    const __m256d DeltaY4 = _mm256_set1_pd(Map.Delta);
    const __m256d Two4 = _mm256_set1_pd(2.0);
    const __m256i One4 = _mm256_set1_epi64x(1);
    const __m256i IterationCount4 = _mm256_set1_epi64x(IterationCount);
    const __m256i ColorPaletteSizeMask4 = _mm256_set1_epi64x(STATIC_ARRAY_SIZE(ColorBuffer->Palette) - 1);
    const __m256d MaxValueSquared4 = _mm256_set1_pd(MaxValue*MaxValue);
    const __m256d ZixResetValue4 = _mm256_set_pd(
        -Map.Left + 3*Map.Delta,
        -Map.Left + 2*Map.Delta, 
        -Map.Left + Map.Delta, 
        -Map.Left
    );
    __m256d Zix4 = ZixResetValue4;
    __m256d Ziy4 = _mm256_set1_pd(Map.Top);
    int AlignedWidth = ColorBuffer->Width - (ColorBuffer->Width % BitsPerIteration);
    int Remain = ColorBuffer->Width - AlignedWidth;

    for (int y = 0; 
             y < ColorBuffer->Height;
             y++, 
             Buffer += Remain)
    {

        for (int x = 0; 
                 x < AlignedWidth; 
                 x += BitsPerIteration)
        {
            /* the compiler will optimize a _mm256_set1_pd(0) to a dedicated vxorps reg, reg, reg 
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
            avx256_access ColorIndexAccess = {
                .m256i = _mm256_and_si256(Counter4, ColorPaletteSizeMask4),
            };
            /* load the corresponding color value of each pixel */
            __m128i Color4 = _mm_set_epi32(
                ColorBuffer->Palette[ColorIndexAccess.QWord[3]],
                ColorBuffer->Palette[ColorIndexAccess.QWord[2]],
                ColorBuffer->Palette[ColorIndexAccess.QWord[1]],
                ColorBuffer->Palette[ColorIndexAccess.QWord[0]]
            );

            /* mask out black color */
            __m256i A = _mm256_shuffle_epi32(UnderIterCount4, 0x88); /* [0, 2, ....]*/
            __m256i B = _mm256_shuffle_epi32(UnderIterCount4, 0x80); /* [...., 4, 6] */
            __m128i ColorMask4 = _mm256_castsi256_si128(
                _mm256_blend_epi32(A, B, 0x0C)                       /* [0, 2, 4, 6] */
            ); 
            Color4 = _mm_and_si128(Color4, ColorMask4);

            /* finally store the color and continue */
            _mm_storeu_si128((void*)Buffer, Color4);
            Buffer += BitsPerIteration;

            Zix4 = _mm256_add_pd(Zix4, DeltaX4);
        }

        Ziy4 = _mm256_sub_pd(Ziy4, DeltaY4);
        Zix4 = ZixResetValue4;
    }
}


