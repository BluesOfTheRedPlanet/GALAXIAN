#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <glew.h>
#include <glfw3.h>
#include <SOIL2.h>
#include <time.h>
#include <locale.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <SDL.h>
#include <SDL_mixer.h>

#include "atlas.h" // ОПТИМИЗАЦИЯ П.3 ТЕКСТУРНЫЙ АТЛАС

#define MAX_BULLETS 100
#define MAX_ESSENCES 100
#define MAX_ENEMIES 100

// ДЛЯ СЕТКИ
#define GRID_SIZE 50
#define CELL_SIZE 200 // 1000px / 5 клеток

#define EXPLOSION_DURATION 2.5f

#ifdef DEBUG
#define CHECK_GL_ERROR() \
{ \
    GLenum err; \
    while ((err = glGetError()) != GL_NO_ERROR) { \
        printf("OpenGL error %s (0x%04X) at %s:%d (last OpenGL call: %s)\n", \
               getGLErrorString(err), err, __FILE__, __LINE__, lastGLCall); \
    } \
}

#define GL_CALL(cmd) \
    snprintf(lastGLCall, sizeof(lastGLCall), "%s", #cmd); \
    cmd; \
    CHECK_GL_ERROR();

#else
#define CHECK_GL_ERROR()
#define GL_CALL(cmd) cmd
#endif

// П.3 для масштабирования врагов из текстурного атласа
#define PLAYER_SCALE 1.0f
#define ENEMY1_SCALE 0.3f 
#define ENEMY2_SCALE 2.0f 
#define ENEMY3_SCALE 2.0f 
#define EXPLOSION_SCALE 0.7f

// FOR OPTIMIZATION
// 
// ПЕРЕМЕННЫЕ ДЛЯ ПОДСЧЁТА FPS
float fps = 0.0f;
int frameCount = 0;
float fpsTimer = 0.0f;
float lastTime = 0.0f;
FILE* fpsLogFile = NULL;

// Объединенный шейдер для всех объектов ОПТИМИЗАЦИЯ П.3
const char* vertexShaderSource =
"#version 460 core\n"
"layout (location = 0) in vec3 aPos;\n"
"layout (location = 1) in vec2 aTexCoord;\n"
"out vec2 TexCoord;\n"
"uniform vec2 uvOffset;\n"
"uniform vec2 uvScale;\n"

"void main() {\n"
"   gl_Position = vec4(aPos, 1.0);\n"
"   TexCoord = aTexCoord * uvScale + uvOffset;\n"
"}\0";

// ОПТИМИЗАЦИЯ П.3 Фрагментный шейдер для объектов из атласа
const char* fragmentAtlasSource =
"#version 460 core\n"
"in vec2 TexCoord;\n"
"out vec4 FragColor;\n"
"uniform sampler2D texture1;\n" // атлас
"void main() {\n"
"   vec4 texColor = texture(texture1, TexCoord);\n"
"   FragColor = texColor;\n"
"   if (FragColor.a < 0.1) discard;\n" // Отбрасываем прозрачные пиксели
"}\0";

const char* fragmentShaderSourceText = "#version 460 core\n"
"out vec4 FragColor;\n"
"in vec2 TexCoord;\n"
"uniform sampler2D text;\n"
"uniform vec3 textColor;\n"
"void main()\n"
"{\n"
"   vec4 sampled = vec4(1.0, 1.0, 1.0, texture(text, TexCoord).r);\n"
"   FragColor = vec4(textColor, 1.0) * sampled;\n"
"}\n\0";

const char* fragmentShaderSourceBullet = "#version 460 core\n"
"out vec4 FragColor;\n"
"void main()\n"
"{\n"
"   FragColor = vec4(1.0, 1.0, 0.0, 1.0);\n"
"}\n\0";

typedef struct {
    float x, y;
    float width, height;
    float spawnTime;
    int exploding;
    float explosionTime;
    int health;
} GameObject;

typedef struct {
    GameObject base;
    int collided;
    float shootTimer;
} Enemy;

typedef struct {
    float x, y;
    float width, height;
    int active;
    int direction; // направление полета пули: 1 - вверх, -1 - вниз
    int damageDealt; // нанесла ли пуля урон
} Bullet;

typedef struct {
    GameObject base;
    int active;
    int collected;
} Essence;

// 2Й ПУНКТ ОПТИМИЗАЦИИ - ДИНАМИЧЕСКИЕ СПИСКИ
typedef struct {
    Enemy* items;
    int count;
    int capacity;
} EnemyList;

typedef struct {
    Bullet* items;
    int count;
    int capacity;
} BulletList;

typedef enum {
    MAIN_MENU,
    LEVELS_MENU
} MenuState;

typedef enum {
    EASY,
    NORMAL,
    HARD
} Difficulty;

Difficulty currentDifficulty = NORMAL;
MenuState currentMenuState = MAIN_MENU;
GLFWwindow* window;
GameObject player;
Enemy enemies[MAX_ENEMIES];
Bullet bullets[MAX_BULLETS];
Essence essences[MAX_ESSENCES];
GLuint menuBG, menuTexture, backgroundTexture, gameOverTexture;
GLuint charTextures[128];
GLuint starTextures[5];
// ОПТИМИЗАЦИЯ П.3 ТЕКСТУРНЫЙ АТЛАС
GLuint textureAtlas;
GLint uvOffsetLoc, uvScaleLoc;
GLint textUvOffsetLoc, textUvScaleLoc;
GLuint shaderAtlas, shaderBullet, shaderText;

// 2Й ПУНКТ ОПТИМИЗАЦИИ - ДИНАМИЧЕСКИЕ СПИСКИ
EnemyList activeEnemies;
BulletList activeBullets;


void initDynamicArrays() {
    // инициализация списка врагов
    activeEnemies.capacity = MAX_ENEMIES;
    activeEnemies.items = (Enemy*)malloc(sizeof(Enemy) * MAX_ENEMIES);
    activeEnemies.count = 0;

    // инициализация списка пуль
    activeBullets.capacity = MAX_BULLETS;
    activeBullets.items = (Bullet*)malloc(sizeof(Bullet) * MAX_BULLETS);
    activeBullets.count = 0;
}

void freeDynamicArrays() {
    free(activeEnemies.items);
    free(activeBullets.items);
}

// 1Й ПУНКТ ОПТИМИЗАЦИИ - ДОБАВЛЕНИЕ СЕТКИ

typedef struct {
    int count;
    Enemy* enemies[20]; // максимум 20 врагов на ячейку
} GridCell;

typedef struct {
    GridCell cells[GRID_SIZE][GRID_SIZE];
} SpatialGrid;

SpatialGrid collisionGrid;

// инициализация сетки
void initGrid() {
    for (int i = 0; i < GRID_SIZE; i++) {
        for (int j = 0; j < GRID_SIZE; j++) {
            collisionGrid.cells[i][j].count = 0;
        }
    }
}

// обновление сетки
void updateGrid() {
    initGrid();

    for (int i = 0; i < activeEnemies.count; i++) { // ОПТИМИЗАЦИЯ П.2 activeEnemies вместо MAX_ENEMIES
        Enemy* enemy = &activeEnemies.items[i];
        if (!enemy->collided && !enemy->base.exploding) {
            int cellX = (int)(enemy->base.x / CELL_SIZE);
            int cellY = (int)(enemy->base.y / CELL_SIZE);
            if (cellX >= 0 && cellX < GRID_SIZE && cellY >= 0 && cellY < GRID_SIZE) {
                GridCell* cell = &collisionGrid.cells[cellX][cellY];
                if (cell->count < 20) {
                    cell->enemies[cell->count++] = enemy; // сохр указатель на врага
                }
            }
        }
    }
}
//

unsigned int shaderProgramBullet, shaderText, VAO, VBO;
int score = 0;
int playerHealth = 10;
int numEnemies = 10;

int inMenu = 1;
int maxScore = 0;

int scoreReached100 = 0;
int scoreReached300 = 0;

int menuMusicPlaying = 0;
int lowHealthMusicPlaying = 0;

int gameOver = 0;
float gameOverStartTime = 0.0f;

int currentStarTextureIndex = 0;
float starTextureChangeTimer = 0.0f;
const float starTextureChangeInterval = 0.3f; // интервал смены изображений звездочек

FT_Face face;
FT_Library ft;

Mix_Music* backgroundMusic;
Mix_Music* lowHealthMusic;
Mix_Music* menuMusic;
Mix_Music* gameOverMusic;
Mix_Chunk* shootSound;
Mix_Chunk* collisionSound;
Mix_Chunk* hitSound;
Mix_Chunk* essenceSound;



void updateVertexBuffer(float x, float y, float width, float height);
void initGameObjects();

void APIENTRY glDebugOutput(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const void* userParam) {
    printf("Debug message (%d): %s\n", id, message);
}

float getTime() {
    return (float)glfwGetTime();
}

void setupViewport(int width, int height) {
    glViewport(0, 0, width, height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluOrtho2D(0.0, width, height, 0.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

void mouse_button_callback(GLFWwindow* window, int button, int action, int mods) {
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
        double xpos, ypos;
        glfwGetCursorPos(window, &xpos, &ypos);
        // Преобразование координат курсора в координаты окна
        int windowWidth, windowHeight;
        glfwGetWindowSize(window, &windowWidth, &windowHeight);
        ypos = windowHeight - ypos; // Инвертируем координату Y

        if (currentMenuState == MAIN_MENU) {
            if (xpos >= 400 && xpos <= 600 && ypos >= 500 && ypos <= 550) {
                inMenu = 0;
                Mix_HaltMusic();
                Mix_PlayMusic(backgroundMusic, -1);
                menuMusicPlaying = 0;
            }
            //Levels
            if (xpos >= 400 && xpos <= 600 && ypos >= 400 && ypos <= 450) {
                currentMenuState = LEVELS_MENU;
            }
            //Quit
            if (xpos >= 400 && xpos <= 600 && ypos >= 300 && ypos <= 350) {
                glfwSetWindowShouldClose(window, GLFW_TRUE);
            }
        }
        else if (currentMenuState == LEVELS_MENU) {
            if (xpos >= 400 && xpos <= 600 && ypos >= 500 && ypos <= 550) {
                currentDifficulty = EASY;
                currentMenuState = MAIN_MENU;
                initGameObjects();
                printf("Выбран уровень Easy\n");
            }
            if (xpos >= 400 && xpos <= 600 && ypos >= 400 && ypos <= 450) {
                currentDifficulty = NORMAL;
                currentMenuState = MAIN_MENU;
                initGameObjects();
                printf("Выбран уровень Normal\n");
            }
            if (xpos >= 400 && xpos <= 600 && ypos >= 300 && ypos <= 350) {
                currentDifficulty = HARD;
                currentMenuState = MAIN_MENU;
                initGameObjects();
                printf("Выбран уровень Hard\n");
            }
        }
    }
}

void loadMaxScore() {
    FILE* file = fopen("score_records.txt", "r");
    if (file) {
        fscanf(file, "%d", &maxScore);
        fclose(file);
    }
}

void saveMaxScore() {
    FILE* file = fopen("score_records.txt", "w");
    if (file) {
        fprintf(file, "%d", maxScore);
        fclose(file);
    }
}

int initAudio() {
    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048) < 0) {
        printf("SDL_mixer could not initialize! SDL_mixer Error: %s\n", Mix_GetError());
        return -1;
    }
    return 0;
}

int loadAudioFiles(Mix_Music** backgroundMusic, Mix_Music** lowHealthMusic, Mix_Music** menuMusic, Mix_Music** gameOverMusic, Mix_Chunk** shootSound, Mix_Chunk** collisionSound, Mix_Chunk** hitSound, Mix_Chunk** essenceSound) {
    *backgroundMusic = Mix_LoadMUS("SOUNDS/bg_OK.mp3");
    *lowHealthMusic = Mix_LoadMUS("SOUNDS/low_health_music.mp3");
    *menuMusic = Mix_LoadMUS("SOUNDS/menu_music.mp3");
    *gameOverMusic = Mix_LoadMUS("SOUNDS/game_over_music.mp3");
    *shootSound = Mix_LoadWAV("SOUNDS/shoot.wav");
    *collisionSound = Mix_LoadWAV("SOUNDS/collision.wav");
    *hitSound = Mix_LoadWAV("SOUNDS/hit.wav");
    *essenceSound = Mix_LoadWAV("SOUNDS/essence.wav");


    if (!*backgroundMusic || !*lowHealthMusic || !*menuMusic || !*shootSound || !*collisionSound || !*hitSound) {
        printf("Failed to load sound effects! SDL_mixer Error: %s\n", Mix_GetError());
        return -1;
    }
    return 0;
}

void initTextRendering() {
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if (FT_Init_FreeType(&ft)) {
        printf("Could not init FreeType Library\n");
        exit(EXIT_FAILURE);
    }

    if (FT_New_Face(ft, "fonts/SuperMario.ttf", 0, &face)) {
        printf("Failed to load font\n");
        exit(EXIT_FAILURE);
    }

    FT_Set_Pixel_Sizes(face, 0, 48);

    glGenTextures(128, charTextures);

    for (unsigned char c = 0; c < 128; c++) {
        if (FT_Load_Char(face, c, FT_LOAD_RENDER)) {
            printf("Failed to load Glyph for character %c\n", c);
            continue;
        }
        glBindTexture(GL_TEXTURE_2D, charTextures[c]);
        glTexImage2D(
            GL_TEXTURE_2D,
            0,
            GL_RED,
            face->glyph->bitmap.width,
            face->glyph->bitmap.rows,
            0,
            GL_RED,
            GL_UNSIGNED_BYTE,
            face->glyph->bitmap.buffer
        );
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }

    printf("Text rendering initialized successfully\n");
}

// ОПТИМИЗАЦИЯ П.4 СРАВНЕНИЕ ДЛЯ СОРТИРОВКИ
int compareEnemies(const void* a, const void* b) {
    const Enemy* enemyA = (const Enemy*)a;
    const Enemy* enemyB = (const Enemy*)b;

    // определяем приоритет текстур: взрывы -> enemy_3 -> enemy_2 -> enemy_1
    int priorityA = enemyA->base.exploding ? 3 :
        (scoreReached300 ? 2 :
            (scoreReached100 ? 1 : 0));

    int priorityB = enemyB->base.exploding ? 3 :
        (scoreReached300 ? 2 :
            (scoreReached100 ? 1 : 0));

    return priorityA - priorityB;
}

// ОПТИМИЗАЦИЯ П.4 определение типа текстуры
TextureData* getEnemyTextureData(const Enemy* enemy) {
    if (enemy->base.exploding) {
        return getTextureData("explosion");
    }
    if (scoreReached300) return getTextureData("enemy_3");
    if (scoreReached100) return getTextureData("enemy_2");
    return getTextureData("enemy_1");
}

void renderText(const char* text, float x, float y, float scale, GLuint* charTextures) {
    // сброс UV-параметров для текста
    glUseProgram(shaderText);
    glUniform2f(textUvOffsetLoc, 0.0f, 0.0f);
    glUniform2f(textUvScaleLoc, 1.0f, 1.0f);

    glUseProgram(shaderText);
    glUniform3f(glGetUniformLocation(shaderText, "textColor"), 0.786f, 0.933f, 0.098f);

    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(VAO);

    while (*text) {
        char c = *text++;
        glBindTexture(GL_TEXTURE_2D, charTextures[c]);

        float xpos = x + face->glyph->bitmap_left * scale;
        float ypos = y - (face->glyph->bitmap.rows - face->glyph->bitmap_top) * scale;

        float w = face->glyph->bitmap.width * scale;
        float h = face->glyph->bitmap.rows * scale;

        updateVertexBuffer(xpos, ypos, w, h);
        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

        x += (face->glyph->advance.x >> 6) * scale; // перемещаем позицию для следующего символа
    }

    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);

    glfwSwapBuffers(window);
}

GLuint loadTexture(const char* path) {
    GLuint textureID;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);

    int width, height;
    unsigned char* image = SOIL_load_image(path, &width, &height, 0, SOIL_LOAD_RGBA);
    if (image) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image);
        glGenerateMipmap(GL_TEXTURE_2D);

        // Установка параметров текстуры
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }
    else {
        printf("Ошибка загрузки текстуры: %s\n", path);
    }
    SOIL_free_image_data(image);
    glBindTexture(GL_TEXTURE_2D, 0);

    return textureID;
}

void initMenuTextures() {
    menuBG = SOIL_load_OGL_texture(
        "ASSETS/menu/menuBG.png",
        SOIL_LOAD_AUTO,
        SOIL_CREATE_NEW_ID,
        SOIL_FLAG_MIPMAPS | SOIL_FLAG_INVERT_Y | SOIL_FLAG_NTSC_SAFE_RGB | SOIL_FLAG_COMPRESS_TO_DXT
    );

    glBindTexture(GL_TEXTURE_2D, menuBG);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // форсированная генерация мипмапов
    glGenerateMipmap(GL_TEXTURE_2D);

    // Анизотропная фильтрация
    if (GLEW_EXT_texture_filter_anisotropic) {
        GLfloat max_aniso = 0.0f;
        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &max_aniso);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, max_aniso);
    }

    // Проверка ошибок
    GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        printf("Menu BG texture error: %s\n", gluErrorString(error));
    }

    menuTexture = loadTexture("ASSETS/menu/menu.png");
}
// ДОБАВЛЕНО - ОПТИМИЗАЦИЯ П.3 ТЕКСТУРНЫЙ АТЛАС
void drawMenuButton(const char* textureName, float x, float y) {
    TextureData* btnData = getTextureData(textureName);
    if (!btnData) return;

    float scaledWidth = btnData->original_width * 3;
    float scaledHeight = btnData->original_height * 3;

    glUseProgram(shaderAtlas);
    glBindTexture(GL_TEXTURE_2D, textureAtlas);
    glUniform2f(uvOffsetLoc, btnData->u_start, btnData->v_start);
    glUniform2f(uvScaleLoc,
        btnData->u_end - btnData->u_start,
        btnData->v_end - btnData->v_start);
    updateVertexBuffer(x, y, scaledWidth, scaledHeight);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
}
// ОПТИМИЗАЦИЯ П.3 изменено
void drawMenu() {
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(shaderAtlas);
    glBindVertexArray(VAO);

    // фона меню
    glBindTexture(GL_TEXTURE_2D, menuBG);
    glUniform2f(uvOffsetLoc, 0.0f, 0.0f);
    glUniform2f(uvScaleLoc, 1.0f, 1.0f);
    updateVertexBuffer(0, 0, 1000, 1000);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

    // фон под кнопочками
    glUniform2f(uvOffsetLoc, 0.0f, 0.0f); // сброс UV
    glUniform2f(uvScaleLoc, 1.0f, 1.0f);
    glBindTexture(GL_TEXTURE_2D, menuTexture);
    updateVertexBuffer(300, 230, 400, 400);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

    if (currentMenuState == MAIN_MENU) {
        drawMenuButton("start_button", 400, 500);
        drawMenuButton("levels_button", 400, 400);
        drawMenuButton("quit_button", 400, 300);
    }
    else if (currentMenuState == LEVELS_MENU) {
        drawMenuButton("easy_button", 400, 500);
        drawMenuButton("normal_button", 400, 400);
        drawMenuButton("hard_button", 400, 300);
    }

    // отрисовка текста
    char maxScoreText[32];
    loadMaxScore();
    sprintf(maxScoreText, "Max Score: %d", maxScore);
    renderText(maxScoreText, 10.0f, 950.0f, 1.0f, charTextures);

    glfwSwapBuffers(window);
}

GLuint textureAtlas;

void initTextures() {
    textureAtlas = loadTexture("ASSETS/spritesheet.png");
    if (textureAtlas == 0) {
        printf("CRITICAL ERROR: Failed to load texture atlas!\n");
        exit(EXIT_FAILURE);
    }
    loadAtlasData();

    glBindTexture(GL_TEXTURE_2D, textureAtlas);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    backgroundTexture = loadTexture("ASSETS/bg/bg.png");

    starTextures[0] = loadTexture("ASSETS/bg/bg3.png");
    starTextures[1] = loadTexture("ASSETS/bg/bg2.png");
    starTextures[2] = loadTexture("ASSETS/bg/bg3.png");
    starTextures[3] = loadTexture("ASSETS/bg/bg4.png");
    starTextures[4] = loadTexture("ASSETS/bg/bg5.png");
    gameOverTexture = loadTexture("ASSETS/game_over.jpg");

}

void initGameObjects() {
    player.x = 375.0f;
    player.y = 50.0f;
    player.width = 50.0f;
    player.height = 50.0f;

    // ADD ОПТИМИЗАЦИЯ П.2
    activeEnemies.count = 0;
    activeBullets.count = 0;
    scoreReached100 = 0;
    scoreReached300 = 0;

    srand((unsigned int)time(NULL)); // инициализация генератора случайных чисел

    for (int i = 0; i < MAX_ESSENCES; i++) {
        essences[i].base.x = (float)(rand() % 950);
        essences[i].base.y = 1000.0f;
        essences[i].base.width = 30.0f;
        essences[i].base.height = 30.0f;
        essences[i].base.spawnTime = (float)(rand() % 3000) / 1000.0f; // рандомное время появления (0-3 секунды)
        essences[i].base.exploding = 0;
        essences[i].base.explosionTime = 0.0f;
        essences[i].active = 0;
        essences[i].collected = 0;
    }
}

// МОДИФИЦИРОВАНО (ОПТИМИЗАЦИЯ П.2 + П.3)
void spawnEnemy() {
    if (activeEnemies.count < activeEnemies.capacity) {
        Enemy* newEnemy = &activeEnemies.items[activeEnemies.count++];
        TextureData* enemyData = NULL;
        float scale_factor = 1.0f;

        // выбор текстуры в зависимости от прогресса
        if (scoreReached300) {
            enemyData = getTextureData("enemy_3");
            scale_factor = ENEMY3_SCALE;
        }
        else if (scoreReached100) {
            enemyData = getTextureData("enemy_2");
            scale_factor = ENEMY2_SCALE;
        }
        else {
            enemyData = getTextureData("enemy_1");
            scale_factor = ENEMY1_SCALE;
        }

        if (!enemyData) {
            printf("ERROR: Enemy texture data not found!\n");
            return;
        }

        // установка параметров из атласа
        newEnemy->base.x = rand() % 900 + 50;
        newEnemy->base.y = 1000;
        newEnemy->base.width = enemyData->original_width * scale_factor;
        newEnemy->base.height = enemyData->original_height * scale_factor;

        // здоровье в зависимости от типа
        newEnemy->base.health =
            (scoreReached300) ? 3 :
            (scoreReached100) ? 2 : 1;

        // отладочный вывод
        printf("Spawned enemy: %s | Size: %.1fx%.1f | Health: %d\n",
            (scoreReached300) ? "enemy_3" :
            (scoreReached100) ? "enemy_2" : "enemy_1",
            newEnemy->base.width,
            newEnemy->base.height,
            newEnemy->base.health);

        newEnemy->base.spawnTime = getTime();
        newEnemy->base.exploding = 0;
        newEnemy->base.explosionTime = 0;
        newEnemy->collided = 0;
        newEnemy->shootTimer = 0.0f;
    }
}

void spawnEssence() {
    for (int i = 0; i < MAX_ESSENCES; i++) {
        if (!essences[i].active) {
            essences[i].base.x = rand() % 900 + 50;
            essences[i].base.y = 1000;
            essences[i].base.width = 30;
            essences[i].base.height = 30;
            essences[i].base.spawnTime = getTime();
            essences[i].active = 1;
            break;
        }
    }
}

void updateEssences(float deltaTime) {
    for (int i = 0; i < MAX_ESSENCES; i++) {
        if (essences[i].active) {
            essences[i].base.y -= 100.0f * deltaTime; // движение вниз

            // деактивация Essence, если он вышел за нижнюю границу экрана
            if (essences[i].base.y + essences[i].base.height < 0.0f) {
                essences[i].active = 0;
            }
        }
    }
}

void initBullets() {
    for (int i = 0; i < MAX_BULLETS; i++) {
        bullets[i].x = 0;
        bullets[i].y = 0;
        bullets[i].width = 0;
        bullets[i].height = 0;
        bullets[i].active = 0;
        bullets[i].direction = 1; // по умолчанию летят вверх
        bullets[i].damageDealt = 0; // сбрасываем флаг урона
    }
}

void shootPlayerBullet(float x, float y) {
    if (activeBullets.count < activeBullets.capacity) {
        Bullet* bullet = &activeBullets.items[activeBullets.count++];
        TextureData* playerData = getTextureData("player");

        // расчет центра игрока
        float playerCenterX = x + (playerData->original_width * PLAYER_SCALE) / 2;
        float playerCenterY = y + (playerData->original_height * PLAYER_SCALE) / 2;

        bullet->x = playerCenterX - 25.0f; // центрирование пули
        bullet->y = playerCenterY - bullet->height / 2;
        bullet->width = 5.0f;
        bullet->height = 10.0f;
        bullet->active = 1;
        bullet->direction = 1;
        bullet->damageDealt = 0;
        Mix_PlayChannel(-1, shootSound, 0);
    }
}

void shootEnemyBullet(float enemyX, float enemyY, float enemyWidth, float enemyHeight) {
    if (activeBullets.count < activeBullets.capacity) {
        Bullet* bullet = &activeBullets.items[activeBullets.count++];
        // центрируем пулю по X и помещаем у нижнего края врага
        bullet->x = enemyX + (enemyWidth / 2) - 2.5f; // 2.5f = половина ширины пули (5px)
        bullet->y = enemyY - enemyHeight / 3;
        bullet->width = 5.0f;
        bullet->height = 10.0f;
        bullet->active = 1;
        bullet->direction = -1;
        bullet->damageDealt = 0;
        Mix_PlayChannel(-1, shootSound, 0);
    }
}

void drawBullets() {
    glUseProgram(shaderBullet);
    for (int i = 0; i < activeBullets.count; i++) {
        if (activeBullets.items[i].active) {
            updateVertexBuffer(activeBullets.items[i].x, activeBullets.items[i].y, activeBullets.items[i].width, activeBullets.items[i].height);
            glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
            CHECK_GL_ERROR();
        }
    }
}

// МОДИФИЦИРОВАНО (ОПТИМИЗАЦИЯ П.2)
void updateBullets(float deltaTime) {
    for (int i = 0; i < activeBullets.count; i++) {
        if (i >= activeBullets.capacity) {
            printf("[CRITICAL] Bullet index out of bounds: %d/%d\n",
                i, activeBullets.capacity);
            exit(EXIT_FAILURE);
        }
        Bullet* bullet = &activeBullets.items[i];

        if (bullet->active) {
            bullet->y += bullet->direction * 300.0f * deltaTime;

            if (bullet->y < 0 || bullet->y > 1000) {
                bullet->active = 0;
            }
        }
    }
    int writeIndex = 0;
    for (int readIndex = 0; readIndex < activeBullets.count; readIndex++) {
        if (activeBullets.items[readIndex].active) {
            activeBullets.items[writeIndex++] = activeBullets.items[readIndex];
        }
    }
    activeBullets.count = writeIndex;
}

// МОДИФИЦИРОВАНО (ОПТИМИЗАЦИЯ П.1 + П.2 + П.3)
void checkBulletCollisions(float currentTime) {
    updateGrid();

    // проверка коллизий пуль игрока с врагами
    for (int i = 0; i < activeBullets.count; i++) {
        Bullet* bullet = &activeBullets.items[i];

        if (bullet->active && bullet->direction == 1) {
            int cellX = (int)(bullet->x / CELL_SIZE);
            int cellY = (int)(bullet->y / CELL_SIZE);

            for (int dx = -1; dx <= 1; dx++) {
                for (int dy = -1; dy <= 1; dy++) {
                    int checkX = cellX + dx;
                    int checkY = cellY + dy;

                    if (checkX >= 0 && checkX < GRID_SIZE &&
                        checkY >= 0 && checkY < GRID_SIZE)
                    {
                        GridCell* cell = &collisionGrid.cells[checkX][checkY];

                        for (int e = 0; e < cell->count; e++) {
                            Enemy* enemy = cell->enemies[e];

                            if (!enemy->collided && !enemy->base.exploding) {
                                // создание хитбоксов с учетом реальных позиций
                                GameObject bulletHitbox = {
                                    bullet->x,
                                    bullet->y,
                                    bullet->width,
                                    bullet->height,
                                    0.0f
                                };

                                GameObject enemyHitbox = {
                                    enemy->base.x,
                                    enemy->base.y,
                                    enemy->base.width,
                                    enemy->base.height,
                                    0.0f
                                };

                                if (checkCollision(bulletHitbox, enemyHitbox)) {
                                    bullet->active = 0;
                                    enemy->base.health -= 1;

                                    if (enemy->base.health <= 0) {
                                        // получение данных текстур
                                        const char* enemyType =
                                            scoreReached300 ? "enemy_3" :
                                            scoreReached100 ? "enemy_2" : "enemy_1";

                                        TextureData* enemyData = getTextureData(enemyType);
                                        TextureData* explosionData = getTextureData("explosion");

                                        if (enemyData && explosionData) {
                                            // рассчёи центра врага
                                            float enemyCenterX = enemy->base.x + enemy->base.width / 2;
                                            float enemyCenterY = enemy->base.y + enemy->base.height / 2;

                                            // установка новых параметров для взрыва
                                            float explosionWidth = explosionData->original_width * EXPLOSION_SCALE;
                                            float explosionHeight = explosionData->original_height * EXPLOSION_SCALE;

                                            // позиционирование взрыва по центру врага
                                            enemy->base.x = enemyCenterX - explosionWidth / 2;
                                            enemy->base.y = enemyCenterY - explosionHeight / 2;
                                            enemy->base.width = explosionWidth;
                                            enemy->base.height = explosionHeight;
                                        }

                                        enemy->base.exploding = 1;
                                        enemy->base.explosionTime = currentTime;
                                        score += 10;
                                        printf("Уничтожен враг! Позиция взрыва: [%.1f, %.1f]\n",
                                            enemy->base.x, enemy->base.y);
                                        Mix_PlayChannel(-1, hitSound, 0);
                                    }
                                    goto next_bullet;
                                }
                            }
                        }
                    }
                }
            }
        next_bullet:;
        }
    }

    // проверка коллизий вражеских пуль с игроком
    for (int i = 0; i < activeBullets.count; i++) {
        Bullet* bullet = &activeBullets.items[i];

        if (bullet->active && bullet->direction == -1 && !bullet->damageDealt) {
            // хитбокс игрока
            GameObject playerHitbox = {
                player.x,
                player.y,
                player.width,
                player.height,
                0.0f
            };

            if (checkCollision(
                (GameObject) {
                bullet->x, bullet->y, bullet->width, bullet->height, 0.0f
            },
                playerHitbox))
            {
                playerHealth -= scoreReached300 ? 3 : (scoreReached100 ? 2 : 1);
                bullet->damageDealt = 1;
                bullet->active = 0;
                printf("Попадание в игрока! Здоровье: %d Позиция пули: [%.1f, %.1f]\n",
                    playerHealth, bullet->x, bullet->y);
            }
        }
    }

    if (playerHealth <= 0 && !gameOver) {
        printf("GAME OVER!\n");
        gameOver = 1;
        gameOverStartTime = currentTime;
        Mix_HaltMusic();
        Mix_PlayMusic(gameOverMusic, 1);
    }
}

// МОДИФИЦИРОВАНО (ОПТИМИЗАЦИЯ П.2)
void updateEnemies(float deltaTime) {
    int isHard = (currentDifficulty == HARD); // ОПТИМИЗАЦИЯ П.5
    for (int i = 0; i < activeEnemies.count; i++) {
        Enemy* enemy = &activeEnemies.items[i];

        if (enemy->base.exploding) {
            //обновляем таймер взрыва
            enemy->base.explosionTime += deltaTime;

            // удаляем взрыв после 1.5 секунд
            if (enemy->base.explosionTime >= EXPLOSION_DURATION) {
                enemy->collided = 1; // помечаем для удаления
                printf("Explosion removed after %.1fs\n", EXPLOSION_DURATION);
            }
        }

        if (!enemy->collided && !enemy->base.exploding) {
            // движение врага
            enemy->base.y -= 100.0f * deltaTime;

            // стрельба только на Hard
            if (isHard && enemy->base.health > 0) {
                enemy->shootTimer += deltaTime;
                if (enemy->shootTimer >= 1.0f) {
                    shootEnemyBullet(enemy->base.x, enemy->base.y, enemy->base.width, enemy->base.height);
                    enemy->shootTimer = 0.0f;
                }
            }

            // проверка выхода за границы
            if (enemy->base.y + enemy->base.height < 0.0f) {
                enemy->collided = 1;
                if (scoreReached300) score -= 20;
            }
        }
    }

    // удаление неактивных врагов
    int writeIndex = 0;
    for (int readIndex = 0; readIndex < activeEnemies.count; readIndex++) {
        if (!activeEnemies.items[readIndex].collided) {
            activeEnemies.items[writeIndex++] = activeEnemies.items[readIndex];
        }
    }
    activeEnemies.count = writeIndex;
}

// ОПТИМИЗАЦИЯ П.5
int checkCollision(GameObject a, GameObject b) {
    float aRight = a.x + a.width;
    float bRight = b.x + b.width;
    float aBottom = a.y + a.height;
    float bBottom = b.y + b.height;

    return (a.x < bRight &&
        aRight > b.x &&
        a.y < bBottom &&
        aBottom > b.y);
}

void checkCollisions() {
    for (int i = 0; i < activeEnemies.count; i++) {
        Enemy* enemy = &activeEnemies.items[i];
        if (checkCollision(player, enemy->base)) {
            if (!enemy->collided) {
                Mix_PlayChannel(-1, collisionSound, 0);
                playerHealth -= scoreReached300 ? 3 : (scoreReached100 ? 2 : 1);
                enemy->collided = 1;
                enemy->base.exploding = 1;
                enemy->base.explosionTime = getTime();
            }
        }
    }

    for (int i = 0; i < MAX_ESSENCES; i++) {
        if (essences[i].active && checkCollision(player, essences[i].base)) {
            Mix_PlayChannel(-1, essenceSound, 0);
            playerHealth += 1;
            essences[i].collected = 1;
            essences[i].active = 0;
        }
    }
}

void updateGameObjects(float deltaTime) {
    updateEnemies(deltaTime);
    updateBullets(deltaTime);

    if (currentDifficulty == EASY) {
        updateEssences(deltaTime);
        static float essenceSpawnTimer = 0.0f;
        essenceSpawnTimer += deltaTime;
        if (essenceSpawnTimer >= 5.0f) {
            spawnEssence();
            essenceSpawnTimer = 0.0f;
        }
    }

    if (score >= 300 && !scoreReached300) {
        scoreReached300 = 1;
        for (int i = 0; i < activeEnemies.count; i++) {
            if (!enemies[i].collided) {
                activeEnemies.items[i].base.exploding = 1;
                activeEnemies.items[i].base.explosionTime = getTime();
            }
        }
    }
    else if (score >= 100 && !scoreReached100) {
        scoreReached100 = 1;
        for (int i = 0; i < activeEnemies.count; i++) {
            activeEnemies.items[i].base.exploding = 1;
            activeEnemies.items[i].base.explosionTime = getTime();
        }
    }

    // обновление таймера смены изображений звездочек
    starTextureChangeTimer += deltaTime;
    if (starTextureChangeTimer >= starTextureChangeInterval) {
        currentStarTextureIndex = (currentStarTextureIndex + 1) % 5;
        starTextureChangeTimer = 0.0f;
    }

    checkCollisions();
}

// ОПТИМИЗАЦИЯ П.3
void initShadersAndBuffers() {
    // компиляция вершинного шейдера
    unsigned int vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vertexShaderSource, NULL);
    glCompileShader(vertexShader);

    // проверка ошибок вершинного шейдера
    int success;
    char infoLog[512];
    glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(vertexShader, 512, NULL, infoLog);
        printf("Ошибка компиляции вершинного шейдера: %s\n", infoLog);
    }

    // создание и компиляция фрагментных шейдеров
    unsigned int fragmentAtlas = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentAtlas, 1, &fragmentAtlasSource, NULL);  // Исправлено!
    glCompileShader(fragmentAtlas);

    unsigned int fragmentBullet = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentBullet, 1, &fragmentShaderSourceBullet, NULL);
    glCompileShader(fragmentBullet);

    unsigned int fragmentText = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentText, 1, &fragmentShaderSourceText, NULL);
    glCompileShader(fragmentText);

    // проверка ошибок фрагментных шейдеров
    GLint compileStatus;
    glGetShaderiv(fragmentAtlas, GL_COMPILE_STATUS, &compileStatus);
    if (!compileStatus) {
        glGetShaderInfoLog(fragmentAtlas, 512, NULL, infoLog);
        printf("Ошибка компиляции фрагментного шейдера (атлас): %s\n", infoLog);
    }

    glGetShaderiv(fragmentBullet, GL_COMPILE_STATUS, &compileStatus);
    if (!compileStatus) {
        glGetShaderInfoLog(fragmentBullet, 512, NULL, infoLog);
        printf("Ошибка компиляции фрагментного шейдера (пули): %s\n", infoLog);
    }

    glGetShaderiv(fragmentText, GL_COMPILE_STATUS, &compileStatus);
    if (!compileStatus) {
        glGetShaderInfoLog(fragmentText, 512, NULL, infoLog);
        printf("Ошибка компиляции фрагментного шейдера (текст): %s\n", infoLog);
    }

    // создание шейдерных программ
    shaderAtlas = glCreateProgram();
    glAttachShader(shaderAtlas, vertexShader);
    glAttachShader(shaderAtlas, fragmentAtlas);
    glLinkProgram(shaderAtlas);

    shaderBullet = glCreateProgram();
    glAttachShader(shaderBullet, vertexShader);
    glAttachShader(shaderBullet, fragmentBullet);
    glLinkProgram(shaderBullet);

    shaderText = glCreateProgram();
    glAttachShader(shaderText, vertexShader);
    glAttachShader(shaderText, fragmentText);
    glLinkProgram(shaderText);

    // проверка ошибок линковки
    glGetProgramiv(shaderAtlas, GL_LINK_STATUS, &success);
    if (!success) {
        glGetProgramInfoLog(shaderAtlas, 512, NULL, infoLog);
        printf("Ошибка связывания шейдерной программы (атлас): %s\n", infoLog);
    }

    glGetProgramiv(shaderBullet, GL_LINK_STATUS, &success);
    if (!success) {
        glGetProgramInfoLog(shaderBullet, 512, NULL, infoLog);
        printf("Ошибка связывания шейдерной программы (пули): %s\n", infoLog);
    }

    glGetProgramiv(shaderText, GL_LINK_STATUS, &success);
    if (!success) {
        glGetProgramInfoLog(shaderText, 512, NULL, infoLog);
        printf("Ошибка связывания шейдерной программы (текст): %s\n", infoLog);
    }

    // получаем uniform-локации
    uvOffsetLoc = glGetUniformLocation(shaderAtlas, "uvOffset");
    uvScaleLoc = glGetUniformLocation(shaderAtlas, "uvScale");
    textUvOffsetLoc = glGetUniformLocation(shaderText, "uvOffset");
    textUvScaleLoc = glGetUniformLocation(shaderText, "uvScale");

    // удаляем шейдеры
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentAtlas);
    glDeleteShader(fragmentBullet);
    glDeleteShader(fragmentText);

    // создание вершинного буфера
    float vertices[] = {
        -0.5f, -0.5f, 0.0f, 0.0f, 0.0f,
         0.5f, -0.5f, 0.0f, 1.0f, 0.0f,
         0.5f,  0.5f, 0.0f, 1.0f, 1.0f,
        -0.5f,  0.5f, 0.0f, 0.0f, 1.0f
    };

    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);

    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_DYNAMIC_DRAW);

    // Устанавливаем атрибуты вершин
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);  // Позиция

    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);  // Текстурные координаты

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    printf("Шейдеры и буферы успешно инициализированы\n");
}

//для обновления буфера вершин ОПТИМИЗАЦИЯ П.5
void updateVertexBuffer(float x, float y, float width, float height) {
    float xNorm = x / 500.0f - 1.0f;
    float yNorm = y / 500.0f - 1.0f;
    float widthNorm = width / 500.0f;
    float heightNorm = height / 500.0f;

    float xPlusWidth = xNorm + widthNorm;
    float yPlusHeight = yNorm + heightNorm;

    float vertices[] = {
        xNorm, yNorm, 0.0f, 0.0f, 1.0f,
        xPlusWidth, yNorm, 0.0f, 1.0f, 1.0f,
        xPlusWidth, yPlusHeight, 0.0f, 1.0f, 0.0f,
        xNorm, yPlusHeight, 0.0f, 0.0f, 0.0f
    };

    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

// ОПТИМИЗАЦИЯ П.3 + П.4
void drawGameObjectsOptimized(float currentTime) {
    glClear(GL_COLOR_BUFFER_BIT);
    CHECK_GL_ERROR();

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBindVertexArray(VAO);

    // Отрисовка фона
    glUseProgram(shaderAtlas);
    glBindTexture(GL_TEXTURE_2D, backgroundTexture);
    glUniform2f(uvOffsetLoc, 0.0f, 0.0f);
    glUniform2f(uvScaleLoc, 1.0f, 1.0f);
    updateVertexBuffer(0, 0, 1000, 1000);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

    // Отрисовка звездочек
    glBindTexture(GL_TEXTURE_2D, starTextures[currentStarTextureIndex]);
    updateVertexBuffer(0, 0, 1000, 1000);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

    // Отрисовка игрока
    TextureData* playerData = getTextureData("player");
    if (playerData) {
        glBindTexture(GL_TEXTURE_2D, textureAtlas);
        glUniform2f(uvOffsetLoc, playerData->u_start, playerData->v_start);
        glUniform2f(uvScaleLoc,
            playerData->u_end - playerData->u_start,
            playerData->v_end - playerData->v_start);
        updateVertexBuffer(player.x, player.y,
            playerData->original_width * PLAYER_SCALE,
            playerData->original_height * PLAYER_SCALE);
        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    }

    // Отрисовка врагов и взрывов с оптимизацией текстур
    TextureData* prevTexture = NULL;
    for (int i = 0; i < activeEnemies.count; i++) {
        Enemy* enemy = &activeEnemies.items[i];
        TextureData* texData = NULL;
        float scale = 1.0f;

        if (enemy->base.exploding) {
            texData = getTextureData("explosion");
            scale = EXPLOSION_SCALE;
        }
        else {
            if (scoreReached300) {
                texData = getTextureData("enemy_3");
                scale = ENEMY3_SCALE;
            }
            else if (scoreReached100) {
                texData = getTextureData("enemy_2");
                scale = ENEMY2_SCALE;
            }
            else {
                texData = getTextureData("enemy_1");
                scale = ENEMY1_SCALE;
            }
        }

        // Переключаем текстуру только при изменении
        if (texData != prevTexture) {
            prevTexture = texData;
            glUniform2f(uvOffsetLoc, texData->u_start, texData->v_start);
            glUniform2f(uvScaleLoc,
                texData->u_end - texData->u_start,
                texData->v_end - texData->v_start);
        }

        updateVertexBuffer(enemy->base.x, enemy->base.y,
            texData->original_width * scale,
            texData->original_height * scale);
        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    }

    // Отрисовка эссенций с оптимизацией
    if (currentDifficulty == EASY) {
        TextureData* essenceData = getTextureData("essence");
        if (essenceData) {
            glUniform2f(uvOffsetLoc, essenceData->u_start, essenceData->v_start);
            glUniform2f(uvScaleLoc,
                essenceData->u_end - essenceData->u_start,
                essenceData->v_end - essenceData->v_start);

            for (int i = 0; i < MAX_ESSENCES; i++) {
                if (essences[i].active) {
                    updateVertexBuffer(essences[i].base.x, essences[i].base.y,
                        essenceData->original_width * 1.2f,
                        essenceData->original_height * 1.2f);
                    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
                }
            }
        }
    }

    // Отрисовка пуль
    glUseProgram(shaderBullet);
    GLint bulletColorLoc = glGetUniformLocation(shaderBullet, "bulletColor");
    for (int i = 0; i < activeBullets.count; i++) {
        if (activeBullets.items[i].active) {
            if (activeBullets.items[i].direction == 1) {
                glUniform3f(bulletColorLoc, 1.0f, 1.0f, 0.0f);
            }
            else {
                glUniform3f(bulletColorLoc, 1.0f, 0.0f, 0.0f);
            }
            updateVertexBuffer(activeBullets.items[i].x,
                activeBullets.items[i].y,
                activeBullets.items[i].width,
                activeBullets.items[i].height);
            glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
        }
    }

    glfwSwapBuffers(window);
    CHECK_GL_ERROR();
}

void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if (action == GLFW_PRESS || action == GLFW_REPEAT) {
        if (key == GLFW_KEY_ESCAPE) {
            if (currentMenuState == LEVELS_MENU) {
                currentMenuState = MAIN_MENU;
            }
            else {
                inMenu = !inMenu;
                if (inMenu) {
                    lowHealthMusicPlaying = 0;
                    Mix_HaltMusic();
                    if (!menuMusicPlaying) {
                        Mix_PlayMusic(menuMusic, -1);
                        menuMusicPlaying = 1;
                    }
                }
                else {
                    Mix_HaltMusic();
                    Mix_PlayMusic(backgroundMusic, -1);
                    menuMusicPlaying = 0;
                }
            }
        }
        else if (inMenu) {
            if (currentMenuState == MAIN_MENU) {
                switch (key) {
                case GLFW_KEY_ENTER:
                    inMenu = 0;
                    Mix_HaltMusic();
                    Mix_PlayMusic(backgroundMusic, -1);
                    menuMusicPlaying = 0;
                    break;
                }
            }
            else if (currentMenuState == LEVELS_MENU) {
                // потом добавлю логику для выбора уровня
            }
        }
        else {
            switch (key) {
            case GLFW_KEY_A:
                player.x -= 15.0f;
                if (player.x < 0) player.x = 0;
                break;
            case GLFW_KEY_D:
                player.x += 15.0f;
                if (player.x + player.width > 1000) player.x = 1000 - player.width;
                break;
            case GLFW_KEY_SPACE:
                shootPlayerBullet(player.x + player.width / 2, player.y + player.height);
                break;
            }
        }
    }
}

GLFWwindow* initWindow(int width, int height, const char* title) {
    if (!glfwInit()) {
        printf("Ошибка инициализации GLFW\n");
        return NULL;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    glfwWindowHint(GLFW_DOUBLEBUFFER, GLFW_TRUE); //двойная буферизация

    GLFWwindow* window = glfwCreateWindow(width, height, title, NULL, NULL);
    if (!window) {
        printf("Ошибка создания окна GLFW\n");
        glfwTerminate();
        return NULL;
    }
    setupViewport(1000, 1000);

    glfwMakeContextCurrent(window);
    glfwSwapInterval(0); // отключение V-Sync

    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) {
        printf("Ошибка инициализации GLEW\n");
        return NULL;
    }

    return window;
}

// изменено ОПТИМИЗАЦИЯ П.3
void drawGameOver() {
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(shaderAtlas);
    glBindVertexArray(VAO);
    glBindTexture(GL_TEXTURE_2D, gameOverTexture);
    glUniform2f(uvOffsetLoc, 0.0f, 0.0f);
    glUniform2f(uvScaleLoc, 1.0f, 1.0f);
    updateVertexBuffer(0, 0, 1000, 1000);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

    if (!Mix_PlayingMusic()) {
        Mix_PlayMusic(gameOverMusic, 1);
    }

    glfwSwapBuffers(window);
}


int main() {
    setlocale(LC_ALL, "Russian");
    setlocale(LC_NUMERIC, "C"); // ДЛЯ КОРРЕКТНОГО ЛОГИРОВАНИЯ

    window = initWindow(1000, 1000, "Galaxian");
    if (!window) {
        printf("Ошибка создания окна GLFW\n");
        glfwTerminate();
        return -1;
    }

    // ФАЙЛ ДЛЯ ЛОГИРОВАНИЯ
    fpsLogFile = fopen("fps_log.csv", "w");
    if (fpsLogFile) {
        fprintf(fpsLogFile, "Timestamp,FrameTime,FPS,Difficulty\n");
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(0); // отключение V-Sync

    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) {
        printf("Ошибка инициализации GLEW\n");
        return -1;
    }

    glEnable(GL_DEBUG_OUTPUT);
    glDebugMessageCallback(glDebugOutput, NULL);

    printf("OpenGL Version: %s\n", glGetString(GL_VERSION));

    if (FT_Init_FreeType(&ft)) {
        printf("Could not init FreeType Library\n");
        return -1;
    }
    if (FT_New_Face(ft, "fonts/SuperMario.ttf", 0, &face)) {
        printf("Failed to load font\n");
        return -1;
    }

    FT_Set_Pixel_Sizes(face, 0, 48);

    if (initAudio() != 0) {
        return -1;
    }
    if (loadAudioFiles(&backgroundMusic, &lowHealthMusic, &menuMusic, &gameOverMusic, &shootSound, &collisionSound, &hitSound, &essenceSound) != 0) {
        return -1;
    }

    Mix_PlayMusic(menuMusic, -1); // -1 для зацикливания

    initShadersAndBuffers();
    initTextures();
    initMenuTextures();
    initTextRendering();
    initGameObjects();
    initBullets();

    glfwSetKeyCallback(window, key_callback);
    glfwSetMouseButtonCallback(window, mouse_button_callback);

    float enemySpawnTimer = 0.0f;
    float enemySpawnInterval = 2.0f;

    // ИНИЦИАЛИЗАЦИЯ ТАЙМЕРОВ
    float lastTime = getTime();
    float logTimer = 0.0f;

    initGrid(); // ОПТИМИЗАЦИЯ П.1
    initDynamicArrays(); // ОПТИМИЗАЦИЯ П.2

    while (!glfwWindowShouldClose(window)) {

        // НАЧАЛО ЗАМЕРА ВРЕМЕНИ КАДРА
        float currentTime = getTime();
        float deltaTime = currentTime - lastTime;
        lastTime = currentTime;

        // ПОДСЧЁТ FPS
        frameCount++;
        fpsTimer += deltaTime;
        if (fpsTimer >= 1.0f) {
            fps = frameCount / fpsTimer;
            frameCount = 0;
            fpsTimer = 0.0f;
        }

        // ЛОГИРОВАНИЕ ДАННЫХ
        if (fpsLogFile) {
            logTimer += deltaTime;
            if (logTimer >= 0.5f) {
                fprintf(fpsLogFile, "%.3f,%.3f,%.1f,%d\n",
                    currentTime,
                    deltaTime * 1000,
                    fps,
                    currentDifficulty);
                logTimer = 0.0f;
            }
        }

        glClear(GL_COLOR_BUFFER_BIT);

        if (inMenu) {
            drawMenu();
            glfwSwapBuffers(window);
        }
        else {
            // установка интервала появления врагов в зависимости от уровня сложности
            if (currentDifficulty == HARD) {
                enemySpawnInterval = 1.0f;
            }
            else {
                enemySpawnInterval = 2.0f;
            }
            // обновление таймера появления врагов
            enemySpawnTimer += deltaTime;
            if (enemySpawnTimer >= enemySpawnInterval) {
                spawnEnemy();
                enemySpawnTimer = 0.0f;
            }

            updateGameObjects(deltaTime);
            checkCollisions();
            checkBulletCollisions(currentTime);

            if (gameOver) {
                drawGameOver();
                if (currentTime - gameOverStartTime >= 5.0f) {
                    glfwSetWindowShouldClose(window, GLFW_TRUE); // окно закроется через 5 секунд
                }
            }
            else {
                drawGameObjectsOptimized(currentTime);

                // ОТРИСОВКА FPS И ВРЕМЕНИ КАДРА
                char fpsText[32];
                sprintf(fpsText, "FPS: %.1f", fps);
                renderText(fpsText, 700.0f, 950.0f, 0.75f, charTextures);

                char frameTimeText[32];
                sprintf(frameTimeText, "Frame: %.2fms", deltaTime * 1000);
                renderText(frameTimeText, 700.0f, 920.0f, 0.75f, charTextures);

                // счёт и здоровье
                char scoreText[32];
                sprintf(scoreText, "Score: %d", score);
                renderText(scoreText, 10.0f, 950.0f, 1.0f, charTextures);

                char healthText[32];
                sprintf(healthText, "Health: %d", playerHealth);
                renderText(healthText, 10.0f, 900.0f, 1.0f, charTextures);

                if (playerHealth <= 5 && Mix_PlayingMusic() && !lowHealthMusicPlaying && !inMenu) {
                    Mix_HaltMusic();
                    Mix_PlayMusic(lowHealthMusic, -1);
                    lowHealthMusicPlaying = 1;
                }

                if (score > maxScore) {
                    maxScore = score;
                    saveMaxScore();
                }
            }
        }
        glfwPollEvents(); // обработка событий
    }

    // ЗАКРЫТЬ ФАЙЛ ЛОГИРОВАНИЯ
    if (fpsLogFile) {
        fclose(fpsLogFile);
    }

    freeDynamicArrays(); // ОПТИМИЗАЦИЯ П.2
    Mix_FreeMusic(backgroundMusic);
    Mix_FreeMusic(lowHealthMusic);
    Mix_FreeMusic(menuMusic);
    Mix_FreeMusic(gameOverMusic);
    Mix_FreeChunk(shootSound);
    Mix_FreeChunk(collisionSound);
    Mix_FreeChunk(hitSound);
    Mix_CloseAudio();

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
////////////////////
