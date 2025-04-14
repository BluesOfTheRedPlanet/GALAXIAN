#define _CRT_SECURE_NO_WARNINGS
#include "atlas.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAX_TEXTURES 50

TextureData atlasData[MAX_TEXTURES];
int textureCount = 0;
float atlasWidth = 0.0f;
float atlasHeight = 0.0f;

void trimWhitespace(char* str) {
    char* end;
    while (isspace((unsigned char)*str)) str++;
    if (*str == 0) return;
    end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    *(end + 1) = 0;
}

void loadAtlasData() {
    printf("\nStart of atlas data download...\n");

    FILE* file = fopen("C:/Users/cyberdemon/Desktop/яѕ/курсова€/GALAXIAN_GAME-master/ASSETS/game_atlas.json", "rb");
    if (!file) {
        printf("Critical error: The JSON file was not found!\n");
        return;
    }
    printf("\nThe JSON file has been opened successfully");

    char buffer[1024];
    char currentName[128] = { 0 };
    int inFramesSection = 0;
    int x = 0, y = 0, w = 0, h = 0;

    while (fgets(buffer, sizeof(buffer), file)) {
        trimWhitespace(buffer);

        // parsim atlas dimensions
        if (strstr(buffer, "\"size\": {")) {
            if (sscanf(buffer, "\"size\": {\"w\":%f, \"h\":%f}", &atlasWidth, &atlasHeight) == 2) {
                printf("Atlas Dimensions: %.0fx%.0f\n", atlasWidth, atlasHeight);
            }
            continue;
        }

        // the beginning of the texture section
        if (strstr(buffer, "\"frames\": {")) {
            inFramesSection = 1;
            printf("\nStart of section textures...\n");
            continue;
        }

        if (inFramesSection) {
            // end of section
            if (strstr(buffer, "}")) {
                inFramesSection = 0;
                printf("End of section textures\n");
                continue;
            }

            // parsing of structure's name
            if (strchr(buffer, '"') && strstr(buffer, ":")) {
                char* start = strchr(buffer, '"') + 1;
                char* end = strchr(start, '"');
                if (end) {
                    size_t len = end - start;
                    strncpy(currentName, start, len);
                    currentName[len] = '\0';
                    printf("\nTexture processing: '%s'\n", currentName);
                }
                continue;
            }

            // parsing frame parameters
            if (strstr(buffer, "\"frame\": {")) {
                while (fgets(buffer, sizeof(buffer), file)) {
                    trimWhitespace(buffer);
                    if (strstr(buffer, "}")) break;

                    if (strstr(buffer, "\"x\":")) sscanf(buffer, "\"x\": %d", &x);
                    if (strstr(buffer, "\"y\":")) sscanf(buffer, "\"y\": %d", &y);
                    if (strstr(buffer, "\"w\":")) sscanf(buffer, "\"w\": %d", &w);
                    if (strstr(buffer, "\"h\":")) sscanf(buffer, "\"h\": %d", &h);
                }

                if (textureCount < MAX_TEXTURES) {
                    TextureData* data = &atlasData[textureCount++];

                    // copy name
                    strncpy(data->name, currentName, sizeof(data->name));
                    data->name[sizeof(data->name) - 1] = '\0';

                    // calculating UV coordinates
                    data->u_start = (float)x / atlasWidth;
                    data->u_end = (float)(x + w) / atlasWidth;
                    data->v_start = 1.0f - (float)(y + h) / atlasHeight; // invert Y
                    data->v_end = 1.0f - (float)y / atlasHeight;

                    // save the original dimensions
                    data->original_width = w;
                    data->original_height = h;

                    printf("   Uploaded successfully:\n");
                    printf("   name: %s\n", data->name);
                    printf("   Position: (%d, %d)\n", x, y);
                    printf("   Size: %dx%d\n", w, h);
                    printf("   UV: [%.3f, %.3f] -> [%.3f, %.3f]\n\n",
                        data->u_start, data->v_start,
                        data->u_end, data->v_end);
                }
            }
        }
    }

    fclose(file);
    printf("\nTextures loaded: %d\n", textureCount);
    if (textureCount == 0) {
        printf("Attention! Not a single texture is loaded!\n");
        printf("Check the structure of the JSON file\n");
    }
}

TextureData* getTextureData(const char* name) {
    for (int i = 0; i < textureCount; i++) {
        if (strcmp(atlasData[i].name, name) == 0) {
            printf("Texture found: %s\n", name);
            return &atlasData[i];
        }
    }
    printf("The '%s' texture was not found in the atlas!\n", name);
    return NULL;
}