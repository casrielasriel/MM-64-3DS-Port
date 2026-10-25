#ifndef PORT_MINIMAP_H
#define PORT_MINIMAP_H

#define PORT_MINIMAP_MAX_MARKS 32
#define PORT_MINIMAP_MAX_LAYERS 2

typedef struct {
    const void* tex;
    short x, y, w, h;
    unsigned char fmt, flipX, flipY, byteXor;
    unsigned char r, g, b, a;
} PortMinimapLayer;

typedef struct {
    unsigned int serial;
    int sceneId;
    unsigned char numLayers;
    PortMinimapLayer layers[PORT_MINIMAP_MAX_LAYERS];
    unsigned char compass, hasStart;
    float playerX, playerY; /* arrow centers on the N64 screen */
    short playerYaw;
    float startX, startY;
    short startYaw;
    unsigned char numMarks;
    unsigned char markType[PORT_MINIMAP_MAX_MARKS];
    short markX[PORT_MINIMAP_MAX_MARKS], markY[PORT_MINIMAP_MAX_MARKS];
    unsigned char markR[PORT_MINIMAP_MAX_MARKS], markG[PORT_MINIMAP_MAX_MARKS], markB[PORT_MINIMAP_MAX_MARKS];
} PortMinimap;

#define PORT_MINIMAP_I4 1
#define PORT_MINIMAP_IA4 2
#define PORT_MINIMAP_I4_ALPHA 3

extern PortMinimap gPortMinimap;
extern int gPortMinimapOnBottom; /* 1 = the touch panel shows the map; the top screen does not */

extern const unsigned char* gPortHudIconSeg;
extern unsigned int gPortHudSerial;           /* bumped by every Interface_Draw */
extern int gPortHudKeys;                      /* small keys shown by the HUD, -1 = none */
extern int gPortHudTatl;
extern int gPortHudTop;                       /* 1 = the N64 HUD on the top screen (default), 0 = hidden */
extern volatile int gPortTouchOcarina;
extern volatile int gPortTouchPage; /* pause page for the next START (-1 = the game's own), z_kaleido_setup.c */

typedef struct {
    int valid; /* a save is loaded and the game is in normal play mode */
    int rupees, keys;
    int health, healthCapacity; /* 16 per heart */
    int magic, magicCapacity;
    int mask, ocarina;
    int day, minutes;
    unsigned char cItem[3], cDisabled[3];
    short cAmmo[3]; /* -1 = no counter */
} PortHudInfo;
void Port_GetHudInfo(PortHudInfo* h);
struct PlayState;
void PortHud_Update(struct PlayState* play);
void PortMap_Begin(int sceneId);
void PortMap_AddMark(int type, int x, int y, int r, int g, int b);

#endif
