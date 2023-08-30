#include "../include/blur.hlsli"

float4 main(VS_OUTPUT IN) : SV_Target
{
    float4 col;

	float2 tex_coord = float2((IN.hposition.x + 1) / 2, (IN.hposition.y - 1) / -2);
    col = float4(blur(tex_coord, true), 1);

    return col;
}