#include "../include/types.hlsli"

float4 main(VS_OUTPUT IN) : SV_TARGET
{
    float4 col = IN.color0 * texture0.Sample(curtex, IN.texcoord0);
    return col;
}