#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>

#include <GLES2/gl2.h>
#include <emscripten.h>
#include <emscripten/html5.h>

namespace {

struct Mat4 {
  std::array<float, 16> m;
};

Mat4 identity() {
  return {{{1.f, 0.f, 0.f, 0.f}, {0.f, 1.f, 0.f, 0.f}, {0.f, 0.f, 1.f, 0.f}, {0.f, 0.f, 0.f, 1.f}}};
}

Mat4 multiply(const Mat4& a, const Mat4& b) {
  Mat4 result = {};
  for (int col = 0; col < 4; ++col) {
    for (int row = 0; row < 4; ++row) {
      float sum = 0.0f;
      for (int i = 0; i < 4; ++i) {
        sum += a.m[i * 4 + row] * b.m[col * 4 + i];
      }
      result.m[col * 4 + row] = sum;
    }
  }
  return result;
}

Mat4 perspective(float fov_radians, float aspect, float near, float far) {
  const float f = 1.0f / std::tan(fov_radians * 0.5f);
  Mat4 result = {};
  result.m[0] = f / aspect;
  result.m[5] = f;
  result.m[10] = (far + near) / (near - far);
  result.m[11] = -1.0f;
  result.m[14] = (2.0f * far * near) / (near - far);
  return result;
}

Mat4 translate(float x, float y, float z) {
  Mat4 result = identity();
  result.m[12] = x;
  result.m[13] = y;
  result.m[14] = z;
  return result;
}

Mat4 rotate_y(float radians) {
  Mat4 result = identity();
  const float c = std::cos(radians);
  const float s = std::sin(radians);
  result.m[0] = c;
  result.m[2] = -s;
  result.m[8] = s;
  result.m[10] = c;
  return result;
}

Mat4 rotate_x(float radians) {
  Mat4 result = identity();
  const float c = std::cos(radians);
  const float s = std::sin(radians);
  result.m[5] = c;
  result.m[6] = s;
  result.m[9] = -s;
  result.m[10] = c;
  return result;
}

GLuint compile_shader(GLenum type, const char* source) {
  GLuint shader = glCreateShader(type);
  glShaderSource(shader, 1, &source, nullptr);
  glCompileShader(shader);

  GLint success = GL_FALSE;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
  if (success != GL_TRUE) {
    char log[512];
    glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
    std::printf("Shader compilation error: %s\n", log);
  }
  return shader;
}

GLuint create_program(const char* vertex_source, const char* fragment_source) {
  GLuint vertex = compile_shader(GL_VERTEX_SHADER, vertex_source);
  GLuint fragment = compile_shader(GL_FRAGMENT_SHADER, fragment_source);

  GLuint program = glCreateProgram();
  glAttachShader(program, vertex);
  glAttachShader(program, fragment);
  glLinkProgram(program);

  GLint success = GL_FALSE;
  glGetProgramiv(program, GL_LINK_STATUS, &success);
  if (success != GL_TRUE) {
    char log[512];
    glGetProgramInfoLog(program, sizeof(log), nullptr, log);
    std::printf("Program link error: %s\n", log);
  }

  glDeleteShader(vertex);
  glDeleteShader(fragment);
  return program;
}

struct Renderer {
  EMSCRIPTEN_WEBGL_CONTEXT_HANDLE context = 0;
  GLuint program = 0;
  GLuint vertex_buffer = 0;
  GLuint index_buffer = 0;
  GLint u_mvp = -1;
  GLint a_position = -1;
  GLint a_color = -1;
  int width = 1;
  int height = 1;
  double start_time_ms = 0.0;
};

Renderer g_renderer;

constexpr char kVertexShader[] = R"(
attribute vec3 a_position;
attribute vec3 a_color;
varying vec3 v_color;
uniform mat4 u_mvp;

void main() {
  v_color = a_color;
  gl_Position = u_mvp * vec4(a_position, 1.0);
}
)";

constexpr char kFragmentShader[] = R"(
precision mediump float;
varying vec3 v_color;

void main() {
  gl_FragColor = vec4(v_color, 1.0);
}
)";

constexpr float kVertices[] = {
    // x, y, z, r, g, b
    -0.5f, -0.5f, 0.5f, 1.f, 0.f, 0.f,   0.5f, -0.5f, 0.5f, 0.f, 1.f, 0.f,
    0.5f,  0.5f,  0.5f, 0.f, 0.f, 1.f,   -0.5f, 0.5f,  0.5f, 1.f, 1.f, 0.f,
    -0.5f, -0.5f, -0.5f, 1.f, 0.f, 1.f,  0.5f, -0.5f, -0.5f, 0.f, 1.f, 1.f,
    0.5f,  0.5f,  -0.5f, 1.f, 0.5f, 0.f, -0.5f, 0.5f,  -0.5f, 0.2f, 0.8f, 0.4f,
};

constexpr unsigned short kIndices[] = {
    0, 1, 2, 2, 3, 0,  // Front
    1, 5, 6, 6, 2, 1,  // Right
    5, 4, 7, 7, 6, 5,  // Back
    4, 0, 3, 3, 7, 4,  // Left
    3, 2, 6, 6, 7, 3,  // Top
    4, 5, 1, 1, 0, 4   // Bottom
};

void sync_canvas_size() {
  double css_width = 0.0;
  double css_height = 0.0;
  emscripten_get_element_css_size("#canvas", &css_width, &css_height);

  int target_width = static_cast<int>(std::round(css_width));
  int target_height = static_cast<int>(std::round(css_height));

  if (target_width <= 0 || target_height <= 0) {
    return;
  }

  if (target_width != g_renderer.width || target_height != g_renderer.height) {
    g_renderer.width = target_width;
    g_renderer.height = target_height;
    emscripten_set_canvas_element_size("#canvas", target_width, target_height);
    glViewport(0, 0, target_width, target_height);
  }
}

void render_frame() {
  sync_canvas_size();

  const double elapsed_s = (emscripten_get_now() - g_renderer.start_time_ms) / 1000.0;

  glEnable(GL_DEPTH_TEST);
  glClearColor(0.05f, 0.09f, 0.15f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  const float aspect = static_cast<float>(g_renderer.width) / static_cast<float>(g_renderer.height);
  const Mat4 projection = perspective(1.1f, aspect, 0.1f, 100.0f);
  const Mat4 model = multiply(rotate_y(static_cast<float>(elapsed_s)),
                              rotate_x(static_cast<float>(elapsed_s * 0.6)));
  const Mat4 view = translate(0.0f, 0.0f, -2.3f);
  const Mat4 mvp = multiply(projection, multiply(view, model));

  glUseProgram(g_renderer.program);
  glUniformMatrix4fv(g_renderer.u_mvp, 1, GL_FALSE, mvp.m.data());

  glBindBuffer(GL_ARRAY_BUFFER, g_renderer.vertex_buffer);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_renderer.index_buffer);

  glEnableVertexAttribArray(g_renderer.a_position);
  glVertexAttribPointer(g_renderer.a_position, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                        reinterpret_cast<void*>(0));

  glEnableVertexAttribArray(g_renderer.a_color);
  glVertexAttribPointer(g_renderer.a_color, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                        reinterpret_cast<void*>(3 * sizeof(float)));

  glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(std::size(kIndices)), GL_UNSIGNED_SHORT, nullptr);
}

}  // namespace

int main() {
  EmscriptenWebGLContextAttributes attrs;
  emscripten_webgl_init_context_attributes(&attrs);
  attrs.alpha = EM_FALSE;
  attrs.depth = EM_TRUE;
  attrs.stencil = EM_FALSE;
  attrs.antialias = EM_TRUE;
  attrs.majorVersion = 2;

  g_renderer.context = emscripten_webgl_create_context("#canvas", &attrs);
  if (g_renderer.context <= 0) {
    std::printf("Failed to create WebGL context\n");
    return 1;
  }

  emscripten_webgl_make_context_current(g_renderer.context);

  g_renderer.program = create_program(kVertexShader, kFragmentShader);
  g_renderer.u_mvp = glGetUniformLocation(g_renderer.program, "u_mvp");
  g_renderer.a_position = glGetAttribLocation(g_renderer.program, "a_position");
  g_renderer.a_color = glGetAttribLocation(g_renderer.program, "a_color");

  glGenBuffers(1, &g_renderer.vertex_buffer);
  glBindBuffer(GL_ARRAY_BUFFER, g_renderer.vertex_buffer);
  glBufferData(GL_ARRAY_BUFFER, sizeof(kVertices), kVertices, GL_STATIC_DRAW);

  glGenBuffers(1, &g_renderer.index_buffer);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_renderer.index_buffer);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(kIndices), kIndices, GL_STATIC_DRAW);

  g_renderer.start_time_ms = emscripten_get_now();

  emscripten_set_main_loop(render_frame, 0, EM_TRUE);
  return 0;
}
