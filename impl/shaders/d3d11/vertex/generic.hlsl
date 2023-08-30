#include "../include/types.hlsli"

#pragma pack_matrix( row_major )
cbuffer vtxBuf : register(b0)
{
    float4x4 worldViewProj;
};

VS_OUTPUT main(VS_INPUT IN)
{
    VS_OUTPUT OUT;

    float4 v = float4(IN.pos.x, IN.pos.y, IN.pos.z, 1.0f);
    //float4 tmp = v;
    float4 tmp = mul(v, worldViewProj);
    OUT.position = tmp;
    OUT.hposition = tmp;
    OUT.color0 = IN.col;
    OUT.texcoord0 = IN.uv;
    OUT.screenPos = float2(IN.pos.x, IN.pos.y);

    return OUT;
}