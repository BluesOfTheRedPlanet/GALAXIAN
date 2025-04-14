#define _CRT_SECURE_NO_WARNINGS

#include "atlas.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAX_TEXTURES 50

TextureData atlasData[MAX_TEXTURES];
int textureCount = 0;
float atlasWidth = 644.0f;
float atlasHeight = 332.0f;

void trimWhitespace(char* str) {
    char* end;
    while (isspace((unsigned char)*str)) str++;
    if (*str == 0) return;
    end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    *(end + 1) = 0;
}

void loadAtlasData() {
    FILE* file = fopen("sprites.txt", "r");
    if (!file) {
        printf("Error: Failed to open sprites.txt\n");
        return;
    }

    char line[256];
    while (fgets(line, sizeof(line), file)) {
        // Удаляем символ новой строки
        line[strcspn(line, "\n")] = 0;

        char name[50];
        int x, y, w, h;

        if (sscanf(line, "%49[^,],%d,%d,%d,%d",
            name, &x, &y, &w, &h) == 5)
        {
            if (textureCount < MAX_TEXTURES) {
                TextureData* data = &atlasData[textureCount++];
                strncpy(data->name, name, sizeof(data->name));

                data->u_start = (float)x / atlasWidth;
                data->u_end = (float)(x + w) / atlasWidth;
                data->v_start = (float)y / atlasHeight;
                data->v_end = (float)(y + h) / atlasHeight;

                data->original_width = w;
                data->original_height = h;

                if (data->u_end > 1.0f || data->v_end > 1.0f) {
                    printf("ERROR: Invalid UVs for %s\n", data->name);
                }

                printf("Loaded: %s | X: %d Y: %d | W: %d H: %d\n",
                    data->name, x, y, w, h);
            }
        }
        else {
            printf("Invalid line: %s\n", line);
        }
    }
    fclose(file);
}

TextureData* getTextureData(const char* name) {
    for (int i = 0; i < textureCount; i++) {
        if (strcmp(atlasData[i].name, name) == 0) {
            return &atlasData[i];
        }
    }
    printf("Texture '%s' not found in atlas!\n", name);
    return NULL;
}
