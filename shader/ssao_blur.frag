#version 330 core
in vec2 TexCoords;
out float fragColor;

uniform sampler2D ssaoInput;

void main()
{
   vec2 texelSize = 1.0 / vec2(textureSize(ssaoInput, 0));
   float result = 0.0;
   float weights = 0.0;

   // 5x5 Gaussian kernel (sigma = 1.0)
   float kernel[25] = float[](
      0.003765, 0.015019, 0.023792, 0.015019, 0.003765,
      0.015019, 0.059912, 0.094907, 0.059912, 0.015019,
      0.023792, 0.094907, 0.150342, 0.094907, 0.023792,
      0.015019, 0.059912, 0.094907, 0.059912, 0.015019,
      0.003765, 0.015019, 0.023792, 0.015019, 0.003765
   );

   int kernelRadius = 2;
   for (int x = -kernelRadius; x <= kernelRadius; ++x)
   {
      for (int y = -kernelRadius; y <= kernelRadius; ++y)
      {
         vec2 offset = vec2(float(x), float(y)) * texelSize;
         float weight = kernel[(x + kernelRadius) * 5 + (y + kernelRadius)];
         result += texture(ssaoInput, TexCoords + offset).r * weight;
         weights += weight;
      }
   }

   fragColor = result / weights;
}