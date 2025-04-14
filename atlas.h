#ifndef ATLAS_H
#define ATLAS_H
#define MAX_TEXTURES 50
typedef struct {
    char name[50];
    int x;
    int y;
    int width;
    int height;
    int original_width;
    int original_height;
    float u_start;
    float u_end;
    float v_start;
    float v_end;
} TextureData;

extern TextureData atlasData[MAX_TEXTURES];
extern int textureCount;
extern float atlasWidth;
extern float atlasHeight;

void loadAtlasData();
TextureData* getTextureData(const char* name);  

#endif