struct VS_INPUT {
	float3 pos : POSITION;
	float4 col : COLOR0;
	float2 uv : TEXCOORD0;
};

struct VS_OUTPUT
{
	float4 position : SV_POSITION;
	float4 hposition : TEXCOORD0;
	float4 color0 : COLOR0;
	float2 texcoord0 : TEXCOORD1;
	float2 screenPos : TEXCOORD2;
};

cbuffer scissorBuf : register(b1)
{
    float4 scissor;
    float4 key_color;
}

sampler curtex : register(s0);
Texture2D texture0 : register(t0);

sampler rtCopySampler : register(s1);
Texture2D rtCopyTex : register(t1);