#include "../include/types.hlsli"

float4 main(VS_OUTPUT IN) : SV_TARGET
{
    float4 color;

    float2 center = float2(scissor.x, scissor.y);
    float2 pos = float2(IN.screenPos.x, IN.screenPos.y);
    float2 distVec = center - pos;
    float distSqr = dot(distVec, distVec);

    if (distSqr > scissor.z)
    {
        color = float4(0, 0, 0, 0);
        return color;
    }

    float distToEdge = sqrt(scissor.z) - sqrt(distSqr);
    float alpha = saturate(distToEdge);
    
    color = IN.color0 * texture0.Sample(curtex, IN.texcoord0);
    color.a *= alpha;
    
    return color;
}