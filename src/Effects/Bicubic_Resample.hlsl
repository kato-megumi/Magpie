// Bicubic Resampling - Pillow-compatible PS version
// Uses expanded kernel support when downscaling to prevent aliasing
// Matches PIL.Image.resize() with Image.BICUBIC resampling behavior
//
// When upscaling: standard 4-tap bicubic (support = 2)
// When downscaling: expanded kernel with more taps (support = 2 * scale_factor)

//!MAGPIE EFFECT
//!VERSION 4
//!GENERIC_DOWNSCALER

#include "StubDefs.hlsli"

//!PARAMETER
//!LABEL B
//!DEFAULT 0.33
//!MIN 0
//!MAX 1
//!STEP 0.01
float paramB;

//!PARAMETER
//!LABEL C
//!DEFAULT 0.33
//!MIN 0
//!MAX 1
//!STEP 0.01
float paramC;

//!TEXTURE
Texture2D INPUT;

//!TEXTURE
Texture2D OUTPUT;

//!SAMPLER
//!FILTER POINT
SamplerState sam;

//!PASS 1
//!STYLE PS
//!DESC Bicubic Resample (Pillow-compatible)
//!IN INPUT
//!OUT OUTPUT

// Mitchell-Netravali cubic kernel
// Support: [-2, 2] when scale = 1
// For downscaling, we scale the kernel: support becomes [-2*s, 2*s] where s = input/output
float bicubicWeight(float x, float B, float C) {
	float ax = abs(x);
	float ax2 = ax * ax;
	float ax3 = ax2 * ax;
	
	if (ax < 1.0) {
		return ((12.0 - 9.0 * B - 6.0 * C) * ax3 +
		        (-18.0 + 12.0 * B + 6.0 * C) * ax2 +
		        (6.0 - 2.0 * B)) / 6.0;
	} else if (ax < 2.0) {
		return ((-B - 6.0 * C) * ax3 +
		        (6.0 * B + 30.0 * C) * ax2 +
		        (-12.0 * B - 48.0 * C) * ax +
		        (8.0 * B + 24.0 * C)) / 6.0;
	}
	return 0.0;
}

// Maximum taps per dimension (covers up to 8x downscale: 2*8*2 = 32)
#define MAX_TAPS 32

float4 Pass1(float2 pos) {
	float2 inputSize = float2(GetInputSize());
	float2 outputSize = float2(GetOutputSize());
	
	// Calculate scale factors (input/output ratio)
	// When downscaling, scale > 1
	float2 scale = inputSize / outputSize;
	
	const float B = paramB;
	const float C = paramC;
	
	// For Pillow compatibility:
	// - support = 2.0 (bicubic base support)
	// - filterscale = max(1.0, scale) for each dimension
	// - effective_support = support * filterscale
	float2 filterScale = max(float2(1.0, 1.0), scale);
	float2 support = 2.0 * filterScale;
	
	// Map output pixel center to input space
	float2 center = pos * inputSize;
	
	// Calculate the range of input pixels to sample
	int2 start = int2(floor(center - support));
	int2 end = int2(ceil(center + support));
	
	// Clamp to valid range
	start = max(start, int2(0, 0));
	end = min(end, int2(inputSize) - 1);
	
	// Limit tap count to prevent excessive loops
	end = min(end, start + int2(MAX_TAPS - 1, MAX_TAPS - 1));
	
	// Accumulate weighted samples
	float3 result = 0.0;
	float weightSum = 0.0;
	
	for (int ky = start.y; ky <= end.y; ++ky) {
		// Distance from this input pixel center to the sample center, normalized by filterScale
		float dy = (float(ky) + 0.5 - center.y) / filterScale.y;
		float wy = bicubicWeight(dy, B, C);
		
		if (wy == 0.0) continue;
		
		for (int kx = start.x; kx <= end.x; ++kx) {
			float dx = (float(kx) + 0.5 - center.x) / filterScale.x;
			float wx = bicubicWeight(dx, B, C);
			
			if (wx == 0.0) continue;
			
			float w = wx * wy;
			result += INPUT.Load(int3(kx, ky, 0)).rgb * w;
			weightSum += w;
		}
	}
	
	// Normalize
	if (weightSum > 0.0) {
		result /= weightSum;
	}
	
	return float4(result, 1.0);
}
