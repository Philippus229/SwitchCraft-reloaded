#include <stdlib.h>
#include <stdio.h>
#include <switch.h>
#include <math.h>
#include <switch/kernel/random.h>

#define STBI_ONLY_PNG
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define FB_WIDTH  320
#define FB_HEIGHT 180
#define SCREEN_W 1280
#define SCREEN_H  720

#define MAX_RENDER_DIST 32

#define WORLD_SIZE 64
#define WORLD_HEIGHT 64

const float math_pi = 3.14159265359f;
float camData[6] = { (float)WORLD_SIZE / 2.f,(float)WORLD_HEIGHT / 2.f,(float)WORLD_SIZE / 2.f,-math_pi/2.f,0.f }; //xPos,yPos,zPos,xRot,yRot

u32 stride;
u32* fbHighRes;
u32 fbLowRes[FB_WIDTH*FB_HEIGHT];

void blitFramebuffer(void) {
    for (int x=0; x<SCREEN_W; x++) {
        for (int y=0; y<SCREEN_H; y++) {
            fbHighRes[y*SCREEN_W+x] = fbLowRes[(int)(y*FB_HEIGHT/SCREEN_H) * FB_WIDTH + (int)(x*FB_WIDTH/SCREEN_W)];
        }
    }
}
int ticks = 0;

static inline void setPixel(int x, int y, u32 color) {
    fbLowRes[y*FB_WIDTH+x] = color;
}

/*u32* loadImage(string filepath) {
    int w, h, n;
    uint8_t* rgba = stbi_load(filepath, &w, &h, &n, 4);
    stbi_image_free(rgba);
    u32 out_img[w*h];
    for (int i=0; i<w*h; i++) {
        out_img[i] = (rgba[4*i+3]<<24) | (rgba[4*i+2]<<16) | (rgba[4*i+1]<<8) | rgba[4*i];
    }
    return (u32*)out_img
}
}*/

static inline int randInt(u64 a, u64 b) { return randomGet64()%(1+b-a) + a; } //random integer a <= x <= b
//static inline float randFloat(void) { return (float)randInt(0,0xFFFFFF) / (float)0xFFFFFF; } //random float 0 <= x <= 1

//#define TEXTURE_MODE_RGBA
#ifdef TEXTURE_MODE_RGBA
u32 *texmap; //16 blocks (3->top,side,bottom) -> default: 0=air,1=grass,4=stone,5=brick,7=log,8=leaves,2/3/6/9-15=dirt
#else
int *texmap;
#endif
int texsize = 16;

#include "textures_png.h"
void loadTextures(char from_file) {
    if (from_file) {
        int w, h, n;
        uint8_t* img = stbi_load_from_memory((const stbi_uc*)textures_png, textures_png_size, &w, &h, &n, 3);
        stbi_image_free(img);
        int s = h/3;
        texsize = s;
        texmap = malloc(sizeof(*texmap)*3*s*s*(int)(w/s));
        for (int x=0; x<w; x++) {
            for (int y=0; y<h; y++) {
                int i = y*w+x;
                texmap[(x%s)+((y%s)+s*(int)(y/s))*s+3*s*s*(int)(x/s)] = (img[3*i]<<16) | (img[3*i+1]<<8) | img[3*i+2];
            }
        }
    } else {
        texmap = malloc(sizeof(*texmap)*3*16*16*16);
        for (int j=0; j<16; j++) {
            int k = 255-randInt(0,96);
            for (int m=0; m<16 * 3; m++) {
                for (int n = 0; n<16; n++) {
                    int i1 = 0x966C4A;
                    int i2 = 0;
                    int i3 = 0;
                    if (j == 4)
                        i1 = 0x7F7F7F;
                    if ((j != 4) || (randInt(0,3) == 0))
                        k = 255-randInt(0,96);
                    if (j == 1) {
                        if (m < (((n*n*3+n*81) >> 2) & 0x3)+18)
                            i1 = 0x6AAA40;
                        else if (m < (((n*n*3+n*81) >> 2) & 0x3)+19)
                            k = k * 2 / 3;
                    }
                    if (j == 7) {
                        i1 = 0x675231;
                        if ((n > 0) && (n < 15) && (((m > 0) && (m < 15)) || ((m > 32) && (m < 47)))) {
                            i1 = 0xBC9862;
                            i2 = n-7;
                            i3 = (m & 0xF)-7;
                            if (i2 < 0)
                                i2 = 1-i2;
                            if (i3 < 0)
                                i3 = 1-i3;
                            if (i3 > i2)
                                i2 = i3;
                            k = 196-randInt(0,32)+i2%3*32;
                        } else if (randInt(0,2) == 0) {
                            k = k*(150-(n & 0x1)*100)/100;
                        }
                    }
                    if (j == 5) {
                        i1 = 0xB53A15;
                        if (((n+m/4*4)%8 == 0) || (m%4 == 0)) i1 = 0xBCAFA5;
                    }
                    i2 = k;
                    if (m >= 32)
                        i2 /= 2;
                    if (j == 8) {
                        i1 = 5298487;
                        if (randInt(0,2) == 0) {
                            i1 = 0;
                            i2 = 255;
                        }
                    }
                    i3 = ((((i1 >> 16) & 0xFF)*i2/255) << 16) |
                         ((((i1 >>  8) & 0xFF)*i2/255) <<  8) |
                           ((i1        & 0xFF)*i2/255);
                    texmap[n+m*16+j*256*3] = i3;
                }
            }
        }
    }
}

char blocks[WORLD_SIZE*WORLD_SIZE*WORLD_HEIGHT];

void generateWorld(void) {
	for (int x = 0; x < WORLD_SIZE; x++) {
		for (int y = 0; y < WORLD_HEIGHT; y++) {
			for (int z = 0; z < WORLD_SIZE; z++) {
				int i = (z << 12) | (y << 6) | x;
				float yd = (y-32.5)*0.4;
				float zd = (z-32.5)*0.4;
				blocks[i] = randInt(0,16);
                float th = randInt(0,256)/256.0f;
				if (th > sqrtf(sqrtf(yd*yd+zd*zd))-0.8f) blocks[i] = 0; //remove blocks in player's path
			}
		}
	}
}

/*void generateWorld(u64 seed) {
    //TODO
}*/

void init() {
    loadTextures(1);
    //loadSkybox();
    generateWorld();
}

static inline int fxmul(int a, int b) {
    return (a*b)>>8;
}

static inline u32 rgbmul(int a, int b) {
    int _r = (((a>>16) & 0xff)*b)>>8;
    int _g = (((a>> 8) & 0xff)*b)>>8;
    int _b = (((a    ) & 0xff)*b)>>8;
    return 0xFF000000 | (_b<<16) | (_g<<8) | _r;
}

void renderView(void) {
    float now = (float)ticks / 1000.f;
    float xRot = sinf(now*math_pi*2)*0.4+math_pi/2;
	float yRot = cosf(now*math_pi*2)*0.4;
	float yCos = cosf(yRot);
	float ySin = sinf(yRot);
	float xCos = cosf(xRot);
	float xSin = sinf(xRot);
	float ox = 32.5+now*64.0;
	float oy = 32.5;
	float oz = 32.5;
	/*float yCos = cosf(camData[4]);
	float ySin = sinf(camData[4]);
	float xCos = cosf(camData[3]);
	float xSin = sinf(camData[3]);
	float ox = 32.5;
	float oy = 32.5;
	float oz = 32.5;*/
	for (int x=0; x<FB_WIDTH; x++) {
		float ___xd = ((float)x - (float)FB_WIDTH / 2.f) / (float)FB_HEIGHT;
		for (int y=0; y<FB_HEIGHT; y++) {
			float  __yd = ((float)y - (float)FB_HEIGHT / 2.f) / (float)FB_HEIGHT;
			float  __zd = 1;
			float ___zd =  __zd*yCos+ __yd*ySin;
			float _yd =  __yd*yCos- __zd*ySin;
			float _xd = ___xd*xCos+___zd*xSin;
			float _zd = ___zd*xCos-___xd*xSin;
			int col = 0;
			int br = 255;
			float ddist = 0;
			float closest = MAX_RENDER_DIST;
			for (int d=0; d<3; d++) {
                float dimLength, initial;
                if      (d == 0) { dimLength = _xd; initial = ox-floor(ox); }
                else if (d == 1) { dimLength = _yd; initial = oy-floor(oy); }
                else             { dimLength = _zd; initial = oz-floor(oz); }
				float ll = 1.0f/(dimLength < 0.f ? -dimLength : dimLength);
				float xd = (_xd)*ll;
				float yd = (_yd)*ll;
				float zd = (_zd)*ll;
				if (dimLength > 0) initial = 1-initial;
				float dist = ll*initial;
				float xp = ox+xd*initial;
				float yp = oy+yd*initial;
				float zp = oz+zd*initial;
				if (dimLength < 0) {
					if (d == 0)	xp--;
					if (d == 1)	yp--;
					if (d == 2)	zp--;
				}
				while (dist < closest) {
					int blockID = blocks[(((int)zp & 63) << 12) | (((int)yp & 63) << 6) | ((int)xp & 63)];
					if (blockID > 0) {
						int u = ((int)((xp+zp)*(float)texsize)) & (texsize-1);
						int v = ((int)(yp*(float)texsize) & (texsize-1)) + texsize;
						if (d == 1) {
							u = ((int)(xp*(float)texsize)) & (texsize-1);
							v = ((int)(zp*(float)texsize)) & (texsize-1);
							if (yd < 0) v += 2*texsize;
						}
						int cc = texmap[u+v*texsize+blockID*texsize*texsize*3];
						if (cc > 0) { //TODO: translucency
							col = cc;
                            ddist = 255-((dist/32*255));
							br = 255*(255-((d+2)%3)*50)/255;
							closest = dist;
						}
					}
					xp += xd;
					yp += yd;
					zp += zd;
					dist += ll;
				}
			}
            setPixel(x, y, rgbmul(col, fxmul(br, ddist)));
        }
    }
}

void renderGUI() {
    char selection[6] = {1,2,4,5,7,8}; //default: grass,dirt,stone,brick,log,leaves
    for (int b=0; b<6; b++) {
        for (int x=0; x<texsize; x++) {
            for (int y=0; y<texsize; y++) {
                int c = texmap[x+(y+texsize)*texsize+selection[b]*texsize*texsize*3]; //side textures
                setPixel(b*texsize+x, y, 0xFF000000 | ((c & 0xFF)<<16) | ((c>>8 & 0xFF)<<8) | (c>>16 & 0xFF));
            }
        }
    }
}

void handleInputs(u64 kDown, u64 kHeld, u64 kUp) {
    //TODO
}

int main(int argc, char* argv[]) {
    NWindow* win = nwindowGetDefault();
    Framebuffer fb;
    framebufferCreate(&fb, win, SCREEN_W, SCREEN_H, PIXEL_FORMAT_RGBA_8888, 1);
    framebufferMakeLinear(&fb);
    init();
    while (appletMainLoop()) {
        hidScanInput();
        u64 kDown = hidKeysDown(CONTROLLER_P1_AUTO);
        u64 kHeld = hidKeysHeld(CONTROLLER_P1_AUTO);
        u64 kUp   = hidKeysUp  (CONTROLLER_P1_AUTO);
        if (kDown & KEY_PLUS) break;
        fbHighRes = (u32*)framebufferBegin(&fb, &stride);
        //randomGet(fbLowRes, FB_WIDTH*FB_HEIGHT*sizeof(u32));
        //for (int i=0; i<FB_WIDTH*FB_HEIGHT; i++) fbLowRes[i] = randInt(0,0xFFFFFFFF);
        handleInputs(kDown, kHeld, kUp);
        renderView();
        renderGUI();
        blitFramebuffer();
        framebufferEnd(&fb);
        ticks = (ticks+1)%1000;
    }
    framebufferClose(&fb);
    return 0;
}
