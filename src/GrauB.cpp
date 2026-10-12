#include <iostream>
#include <vector>
#include <fstream>
#include <string>
#include <algorithm>

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.cpp"

using namespace std;
using namespace glm;

const GLuint WIDTH = 1024, HEIGHT = 768;

// ==========================================
// ESTRUTURAS DO JOGO
// ==========================================
struct MapConfig {
    int tileW, tileH;
    int rows, cols;
};

struct Item {
    int col, row;
    bool active;
};

MapConfig mapInfo;
vector<vector<int>> mapData;
vector<int> walkableTiles; // Lida do TXT
vector<Item> items;        // Lida do TXT

int playerCol = 1;
int playerRow = 1;
int score = 0;
int gameState = 0; // 0 = Jogando, 1 = Venceu, 2 = Perdeu

// Variáveis de Animação (Spritesheet 8x8)
int currentDir = 0;   // 0 a 7
int currentFrame = 0; // 0 a 7
float lastFrameTime = 0.0f;
bool isMoving = false;

float startX = WIDTH / 2.0f;
float startY = 100.0f;

// ==========================================
// SHADERS (Agora com suporte a Spritesheet)
// ==========================================
const GLchar* vertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aTexCoord;

uniform mat4 projection;
uniform mat4 model;

// Variáveis para recortar a Sprite
uniform vec2 spriteOffset;
uniform vec2 spriteSize;

out vec2 TexCoord;
void main() {
    gl_Position = projection * model * vec4(aPos, 0.0, 1.0);
    TexCoord = vec2(aTexCoord.x * spriteSize.x + spriteOffset.x, 
                    aTexCoord.y * spriteSize.y + spriteOffset.y);
}
)";

const GLchar* fragmentShaderSource = R"(
#version 330 core
in vec2 TexCoord;
out vec4 FragColor;
uniform sampler2D spriteTexture;
void main() {
    vec4 color = texture(spriteTexture, TexCoord);
    if(color.a < 0.1) discard; 
    FragColor = color;
}
)";

// ==========================================
// FUNÇÕES DE LEITURA E SUPORTE
// ==========================================
void loadMapFile(const string& filepath) {
    ifstream file(filepath);
    if (!file.is_open()) {
        cerr << "ERRO: Arquivo " << filepath << " nao encontrado!" << endl;
        exit(1);
    }
    
    file >> mapInfo.tileW >> mapInfo.tileH;
    file >> mapInfo.rows >> mapInfo.cols;

    mapData.resize(mapInfo.rows, vector<int>(mapInfo.cols));
    for (int r = 0; r < mapInfo.rows; r++) {
        for (int c = 0; c < mapInfo.cols; c++) {
            file >> mapData[r][c];
        }
    }

    // Lê lógica de Caminháveis
    int numWalkable;
    file >> numWalkable;
    for(int i=0; i < numWalkable; i++) {
        int id; file >> id;
        walkableTiles.push_back(id);
    }

    // Lê Objetos/Itens
    int numItems;
    file >> numItems;
    for(int i=0; i < numItems; i++) {
        Item it;
        file >> it.col >> it.row;
        it.active = true;
        items.push_back(it);
    }
    file.close();
}

GLuint loadTexture(const string& path) {
    GLuint tex;
    int x, y, n;
    // Opcional: stbi_set_flip_vertically_on_load(true);
    unsigned char* data = stbi_load(path.c_str(), &x, &y, &n, 4);
    if (!data) return 0;
    
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, x, y, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    stbi_image_free(data);
    return tex;
}

GLuint setupQuad() {
    GLfloat vertices[] = {
        0.0f, 1.0f,  0.0f, 1.0f, 1.0f, 0.0f,  1.0f, 0.0f, 0.0f, 0.0f,  0.0f, 0.0f, 
        0.0f, 1.0f,  0.0f, 1.0f, 1.0f, 1.0f,  1.0f, 1.0f, 1.0f, 0.0f,  1.0f, 0.0f  
    };
    GLuint VBO, VAO;
    glGenVertexArrays(1, &VAO); glGenBuffers(1, &VBO);
    glBindVertexArray(VAO); glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), (GLvoid*)0); glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), (GLvoid*)(2 * sizeof(GLfloat))); glEnableVertexAttribArray(1);
    return VAO;
}

void drawSprite(GLuint shader, GLuint quadVAO, GLuint texture, float x, float y, float w, float h, float offX = 0.0f, float offY = 0.0f, float sizeX = 1.0f, float sizeY = 1.0f) {
    mat4 model = mat4(1.0f);
    model = translate(model, vec3(x, y, 0.0f));
    model = scale(model, vec3(w, h, 1.0f));
    
    glUniformMatrix4fv(glGetUniformLocation(shader, "model"), 1, GL_FALSE, value_ptr(model));
    glUniform2f(glGetUniformLocation(shader, "spriteOffset"), offX, offY);
    glUniform2f(glGetUniformLocation(shader, "spriteSize"), sizeX, sizeY);
    
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

// ==========================================
// LÓGICA E CONTROLES
// ==========================================
void checkCollisions() {
    // 1. Checa se o piso atual não é caminhável (Morte)
    int currentTileID = mapData[playerRow][playerCol];
    if (find(walkableTiles.begin(), walkableTiles.end(), currentTileID) == walkableTiles.end()) {
        gameState = 2; // Perdeu
        cout << "VOCE PISOU NA AGUA! GAME OVER!" << endl;
    }

    // 2. Checa coleta de itens
    for (auto& item : items) {
        if (item.active && item.col == playerCol && item.row == playerRow) {
            item.active = false;
            score++;
            cout << "Item coletado! Placar: " << score << "/" << items.size() << endl;
            if (score == items.size()) {
                gameState = 1; // Venceu
                cout << "PARABENS! VOCE VENCEU!" << endl;
            }
        }
    }
}

// Função que verifica se o próximo tile é seguro
bool isWalkable(int r, int c) {
    if (r < 0 || r >= mapInfo.rows || c < 0 || c >= mapInfo.cols) return false;
    int tileID = mapData[r][c];
    // Retorna true apenas se o ID do chão estiver na lista dos permitidos (lidos do map.txt)
    return find(walkableTiles.begin(), walkableTiles.end(), tileID) != walkableTiles.end();
}

void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if (action == GLFW_PRESS && gameState == 0) {
        int nextC = playerCol;
        int nextR = playerRow;

        if (key == GLFW_KEY_RIGHT) { nextC++; currentDir = 6; } // E
        if (key == GLFW_KEY_LEFT)  { nextC--; currentDir = 2; } // O
        if (key == GLFW_KEY_DOWN)  { nextR++; currentDir = 0; } // S
        if (key == GLFW_KEY_UP)    { nextR--; currentDir = 4; } // N
        if (key == GLFW_KEY_E) { nextC++; nextR--; currentDir = 5; } // NE
        if (key == GLFW_KEY_Q) { nextC--; nextR--; currentDir = 3; } // NO
        if (key == GLFW_KEY_C) { nextC++; nextR++; currentDir = 7; } // SE
        if (key == GLFW_KEY_Z) { nextC--; nextR++; currentDir = 1; } // SO

        // Apenas aplica o movimento se for diferente do atual E o tile de destino for caminhável
        if ((nextC != playerCol || nextR != playerRow) && isWalkable(nextR, nextC)) {
            playerCol = nextC;
            playerRow = nextR;
            isMoving = true;
            
            // Troca a cor do piso
            if (mapData[playerRow][playerCol] == 1) {
                mapData[playerRow][playerCol] = 2;
            }
            checkCollisions(); // Checa se pegou um item
        }
    } else if (action == GLFW_RELEASE) {
        isMoving = false;
    }
}

// ==========================================
// MAIN LOOP
// ==========================================
int main() {
    loadMapFile("../map.txt");

    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(WIDTH, HEIGHT, "Trabalho Final: Isometrico", nullptr, nullptr);
    glfwMakeContextCurrent(window);
    glfwSetKeyCallback(window, key_callback);
    gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    GLuint vs = glCreateShader(GL_VERTEX_SHADER); glShaderSource(vs, 1, &vertexShaderSource, NULL); glCompileShader(vs);
    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER); glShaderSource(fs, 1, &fragmentShaderSource, NULL); glCompileShader(fs);
    GLuint shaderProgram = glCreateProgram(); glAttachShader(shaderProgram, vs); glAttachShader(shaderProgram, fs); glLinkProgram(shaderProgram);
    GLuint quadVAO = setupQuad();

    GLuint texFloor   = loadTexture("../assets/chao_iso.png");
    GLuint texStepped = loadTexture("../assets/chao_piso.png");
    GLuint texWater   = loadTexture("../assets/agua_iso.png"); 
    GLuint texPlayer  = loadTexture("../assets/player_iso.png"); // A sua spritesheet!
    GLuint texItem    = loadTexture("../assets/item.png");       // Colete qualquer item que tiver aí

    mat4 projection = ortho(0.0f, (float)WIDTH, (float)HEIGHT, 0.0f, -1.0f, 1.0f);

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        glClearColor(0.1f, 0.1f, 0.15f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glUseProgram(shaderProgram);
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "projection"), 1, GL_FALSE, value_ptr(projection));

        // Atualiza a Animação
        float currentTime = glfwGetTime();
        if (isMoving && currentTime - lastFrameTime > 0.1f) {
            currentFrame = (currentFrame + 1) % 8; // 8 colunas na imagem
            lastFrameTime = currentTime;
        } else if (!isMoving) {
            currentFrame = 0; // Volta a ficar parado
        }

        // --- RENDERIZAÇÃO ---
        for (int r = 0; r < mapInfo.rows; r++) {
            for (int c = 0; c < mapInfo.cols; c++) {
                
                float screenX = startX + (c - r) * (mapInfo.tileW / 2.0f);
                float screenY = startY + (c + r) * (mapInfo.tileH / 2.0f);

                GLuint currentTex = texWater;
                if (mapData[r][c] == 1) currentTex = texFloor;
                if (mapData[r][c] == 2) currentTex = texStepped;

                // Desenha o chão inteiro (Offset 0, Tamanho 1)
                drawSprite(shaderProgram, quadVAO, currentTex, screenX, screenY, mapInfo.tileW, mapInfo.tileH, 0.0f, 0.0f, 1.0f, 1.0f);

                // Desenha os Itens no mapa
                for (auto& item : items) {
                    if (item.active && item.col == c && item.row == r) {
                        drawSprite(shaderProgram, quadVAO, texItem, screenX + 16.0f, screenY - 16.0f, 32.0f, 32.0f, 0.0f, 0.0f, 1.0f, 1.0f);
                    }
                }

                // Desenha o Jogador Animado (Recorta 1/8 da imagem)
                if (r == playerRow && c == playerCol) {
                    float spriteW = 1.0f / 8.0f; // 8 Colunas
                    float spriteH = 1.0f / 8.0f; // 8 Linhas
                    
                    // A matemática para pegar o boneco certo na imagem "Walk.png"
                    float offX = currentFrame * spriteW;
                    float offY = currentDir * spriteH;

                    drawSprite(shaderProgram, quadVAO, texPlayer, screenX, screenY - (mapInfo.tileH * 1.5f), mapInfo.tileW, mapInfo.tileH * 2.5f, offX, offY, spriteW, spriteH);
                }
            }
        }

        glfwSwapBuffers(window);
    }
    glfwTerminate();
    return 0;
}