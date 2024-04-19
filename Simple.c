
#include "Common.h"
#include <stdint.h>

INLINE void RenderMandelbrotSet64_Unopt(
    color_buffer *ColorBuffer,
    coordmap64 Map,
    int IterationCount, 
    double MaxValue
)
{
     /* 
      * For each pixel, we compute 
      *    Z_next = Z_current^2 + Z_initial
      * A complex number Z = a + bi is in the set if |Z| remains bounded for all iterations
      * => sqrt(a^2 + b^2) <  some value
      * or      a^2 + b^2  < (some value)^2
      *
      * computing Z loop:
      *    Zx_next = Zx^2 - Zy^2 + Zx_initial
      *    Zy_next = 2 * Zx * Zy + Zy_initial
      * if Zx_next^2 + Zy_next^2 > some threshold 
      *    then it is not in the set, 
      * otherwise Z_curr = Z_next, continue until a max iteration count is reached
      */

     u32 *Buffer = ColorBuffer->Ptr;
     double MaxValueSquared = MaxValue * MaxValue;
     double Left = -Map.Left;
     double Zix = Left;
     double Ziy = Map.Top;
     for (int y = 0; 
              y < ColorBuffer->Height; 
              y++, 
              Ziy -= Map.Delta,
              Zix = Left) 
     {
         for (int x = 0; 
                  x < ColorBuffer->Width; 
                  x++,
                  Zix += Map.Delta)
         {
             double Zx = 0;
             double Zy = 0;

             /* calculate whether the current Z_initial is in the set or not */
             int i;
             for (i = 0; 
                  i < IterationCount
                  && (Zx*Zx + Zy*Zy) < MaxValueSquared;
                  i++)
             {
                 double Tmp = Zx*Zx - Zy*Zy + Zix;
                 Zy = 2.0*Zy*Zx + Ziy;
                 Zx = Tmp;
             }

             /* determine the color */
             u32 Color = 0;
             if (i < IterationCount)
             {
                 int ColorIndex = i % STATIC_ARRAY_SIZE(ColorBuffer->Palette);
                 Color = ColorBuffer->Palette[ColorIndex];
             }

             /* the final step, writing the color */
             *Buffer++ = Color;
         }
     }
}


