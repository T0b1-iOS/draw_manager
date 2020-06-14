struct VS_INPUT
{
	float3 position : POSITION;
	float4 color0 : COLOR0;
	float2 texcoord0 : TEXCOORD0;
};

struct VS_OUTPUT
{
	float4 position : POSITION;
	float4 hposition : TEXCOORD0;
	float4 color0 : COLOR0;
	float2 texcoord0 : TEXCOORD1;
	float2 screenPos : TEXCOORD2;
};

struct PS_OUTPUT
{
	float4 color : COLOR;
};

sampler curtex : register(s0);
sampler backbuffer : register(s1);  // backbuffer copy

float4 dimension : register(c4);
// x,y = center; z = radius*radius; screenSpace
float4 scissor : register(c5);
float4 key : register(c8);

row_major float4x4 worldViewProj : register(c9);

/*bool overlay : register(b0);*/
bool samplerAvailable : register(b1);