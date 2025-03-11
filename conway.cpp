#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include <cstdlib>

#define WIDTH 2000
#define HEIGHT 2000

class GridVisualizer {
private:
    GLFWwindow* window;
    GLuint computeProgram, renderProgram, textures[2], vao;
    GLuint currentTextureIdx;

    const char* computeShaderSource = R"(
        #version 430 core
        layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;
        layout(r8, binding = 0) uniform readonly image2D currentGrid;
        layout(r8, binding = 1) uniform writeonly image2D nextGrid;
        void main() {
            ivec2 pos = ivec2(gl_GlobalInvocationID.xy);
            ivec2 size = imageSize(currentGrid);
            if (pos.x >= size.x || pos.y >= size.y) return;
            float current = imageLoad(currentGrid, pos).r;
            int liveNeighbors = 0;
            for (int dy = -1; dy <= 1; dy++) {
                for (int dx = -1; dx <= 1; dx++) {
                    if (dx == 0 && dy == 0) continue;
                    ivec2 neighborPos = (pos + ivec2(dx, dy) + size) % size;
                    liveNeighbors += imageLoad(currentGrid, neighborPos).r > 0.5 ? 1 : 0;
                }
            }
            float nextState = 0.0;
            if (current > 0.5) {
                nextState = (liveNeighbors == 2 || liveNeighbors == 3) ? 1.0 : 0.0;
            } else {
                nextState = (liveNeighbors == 3) ? 1.0 : 0.0;
            }
            imageStore(nextGrid, pos, vec4(nextState, 0.0, 0.0, 1.0));
        }
    )";

    const char* vertexShaderSource = R"(
        #version 330 core
        out vec2 TexCoord;
        void main() {
            TexCoord = vec2((gl_VertexID & 1), (gl_VertexID >> 1));
            gl_Position = vec4(TexCoord * 2.0 - 1.0, 0.0, 1.0);
        }
    )";

    const char* fragmentShaderSource = R"(
        #version 330 core
        in vec2 TexCoord;
        out vec4 FragColor;
        uniform sampler2D gridTexture;
        void main() {
            float value = texture(gridTexture, TexCoord).r;
            FragColor = vec4(value, value, value, 1.0);
        }
    )";

    GLuint createShader(GLenum type, const char* source) {
        GLuint shader = glCreateShader(type);
        glShaderSource(shader, 1, &source, NULL);
        glCompileShader(shader);
        GLint success;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
        if (!success) {
            char infoLog[512];
            glGetShaderInfoLog(shader, 512, NULL, infoLog);
            std::cerr << "Shader Error: " << infoLog << std::endl;
        }
        return shader;
    }

    GLuint createProgram(GLuint vertexShader, GLuint fragmentShader) {
        GLuint program = glCreateProgram();
        glAttachShader(program, vertexShader);
        glAttachShader(program, fragmentShader);
        glLinkProgram(program);
        GLint success;
        glGetProgramiv(program, GL_LINK_STATUS, &success);
        if (!success) {
            char infoLog[512];
            glGetProgramInfoLog(program, 512, NULL, infoLog);
            std::cerr << "Program Link Error: " << infoLog << std::endl;
        }
        return program;
    }

    GLuint createComputeProgram() {
        GLuint shader = createShader(GL_COMPUTE_SHADER, computeShaderSource);
        GLuint program = glCreateProgram();
        glAttachShader(program, shader);
        glLinkProgram(program);
        GLint success;
        glGetProgramiv(program, GL_LINK_STATUS, &success);
        if (!success) {
            char infoLog[512];
            glGetProgramInfoLog(program, 512, NULL, infoLog);
            std::cerr << "Compute Program Link Error: " << infoLog << std::endl;
        }
        glDeleteShader(shader);
        return program;
    }

public:
    GridVisualizer() {
        if (!glfwInit()) { std::cerr << "GLFW init failed\n"; return; }
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
        window = glfwCreateWindow(WIDTH, HEIGHT, "Conway's Game of Life", NULL, NULL);
        if (!window) { std::cerr << "Window creation failed\n"; glfwTerminate(); return; }
        glfwMakeContextCurrent(window);
        if (glewInit() != GLEW_OK) { std::cerr << "GLEW init failed\n"; return; }

        glViewport(0, 0, WIDTH, HEIGHT);

        GLuint vertexShader = createShader(GL_VERTEX_SHADER, vertexShaderSource);
        GLuint fragmentShader = createShader(GL_FRAGMENT_SHADER, fragmentShaderSource);
        renderProgram = createProgram(vertexShader, fragmentShader);
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);

        computeProgram = createComputeProgram();

        glGenTextures(2, textures);
        for (int i = 0; i < 2; i++) {
            glBindTexture(GL_TEXTURE_2D, textures[i]);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, WIDTH, HEIGHT, 0, GL_RED, GL_UNSIGNED_BYTE, NULL);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            GLenum err = glGetError();
            if (err != GL_NO_ERROR) {
                std::cerr << "Texture " << i << " creation error: " << err << "\n";
            }
        }
        currentTextureIdx = 0;

        glGenVertexArrays(1, &vao);
        glBindVertexArray(vao);

        glUseProgram(renderProgram);
        GLint loc = glGetUniformLocation(renderProgram, "gridTexture");
        if (loc != -1) {
            glUniform1i(loc, 0);
        } else {
            std::cout << "Note: gridTexture uniform not present in red shader (expected)\n";
        }
        glClearColor(0.2f, 0.2f, 0.2f, 1.0f);

        std::cout << "OpenGL Version: " << glGetString(GL_VERSION) << std::endl;
    }

    void initializeGrid() {
        GLubyte* initialData = new GLubyte[WIDTH * HEIGHT];
        for (int i = 0; i < WIDTH * HEIGHT; i++) {
            initialData[i] = rand() % 2 ? 255 : 0;
        }
        glBindTexture(GL_TEXTURE_2D, textures[0]);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, WIDTH, HEIGHT, GL_RED, GL_UNSIGNED_BYTE, initialData);
        GLenum err = glGetError();
        if (err != GL_NO_ERROR) {
            std::cerr << "Texture upload error: " << err << "\n";
        }
        delete[] initialData;

        GLubyte* checkData = new GLubyte[WIDTH * HEIGHT];
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RED, GL_UNSIGNED_BYTE, checkData);
        err = glGetError();
        if (err != GL_NO_ERROR) {
            std::cerr << "glGetTexImage error: " << err << "\n";
        } else {
            std::cout << "Textures uploaded\n";
        }
        delete[] checkData;
    }

    void computeStep() {
        glUseProgram(computeProgram);
        glBindImageTexture(0, textures[currentTextureIdx], 0, GL_FALSE, 0, GL_READ_ONLY, GL_R8);
        glBindImageTexture(1, textures[1 - currentTextureIdx], 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_R8);

        GLuint numGroupsX = (WIDTH + 15) / 16;
        GLuint numGroupsY = (HEIGHT + 15) / 16;
        glDispatchCompute(numGroupsX, numGroupsY, 1);

        GLenum err = glGetError();
        if (err != GL_NO_ERROR) {
            std::cout << "Compute: Reading from " << textures[currentTextureIdx] << ", Writing to " << textures[1 - currentTextureIdx] << "\n";
            std::cerr << "Error after glDispatchCompute: " << err << "\n";
        }

        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
        currentTextureIdx = 1 - currentTextureIdx;
    }

    void renderFrame() {
        if (!window) return;
        glClear(GL_COLOR_BUFFER_BIT);

        glUseProgram(renderProgram);
        GLenum err = glGetError();
        if (err != GL_NO_ERROR) {
            std::cerr << "Error after glUseProgram: " << err << "\n";
        }

        glBindVertexArray(vao);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, textures[currentTextureIdx]);
        GLint boundTexture;
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &boundTexture);

        GLint loc = glGetUniformLocation(renderProgram, "gridTexture");
        
        if (loc != -1) {
            glUniform1i(loc, 0);
        } else {
            std::cout << "Render: Bound texture ID: " << boundTexture << " (expected " << textures[currentTextureIdx] << ")\n";
            std::cout << "gridTexture uniform location: " << loc << "\n";
            std::cerr << "gridTexture uniform not found (expected with red shader)\n";
        }

        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        err = glGetError();
        if (err != GL_NO_ERROR) {
            std::cerr << "Error after glDrawArrays: " << err << "\n";
        }

        // FPS calculation
        static double lastTime = glfwGetTime();
        static int frameCount = 0;
        double currentTime = glfwGetTime();
        frameCount++;
        if (currentTime - lastTime >= 1.0) {
            float fps = frameCount / (currentTime - lastTime);
            std::cout << "FPS: " << fps << "\n";
            frameCount = 0;
            lastTime = currentTime;
        }

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    bool isWindowOpen() {
        return window && !glfwWindowShouldClose(window);
    }

    void cleanup() {
        if (window) {
            glDeleteVertexArrays(1, &vao);
            glDeleteTextures(2, textures);
            glDeleteProgram(computeProgram);
            glDeleteProgram(renderProgram);
            glfwDestroyWindow(window);
            glfwTerminate();
        }
    }
};

int main() {
    GridVisualizer viz;
    viz.initializeGrid();

    while (viz.isWindowOpen()) {
        viz.computeStep();
        viz.renderFrame();
    }

    viz.cleanup();
    return 0;
}
