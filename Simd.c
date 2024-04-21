
#include <immintrin.h>
#include "Common.h"


typedef union m256_access
{
    __m256i m256i;
    __m256d m256d;
    __m256 m256;
    u32 DWord[8];
    u64 QWord[4];
} m256_access;

typedef union m128_access 
{
    __m128i m128i;
    __m128d m128d;
    __m128  m128;
    u32 DWord[4];
    u64 QWord[2];
} m128_access;

void RenderMandelbrotSet32_SSE(
    color_buffer *ColorBuffer,
    const coordmap *Map,
    int IterationCount,
    double MaxValue
)
{
    /* processing 4 pixels at a time */
    int BitsPerIteration = 4;

    u32 *Buffer = ColorBuffer->Ptr;
    const __m128  DeltaX4 = _mm_set1_ps(Map->Delta*BitsPerIteration);
    const __m128  DeltaY4 = _mm_set1_ps(Map->Delta);
    const __m128  Two4 = _mm_set1_ps(2.0);
    const __m128i One4 = _mm_set1_epi32(1);
    const __m128i IterationCount4 = _mm_set1_epi32(IterationCount);
    const __m128i ColorPaletteSizeMask4 = _mm_set1_epi32(STATIC_ARRAY_SIZE(ColorBuffer->Palette) - 1);
    const __m128  MaxValueSquared4 = _mm_set1_ps(MaxValue*MaxValue);
    const __m128  ZixResetValue4 = _mm_set_ps(
        -Map->Left + 3*Map->Delta,
        -Map->Left + 2*Map->Delta, 
        -Map->Left + Map->Delta, 
        -Map->Left
    );
    __m128 Zix4 = ZixResetValue4;
    __m128 Ziy4 = _mm_set1_ps(Map->Top);
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
            __m128 Zx4 = _mm_setzero_ps();
            __m128 Zy4 = _mm_setzero_ps();

            /* registers that store conditions */
            __m128i BoundedValue4,
                    UnderIterCount4;

            /* initialize counter for each pixel */
            __m128i Counter4 = _mm_castps_si128(_mm_setzero_ps());
LoopHead:
            {
                /* calculate Zx and Zy */
                {
                    /* y*y */
                    __m128 yy4 = _mm_mul_ps(Zy4, Zy4);

                    /* x*x + x0 */
                    __m128 xx4 = _mm_mul_ps(Zx4, Zx4);
                    __m128 xx_ix4 = _mm_add_ps(xx4, Zix4); 

                    /* Zy = 2*x*y + y0 */
                    Zy4 = _mm_mul_ps(Zy4, Two4);
                    Zy4 = _mm_mul_ps(Zy4, Zx4);
                    Zy4 = _mm_add_ps(Zy4, Ziy4);

                    /* x = (x*x + x0) - (y*y) */
                    Zx4 = _mm_sub_ps(xx_ix4, yy4);
                }


                /* we have finished calculating the values for Zx and Zy, 
                 * now check iteration counter and bound */
                /*
                 * FirstCond = x*x + y*y < MaxValueSquared
                 * SecondCond = Counter < IterationCount
                 * IncrementMask = FirstCond & SecondCond & 1
                 * Counter += IncrementMask
                 */
                __m128 xx4 = _mm_mul_ps(Zx4, Zx4);
                __m128 yy4 = _mm_mul_ps(Zy4, Zy4);
                __m128 TestValue4 = _mm_add_ps(xx4, yy4);

                /* First = TestValue < MaxValueSquared4 */
                BoundedValue4 = _mm_castps_si128(
                    _mm_cmp_ps(TestValue4, MaxValueSquared4, 1) /* compare less than */
                );
                /* Second = IterationCount > Counter */
                UnderIterCount4 = _mm_cmpgt_epi32(IterationCount4, Counter4);

                /* First & Second */
                __m128i FirstAndSecond4 = _mm_and_si128(BoundedValue4, UnderIterCount4);

                /* increment */
                __m128i IncrementMask4 = _mm_and_si128(FirstAndSecond4, One4);
                Counter4 = _mm_add_epi32(Counter4, IncrementMask4);

                /* when increment mask is NOT zero, we still need to increment and continue the loop */
                if (_mm_movemask_epi8(FirstAndSecond4))
                    goto LoopHead;
            }

            /* we have finished calculating the iteration count for each pixel */
            /* now it's time to determine the color */

            /* if a particular pixel stays bounded, it will recieve a color value, 
             * otherwise its color is 0 (black) */

            /* split the Counter of each pixel */
            __m128i ColorIndex4 = _mm_and_si128(Counter4, ColorPaletteSizeMask4);
            /* load the corresponding color value of each pixel */
            __m128i Color4 = _mm_set_epi32(
                ColorBuffer->Palette[_mm_extract_epi32(ColorIndex4, 3)],
                ColorBuffer->Palette[_mm_extract_epi32(ColorIndex4, 2)],
                ColorBuffer->Palette[_mm_extract_epi32(ColorIndex4, 1)],
                ColorBuffer->Palette[_mm_extract_epi32(ColorIndex4, 0)]
            );

            /* mask out black color */
            Color4 = _mm_and_si128(Color4, UnderIterCount4);

            /* finally store the color and continue */
            _mm_storeu_si128((void*)Buffer, Color4);
            Buffer += BitsPerIteration;

            Zix4 = _mm_add_ps(Zix4, DeltaX4);
        }

        Ziy4 = _mm_sub_ps(Ziy4, DeltaY4);
        Zix4 = ZixResetValue4;
    }
}

void RenderMandelbrotSet64_SSE(
    color_buffer *ColorBuffer,
    const coordmap *Map,
    int IterationCount,
    double MaxValue
)
{
    /* processing 2 pixels at a time */
    int BitsPerIteration = 2;

    u32 *Buffer = ColorBuffer->Ptr;
    const __m128d DeltaX2 = _mm_set1_pd(Map->Delta*BitsPerIteration);
    const __m128d DeltaY2 = _mm_set1_pd(Map->Delta);
    const __m128d Two2 = _mm_set1_pd(2.0);
    const __m128i One2 = _mm_set1_epi64x(1);
    const __m128i IterationCount2 = _mm_set1_epi64x(IterationCount);
    const __m128i ColorPaletteSizeMask2 = _mm_set1_epi64x(STATIC_ARRAY_SIZE(ColorBuffer->Palette) - 1);
    const __m128d MaxValueSquared2 = _mm_set1_pd(MaxValue*MaxValue);
    const __m128d ZixResetValue2 = _mm_set_pd(
        -Map->Left + Map->Delta, 
        -Map->Left
    );
    __m128d Zix2 = ZixResetValue2;
    __m128d Ziy2 = _mm_set1_pd(Map->Top);
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
            __m128d Zx2 = _mm_setzero_pd();
            __m128d Zy2 = _mm_setzero_pd();

            /* registers that store conditions */
            __m128i BoundedValue2,
                    UnderIterCount2;

            /* initialize counter for each pixel */
            __m128i Counter2 = _mm_castpd_si128(_mm_setzero_pd());
LoopHead:
            {
                /* calculate Zx and Zy */
                {
                    /* y*y */
                    __m128d yy2 = _mm_mul_pd(Zy2, Zy2);

                    /* x*x + x0 */
                    __m128d xx2 = _mm_mul_pd(Zx2, Zx2);
                    __m128d xx_ix2 = _mm_add_pd(xx2, Zix2); 

                    /* Zy = 2*x*y + y0 */
                    Zy2 = _mm_mul_pd(Zy2, Two2);
                    Zy2 = _mm_mul_pd(Zy2, Zx2);
                    Zy2 = _mm_add_pd(Zy2, Ziy2);

                    /* x = (x*x + x0) - (y*y) */
                    Zx2 = _mm_sub_pd(xx_ix2, yy2);
                }


                /* x^2 + y^2 */
                __m128d xx2 = _mm_mul_pd(Zx2, Zx2);
                __m128d yy2 = _mm_mul_pd(Zy2, Zy2);
                __m128d TestValue2 = _mm_add_pd(xx2, yy2);

                /* x^2 + y^2 < MaxValue^2 */
                BoundedValue2 = _mm_castpd_si128(
                    _mm_cmp_pd(TestValue2, MaxValueSquared2, 1) /* compare less than */
                );
                UnderIterCount2 = _mm_cmpgt_epi64(IterationCount2, Counter2);
                __m128i FirstAndSecond2 = _mm_and_si128(BoundedValue2, UnderIterCount2);

                __m128i IncrementMask2 = _mm_and_si128(FirstAndSecond2, One2);
                Counter2 = _mm_add_epi64(Counter2, IncrementMask2);
                if (_mm_movemask_epi8(FirstAndSecond2))
                    goto LoopHead;
            }

            /* load the corresponding color value of each pixel */
            __m128i ColorIndex2 = _mm_and_si128(Counter2, ColorPaletteSizeMask2);
            __m128i Color2 = _mm_set_epi64x(
                ColorBuffer->Palette[_mm_extract_epi64(ColorIndex2, 1)],
                ColorBuffer->Palette[_mm_extract_epi64(ColorIndex2, 0)]
            );
            Color2 = _mm_and_si128(Color2, UnderIterCount2);

            /* finally store the color and continue */
            Buffer[0] = _mm_extract_epi64(Color2, 0);
            Buffer[1] = _mm_extract_epi64(Color2, 1);
            Buffer += BitsPerIteration;

            Zix2 = _mm_add_pd(Zix2, DeltaX2);
        }

        Ziy2 = _mm_sub_pd(Ziy2, DeltaY2);
        Zix2 = ZixResetValue2;
    }
}


void RenderMandelbrotSet64_SSEFMA(
    color_buffer *ColorBuffer,
    const coordmap *Map,
    int IterationCount,
    double MaxValue
)
{
    /* processing 2 pixels at a time */
    int BitsPerIteration = 2;

    u32 *Buffer = ColorBuffer->Ptr;
    const __m128d DeltaX2 = _mm_set1_pd(Map->Delta*BitsPerIteration);
    const __m128d DeltaY2 = _mm_set1_pd(Map->Delta);
    const __m128d Two2 = _mm_set1_pd(2.0);
    const __m128i One2 = _mm_set1_epi64x(1);
    const __m128i IterationCount2 = _mm_set1_epi64x(IterationCount);
    const __m128i ColorPaletteSizeMask2 = _mm_set1_epi64x(STATIC_ARRAY_SIZE(ColorBuffer->Palette) - 1);
    const __m128d MaxValueSquared2 = _mm_set1_pd(MaxValue*MaxValue);
    const __m128d ZixResetValue2 = _mm_set_pd(
        -Map->Left + Map->Delta, 
        -Map->Left
    );
    __m128d Zix2 = ZixResetValue2;
    __m128d Ziy2 = _mm_set1_pd(Map->Top);
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
            __m128d Zx2 = _mm_setzero_pd();
            __m128d Zy2 = _mm_setzero_pd();

            /* registers that store conditions */
            __m128i BoundedValue2,
                    UnderIterCount2;

            /* initialize counter for each pixel */
            __m128i Counter2 = _mm_castpd_si128(_mm_setzero_pd());
LoopHead:
            {
                /* calculate Zx and Zy */
                {
                    /* y*y */
                    __m128d yy2 = _mm_mul_pd(Zy2, Zy2);

                    /* x*x + x0 */
                    __m128d xx_ix2 = _mm_fmadd_pd(Zx2, Zx2, Zix2); 

                    /* Zy = 2*x*y + y0 */
                    Zy2 = _mm_fmadd_pd(
                            Two2, 
                            _mm_mul_pd(Zy2, Zx2),
                        Ziy2
                    );

                    /* x = (x*x + x0) - (y*y) */
                    Zx2 = _mm_sub_pd(xx_ix2, yy2);
                }


                /* x^2 + y^2 */
                __m128d xx2 = _mm_mul_pd(Zx2, Zx2);
                __m128d TestValue2 = _mm_fmadd_pd(Zy2, Zy2, xx2);

                /* x^2 + y^2 < MaxValue^2 */
                BoundedValue2 = _mm_castpd_si128(
                    _mm_cmp_pd(TestValue2, MaxValueSquared2, 1) /* compare less than */
                );
                UnderIterCount2 = _mm_cmpgt_epi64(IterationCount2, Counter2);
                __m128i FirstAndSecond2 = _mm_and_si128(BoundedValue2, UnderIterCount2);

                __m128i IncrementMask2 = _mm_and_si128(FirstAndSecond2, One2);
                Counter2 = _mm_add_epi64(Counter2, IncrementMask2);
                if (_mm_movemask_epi8(FirstAndSecond2))
                    goto LoopHead;
            }

            /* load the corresponding color value of each pixel */
            __m128i ColorIndex2 = _mm_and_si128(Counter2, ColorPaletteSizeMask2);
            __m128i Color2 = _mm_set_epi64x(
                ColorBuffer->Palette[_mm_extract_epi64(ColorIndex2, 1)],
                ColorBuffer->Palette[_mm_extract_epi64(ColorIndex2, 0)]
            );
            Color2 = _mm_and_si128(Color2, UnderIterCount2);

            /* finally store the color and continue */
            Buffer[0] = _mm_extract_epi64(Color2, 0);
            Buffer[1] = _mm_extract_epi64(Color2, 1);
            Buffer += BitsPerIteration;

            Zix2 = _mm_add_pd(Zix2, DeltaX2);
        }

        Ziy2 = _mm_sub_pd(Ziy2, DeltaY2);
        Zix2 = ZixResetValue2;
    }
}


void RenderMandelbrotSet32_SSEFMA(
    color_buffer *ColorBuffer,
    const coordmap *Map,
    int IterationCount,
    double MaxValue
)
{
    /* processing 4 pixels at a time */
    int BitsPerIteration = 4;

    u32 *Buffer = ColorBuffer->Ptr;
    const __m128  DeltaX4 = _mm_set1_ps(Map->Delta*BitsPerIteration);
    const __m128  DeltaY4 = _mm_set1_ps(Map->Delta);
    const __m128  Two4 = _mm_set1_ps(2.0);
    const __m128i One4 = _mm_set1_epi32(1);
    const __m128i IterationCount4 = _mm_set1_epi32(IterationCount);
    const __m128i ColorPaletteSizeMask4 = _mm_set1_epi32(STATIC_ARRAY_SIZE(ColorBuffer->Palette) - 1);
    const __m128  MaxValueSquared4 = _mm_set1_ps(MaxValue*MaxValue);
    const __m128  ZixResetValue4 = _mm_set_ps(
        -Map->Left + 3*Map->Delta,
        -Map->Left + 2*Map->Delta, 
        -Map->Left + Map->Delta, 
        -Map->Left
    );
    __m128 Zix4 = ZixResetValue4;
    __m128 Ziy4 = _mm_set1_ps(Map->Top);
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
            __m128 Zx4 = _mm_setzero_ps();
            __m128 Zy4 = _mm_setzero_ps();

            /* registers that store conditions */
            __m128i BoundedValue4,
                    UnderIterCount4;

            /* initialize counter for each pixel */
            __m128i Counter4 = _mm_castps_si128(_mm_setzero_ps());
LoopHead:
            {
                /* calculate Zx and Zy */
                {
                    /* y*y */
                    __m128 yy4 = _mm_mul_ps(Zy4, Zy4);

                    /* x*x + x0 */
                    __m128 xx_ix4 = _mm_fmadd_ps(Zx4, Zx4, Zix4);

                    /* Zy = 2*x*y + y0 */
                    Zy4 = _mm_fmadd_ps(
                            Two4,
                            _mm_mul_ps(Zx4, Zy4),
                        Ziy4
                    );

                    /* x = (x*x + x0) - (y*y) */
                    Zx4 = _mm_sub_ps(xx_ix4, yy4);
                }


                /* we have finished calculating the values for Zx and Zy, 
                 * now check iteration counter and bound */
                /*
                 * FirstCond = x*x + y*y < MaxValueSquared
                 * SecondCond = Counter < IterationCount
                 * IncrementMask = FirstCond & SecondCond & 1
                 * Counter += IncrementMask
                 */
                __m128 xx4 = _mm_mul_ps(Zx4, Zx4);
                __m128 TestValue4 = _mm_fmadd_ps(Zy4, Zy4, xx4);

                /* First = TestValue < MaxValueSquared4 */
                BoundedValue4 = _mm_castps_si128(
                    _mm_cmp_ps(TestValue4, MaxValueSquared4, 1) /* compare less than */
                );
                /* Second = IterationCount > Counter */
                UnderIterCount4 = _mm_cmpgt_epi32(IterationCount4, Counter4);

                /* First & Second */
                __m128i FirstAndSecond4 = _mm_and_si128(BoundedValue4, UnderIterCount4);

                /* increment */
                __m128i IncrementMask4 = _mm_and_si128(FirstAndSecond4, One4);
                Counter4 = _mm_add_epi32(Counter4, IncrementMask4);

                /* when increment mask is NOT zero, we still need to increment and continue the loop */
                if (_mm_movemask_epi8(FirstAndSecond4))
                    goto LoopHead;
            }

            /* we have finished calculating the iteration count for each pixel */
            /* now it's time to determine the color */

            /* if a particular pixel stays bounded, it will recieve a color value, 
             * otherwise its color is 0 (black) */

            /* split the Counter of each pixel */
            __m128i ColorIndex4 = _mm_and_si128(Counter4, ColorPaletteSizeMask4);
            /* load the corresponding color value of each pixel */
            __m128i Color4 = _mm_set_epi32(
                ColorBuffer->Palette[_mm_extract_epi32(ColorIndex4, 3)],
                ColorBuffer->Palette[_mm_extract_epi32(ColorIndex4, 2)],
                ColorBuffer->Palette[_mm_extract_epi32(ColorIndex4, 1)],
                ColorBuffer->Palette[_mm_extract_epi32(ColorIndex4, 0)]
            );

            /* mask out black color */
            Color4 = _mm_and_si128(Color4, UnderIterCount4);

            /* finally store the color and continue */
            _mm_storeu_si128((void*)Buffer, Color4);
            Buffer += BitsPerIteration;

            Zix4 = _mm_add_ps(Zix4, DeltaX4);
        }

        Ziy4 = _mm_sub_ps(Ziy4, DeltaY4);
        Zix4 = ZixResetValue4;
    }
}



void RenderMandelbrotSet32_AVX(
    color_buffer *ColorBuffer,
    const coordmap *Map,
    int IterationCount,
    double MaxValue
)
{
    /* processing 8 pixels at a time */
    int BitsPerIteration = 8;

    u32 *Buffer = ColorBuffer->Ptr;
    const __m256  DeltaX8 = _mm256_set1_ps(Map->Delta*BitsPerIteration);
    const __m256  DeltaY8 = _mm256_set1_ps(Map->Delta);
    const __m256  Two8 = _mm256_set1_ps(2.0);
    const __m256i One8 = _mm256_set1_epi32(1);
    const __m256i IterationCount8 = _mm256_set1_epi32(IterationCount);
    const __m256i ColorPaletteSizeMask8 = _mm256_set1_epi32(STATIC_ARRAY_SIZE(ColorBuffer->Palette) - 1);
    const __m256  MaxValueSquared8 = _mm256_set1_ps(MaxValue*MaxValue);
    const __m256  ZixResetValue8 = _mm256_set_ps(
        -Map->Left + 7*Map->Delta,
        -Map->Left + 6*Map->Delta, 
        -Map->Left + 5*Map->Delta, 
        -Map->Left + 4*Map->Delta, 
        -Map->Left + 3*Map->Delta,
        -Map->Left + 2*Map->Delta, 
        -Map->Left + Map->Delta, 
        -Map->Left
    );
    __m256 Zix8 = ZixResetValue8;
    __m256 Ziy8 = _mm256_set1_ps(Map->Top);
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
                {
                    /* x = x*x - y*y + x0 */
                    /* y*y */
                    __m256 yy8 = _mm256_mul_ps(Zy8, Zy8);

                    /* x*x + x0 */
                    __m256 xx8 = _mm256_mul_ps(Zx8, Zx8);
                    __m256 xx_ix8 = _mm256_add_ps(xx8, Zix8);

                    /* y = 2*x*y + y0 */
                    Zy8 = _mm256_mul_ps(Two8, Zy8);
                    Zy8 = _mm256_mul_ps(Zy8, Zx8);
                    Zy8 = _mm256_add_ps(Zy8, Ziy8);

                    /* x = (x*x + x0) - (y*y) */
                    Zx8 = _mm256_sub_ps(xx_ix8, yy8);
                }


                /* x^2 + y^2 < MaxValueSquare */
                __m256 xx8 = _mm256_mul_ps(Zx8, Zx8);
                __m256 yy8 = _mm256_mul_ps(Zy8, Zy8);
                __m256 TestValue8 = _mm256_add_ps(xx8, yy8);

                /* First = TestValue < MaxValueSquared4 */
                BoundedValue8 = _mm256_castps_si256(
                    _mm256_cmp_ps(TestValue8, MaxValueSquared8, 1) /* compare less than */
                );
                UnderIterCount8 = _mm256_cmpgt_epi32(IterationCount8, Counter8);
                __m256i FirstAndSecond8 = _mm256_and_si256(BoundedValue8, UnderIterCount8);

                __m256i IncrementMask8 = _mm256_and_si256(FirstAndSecond8, One8);
                Counter8 = _mm256_add_epi32(Counter8, IncrementMask8);
                if (_mm256_movemask_epi8(FirstAndSecond8))
                    goto LoopHead;
            }

            __m256i ColorIndex = _mm256_and_si256(
                Counter8, ColorPaletteSizeMask8
            );
            __m256i Color8 = _mm256_set_epi32(
                ColorBuffer->Palette[_mm256_extract_epi32(ColorIndex, 7)],
                ColorBuffer->Palette[_mm256_extract_epi32(ColorIndex, 6)],
                ColorBuffer->Palette[_mm256_extract_epi32(ColorIndex, 5)],
                ColorBuffer->Palette[_mm256_extract_epi32(ColorIndex, 4)],
                ColorBuffer->Palette[_mm256_extract_epi32(ColorIndex, 3)],
                ColorBuffer->Palette[_mm256_extract_epi32(ColorIndex, 2)],
                ColorBuffer->Palette[_mm256_extract_epi32(ColorIndex, 1)],
                ColorBuffer->Palette[_mm256_extract_epi32(ColorIndex, 0)]
            );

            /* finally store the color and continue */
            Color8 = _mm256_and_si256(Color8, UnderIterCount8);
            _mm256_storeu_si256((void*)Buffer, Color8);
            Buffer += BitsPerIteration;

            Zix8 = _mm256_add_ps(Zix8, DeltaX8);
        }

        Ziy8 = _mm256_sub_ps(Ziy8, DeltaY8);
        Zix8 = ZixResetValue8;
    }
}


void RenderMandelbrotSet64_AVX(
    color_buffer *ColorBuffer,
    const coordmap *Map,
    int IterationCount,
    double MaxValue
)
{
    /* processing 4 pixels at a time */
    int BitsPerIteration = 4;

    u32 *Buffer = ColorBuffer->Ptr;
    const __m256d DeltaX4 = _mm256_set1_pd(Map->Delta*BitsPerIteration);
    const __m256d DeltaY4 = _mm256_set1_pd(Map->Delta);
    const __m256d Two4 = _mm256_set1_pd(2.0);
    const __m256i One4 = _mm256_set1_epi64x(1);
    const __m256i IterationCount4 = _mm256_set1_epi64x(IterationCount);
    const __m256i ColorPaletteSizeMask4 = _mm256_set1_epi64x(STATIC_ARRAY_SIZE(ColorBuffer->Palette) - 1);
    const __m256d MaxValueSquared4 = _mm256_set1_pd(MaxValue*MaxValue);
    const __m256d ZixResetValue4 = _mm256_set_pd(
        -Map->Left + 3*Map->Delta,
        -Map->Left + 2*Map->Delta, 
        -Map->Left + Map->Delta, 
        -Map->Left
    );
    __m256d Zix4 = ZixResetValue4;
    __m256d Ziy4 = _mm256_set1_pd(Map->Top);
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
                {
                    __m256d yy4 = _mm256_mul_pd(Zy4, Zy4);

                    /* x*x + x0 */
                    __m256d xx4 = _mm256_mul_pd(Zx4, Zx4);
                    __m256d xx_ix4 = _mm256_add_pd(xx4, Zix4);

                    /* y = 2*x*y + y0 */
                    Zy4 = _mm256_mul_pd(Two4, Zy4);
                    Zy4 = _mm256_mul_pd(Zy4, Zx4);
                    Zy4 = _mm256_add_pd(Zy4, Ziy4);


                    /* x = (x*x + x0) - (y*y) */
                    Zx4 = _mm256_sub_pd(xx_ix4, yy4);
                }
                
                __m256d xx4 = _mm256_mul_pd(Zx4, Zx4);
                __m256d yy4 = _mm256_mul_pd(Zy4, Zy4);
                __m256d TestValue4 = _mm256_add_pd(xx4, yy4);

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
            /* load the corresponding color value of each pixel */
            __m256i ColorIndex4 = _mm256_and_si256(Counter4, ColorPaletteSizeMask4);
            __m128i Color4 = _mm_set_epi32(
                ColorBuffer->Palette[_mm256_extract_epi64(ColorIndex4, 3)],
                ColorBuffer->Palette[_mm256_extract_epi64(ColorIndex4, 2)],
                ColorBuffer->Palette[_mm256_extract_epi64(ColorIndex4, 1)],
                ColorBuffer->Palette[_mm256_extract_epi64(ColorIndex4, 0)]
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






void RenderMandelbrotSet32_AVXFMA(
    color_buffer *ColorBuffer,
    const coordmap *Map,
    int IterationCount,
    double MaxValue
)
{
    /* processing 8 pixels at a time */
    int BitsPerIteration = 8;

    u32 *Buffer = ColorBuffer->Ptr;
    const __m256  DeltaX8 = _mm256_set1_ps(Map->Delta*BitsPerIteration);
    const __m256  DeltaY8 = _mm256_set1_ps(Map->Delta);
    const __m256  Two8 = _mm256_set1_ps(2.0);
    const __m256i One8 = _mm256_set1_epi32(1);
    const __m256i IterationCount8 = _mm256_set1_epi32(IterationCount);
    const __m256i ColorPaletteSizeMask8 = _mm256_set1_epi32(STATIC_ARRAY_SIZE(ColorBuffer->Palette) - 1);
    const __m256  MaxValueSquared8 = _mm256_set1_ps(MaxValue*MaxValue);
    const __m256  ZixResetValue8 = _mm256_set_ps(
        -Map->Left + 7*Map->Delta,
        -Map->Left + 6*Map->Delta, 
        -Map->Left + 5*Map->Delta, 
        -Map->Left + 4*Map->Delta, 
        -Map->Left + 3*Map->Delta,
        -Map->Left + 2*Map->Delta, 
        -Map->Left + Map->Delta, 
        -Map->Left
    );
    __m256 Zix8 = ZixResetValue8;
    __m256 Ziy8 = _mm256_set1_ps(Map->Top);
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


                __m256 xx8 = _mm256_mul_ps(Zx8, Zx8);
                __m256 TestValue8 = _mm256_fmadd_ps(Zy8, Zy8, xx8);

                /* First = TestValue < MaxValueSquared4 */
                BoundedValue8 = _mm256_castps_si256(
                    _mm256_cmp_ps(TestValue8, MaxValueSquared8, 1) /* compare less than */
                );
                UnderIterCount8 = _mm256_cmpgt_epi32(IterationCount8, Counter8);
                __m256i FirstAndSecond8 = _mm256_and_si256(BoundedValue8, UnderIterCount8);

                __m256i IncrementMask8 = _mm256_and_si256(FirstAndSecond8, One8);
                Counter8 = _mm256_add_epi32(Counter8, IncrementMask8);
                if (_mm256_movemask_epi8(FirstAndSecond8))
                    goto LoopHead;
            }

            __m256i ColorIndex = _mm256_and_si256(
                Counter8, ColorPaletteSizeMask8
            );
            __m256i Color8 = _mm256_set_epi32(
                ColorBuffer->Palette[_mm256_extract_epi32(ColorIndex, 7)],
                ColorBuffer->Palette[_mm256_extract_epi32(ColorIndex, 6)],
                ColorBuffer->Palette[_mm256_extract_epi32(ColorIndex, 5)],
                ColorBuffer->Palette[_mm256_extract_epi32(ColorIndex, 4)],
                ColorBuffer->Palette[_mm256_extract_epi32(ColorIndex, 3)],
                ColorBuffer->Palette[_mm256_extract_epi32(ColorIndex, 2)],
                ColorBuffer->Palette[_mm256_extract_epi32(ColorIndex, 1)],
                ColorBuffer->Palette[_mm256_extract_epi32(ColorIndex, 0)]
            );

            /* finally store the color and continue */
            Color8 = _mm256_and_si256(Color8, UnderIterCount8);
            _mm256_storeu_si256((void*)Buffer, Color8);
            Buffer += BitsPerIteration;

            Zix8 = _mm256_add_ps(Zix8, DeltaX8);
        }

        Ziy8 = _mm256_sub_ps(Ziy8, DeltaY8);
        Zix8 = ZixResetValue8;
    }
}

void RenderMandelbrotSet64_AVXFMA(
    color_buffer *ColorBuffer,
    const coordmap *Map,
    int IterationCount,
    double MaxValue
)
{
    /* processing 4 pixels at a time */
    int BitsPerIteration = 4;

    u32 *Buffer = ColorBuffer->Ptr;
    const __m256d DeltaX4 = _mm256_set1_pd(Map->Delta*BitsPerIteration);
    const __m256d DeltaY4 = _mm256_set1_pd(Map->Delta);
    const __m256d Two4 = _mm256_set1_pd(2.0);
    const __m256i One4 = _mm256_set1_epi64x(1);
    const __m256i IterationCount4 = _mm256_set1_epi64x(IterationCount);
    const __m256i ColorPaletteSizeMask4 = _mm256_set1_epi64x(STATIC_ARRAY_SIZE(ColorBuffer->Palette) - 1);
    const __m256d MaxValueSquared4 = _mm256_set1_pd(MaxValue*MaxValue);
    const __m256d ZixResetValue4 = _mm256_set_pd(
        -Map->Left + 3*Map->Delta,
        -Map->Left + 2*Map->Delta, 
        -Map->Left + Map->Delta, 
        -Map->Left
    );
    __m256d Zix4 = ZixResetValue4;
    __m256d Ziy4 = _mm256_set1_pd(Map->Top);
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
            m256_access ColorIndexAccess = {
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



