#include "shaders.hpp"

namespace util::draw::shaders
{
	const char *pixel_shader = R"(
struct VS_OUTPUT
{
    float4 position : POSITION;
    float4 hposition : TEXCOORD0;
    float4 color0 : COLOR0;
    float2 texcoord0 : TEXCOORD1;
};

struct PS_OUTPUT
{
    float4 color : COLOR;
};

sampler curtex : register(s0);
sampler backbuffer : register(s1);
float4 dimension : register(c4);

struct matrix_holder {
    float bmatrix[25];
};

matrix_holder get2DMatrix( float filterVal, float strength)
{
    float matrix1[ 25 ] =
    {
        0, 1, 2, 1, 0,
        1, 3, 5, 3, 1,
        2, 5, 16, 5, 2,
        1, 3, 5, 3, 1,
        0, 1, 2, 1, 0
    };

    matrix_holder ret;

    float sum = 0;

    for( int i = 0; i < 25; i++ )
    {
        if( i == 12 )
            sum += filterVal;
        else
            sum += matrix1[ i ];
    }

    for( int j = 0; j < 25; j++ )
    {
        if( j == 12 )
            ret.bmatrix[ j ] = filterVal / sum;
        else
            ret.bmatrix[ j ] = matrix1[ j ] / sum;
    }

    return ret;
}

float3 blur2D( matrix_holder bmatrix, float4 pos ) {
    int width = dimension.x;
    int height = dimension.y;

    float rsum = 0;
    float gsum = 0;
    float bsum = 0;

    float3 result;
    float2 step = float2(1 / dimension.x, 1 / dimension.y);

    for( int px = -2; px <= 2; px++ )
    {
        for( int py = -2; py <= 2; py++ )
        {
            float g = bmatrix.bmatrix[ 5 * ( px + 2 ) + ( py + 2 ) ];

            float2 coord = float2( pos.z + px * step.x, pos.w + py * step.y );
            coord.x = clamp( coord.x, 0, 0.9999999 );
            coord.y = clamp( coord.y, -0.9999999, -0.0000001 );

            float4 col = tex2D( backbuffer, coord );
            rsum += col.r * g;
            gsum += col.g * g;
            bsum += col.b * g;
        }
    }

    result.r = rsum;
    result.g = gsum;
    result.b = bsum;

    return result;
}

PS_OUTPUT main( VS_OUTPUT IN )
{
    PS_OUTPUT OUT;
    float2 tex_coord = float2( (IN.hposition.x + 1) / 2, (IN.hposition.y + 1) / -2 );
    tex_coord.x += (1 / dimension.x);
    tex_coord.y += ( 1 / dimension.y );

    matrix_holder holder = get2DMatrix(5, 0.5);

    float3 result = blur2D(holder, float4(tex_coord * dimension, tex_coord));
    OUT.color = float4( result, 1 );

    return OUT;
}
    )";

	const char *vertex_shader = R"(
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
};

row_major float4x4 worldViewProj : register(c9);

VS_OUTPUT main( VS_INPUT IN )
{
    VS_OUTPUT OUT;

    float4 v = float4( IN.position.x,
                       IN.position.y,
                       IN.position.z,
                       1.0f );
    float4 tmp = mul( v, worldViewProj );
    OUT.position = tmp;
    OUT.hposition = tmp;
    OUT.color0 = IN.color0;
    OUT.texcoord0 = IN.texcoord0;

    return OUT;
}
    )";

	const char *key_shader = R"(
struct VS_OUTPUT
{
    float4 position : POSITION;
    float4 hposition : TEXCOORD0;
    float4 color0 : COLOR0;
    float2 texcoord0 : TEXCOORD1;
};

struct PS_OUTPUT
{
    float4 color : COLOR;
};

sampler curtex : register(s0);
float4 key : register(c8);

PS_OUTPUT main( VS_OUTPUT IN )
{
	PS_OUTPUT OUT;

	float4 col = tex2D( curtex, IN.texcoord0 );

	if(col.r == key.r
		&& col.g == key.g
		&& col.b == key.b) 
	{
		OUT.color = float4(0,0,0,0);
	} else 
	{
		OUT.color = col;
	}
	
	return OUT;
}
    )";

	const char *game_circle_scissor = R"(
struct VS_OUTPUT					
{									
	float4 position : POSITION;		
	float2 tex : TEXCOORD0;
    float4 screenPos : POSITION2;
};

sampler curtex : register(s0);
// x,y = center; z = radius*radius; screenSpace
float4 scissor : register(c0);

float4 main( VS_OUTPUT i ) : COLOR
{
	float2 center = float2(scissor.x, scissor.y);
	float2 pos = float2(i.screenPos.x, i.screenPos.y);
	float2 distVec = center - pos;
	float distSqr = dot(distVec, distVec);
	
	if(distSqr > scissor.z) {
		return float4(0,0,0,0);
	}
	
	float4 col = tex2D(curtex, i.tex);
	return col;
}
    )";
}
