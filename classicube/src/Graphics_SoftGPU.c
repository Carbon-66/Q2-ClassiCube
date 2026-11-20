#define CC_DYNAMIC_VBS_ARE_STATIC
#define CC_SCRATCH_VBS_ARE_SHARED_DYNAMIC
#include "Core.h"
#if CC_GFX_BACKEND == CC_GFX_BACKEND_SOFTGPU
#include "_GraphicsBase.h"
#include "Errors.h"
#include "Window.h"

static cc_bool faceCulling;
static int fb_width, fb_height; 
static struct Bitmap fb_bmp;
static float vp_hwidth, vp_hheight;
static int fb_maxX, fb_maxY;

static BitmapCol* colorBuffer;
static BitmapCol clearColor;
static cc_bool colWrite = true;
static int cb_stride;

static float* depthBuffer;
static int db_stride;

static void* gfx_vertices;
static GfxResourceID white_square;

#ifdef CC_BUILD_Q2
/* The render scale (q2-renderscale, Graphics options in Smooth lighting's place): the world is
 * drawn in a smaller picture (q2_low) and stretched over the view's (the nearest pixel) before the
 * first 2D draw, so the hotbar, text and menus stay sharp. colorBuffer, depthBuffer and the sizes
 * below are the picture being drawn: the world's (q2_low, or the view's at 100%) from a clear, the
 * view's from the first 2D draw on. */
static BitmapCol* q2_full;      /* the view's picture (fb_bmp) */
static int q2_fullW, q2_fullH;  /* 375 x 320 */
static BitmapCol* q2_low;       /* the world's, below 100% (else NULL: drawn in q2_full) */
static int q2_lowW, q2_lowH;
static int q2_scalePct = 100;
static cc_bool q2_scenePending; /* the world's picture is drawn but not yet stretched */
#endif

static void Gfx_RestoreState(void) {
	InitDefaultResources();

	// 1x1 dummy white texture
	struct Bitmap bmp;
	BitmapCol pixels[1] = { BITMAPCOLOR_WHITE };
	Bitmap_Init(bmp, 1, 1, pixels);
	white_square = Gfx_CreateTexture(&bmp, 0, false);
}

static void Gfx_FreeState(void) {
	FreeDefaultResources();
	Gfx_DeleteTexture(&white_square);
}

void Gfx_Create(void) {
	Gfx.MaxTexWidth  = 4096;
	Gfx.MaxTexHeight = 4096;
	Gfx.Created      = true;
	Gfx.BackendType  = CC_GFX_BACKEND_SOFTGPU;
#ifdef CC_BUILD_Q2
	/* the map's border (the water and bedrock past the world's edge, EnvRenderer.c) in 64-block
	 * quads, not up to 2048: a huge quad passing near or behind the eye projects corners thousands
	 * of pixels off the screen, past the float edge functions' precision, and whole triangles or
	 * strips of the edge water dropped out (the bedrock below showed through as dark patches) */
	Gfx.Limitations  = GFX_LIMIT_MINIMAL | GFX_LIMIT_MAX_VERTEX_SIZE;
#else
	Gfx.Limitations  = GFX_LIMIT_MINIMAL;
#endif
}

static void DestroyBuffers(void) {
	Window_FreeFramebuffer(&fb_bmp);
	Mem_Free(depthBuffer);
	depthBuffer = NULL;
#ifdef CC_BUILD_Q2
	if (q2_low) Mem_Free(q2_low);
	q2_low = NULL;
#endif
}

void Gfx_Free(void) { 
	Gfx_FreeState();
	DestroyBuffers();
}


typedef struct CCTexture {
	unsigned short width, height;
	BitmapCol pixels[];
} CCTexture;

static CCTexture* curTexture;
static BitmapCol* curTexPixels;
static int curTexWidth, curTexHeight;
static int texWidthMask, texHeightMask;
static int texSinglePixel;
		
void Gfx_BindTexture(GfxResourceID texId) {
	if (!texId) texId = white_square;
	CCTexture* tex = texId;

	curTexture   = tex;
	curTexPixels = tex->pixels;
	curTexWidth  = tex->width;
	curTexHeight = tex->height;

	texWidthMask   = (1 << Math_ilog2(tex->width))  - 1;
	texHeightMask  = (1 << Math_ilog2(tex->height)) - 1;

	/* Technically the optimisation should only apply if width and height is 1 */
	/* But it's worth sacrificing this, so that rendering the world when */
	/*   no texture pack can use the more optimised rendering path */
	texSinglePixel = curTexWidth == 1;
}
		
void Gfx_DeleteTexture(GfxResourceID* texId) {
	GfxResourceID data = *texId;
	if (data) Mem_Free(data);
	*texId = NULL;
}
		
GfxResourceID Gfx_AllocTexture(struct Bitmap* bmp, int rowWidth, cc_uint8 flags, cc_bool mipmaps) {
	CCTexture* tex = (CCTexture*)Mem_TryAlloc(2 + bmp->width * bmp->height, BITMAPCOLOR_SIZE);
	if (!tex) return NULL;

	tex->width  = bmp->width;
	tex->height = bmp->height;

	CopyPixels(tex->pixels, bmp->width * BITMAPCOLOR_SIZE,
			   bmp->scan0,  rowWidth * BITMAPCOLOR_SIZE,
			   bmp->width,  bmp->height);
	return tex;
}

void Gfx_UpdateTexture(GfxResourceID texId, int x, int y, struct Bitmap* part, int rowWidth, cc_bool mipmaps) {
	CCTexture* tex = (CCTexture*)texId;
	BitmapCol* dst = (tex->pixels + x) + y * tex->width;

	CopyPixels(dst,         tex->width * BITMAPCOLOR_SIZE,
			   part->scan0, rowWidth   * BITMAPCOLOR_SIZE,
			   part->width, part->height);
}

void Gfx_EnableMipmaps(void)  { }
void Gfx_DisableMipmaps(void) { }


/*########################################################################################################################*
*------------------------------------------------------State management---------------------------------------------------*
*#########################################################################################################################*/
void Gfx_SetFog(cc_bool enabled)         { }
static void SetFogColor(PackedCol color) { } // TODO PACK_GIFTAG(q, GS_SET_FOGCOL(0,0,0), GS_REG_FOGCOL);
static void SetFogDensity(float value)   { }
static void SetFogEnd(float value)       { }
static void SetFogMode(FogFunc func)     { }

void Gfx_SetFaceCulling(cc_bool enabled) {
	faceCulling = enabled;
}

static void SetAlphaTest(cc_bool enabled) {
	/* Uses value from Gfx_SetAlphaTest */
}

static void SetAlphaBlend(cc_bool enabled) {
	/* Uses value from Gfx_SetAlphaBlending */
}

void Gfx_SetAlphaArgBlend(cc_bool enabled) { }

static void ClearColorBuffer(void) {
	int i, x, y, size = fb_width * fb_height;

	if (cb_stride == fb_width) {
		for (i = 0; i < size; i++) colorBuffer[i] = clearColor;
	} else {
		/* Slower partial buffer clear */
		for (y = 0; y < fb_height; y++) {
			i = y * cb_stride;
			for (x = 0; x < fb_width; x++) {
				colorBuffer[i + x] = clearColor;
			}
		}
	}
}

#ifdef CC_BUILD_Q2
/* Occlusion culling (q2-occlusion, Q2_Occlusion in Platform_Q2.c): MapRenderer asks, before drawing
 * a chunk, whether its box would be hidden anyway, all behind what's already been drawn. Chunks are
 * drawn nearest first, so the hills, walls and floors in front fill the depth buffer before the
 * chunks they hide are reached. The test reads a coarse copy of the depth buffer: for each 8 x 8
 * pixel tile, the farthest depth in it, worked out again only when something has drawn in the tile
 * since (the tile is marked by every triangle drawn with depth writes over its bounding box). The
 * box is hidden where every tile it covers is all nearer than the box's nearest corner: then every
 * pixel of the chunk would fail the depth test, and the picture is the same without it. */
#define Q2_TILE_SHIFT 3
#define Q2_TILES_MAX  (64 * 64)
extern cc_bool Q2_Occlusion;
static float    q2_tileFar[Q2_TILES_MAX];
/* The depth buffer in 16 bits (from 1.21; Q2_DEPTH16=0 builds the float one): half the memory
 * (240 KB, not 480) to read and write for each pixel tested, and half to clear each frame, and on
 * MIPS an integer test (a shift, a load, a compare) in place of two float compares. Counted on the
 * host (test/count.sh, bench view): 18% fewer L1 and 15% fewer last-level cache misses a frame.
 * z/w is kept in 65280ths: its step to the next block face at the view distance (45) is about a
 * third of a block, and frames differ from the float buffer's only in single pixels along edges. */
#ifndef Q2_DEPTH16
#define Q2_DEPTH16 1
#endif
#define Q2_ZSCALE 2139095040.0f /* 65280 * 32768: see DrawTriangle3D */
#define Math_Clamp_(v, lo, hi) ((v) < (lo) ? (lo) : (v) > (hi) ? (hi) : (v))
static cc_uint8 q2_tileDirty[Q2_TILES_MAX];
static int      q2_tilesX, q2_tilesY; /* 0: the screen's too big for the tiles, no culling */
#define Q2_FAR 100000000.0f
#endif

static void ClearDepthBuffer(void) {
	int i, size = fb_width * fb_height;
#if Q2_DEPTH16
	Mem_Set(depthBuffer, 0xFF, size * 2);
#else
	for (i = 0; i < size; i++) depthBuffer[i] = 100000000.0f;
#endif
#ifdef CC_BUILD_Q2
	q2_tilesX = (fb_width  + (1 << Q2_TILE_SHIFT) - 1) >> Q2_TILE_SHIFT;
	q2_tilesY = (fb_height + (1 << Q2_TILE_SHIFT) - 1) >> Q2_TILE_SHIFT;
	if (q2_tilesX * q2_tilesY > Q2_TILES_MAX) q2_tilesX = q2_tilesY = 0;
	for (i = 0; i < q2_tilesX * q2_tilesY; i++) { q2_tileFar[i] = Q2_FAR; q2_tileDirty[i] = 0; }
#endif
}

#ifdef CC_BUILD_Q2
/* Draws in buf (w x h): the sizes, and the viewport, its */
static void Q2_UseTarget(BitmapCol* buf, int w, int h) {
	colorBuffer = buf;
	cb_stride   = w;
	db_stride   = w;
	fb_width    = w; fb_height = h;
	fb_maxX     = w - 1; fb_maxY = h - 1;
	vp_hwidth   = w / 2.0f;
	vp_hheight  = h / 2.0f;
}

/* The world's picture stretched over the view's, the nearest pixel (a row repeated is copied) */
static void Q2_Stretch(void) {
	static int srcX[1024];
	int x, y, sy, lastSy = -1;
	if (q2_fullW > 1024) return;
	for (x = 0; x < q2_fullW; x++) srcX[x] = x * q2_lowW / q2_fullW;

	for (y = 0; y < q2_fullH; y++)
	{
		BitmapCol* out = q2_full + y * q2_fullW;
		const BitmapCol* in;
		sy = y * q2_lowH / q2_fullH;
		if (sy == lastSy) { Mem_Copy(out, out - q2_fullW, q2_fullW * BITMAPCOLOR_SIZE); continue; }
		lastSy = sy;
		in = q2_low + sy * q2_lowW;
		for (x = 0; x < q2_fullW; x++) out[x] = in[srcX[x]];
	}
}

/* Before 2D, and at a frame's end: the world's picture onto the view's, then the view's drawn in */
static void Q2_Begin2D(void) {
	if (q2_scenePending && q2_low) Q2_Stretch();
	q2_scenePending = false;
	if (colorBuffer != q2_full) Q2_UseTarget(q2_full, q2_fullW, q2_fullH);
}

/* The world's picture at pct percent of the view's each way (q2-renderscale): from the next frame */
void Q2_SetRenderScale(int pct) {
	if (pct < 25 || pct > 100) pct = 100;
	q2_scalePct = pct;
	if (q2_low && colorBuffer == q2_low) Q2_UseTarget(q2_full, q2_fullW, q2_fullH);
	q2_scenePending = false;
	if (q2_low) { Mem_Free(q2_low); q2_low = NULL; }
	if (!q2_full || pct == 100) return;

	q2_lowW = (q2_fullW * pct + 50) / 100;
	q2_lowH = (q2_fullH * pct + 50) / 100;
	q2_low  = (BitmapCol*)Mem_TryAlloc(q2_lowW * q2_lowH, BITMAPCOLOR_SIZE);
	if (!q2_low) Platform_LogConst("No memory for the render scale's picture: 100%");
}
int Q2_GetRenderScale(void) { return q2_scalePct; }
#endif

void Gfx_ClearBuffers(GfxBuffers buffers) {
#ifdef CC_BUILD_Q2
	/* a new frame's world: in its own picture below 100% */
	if (q2_low) Q2_UseTarget(q2_low, q2_lowW, q2_lowH);
	else        Q2_UseTarget(q2_full, q2_fullW, q2_fullH);
	if (buffers & GFX_BUFFER_COLOR) q2_scenePending = true;
#endif
	if (buffers & GFX_BUFFER_COLOR) ClearColorBuffer();
	if (buffers & GFX_BUFFER_DEPTH) ClearDepthBuffer();
}

void Gfx_ClearColor(PackedCol color) {
	int R = PackedCol_R(color);
	int G = PackedCol_G(color);
	int B = PackedCol_B(color);
	int A = PackedCol_A(color);

	clearColor = BitmapCol_Make(R, G, B, A);
}

static void SetDepthTest(cc_bool enabled) {
	/* Uses value from Gfx_SetDepthTest */
}

static void SetDepthWrite(cc_bool enabled) {
	/* Uses value from Gfx_SetDepthWrite */
}

static void SetColorWrite(cc_bool r, cc_bool g, cc_bool b, cc_bool a) {
	// TODO
	colWrite = r || g || b || a;
}

void Gfx_DepthOnlyRendering(cc_bool depthOnly) {
    DefaultDepthOnlyRendering(depthOnly);
}


/*########################################################################################################################*
*-------------------------------------------------------Index buffers-----------------------------------------------------*
*#########################################################################################################################*/
GfxResourceID Gfx_CreateIb2(int count, Gfx_FillIBFunc fillFunc, void* obj) {
	return (void*)1;
}

void Gfx_BindIb(GfxResourceID ib) { }
void Gfx_DeleteIb(GfxResourceID* ib) { }


/*########################################################################################################################*
*-------------------------------------------------------Vertex buffers----------------------------------------------------*
*#########################################################################################################################*/
static GfxResourceID Gfx_AllocStaticVb(VertexFormat fmt, int count) {
	return Mem_TryAlloc(count, strideSizes[fmt]);
}

void Gfx_BindVb(GfxResourceID vb) { gfx_vertices = vb; }

void Gfx_DeleteVb(GfxResourceID* vb) {
	GfxResourceID data = *vb;
	if (data) Mem_Free(data);
	*vb = 0;
}

void* Gfx_LockVb(GfxResourceID vb, VertexFormat fmt, int count) { return vb; }

void Gfx_UnlockVb(GfxResourceID vb) { }


/*########################################################################################################################*
*---------------------------------------------------------Matrices--------------------------------------------------------*
*#########################################################################################################################*/
static float texOffsetX, texOffsetY;
static struct Matrix _view, _proj, _mvp;

void Gfx_LoadMatrix(MatrixType type, const struct Matrix* matrix) {
	if (type == MATRIX_VIEW) _view = *matrix;
	if (type == MATRIX_PROJ) _proj = *matrix;

	Matrix_Mul(&_mvp, &_view, &_proj);
}

void Gfx_LoadMVP(const struct Matrix* view, const struct Matrix* proj, struct Matrix* mvp) {
	_view = *view;
	_proj = *proj;

	Matrix_Mul(mvp, view, proj);
	_mvp  = *mvp;
}

void Gfx_EnableTextureOffset(float x, float y) {
	texOffsetX = x;
	texOffsetY = y;
}

void Gfx_DisableTextureOffset(void) {
	texOffsetX = 0;
	texOffsetY = 0;
}

void Gfx_CalcOrthoMatrix(struct Matrix* matrix, float width, float height, float zNear, float zFar) {
	/* Source https://learn.microsoft.com/en-us/windows/win32/direct3d9/d3dxmatrixorthooffcenterrh */
	/*   The simplified calculation below uses: L = 0, R = width, T = 0, B = height */
	/* NOTE: This calculation is shared with Direct3D 11 backend */
	*matrix = Matrix_Identity;

	matrix->row1.x =  2.0f / width;
	matrix->row2.y = -2.0f / height;
	matrix->row3.z =  1.0f / (zNear - zFar);

	matrix->row4.x = -1.0f;
	matrix->row4.y =  1.0f;
	matrix->row4.z = zNear / (zNear - zFar);
}

static float Cotangent(float x) { return Math_CosF(x) / Math_SinF(x); }
void Gfx_CalcPerspectiveMatrix(struct Matrix* matrix, float fov, float aspect, float zFar) {
	float zNear = 0.1f;

	/* Source https://learn.microsoft.com/en-us/windows/win32/direct3d9/d3dxmatrixperspectivefovrh */
	/* NOTE: This calculation is shared with Direct3D 11 backend */
	float c = Cotangent(0.5f * fov);
	*matrix = Matrix_Identity;

	matrix->row1.x =  c / aspect;
	matrix->row2.y =  c;
	matrix->row3.z = zFar / (zNear - zFar);
	matrix->row3.w = -1.0f;
	matrix->row4.z = (zNear * zFar) / (zNear - zFar);
	matrix->row4.w =  0.0f;
}


/*########################################################################################################################*
*---------------------------------------------------------Rendering-------------------------------------------------------*
*#########################################################################################################################*/
typedef struct Vector3 { float x, y, z; } Vector3;
typedef struct Vector2 { float x, y; } Vector2;
typedef struct Vertex_ {
	float x, y, z, w;
	float u, v;
	PackedCol c;
} Vertex;

static void TransformVertex2D(int index, Vertex* vertex) {
	// TODO: avoid the multiply, just add down in DrawTriangles
	char* ptr = (char*)gfx_vertices + index * gfx_stride;
	Vector3* pos = (Vector3*)ptr;
	vertex->x = pos->x;
	vertex->y = pos->y;

	if (gfx_format != VERTEX_FORMAT_TEXTURED) {
		struct VertexColoured* v = (struct VertexColoured*)ptr;
		vertex->u = 0.0f;
		vertex->v = 0.0f;
		vertex->c = v->Col;
	} else {
		struct VertexTextured* v = (struct VertexTextured*)ptr;
		vertex->u = v->U;
		vertex->v = v->V;
		vertex->c = v->Col;
	}
}

static int TransformVertex3D(int index, Vertex* vertex) {
	// TODO: avoid the multiply, just add down in DrawTriangles
	char* ptr = (char*)gfx_vertices + index * gfx_stride;
	Vector3* pos = (Vector3*)ptr;

	vertex->x = pos->x * _mvp.row1.x + pos->y * _mvp.row2.x + pos->z * _mvp.row3.x + _mvp.row4.x;
	vertex->y = pos->x * _mvp.row1.y + pos->y * _mvp.row2.y + pos->z * _mvp.row3.y + _mvp.row4.y;
	vertex->z = pos->x * _mvp.row1.z + pos->y * _mvp.row2.z + pos->z * _mvp.row3.z + _mvp.row4.z;
	vertex->w = pos->x * _mvp.row1.w + pos->y * _mvp.row2.w + pos->z * _mvp.row3.w + _mvp.row4.w;

	if (gfx_format != VERTEX_FORMAT_TEXTURED) {
		struct VertexColoured* v = (struct VertexColoured*)ptr;
		vertex->u = 0.0f;
		vertex->v = 0.0f;
		vertex->c = v->Col;
	} else {
		struct VertexTextured* v = (struct VertexTextured*)ptr;
		vertex->u = (v->U + texOffsetX);
		vertex->v = (v->V + texOffsetY);
		vertex->c = v->Col;
	}
	return vertex->z >= 0.0f;
}

static void ViewportVertex3D(Vertex* vertex) {
	float invW = 1.0f / vertex->w;

	vertex->x = vp_hwidth  * (1 + vertex->x * invW);
	vertex->y = vp_hheight * (1 - vertex->y * invW);
	vertex->z = vertex->z * invW;
	vertex->w = invW;

	vertex->u *= invW;
	vertex->v *= invW;
}

// Ensure it's inlined, whereas Math_FloorF might not be
static CC_INLINE int FastFloor(float value) {
	int valueI = (int)value;
	return valueI > value ? valueI - 1 : valueI;
}

static void DrawSprite2D(Vertex* V0, Vertex* V1, Vertex* V2) {
	PackedCol vColor = V0->c;
	int minX = (int)V0->x;
	int minY = (int)V0->y;
	int maxX = (int)V1->x;
	int maxY = (int)V2->y;

	// Reject triangles completely outside
	if (maxX < 0 || minX > fb_maxX) return;
	if (maxY < 0 || minY > fb_maxY) return;

	int begTX = (int)(V0->u * curTexWidth);
	int begTY = (int)(V0->v * curTexHeight);
	int delTX = (int)(V1->u * curTexWidth)  - begTX;
	int delTY = (int)(V2->v * curTexHeight) - begTY;

	int width = maxX - minX, height = maxY - minY;
	if (width == 0) width = 1;
	if (height == 0) height = 1;

	int fast =  delTX == width && delTY == height && 
				(begTX + delTX < curTexWidth ) && 
				(begTY + delTY < curTexHeight);

	// Perform scissoring
	minX = max(minX, 0); maxX = min(maxX, fb_maxX);
	minY = max(minY, 0); maxY = min(maxY, fb_maxY);

	int x, y;
	for (y = minY; y <= maxY; y++) 
	{
		int texY = fast ? (begTY + (y - minY)) : (((begTY + delTY * (y - minY) / height)) & texHeightMask);
		for (x = minX; x <= maxX; x++) 
		{
			int texX = fast ? (begTX + (x - minX)) : (((begTX + delTX * (x - minX) / width)) & texWidthMask);
			int texIndex = texY * curTexWidth + texX;

			BitmapCol color = curTexPixels[texIndex];
			int R, G, B, A;

			A = BitmapCol_A(color);
			if (gfx_alphaBlend && A == 0) continue;
			int cb_index = y * cb_stride + x;

			if (gfx_alphaBlend && A != 255) {
				BitmapCol dst = colorBuffer[cb_index];
				int dstR = BitmapCol_R(dst);
				int dstG = BitmapCol_G(dst);
				int dstB = BitmapCol_B(dst);

				R = BitmapCol_R(color);
				G = BitmapCol_G(color);
				B = BitmapCol_B(color);

				// R = (R * A + dstR * (255 - A)) >> 8;
				R = dstR + ((A * (R - dstR)) >> 8);
				G = dstG + ((A * (G - dstG)) >> 8);
				B = dstB + ((A * (B - dstB)) >> 8);
				color = BitmapCol_Make(R, G, B, 0xFF);
			}

			if (vColor != PACKEDCOL_WHITE) {
				int r1 = PackedCol_R(vColor), r2 = BitmapCol_R(color);
				R = ( r1 * r2 ) >> 8;
				int g1 = PackedCol_G(vColor), g2 = BitmapCol_G(color);
				G = ( g1 * g2 ) >> 8;
				int b1 = PackedCol_B(vColor), b2 = BitmapCol_B(color);
				B = ( b1 * b2 ) >> 8;

				color = BitmapCol_Make(R, G, B, 0xFF);
			}

			colorBuffer[cb_index] = color;
		}
	}
}

#define edgeFunction(ax,ay, bx,by, cx,cy) (((bx) - (ax)) * ((cy) - (ay)) - ((by) - (ay)) * ((cx) - (ax)))

static void DrawTriangle2D(Vertex* V0, Vertex* V1, Vertex* V2) {
	int x0 = (int)V0->x, y0 = (int)V0->y;
	int x1 = (int)V1->x, y1 = (int)V1->y;
	int x2 = (int)V2->x, y2 = (int)V2->y;
	int minX = min(x0, min(x1, x2));
	int minY = min(y0, min(y1, y2));
	int maxX = max(x0, max(x1, x2));
	int maxY = max(y0, max(y1, y2));

	// Reject triangles completely outside
	if (maxX < 0 || minX > fb_maxX) return;
	if (maxY < 0 || minY > fb_maxY) return;

	// Perform scissoring
	minX = max(minX, 0); maxX = min(maxX, fb_maxX);
	minY = max(minY, 0); maxY = min(maxY, fb_maxY);

	float u0 = V0->u * curTexWidth,  u1 = V1->u * curTexWidth,  u2 = V2->u * curTexWidth;
	float v0 = V0->v * curTexHeight, v1 = V1->v * curTexHeight, v2 = V2->v * curTexHeight;
	PackedCol color = V0->c;

	int area = edgeFunction(x0,y0, x1,y1, x2,y2);
	float factor = 1.0f / area;
	int x, y;
	
	// https://fgiesen.wordpress.com/2013/02/10/optimizing-the-basic-rasterizer/
	// Essentially these are the deltas of edge functions between X/Y and X/Y + 1 (i.e. one X/Y step)
	int dx01  = y0 - y1, dy01 = x1 - x0;
	int dx12  = y1 - y2, dy12 = x2 - x1;
	int dx20  = y2 - y0, dy20 = x0 - x2;

	float bc0_start = edgeFunction(x1,y1, x2,y2, minX+0.5f,minY+0.5f);
	float bc1_start = edgeFunction(x2,y2, x0,y0, minX+0.5f,minY+0.5f);
	float bc2_start = edgeFunction(x0,y0, x1,y1, minX+0.5f,minY+0.5f);

	for (y = minY; y <= maxY; y++, bc0_start += dy12, bc1_start += dy20, bc2_start += dy01) 
	{
		float bc0 = bc0_start;
		float bc1 = bc1_start;
		float bc2 = bc2_start;

		for (x = minX; x <= maxX; x++, bc0 += dx12, bc1 += dx20, bc2 += dx01) 
		{
			float ic0 = bc0 * factor;
			float ic1 = bc1 * factor;
			float ic2 = bc2 * factor;

			if (ic0 < 0 || ic1 < 0 || ic2 < 0) continue;
			int cb_index = y * cb_stride + x;

			int R, G, B, A;
			if (gfx_format == VERTEX_FORMAT_TEXTURED) {
				float u = ic0 * u0 + ic1 * u1 + ic2 * u2;
				float v = ic0 * v0 + ic1 * v1 + ic2 * v2;
				int texX = ((int)u) & texWidthMask;
				int texY = ((int)v) & texHeightMask;
				int texIndex = texY * curTexWidth + texX;

				BitmapCol tColor = curTexPixels[texIndex];
				int a1 = PackedCol_A(color), a2 = BitmapCol_A(tColor);
				A = ( a1 * a2 ) >> 8;
				int r1 = PackedCol_R(color), r2 = BitmapCol_R(tColor);
				R = ( r1 * r2 ) >> 8;
				int g1 = PackedCol_G(color), g2 = BitmapCol_G(tColor);
				G = ( g1 * g2 ) >> 8;
				int b1 = PackedCol_B(color), b2 = BitmapCol_B(tColor);
				B = ( b1 * b2 ) >> 8;
			} else {
				R = PackedCol_R(color);
				G = PackedCol_G(color);
				B = PackedCol_B(color);
				A = PackedCol_A(color);
			}

			if (gfx_alphaTest && A < 0x80) continue;
			if (gfx_alphaBlend && A == 0)  continue;

			if (gfx_alphaBlend && A != 255) {
				BitmapCol dst = colorBuffer[cb_index];
				int dstR = BitmapCol_R(dst);
				int dstG = BitmapCol_G(dst);
				int dstB = BitmapCol_B(dst);

				// R = (R * A + dstR * (255 - A)) >> 8;
				R = dstR + ((A * (R - dstR)) >> 8);
				G = dstG + ((A * (G - dstG)) >> 8);
				B = dstB + ((A * (B - dstB)) >> 8);
			}

			colorBuffer[cb_index] = BitmapCol_Make(R, G, B, 0xFF);
		}
	}
}

#define MultiplyColors(vColor, tColor) \
	a1 = PackedCol_A(vColor); \
	a2 = BitmapCol_A(tColor); \
	A  = ( a1 * a2 ) >> 8;    \
\
	r1 = PackedCol_R(vColor); \
	r2 = BitmapCol_R(tColor); \
	R  = ( r1 * r2 ) >> 8;    \
\
	g1 = PackedCol_G(vColor); \
	g2 = BitmapCol_G(tColor); \
	G  = ( g1 * g2 ) >> 8;    \
\
	b1 = PackedCol_B(vColor); \
	b2 = BitmapCol_B(tColor); \
	B  = ( b1 * b2 ) >> 8;    \

#ifdef CC_BUILD_Q2
/* Pixels along a row between exact texture coordinates (q2-texspan; 0: every pixel exact): see
 * DrawTriangle3D. Read with q2-cores before the first 3D draw. 0 by default: on the Q2, 8 drew no faster. */
static int q2_texSpan = 0;
/* two threads: bands of 1 << q2_bandShift rows each (q2-bandrows in options.txt: 1, 2, 4, 8, 16;
 * 16 by default: on the host 16 drew fastest, 1 slowest, by about a tenth) */
static int q2_bandShift = 4;
/* each batch of quads: hidden quads skipped (Q2_QuadHidden; occlusion on, depth tested, tiles kept) */
static cc_bool q2_occlusionNow;
/* what the main thread is doing, for the stall watcher (Platform_Q2.c) */
extern volatile const char* Q2_Phase;
#endif

#ifdef CC_BUILD_Q2
/* The corners where they are, to a fraction of a pixel, not cut down to whole pixels: cut down,
 * two triangles meeting along an edge could leave a hairline between them where a long merged
 * face meets shorter ones (a T-junction), and the sky or the dark showed through the blocks.
 * Every pixel's centre is tested against the exact edges instead. */
typedef float TriCoord;
#else
typedef int TriCoord;
#endif

/* phase: the rows to fill, all (-1), or for two threads (Q2) the 16-row bands of one parity (0:
 * rows 0-15, 32-47 and so on; 1: rows 16-31, 48-63...) */
static void DrawTriangle3D(Vertex* V0, Vertex* V1, Vertex* V2, int phase) {
	TriCoord x0 = (TriCoord)V0->x, y0 = (TriCoord)V0->y;
	TriCoord x1 = (TriCoord)V1->x, y1 = (TriCoord)V1->y;
	TriCoord x2 = (TriCoord)V2->x, y2 = (TriCoord)V2->y;
	/* the pixels whose centres can be inside: the bounding box (whole, as the corners' pixels) */
	int minX = (int)min(x0, min(x1, x2));
	int minY = (int)min(y0, min(y1, y2));
	int maxX = (int)max(x0, max(x1, x2));
	int maxY = (int)max(y0, max(y1, y2));

	TriCoord area = edgeFunction(x0,y0, x1,y1, x2,y2);
	if (faceCulling) {
		// https://gamedev.stackexchange.com/questions/203694/how-to-make-backface-culling-work-correctly-in-both-orthographic-and-perspective
		if (area < 0) return;
	}

	// Reject triangles completely outside
	if (maxX < 0 || minX > fb_maxX) return;
	if (maxY < 0 || minY > fb_maxY) return;

	// Perform scissoring
	minX = max(minX, 0); maxX = min(maxX, fb_maxX);
	minY = max(minY, 0); maxY = min(maxY, fb_maxY);

	// NOTE: W in frag variables below is actually 1/W 
	float factor = 1.0f / area;
	float w0 = V0->w, w1 = V1->w, w2 = V2->w;
	
	// TODO proper clipping
	if (w0 <= 0 || w1 <= 0 || w2 <= 0) {
		return;
	}

	float z0 = V0->z, z1 = V1->z, z2 = V2->z;
	PackedCol color = V0->c;

	float u0 = V0->u * curTexWidth,  u1 = V1->u * curTexWidth,  u2 = V2->u * curTexWidth;
	float v0 = V0->v * curTexHeight, v1 = V1->v * curTexHeight, v2 = V2->v * curTexHeight;
	
	// https://fgiesen.wordpress.com/2013/02/10/optimizing-the-basic-rasterizer/
	// Essentially these are the deltas of edge functions between X/Y and X/Y + 1 (i.e. one X/Y step)
	TriCoord dx01  = y0 - y1, dy01 = x1 - x0;
	TriCoord dx12  = y1 - y2, dy12 = x2 - x1;
	TriCoord dx20  = y2 - y0, dy20 = x0 - x2;

	float bc0_start = edgeFunction(x1,y1, x2,y2, minX+0.5f,minY+0.5f);
	float bc1_start = edgeFunction(x2,y2, x0,y0, minX+0.5f,minY+0.5f);
	float bc2_start = edgeFunction(x0,y0, x1,y1, minX+0.5f,minY+0.5f);

	int R, G, B, A, x, y;
	int a1, r1, g1, b1;
	int a2, r2, g2, b2;
	cc_bool texturing = gfx_format == VERTEX_FORMAT_TEXTURED;

	if (!texturing) {
		R = PackedCol_R(color);
		G = PackedCol_G(color);
		B = PackedCol_B(color);
		A = PackedCol_A(color);
	} else if (texSinglePixel) {
		/* Don't need to calculate complicated texturing in this case */
		float rawY0 = v0 / w0;
		float rawY1 = v1 / w1;

		float rawY = min(rawY0, rawY1);
		int texY   = (int)(rawY + 0.01f) & texHeightMask;
		MultiplyColors(color, curTexPixels[texY * curTexWidth]);
		texturing = false;
	}

#ifdef CC_BUILD_Q2
#define Q2_SPAN_MAX 16
	/* The Q2's CPU fills every pixel itself, which is most of a frame's time, so this loop does
	 * less per pixel than the one below while drawing the same pixels:
	 * - each row runs only across the triangle, not its whole bounding box: the bounds come from
	 *   the edge functions, widened a pixel so the exact test below still decides every pixel;
	 * - depth is z/w, which is linear across the screen, stepped per pixel with no divide, and
	 *   tested first: the divide for 1/w (perspective-correct texture coordinates) is only done
	 *   for a pixel that passes, where blocks hide much of what's behind them. z/w orders pixels
	 *   as the per-pixel value below does, and nothing else reads the depth buffer;
	 * - the edge functions, z/w, 1/w and u/w, v/w are all linear across the screen, so each is
	 *   stepped by an add per pixel, not made again from the three corners' values;
	 * - the texture coordinates are exact (the divide) only every q2_texSpan pixels along a row,
	 *   at the first pixel drawn in each stretch and at its end, and stepped linearly between (as
	 *   Quake's renderer did): a stretch with nothing drawn costs no divide at all. Texel edges
	 *   move a pixel here and there. q2-texspan in options.txt: 0 is exact, every pixel. */
	if (!area) return;
	if (gfx_depthWrite && q2_tilesX) {
		/* the tiles this may draw in: their farthest depth is worked out again when next asked */
		int ty, tx0 = minX >> Q2_TILE_SHIFT, tx1 = maxX >> Q2_TILE_SHIFT;
		for (ty = minY >> Q2_TILE_SHIFT; ty <= (maxY >> Q2_TILE_SHIFT); ty++)
		{
			/* with two threads, only the tiles in this thread's bands: it draws nothing in the
			 * others', and each thread's tiles are then its own while they draw */
			if (phase >= 0 && q2_bandShift >= Q2_TILE_SHIFT &&
				(((ty << Q2_TILE_SHIFT) >> q2_bandShift) & 1) != phase) continue;
			Mem_Set(&q2_tileDirty[ty * q2_tilesX + tx0], 1, tx1 - tx0 + 1);
		}
	}
	{
		float s    = area > 0 ? 1.0f : -1.0f; /* inside: every edge function's sign is the area's */
		/* a value linear across the screen: at (minX, minY) and its steps in x and y */
#define Q2_LINEAR(a, b, c, at, sx, sy) \
		float at = (bc0_start * (a) + bc1_start * (b) + bc2_start * (c)) * factor; \
		float sx = (dx12 * (a) + dx20 * (b) + dx01 * (c)) * factor; \
		float sy = (dy12 * (a) + dy20 * (b) + dy01 * (c)) * factor;
		Q2_LINEAR(z0, z1, z2, zRow, dzdx, dzdy)   /* z/w */
		Q2_LINEAR(w0, w1, w2, wRow, dwdx, dwdy)   /* 1/w */
		Q2_LINEAR(u0, u1, u2, uRow, dudx, dudy)   /* u/w */
		Q2_LINEAR(v0, v1, v2, vRow, dvdx, dvdy)   /* v/w */
#undef Q2_LINEAR
		float e[3], d[3];
		/* the state, in locals: the build's -fno-strict-aliasing makes the compiler read a global
		 * again after every store to a buffer, which is every pixel */
		const cc_bool depthTest = gfx_depthTest, depthWrite = gfx_depthWrite;
		const cc_bool alphaTest = gfx_alphaTest, alphaBlend = gfx_alphaBlend, colorWrite = colWrite;
		const int span = q2_texSpan;
		/* 1/n for a stretch's length n, 1 to Q2_SPAN_MAX */
		static float invLen[Q2_SPAN_MAX + 1];
		if (!invLen[1]) { int k; for (k = 1; k <= Q2_SPAN_MAX; k++) invLen[k] = 1.0f / k; }
		const int texW = curTexWidth, texWMask = texWidthMask, texHMask = texHeightMask;
		/* the texture's row held within the corners' rows: inside a triangle it always is, but at
		 * its edges (rounding, the edge allowance) it ran a little past, and a block's tile in the
		 * atlas ends where the next, below it, begins: at a water face's edge, seen low, a row of
		 * pixels took the next tile's empty texels and the water there was drawn clear (a line of
		 * the dirt under it showed through). Rows only: the atlas is one tile wide, so a column
		 * past the edge wraps within the same tile. Held before the wrap, so faces repeating a
		 * texture (the map's border) still repeat. (A corner's v is stored over w here.) */
		const float cv0 = V0->v / V0->w, cv1 = V1->v / V1->w, cv2 = V2->v / V2->w;
		const int rowLo = (int)(min(cv0, min(cv1, cv2)) * curTexHeight);
		const int rowHi = (int)(max(cv0, max(cv1, cv2)) * curTexHeight - 0.01f);
		BitmapCol* const texels = curTexPixels;
		BitmapCol* const cBuf   = colorBuffer;
#if Q2_DEPTH16
		cc_uint16* const dBuf   = (cc_uint16*)depthBuffer;
		/* z/w stepped as an integer, in 65280ths shifted up 15 (Q2_ZSCALE): the depth to store is
		 * then one shift away. Past the far plane z/w reaches only about 1.002, so 65280ths keep
		 * that in 16 bits too, in order. A step too large to hold is only in a triangle less than
		 * a pixel wide, whose rows have one pixel each: the step is never taken there */
		const float dzs = dzdx * Q2_ZSCALE;
		const int dzf = dzs > 2147483000.0f ? 2147483000 : dzs < -2147483000.0f ? -2147483000 : (int)dzs;
#else
		float* const     dBuf   = depthBuffer;
#endif
		d[0] = s * dx12; d[1] = s * dx20; d[2] = s * dx01;
		/* where each edge crosses a row: e / d, a multiply by 1 / d worked out once here */
		float invD[3];
		invD[0] = d[0] ? 1.0f / d[0] : 0; invD[1] = d[1] ? 1.0f / d[1] : 0; invD[2] = d[2] ? 1.0f / d[2] : 0;
		/* A pixel within a hundredth of a pixel of an edge counts as inside: two triangles sharing
		 * an edge each work it out with their own rounding, and a pixel right on it could
		 * otherwise be left by both (a crack); this way both draw it */
		float tol[3];
		tol[0] = 0.01f * (Math_AbsF(dx12) + Math_AbsF(dy12));
		tol[1] = 0.01f * (Math_AbsF(dx20) + Math_AbsF(dy20));
		tol[2] = 0.01f * (Math_AbsF(dx01) + Math_AbsF(dy01));

		/* rows: all, or for two threads only this thread's 16-row bands (phase 0 or 1): bands, not
		 * alternate rows, so the two cores write apart in memory and seldom share a cache line;
		 * every other band, so sky above and ground below share out evenly */
		const float sz = dzdy, sw = dwdy, su = dudy, sv = dvdy;
		/* the edge functions at each row's start, from the first row's: worked out per row, not
		 * stepped, so rounding doesn't build up down a tall triangle */
		const float bc0_top = bc0_start, bc1_top = bc1_start, bc2_top = bc2_start;
		const int yTop = minY;

		for (y = minY; y <= maxY; y++, zRow += sz, wRow += sw, uRow += su, vRow += sv)
		{
			int xs = minX, xe = maxX, k, empty = 0;
			float rows;
			if (phase >= 0 && ((y >> q2_bandShift) & 1) != phase) {
				/* the other thread's band: on to the start of the next (the loop adds the last row) */
				int skip = (((y >> q2_bandShift) + 1) << q2_bandShift) - y - 1;
				y += skip;
				zRow += sz * skip; wRow += sw * skip; uRow += su * skip; vRow += sv * skip;
				continue;
			}
			rows = (float)(y - yTop);
			e[0] = s * (bc0_top + dy12 * rows) + tol[0];
			e[1] = s * (bc1_top + dy20 * rows) + tol[1];
			e[2] = s * (bc2_top + dy01 * rows) + tol[2];

			for (k = 0; k < 3; k++) {
				/* to a thousandth of a pixel past the crossing, not a pixel: a face seen edge on
				 * is many short rows, and a pixel each side was much of each (the test below still
				 * decides each pixel; the edges already lean out a hundredth of a pixel, tol) */
				if (d[k] > 0) {        /* inside from x on, where e + d * (x - minX) >= 0 */
					float t = -e[k] * invD[k] - 0.001f;
					if (t > 0) xs = max(xs, minX + (int)t + ((float)(int)t < t));
				} else if (d[k] < 0) { /* inside up to x */
					float t = -e[k] * invD[k] + 0.001f;
					if (t < 0) { empty = 1; break; }
					if (t < maxX - minX) xe = min(xe, minX + (int)t);
				} else if (e[k] < 0) { empty = 1; break; }
			}
			if (empty || xs > xe) continue;

			int n = xs - minX;
			/* the edge functions with the area's sign: a pixel is in where all three are >= 0 */
			float e0 = e[0] + d[0] * n, e1 = e[1] + d[1] * n, e2 = e[2] + d[2] * n;
			float z  = zRow + dzdx * n, iw = wRow + dwdx * n;
#if Q2_DEPTH16
			int zf = (int)(Math_Clamp_(z, -0.5f, 1.0035f) * Q2_ZSCALE);
#endif
			float uw = uRow + dudx * n, vw = vRow + dvdx * n;
			int db_index = y * db_stride + xs;
			int cb_index = y * cb_stride + xs;
			int spanEnd = xs; /* the pixel past the stretch whose u and v below are stepped */
			float u = 0, v = 0, du = 0, dv = 0;

#if Q2_DEPTH16
#define Q2_ZSTEP zf += dzf
#else
#define Q2_ZSTEP z += dzdx
#endif
			for (x = xs; x <= xe; x++, e0 += d[0], e1 += d[1], e2 += d[2], Q2_ZSTEP, iw += dwdx,
				 uw += dudx, vw += dvdx, db_index++, cb_index++, u += du, v += dv)
			{
				if (e0 < 0 || e1 < 0 || e2 < 0) continue;
#if Q2_DEPTH16
				/* below 0 (before the near plane) the unsigned shift is past any depth stored */
				cc_uint32 zq = (cc_uint32)zf >> 15;
				if (depthTest && zq > dBuf[db_index]) continue;
#define Q2_DEPTH zq
#else
#define Q2_DEPTH z
				if (depthTest && (z < 0 || z > dBuf[db_index])) continue;
#endif
				if (!colorWrite) {
					if (depthWrite) dBuf[db_index] = Q2_DEPTH;
					continue;
				}

				if (texturing) {
					if (!span) {
						float w = 1 / iw;
						u = uw * w;
						v = vw * w;
					} else if (x >= spanEnd) {
						/* a new stretch from here: exact here and at its end, in between stepped */
						int len = min(span, xe + 1 - x);
						float w  = 1 / iw;
						float we = 1 / (iw + dwdx * len);
						u  = uw * w;
						v  = vw * w;
						du = ((uw + dudx * len) * we - u) * invLen[len];
						dv = ((vw + dvdx * len) * we - v) * invLen[len];
						spanEnd = x + len;
					}
					int row  = (int)v;
					int texX = ((int)u) & texWMask;
					int texY;
					row  = row < rowLo ? rowLo : row > rowHi ? rowHi : row;
					texY = row & texHMask;
					BitmapCol tColor = texels[texY * texW + texX];
					MultiplyColors(color, tColor);
				}

				if (alphaTest && A < 0x80) continue;
				if (depthWrite) dBuf[db_index] = Q2_DEPTH;

				if (!alphaBlend) {
					cBuf[cb_index] = BitmapCol_Make(R, G, B, 0xFF);
					continue;
				}
				BitmapCol dst = cBuf[cb_index];
				int dstR = BitmapCol_R(dst);
				int dstG = BitmapCol_G(dst);
				int dstB = BitmapCol_B(dst);
				int finR = dstR + ((A * (R - dstR)) >> 8);
				int finG = dstG + ((A * (G - dstG)) >> 8);
				int finB = dstB + ((A * (B - dstB)) >> 8);
				cBuf[cb_index] = BitmapCol_Make(finR, finG, finB, 0xFF);
			}
#undef Q2_DEPTH
		}
		return;
	}
#endif
	for (y = minY; y <= maxY; y++, bc0_start += dy12, bc1_start += dy20, bc2_start += dy01) 
	{
		float bc0 = bc0_start;
		float bc1 = bc1_start;
		float bc2 = bc2_start;

		for (x = minX; x <= maxX; x++, bc0 += dx12, bc1 += dx20, bc2 += dx01) 
		{
			float ic0 = bc0 * factor;
			float ic1 = bc1 * factor;
			float ic2 = bc2 * factor;
			if (ic0 < 0 || ic1 < 0 || ic2 < 0) continue;
			int db_index = y * db_stride + x;

			float w = 1 / (ic0 * w0 + ic1 * w1 + ic2 * w2);
			float z = (ic0 * z0 + ic1 * z1 + ic2 * z2) * w;

			if (gfx_depthTest && (z < 0 || z > depthBuffer[db_index])) continue;
			if (!colWrite) {
				if (gfx_depthWrite) depthBuffer[db_index] = z;
				continue;
			}

			if (texturing) {
				float u = (ic0 * u0 + ic1 * u1 + ic2 * u2) * w;
				float v = (ic0 * v0 + ic1 * v1 + ic2 * v2) * w;
				int texX = ((int)u) & texWidthMask;
				int texY = ((int)v) & texHeightMask;

				int texIndex = texY * curTexWidth + texX;
				BitmapCol tColor = curTexPixels[texIndex];

				MultiplyColors(color, tColor);
			}

			if (gfx_alphaTest && A < 0x80) continue;
			if (gfx_depthWrite) depthBuffer[db_index] = z;
			int cb_index = y * cb_stride + x;
			
			if (!gfx_alphaBlend) {
				colorBuffer[cb_index] = BitmapCol_Make(R, G, B, 0xFF);
				continue;
			}

			BitmapCol dst = colorBuffer[cb_index];
			int dstR = BitmapCol_R(dst);
			int dstG = BitmapCol_G(dst);
			int dstB = BitmapCol_B(dst);

			// int finR = (R * A + dstR * (255 - A)) >> 8;
			int finR = dstR + ((A * (R - dstR)) >> 8);
			int finG = dstG + ((A * (G - dstG)) >> 8);
			int finB = dstB + ((A * (B - dstB)) >> 8);
			colorBuffer[cb_index] = BitmapCol_Make(finR, finG, finB, 0xFF);
		}
	}
}

#define V0_VIS (1 << 0)
#define V1_VIS (1 << 1)
#define V2_VIS (1 << 2)
#define V3_VIS (1 << 3)

// https://github.com/behindthepixels/EDXRaster/blob/master/EDXRaster/Core/Clipper.h
static void ClipLine(Vertex* v1, Vertex* v2, Vertex* V) {
	float t  = Math_AbsF(v1->z / (v2->z - v1->z));
	float invt = 1.0f - t;
	
	V->x = invt * v1->x + t * v2->x;
	V->y = invt * v1->y + t * v2->y;
	//V->z = invt * v1->z + t * v2->z;
	V->z = 0.0f; // clipped against near plane anyways (I.e Z/W = 0 --> Z = 0)
	V->w = invt * v1->w + t * v2->w;
	
	V->u = invt * v1->u + t * v2->u;
	V->v = invt * v1->v + t * v2->v;
	V->c = v1->c;
}

// https://casual-effects.com/research/McGuire2011Clipping/clip.glsl
static void DrawClipped(int mask, Vertex* v0, Vertex* v1, Vertex* v2, Vertex* v3, int phase) {
	Vertex tmp[2];
	Vertex* a = &tmp[0];
	Vertex* b = &tmp[1];

    switch (mask) {
	case V0_VIS:
	{
		//		   v0
		//		  / |
		//       /   |
		// .....A....B...
		//    /      |
		//  v3--v2---v1
		ClipLine(v3, v0, a);
		ClipLine(v0, v1, b);

		ViewportVertex3D(v0);
		ViewportVertex3D(a);
		ViewportVertex3D(b);

		DrawTriangle3D(v0, a, b, phase);
	}
    break;
	case V1_VIS:
	{
		//		   v1
		//		  / |
		//       /   |
		// ....A.....B...
		//    /      |
		//  v0--v3---v2
		ClipLine(v0, v1, a);
		ClipLine(v1, v2, b);

		ViewportVertex3D(v1);
		ViewportVertex3D(a);
		ViewportVertex3D(b);

		DrawTriangle3D(a, b, v1, phase);
	} break;
	case V2_VIS:
	{
		//		   v2
		//		  / |
		//       /   |
		// ....A.....B...
		//    /      |
		//  v1--v0---v3
		ClipLine(v1, v2, a);
		ClipLine(v2, v3, b);

		ViewportVertex3D(v2);
		ViewportVertex3D(a);
		ViewportVertex3D(b);

		DrawTriangle3D(a, b, v2, phase);
	} break;
	case V3_VIS:
	{
		//		   v3
		//		  / |
		//       /   |
		// ....A.....B...
		//    /      |
		//  v2--v1---v0
		ClipLine(v2, v3, a);
		ClipLine(v3, v0, b);

		ViewportVertex3D(v3);
		ViewportVertex3D(a);
		ViewportVertex3D(b);
		
		DrawTriangle3D(b, v3, a, phase);
	}
	break;
	case V0_VIS | V1_VIS:
	{
		//    v0-----------v1
		//     \		   |
		//   ...B.........A...
		//		 \		  |
		//		  v3-----v2
		ClipLine(v1, v2, a);
		ClipLine(v3, v0, b);

		ViewportVertex3D(v0);
		ViewportVertex3D(v1);
		ViewportVertex3D(a);
		ViewportVertex3D(b);

		DrawTriangle3D(v1, v0,  a, phase);
		DrawTriangle3D(a,  v0,  b, phase);
	} break;
	// case V0_VIS | V2_VIS: degenerate case that should never happen
	case V0_VIS | V3_VIS:
	{
		//    v3-----------v0
		//     \		   |
		//   ...B.........A...
		//		 \		  |
		//		  v2-----v1
		ClipLine(v0, v1, a);
		ClipLine(v2, v3, b);

		ViewportVertex3D(v0);
		ViewportVertex3D(v3);
		ViewportVertex3D(a);
		ViewportVertex3D(b);

		DrawTriangle3D(a, v0,  b, phase);
		DrawTriangle3D(b, v0, v3, phase);
	} break;
	case V1_VIS | V2_VIS:
	{
		//    v1-----------v2
		//     \		   |
		//   ...B.........A...
		//		 \		  |
		//		  v0-----v3
		ClipLine(v2, v3, a);
		ClipLine(v0, v1, b);

		ViewportVertex3D(v1);
		ViewportVertex3D(v2);
		ViewportVertex3D(a);
		ViewportVertex3D(b);

		DrawTriangle3D(v1,  b, v2, phase);
		DrawTriangle3D(v2,  b,  a, phase);
	} break;
	// case V1_VIS | V3_VIS: degenerate case that should never happen
	case V2_VIS | V3_VIS:
	{
		//    v2-----------v3
		//     \		   |
		//   ...B.........A...
		//		 \		  |
		//		  v1-----v0
		ClipLine(v3, v0, a);
		ClipLine(v1, v2, b);

		ViewportVertex3D(v2);
		ViewportVertex3D(v3);
		ViewportVertex3D(a);
		ViewportVertex3D(b);

		DrawTriangle3D( b,  a, v2, phase);
		DrawTriangle3D(v2,  a, v3, phase);
	} break;
	case V0_VIS | V1_VIS | V2_VIS:
	{
		//		  --v1--
		//    v0--      --v2
		//      \		 /
		//   ....B.....A...
		//		  \   /
		//		    v3
		// v1,v2,v0  v2,v0,A  v0,A,B
		ClipLine(v2, v3, a);
		ClipLine(v3, v0, b);

		ViewportVertex3D(v0);
		ViewportVertex3D(v1);
		ViewportVertex3D(v2);
		ViewportVertex3D(a);
		ViewportVertex3D(b);

		DrawTriangle3D(v1, v0, v2, phase);
		DrawTriangle3D(v2, v0,  a, phase);
		DrawTriangle3D(v0,  b,  a, phase);
	} break;
	case V0_VIS | V1_VIS | V3_VIS:
	{
		//		  --v0--
		//    v3--      --v1
		//      \		 /
		//   ....B.....A...
		//		  \   /
		//		    v2
		// v0,v1,v3  v1,v3,A  v3,A,B
		ClipLine(v1, v2, a);
		ClipLine(v2, v3, b);

		ViewportVertex3D(v0);
		ViewportVertex3D(v1);
		ViewportVertex3D(v3);
		ViewportVertex3D(a);
		ViewportVertex3D(b);

		DrawTriangle3D(v0, v3, v1, phase);
		DrawTriangle3D(v1, v3,  a, phase);
		DrawTriangle3D(v3,  b,  a, phase);
	} break;
	case V0_VIS | V2_VIS | V3_VIS:
	{
		//		  --v3--
		//    v2--      --v0
		//      \		 /
		//   ....B.....A...
		//		  \   /
		//		    v1
		// v3,v0,v2  v0,v2,A  v2,A,B
		ClipLine(v0, v1, a);
		ClipLine(v1, v2, b);

		ViewportVertex3D(v0);
		ViewportVertex3D(v2);
		ViewportVertex3D(v3);
		ViewportVertex3D(a);
		ViewportVertex3D(b);

		DrawTriangle3D(v3, v2, v0, phase);
		DrawTriangle3D(v0, v2,  a, phase);
		DrawTriangle3D(v2,  b,  a, phase);
	} break;
	case V1_VIS | V2_VIS | V3_VIS:
	{
		//		  --v2--
		//    v1--      --v3
		//      \		 /
		//   ....B.....A...
		//		  \   /
		//		    v0
		// v2,v3,v1  v3,v1,A  v1,A,B
		ClipLine(v3, v0, a);
		ClipLine(v0, v1, b);

		ViewportVertex3D(v1);
		ViewportVertex3D(v2);
		ViewportVertex3D(v3);
		ViewportVertex3D(a);
		ViewportVertex3D(b);

		DrawTriangle3D(v2, v1, v3, phase);
		DrawTriangle3D(v3, v1,  a, phase);
		DrawTriangle3D(v1,  b,  a, phase);
	} break;
	}
}

/* The 3D quads startVertex on, filling the rows phase gives (see DrawTriangle3D) */
#ifdef CC_BUILD_Q2
static float Q2_TileFar(int tx, int ty) {
	int i = ty * q2_tilesX + tx, x, y;
	if (q2_tileDirty[i]) {
		int x0 = tx << Q2_TILE_SHIFT, x1 = min(x0 + (1 << Q2_TILE_SHIFT), fb_width);
		int y0 = ty << Q2_TILE_SHIFT, y1 = min(y0 + (1 << Q2_TILE_SHIFT), fb_height);
#if Q2_DEPTH16
		int far = 0, d;
		for (y = y0; y < y1; y++)
		{
			const cc_uint16* row = (const cc_uint16*)depthBuffer + y * db_stride;
			for (x = x0; x < x1; x++) { d = row[x]; if (d > far) far = d; }
		}
		/* a pixel stored as d is nearer than (d + 1) / 65280; one never drawn, as far as can be */
		q2_tileFar[i]   = far == 65535 ? Q2_FAR : (far + 1) / 65280.0f;
#else
		float far = 0, d;
		for (y = y0; y < y1; y++)
		{
			const float* row = depthBuffer + y * db_stride;
			for (x = x0; x < x1; x++) { d = row[x]; if (d > far) far = d; }
		}
		q2_tileFar[i]   = far;
#endif
		q2_tileDirty[i] = 0;
	}
	return q2_tileFar[i];
}

/* Whether the box (world coordinates) would draw nothing: all off the screen, or all behind what's
 * drawn so far. Conservative: true only when nothing in it could show. Called between draws (the
 * worker thread is idle then: DrawQuads3DAll waits for it). */
cc_bool Gfx_Q2BoxHidden(float x0, float y0, float z0, float x1, float y1, float z1) {
	float minX = 1e30f, maxX = -1e30f, minY = 1e30f, maxY = -1e30f, near = 1e30f;
	int i, tx, ty, tx0, tx1, ty0, ty1;
	if (!Q2_Occlusion || !q2_tilesX) return false;

	for (i = 0; i < 8; i++)
	{
		float x = (i & 1) ? x1 : x0, y = (i & 2) ? y1 : y0, z = (i & 4) ? z1 : z0;
		float cx = x * _mvp.row1.x + y * _mvp.row2.x + z * _mvp.row3.x + _mvp.row4.x;
		float cy = x * _mvp.row1.y + y * _mvp.row2.y + z * _mvp.row3.y + _mvp.row4.y;
		float cz = x * _mvp.row1.z + y * _mvp.row2.z + z * _mvp.row3.z + _mvp.row4.z;
		float cw = x * _mvp.row1.w + y * _mvp.row2.w + z * _mvp.row3.w + _mvp.row4.w;
		float iw, sx, sy, d;
		/* a corner behind the near plane: its faces are clipped, and the box can't be judged */
		if (cz < 0 || cw <= 0) return false;

		iw = 1.0f / cw;
		sx = vp_hwidth  * (1 + cx * iw);
		sy = vp_hheight * (1 - cy * iw);
		d  = cz * iw; /* as DrawTriangle3D's depth: z/w */
		if (sx < minX) minX = sx;
		if (sx > maxX) maxX = sx;
		if (sy < minY) minY = sy;
		if (sy > maxY) maxY = sy;
		if (d  < near) near = d;
	}
	/* wholly off the screen (the box is inside its corners' outline) */
	if (maxX < -1 || maxY < -1 || minX > fb_maxX + 1 || minY > fb_maxY + 1) return true;

	/* a pixel's margin around, and a little nearer, for rounding */
	tx0 = max((int)minX - 1, 0)       >> Q2_TILE_SHIFT;
	tx1 = min((int)maxX + 1, fb_maxX) >> Q2_TILE_SHIFT;
	ty0 = max((int)minY - 1, 0)       >> Q2_TILE_SHIFT;
	ty1 = min((int)maxY + 1, fb_maxY) >> Q2_TILE_SHIFT;
	near -= 0.00001f;

	for (ty = ty0; ty <= ty1; ty++)
		for (tx = tx0; tx <= tx1; tx++)
		{
			if (Q2_TileFar(tx, ty) >= near) return false;
		}
	return true;
}
#endif

#ifdef CC_BUILD_Q2
/* Whether a quad, on the screen and in front of the eye (its corners' screen positions and z/w),
 * would draw nothing for this thread (from 1.19): all behind what's drawn already, by the tiles
 * occlusion culling keeps (Gfx_Q2BoxHidden tests whole chunks with them, before they're drawn), or
 * in no row of this thread's (phase: its 16-row bands). Counted in the bench view, half the
 * triangles drawn were in chunks with no pixel left in the frame: under and beside the eye, whose
 * boxes reach behind it and can't be judged whole, and faces behind hills in chunks partly in view.
 * A quad's own box is judged here, so such a face costs its corners and this test, not the setup of
 * two triangles and a pass over every row of them. Both threads read the tiles while drawing: the
 * depth buffer only gets nearer within a frame, so a tile's far depth read early is only farther
 * (it hides less), never wrong. */
#define Q2_QUAD_TILES q2_quadTiles
static int q2_quadTiles = 12;
static cc_bool Q2_QuadHidden(const Vertex* v, int phase) {
	float minX = v[0].x, maxX = v[0].x, minY = v[0].y, maxY = v[0].y, near = v[0].z;
	int k, tx, ty, tx0, tx1, ty0, ty1;
	for (k = 1; k < 4; k++)
	{
		if (v[k].x < minX) minX = v[k].x;
		if (v[k].x > maxX) maxX = v[k].x;
		if (v[k].y < minY) minY = v[k].y;
		if (v[k].y > maxY) maxY = v[k].y;
		if (v[k].z < near) near = v[k].z;
	}
	/* as DrawTriangle3D's own test: the corners' whole pixels */
	if (maxX < 0 || maxY < 0 || (int)minX > fb_maxX || (int)minY > fb_maxY) return true;

	ty0 = max((int)minY, 0); ty1 = min((int)maxY, fb_maxY);
	if (phase >= 0) {
		/* no row of this thread's band between: nothing for it to draw (the bands are 16 rows,
		 * so a quad of a few rows often has none) */
		int b0 = ty0 >> q2_bandShift, b1 = ty1 >> q2_bandShift;
		if (b0 == b1 && (b0 & 1) != phase) return true;
	}
	if (!q2_occlusionNow) return false;

	tx0 = max((int)minX, 0) >> Q2_TILE_SHIFT;
	tx1 = min((int)maxX, fb_maxX) >> Q2_TILE_SHIFT;
	ty0 >>= Q2_TILE_SHIFT; ty1 >>= Q2_TILE_SHIFT;
	/* a large quad (near the eye) is seldom all hidden, and its test (each tile's far depth
	 * worked out again since something drew there) costs more than it saves: drawn untested */
	if ((tx1 - tx0 + 1) * (ty1 - ty0 + 1) > Q2_QUAD_TILES) return false;
	near -= 0.00001f;

	for (ty = ty0; ty <= ty1; ty++)
	{
		/* a tile row wholly in the other thread's bands isn't this thread's to judge */
		if (phase >= 0 && q2_bandShift >= Q2_TILE_SHIFT &&
			(((ty << Q2_TILE_SHIFT) >> q2_bandShift) & 1) != phase) continue;
		for (tx = tx0; tx <= tx1; tx++)
		{
			if (Q2_TileFar(tx, ty) >= near) return false;
		}
	}
	return true;
}
#endif

static void DrawQuads3D(int startVertex, int verticesCount, int phase) {
	Vertex vertices[4];
	int i, j = startVertex;

	// 4 vertices = 1 quad = 2 triangles
	for (i = 0; i < verticesCount / 4; i++, j += 4)
	{
		int clip = TransformVertex3D(j + 0, &vertices[0]) << 0
				|  TransformVertex3D(j + 1, &vertices[1]) << 1
				|  TransformVertex3D(j + 2, &vertices[2]) << 2
				|  TransformVertex3D(j + 3, &vertices[3]) << 3;

		if (clip == 0) {
			// Quad entirely clipped
		} else if (clip == 0x0F) {
			// Quad entirely visible
			ViewportVertex3D(&vertices[0]);
			ViewportVertex3D(&vertices[1]);
			ViewportVertex3D(&vertices[2]);
			ViewportVertex3D(&vertices[3]);
#ifdef CC_BUILD_Q2
			if (Q2_QuadHidden(vertices, phase)) continue;
#endif

			DrawTriangle3D(&vertices[0], &vertices[2], &vertices[1], phase);
			DrawTriangle3D(&vertices[2], &vertices[0], &vertices[3], phase);
		} else {
			// Quad partially visible
			DrawClipped(clip, &vertices[0], &vertices[1], &vertices[2], &vertices[3], phase);
		}
	}
}

#ifdef CC_BUILD_Q2
/* The Q2's X2000 has two cores. Filling triangles is most of a frame, so a worker thread draws
 * every batch of 3D quads with the main thread: each fills its own 16-row bands of the screen
 * (DrawTriangle3D's phase), each transforming the corners itself. The main thread waits for the
 * worker before it returns, so the renderer's state, the textures and the buffers never change
 * while the two draw, and the order of draws is kept. q2-cores=1 (Second core off) keeps one.
 *
 * How the two meet, kept to plain hand-offs: the whole Q2 froze after minutes of both cores
 * drawing (not with one, nor for heat or the battery: it froze charging), and Q2 Pod's author
 * pointed at the timings between the cores. So, from 1.10:
 * - the worker never reads the clock (it read it every 1024 looks while it waited);
 * - a wait, on either side, looks at the other's counter a bounded number of times (a batch
 *   comes every few tens of microseconds within a frame), then sleeps in the kernel (a futex)
 *   until the other wakes it: between frames, in menus, the second core sleeps;
 * - the bands keep the two cores' writes apart in memory (alternate rows put them in the same
 *   cache lines all the time).
 * The counters are sequentially consistent atomics: a side that goes to sleep says so first and
 * then looks again, and a side that hands over looks for a sleeper after it, so a wake-up can't
 * be missed (one of the two always sees the other). */
#include <pthread.h>
#include <unistd.h>
#include "Options.h"
#ifdef __mips__
#define Q2_NR_FUTEX 4238 /* o32 */
#else
#include <sys/syscall.h>
#define Q2_NR_FUTEX SYS_futex
#endif
long syscall(long number, ...);
#define Q2_FUTEX_WAIT 128 /* FUTEX_WAIT | FUTEX_PRIVATE_FLAG */
#define Q2_FUTEX_WAKE 129 /* FUTEX_WAKE | FUTEX_PRIVATE_FLAG */
#define Q2_LOOK_US    1000 /* how long a side looks at the other's counter before sleeping */

static struct { int start, count; } q2_job;
static int q2_seq, q2_done;              /* the job's number, the last the worker finished */
static int q2_workerAsleep, q2_mainAsleep;
/* for the log's speed note (Q2_TwoCoreStats): hand-offs, each side's sleeps, the main thread's
 * waits for the worker in microseconds (the main thread reads the clock; the worker doesn't) */
static volatile int q2_handoffs, q2_workerSleeps, q2_mainSleeps;
static volatile cc_uint64 q2_mainWaitUs;
static int q2_threads;      /* 0: not decided; 1: one thread; 2: the worker runs */
static int q2_looks;        /* looks in about Q2_LOOK_US: timed once by the main thread, at the start */
static int q2_workerUp;     /* the worker thread was started (it stays, asleep, with one thread) */

#define Q2_LOAD(v)     __atomic_load_n(&(v), __ATOMIC_SEQ_CST)
#define Q2_STORE(v, x) __atomic_store_n(&(v), (x), __ATOMIC_SEQ_CST)

/* Sleeps while *addr is still value (returns at once if it isn't) */
static void Q2_Sleep(int* addr, int value) { syscall(Q2_NR_FUTEX, addr, Q2_FUTEX_WAIT, value, NULL, NULL, 0); }
static void Q2_Wake(int* addr)             { syscall(Q2_NR_FUTEX, addr, Q2_FUTEX_WAKE, 1, NULL, NULL, 0); }

static void* Q2_Worker(void* arg) {
	int seen = 0, seq = 0, i;
	for (;;) {
		for (i = 0; i < q2_looks; i++) {
			seq = Q2_LOAD(q2_seq);
			if (seq != seen) break;
		}
		if (seq == seen) {
			Q2_STORE(q2_workerAsleep, 1);
			if (Q2_LOAD(q2_seq) == seen) { q2_workerSleeps++; Q2_Sleep(&q2_seq, seen); }
			Q2_STORE(q2_workerAsleep, 0);
			continue;
		}
		seen = seq;
		DrawQuads3D(q2_job.start, q2_job.count, 1);
		Q2_STORE(q2_done, seq);
		if (Q2_LOAD(q2_mainAsleep)) Q2_Wake(&q2_done);
	}
	return NULL;
}

/* One or both cores draw the world (q2-cores; Graphics options' Second core): changed between draws */
/* How many looks at a counter take Q2_LOOK_US, timed here on the main thread (the worker reads no
 * clock): the bound for both sides' looks before they sleep */
static void Q2_TimeLooks(void) {
	int i, n = 100000, us, look_us = Q2_LOOK_US;
	cc_uint64 beg = Stopwatch_Measure();
	for (i = 0; i < n; i++) (void)Q2_LOAD(q2_done);
	us = (int)Stopwatch_ElapsedMicroseconds(beg, Stopwatch_Measure());
	if (us < 1) us = 1;
	q2_looks = (int)((cc_uint64)n * Q2_LOOK_US / us);
	q2_looks = max(10000, min(q2_looks, 5000000));
	Platform_Log2("Two cores: %i looks at the other's counter (about %i us), then sleep", &q2_looks, &look_us);
}

void Q2_SetCores(int n) {
	if (n == 2 && !q2_workerUp) {
		pthread_t t;
		Q2_TimeLooks();
		if (pthread_create(&t, NULL, Q2_Worker, NULL)) n = 1;
		else q2_workerUp = 1;
	}
	q2_threads = n;
	Platform_Log1("Drawing on %i threads", &q2_threads);
}
/* The counts since the last call (for Platform_Q2.c's speed note) */
void Q2_TwoCoreStats(int* handoffs, int* workerSleeps, int* mainSleeps, int* mainWaitMs) {
	*handoffs = q2_handoffs; *workerSleeps = q2_workerSleeps; *mainSleeps = q2_mainSleeps;
	*mainWaitMs = (int)(q2_mainWaitUs / 1000);
	q2_handoffs = q2_workerSleeps = q2_mainSleeps = 0; q2_mainWaitUs = 0;
}
int Q2_GetCores(void) { return q2_threads ? q2_threads : Options_GetInt("q2-cores", 1, 2, 2); }

static void DrawQuads3DAll(int startVertex, int verticesCount) {
	int seq, done, i;
	if (!q2_threads) {
		q2_texSpan = Options_GetInt("q2-texspan", 0, Q2_SPAN_MAX, 0);
		{
			int rows = Options_GetInt("q2-bandrows", 1, 16, 16);
			for (q2_bandShift = 0; (2 << q2_bandShift) <= rows && q2_bandShift < 4; q2_bandShift++) { }
			rows = 1 << q2_bandShift;
			Platform_Log1("Two cores: bands of %i rows each", &rows);
		}
		Platform_Log1("Texture coordinates exact every %i pixels (0: every pixel)", &q2_texSpan);
		q2_quadTiles = Options_GetInt("q2-quadtiles", 0, 4096, 12);
		Q2_SetCores(Options_GetInt("q2-cores", 1, 2, 2));
	}
	q2_occlusionNow = Q2_Occlusion && q2_tilesX && gfx_depthTest;
	if (q2_threads == 1) { DrawQuads3D(startVertex, verticesCount, -1); return; }

	q2_job.start = startVertex;
	q2_job.count = verticesCount;
	seq = q2_seq + 1;
	Q2_STORE(q2_seq, seq);
	if (Q2_LOAD(q2_workerAsleep)) Q2_Wake(&q2_seq);
	DrawQuads3D(startVertex, verticesCount, 0);

	Q2_Phase = "waiting for the second core's rows";
	q2_handoffs++;
	if (Q2_LOAD(q2_done) != seq) {
		cc_uint64 beg = Stopwatch_Measure();
		for (i = 0; ; i++) {
			done = Q2_LOAD(q2_done);
			if (done == seq) break;
			if (i < q2_looks) continue;
			Q2_STORE(q2_mainAsleep, 1);
			if (Q2_LOAD(q2_done) == done) { q2_mainSleeps++; Q2_Sleep(&q2_done, done); }
			Q2_STORE(q2_mainAsleep, 0);
		}
		q2_mainWaitUs += Stopwatch_ElapsedMicroseconds(beg, Stopwatch_Measure());
	}
	Q2_Phase = "drawing";
}
#else
static void DrawQuads3DAll(int startVertex, int verticesCount) {
	DrawQuads3D(startVertex, verticesCount, -1);
}
#endif

void DrawQuads(int startVertex, int verticesCount, DrawHints hints) {
	Vertex vertices[4];
	int i, j = startVertex;

#ifdef CC_BUILD_Q2
	if (gfx_rendering2D) Q2_Begin2D();
#endif
	if (gfx_rendering2D && (hints & (DRAW_HINT_SPRITE|DRAW_HINT_RECT))) {
		// 4 vertices = 1 quad = 2 triangles
		for (i = 0; i < verticesCount / 4; i++, j += 4)
		{
			TransformVertex2D(j + 0, &vertices[0]);
			TransformVertex2D(j + 1, &vertices[1]);
			TransformVertex2D(j + 2, &vertices[2]);

			DrawSprite2D(&vertices[0], &vertices[1], &vertices[2]);
		}
	} else if (gfx_rendering2D) {
		// 4 vertices = 1 quad = 2 triangles
		for (i = 0; i < verticesCount / 4; i++, j += 4)
		{
			TransformVertex2D(j + 0, &vertices[0]);
			TransformVertex2D(j + 1, &vertices[1]);
			TransformVertex2D(j + 2, &vertices[2]);
			TransformVertex2D(j + 3, &vertices[3]);

			DrawTriangle2D(&vertices[0], &vertices[2], &vertices[1]);
			DrawTriangle2D(&vertices[2], &vertices[0], &vertices[3]);
		}
	} else {
		DrawQuads3DAll(startVertex, verticesCount);
	}
}

static void SetVertexFormat(VertexFormat fmt) { }

void Gfx_DrawVb_Lines(int verticesCount) { } /* TODO */

void Gfx_DrawVb_IndexedTris_Range(int verticesCount, int startVertex, DrawHints hints) {
	DrawQuads(startVertex, verticesCount, hints);
}

void Gfx_DrawVb_IndexedTris(int verticesCount) {
	DrawQuads(0, verticesCount, DRAW_HINT_NONE);
}

void Gfx_DrawIndexedTris_T2fC4b(int verticesCount, int startVertex, DrawHints hints) {
	DrawQuads(startVertex, verticesCount, hints);
}


/*########################################################################################################################*
*---------------------------------------------------------Other/Misc------------------------------------------------------*
*#########################################################################################################################*/
static BitmapCol* CB_GetRow(struct Bitmap* bmp, int y, void* ctx) {
	return colorBuffer + cb_stride * y;
}

cc_result Gfx_TakeScreenshot(struct Stream* output) {
	struct Bitmap bmp;
#ifdef CC_BUILD_Q2
	Q2_Begin2D(); /* the view's picture */
#endif
	Bitmap_Init(bmp, fb_width, fb_height, NULL);
	return Png_Encode(&bmp, output, CB_GetRow, false, NULL);
}

cc_bool Gfx_WarnIfNecessary(void) { return false; }
cc_bool Gfx_GetUIOptions(struct MenuOptionsScreen* s) { return false; }

void Gfx_BeginFrame(void) { }

void Gfx_EndFrame(void) {
    Rect2D r;
#ifdef CC_BUILD_Q2
	Q2_Begin2D(); /* the world stretched over the view, if nothing in 2D did that */
#endif
	r.x = 0;
	r.y = 0; 
	r.width  = fb_width;
	r.height = fb_height;
	
	Window_DrawFramebuffer(r, &fb_bmp);
}

void Gfx_SetVSync(cc_bool vsync) {
	gfx_vsync = vsync;
}

void Gfx_OnWindowResize(int width, int height) {
	if (depthBuffer) DestroyBuffers();

#ifdef CC_BUILD_Q2
	Window_AllocFramebuffer(&fb_bmp, width, height);
	q2_full  = fb_bmp.scan0;
	q2_fullW = width; q2_fullH = height;
	depthBuffer = Mem_Alloc(width * height, 4, "depth buffer");
	Q2_UseTarget(q2_full, q2_fullW, q2_fullH);
	Q2_SetRenderScale(Options_GetInt("q2-renderscale", 25, 100, 100));
	Platform_Log1("Render scale: %i percent", &q2_scalePct);
#else
	fb_width   = width;
	fb_height  = height;

	Window_AllocFramebuffer(&fb_bmp, width, height);
	colorBuffer = fb_bmp.scan0;
	cb_stride   = fb_bmp.width;

	depthBuffer = Mem_Alloc(width * height, 4, "depth buffer");
	db_stride   = width;

	Gfx_SetViewport(0, 0, width, height);
	Gfx_SetScissor (0, 0, width, height);
#endif
}

#ifdef CC_BUILD_Q2
/* the whole of whichever picture is being drawn (Q2_UseTarget): the game only ever asks for that */
void Gfx_SetViewport(int x, int y, int w, int h) { }
void Gfx_SetScissor (int x, int y, int w, int h) { }
#else
void Gfx_SetViewport(int x, int y, int w, int h) {
	vp_hwidth  = w / 2.0f;
	vp_hheight = h / 2.0f;
}

void Gfx_SetScissor (int x, int y, int w, int h) {
	/* TODO minX/Y */
	fb_maxX = x + w - 1;
	fb_maxY = y + h - 1;
}
#endif

void Gfx_GetApiInfo(cc_string* info) {
	int pointerSize = sizeof(void*) * 8;
	String_Format1(info, "-- Using software (%i bit) --\n", &pointerSize);
	PrintMaxTextureInfo(info);
}

cc_bool Gfx_TryRestoreContext(void) { return true; }
#endif
