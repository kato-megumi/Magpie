//!MAGPIE EFFECT
//!VERSION 4
//!SORT_NAME TemporalAA

// Temporal Anti-Aliasing (TAA) for post-processing chains.
//
// Notes / limitations:
// - This is "TAA without true motion vectors". It reduces shimmering and temporal noise,
//   but fast motion can still ghost/blur compared to engine-integrated TAA.
// - Uses PREV_INPUT for motion detection and as temporal history reference.

#include "StubDefs.hlsli"

// --- Parameters ---

//!PARAMETER
//!LABEL History Weight
//!DEFAULT 0.9
//!MIN 0.0
//!MAX 0.98
//!STEP 0.01
float historyWeight;

//!PARAMETER
//!LABEL Motion Threshold
//!DEFAULT 0.02
//!MIN 0.0
//!MAX 0.2
//!STEP 0.001
float motionThreshold;

//!PARAMETER
//!LABEL Motion Boost
//!DEFAULT 6.0
//!MIN 0.0
//!MAX 40.0
//!STEP 0.5
float motionBoost;

//!PARAMETER
//!LABEL Sharpen
//!DEFAULT 0.0
//!MIN 0.0
//!MAX 2.0
//!STEP 0.01
float sharpen;

//!PARAMETER
//!LABEL Reprojection Search (0/1)
//!DEFAULT 1.0
//!MIN 0.0
//!MAX 1.0
//!STEP 1.0
float enableReprojection;


//!TEXTURE
Texture2D INPUT;

//!TEXTURE
//!WIDTH INPUT_WIDTH
//!HEIGHT INPUT_HEIGHT
Texture2D OUTPUT;

// Built-in (created by EffectCompiler)
//!TEXTURE
Texture2D PREV_INPUT;


static MF Luma(MF3 c) {
	// Rec.709
	return dot(c, MF3(0.2126, 0.7152, 0.0722));
}

static uint2 ClampCoord(int2 p, uint2 size) {
	p.x = clamp(p.x, 0, (int)size.x - 1);
	p.y = clamp(p.y, 0, (int)size.y - 1);
	return (uint2)p;
}

static void NeighborhoodMinMax(uint2 p, uint2 size, out MF3 mn, out MF3 mx) {
	mn = MF3(1e9, 1e9, 1e9);
	mx = MF3(-1e9, -1e9, -1e9);

	[unroll]
	for (int dy = -1; dy <= 1; ++dy) {
		[unroll]
		for (int dx = -1; dx <= 1; ++dx) {
			uint2 q = ClampCoord(int2(p) + int2(dx, dy), size);
			MF3 c = (MF3)INPUT[q].rgb;
			mn = min(mn, c);
			mx = max(mx, c);
		}
	}
}

static MF3 Blur4(uint2 p, uint2 size) {
	uint2 p0 = ClampCoord(int2(p) + int2(-1, 0), size);
	uint2 p1 = ClampCoord(int2(p) + int2(1, 0), size);
	uint2 p2 = ClampCoord(int2(p) + int2(0, -1), size);
	uint2 p3 = ClampCoord(int2(p) + int2(0, 1), size);
	return ((MF3)INPUT[p0].rgb + (MF3)INPUT[p1].rgb + (MF3)INPUT[p2].rgb + (MF3)INPUT[p3].rgb) * (MF)0.25;
}

static int2 FindBestReprojectionOffset(uint2 p, uint2 size, MF3 curRgb) {
	// Small search in PREV_INPUT to approximate motion vectors.
	// Returns offset to sample PREV_INPUT.
	MF best = (MF)1e9;
	int2 bestOfs = int2(0, 0);

	[unroll]
	for (int dy = -1; dy <= 1; ++dy) {
		[unroll]
		for (int dx = -1; dx <= 1; ++dx) {
			uint2 q = ClampCoord(int2(p) + int2(dx, dy), size);
			MF3 prevRgb = (MF3)PREV_INPUT[q].rgb;
			MF d = abs(Luma(curRgb) - Luma(prevRgb));
			if (d < best) {
				best = d;
				bestOfs = int2(dx, dy);
			}
		}
	}

	return bestOfs;
}


//!PASS 1
//!DESC Temporal AA
//!IN INPUT, PREV_INPUT
//!OUT OUTPUT
//!BLOCK_SIZE 16, 16
//!NUM_THREADS 64
void Pass1(uint2 blockStart, uint3 threadId) {
	uint2 gxy = (Rmp8x8(threadId.x) << 1) + blockStart;
	uint2 size = GetInputSize();
	if (gxy.x >= size.x || gxy.y >= size.y) {
		return;
	}

	MF4 cur = (MF4)INPUT[gxy];
	MF3 curRgb = (MF3)cur.rgb;

	int2 ofs = int2(0, 0);
	if (enableReprojection > (MF)0.5) {
		ofs = FindBestReprojectionOffset(gxy, size, curRgb);
	}

	uint2 prevPos = ClampCoord(int2(gxy) + ofs, size);
	MF3 prevInRgb = (MF3)PREV_INPUT[prevPos].rgb;
	// Use PREV_INPUT as history (temporal reference from previous frame)
	MF3 histRgb = prevInRgb;

	// Reactive mask: when current differs from reprojected prev input, trust current more.
	MF diff = max(abs(curRgb.r - prevInRgb.r), max(abs(curRgb.g - prevInRgb.g), abs(curRgb.b - prevInRgb.b)));
	MF reactive = saturate((diff - (MF)motionThreshold) * (MF)motionBoost);
	MF hw = lerp((MF)historyWeight, (MF)0.0, reactive);

	// Neighborhood clamp to avoid history pulling colors outside current neighborhood.
	MF3 nMin, nMax;
	NeighborhoodMinMax(gxy, size, nMin, nMax);
	MF3 histClamped = clamp(histRgb, nMin, nMax);

	MF3 taa = lerp(curRgb, histClamped, hw);

	// Optional mild sharpening to counter TAA blur.
	if (sharpen > (MF)0.0) {
		MF3 blur = Blur4(gxy, size);
		taa = taa + (MF)sharpen * (taa - blur);
	}

	OUTPUT[gxy] = MF4(taa, (MF)1.0);
}
