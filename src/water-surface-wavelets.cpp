#include <cstdlib>

#if defined(WIN32)
#pragma warning(disable:4996)
#include <GL/glut.h>
#include <GL/freeglut.h>
#ifdef NDEBUG
#pragma comment(linker, "/subsystem:\"windows\" /entry:\"mainCRTStartup\"")
#endif // NDEBUG

#define _CRTDBG_MAP_ALLOC
#include <cstdlib>
#include <crtdbg.h>

#elif defined(__APPLE__) || defined(MACOSX)
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#include <GLUT/glut.h>
#else // MACOSX
#include <GL/glut.h>
#include <GL/freeglut.h>
#endif // unix

#define GLM_FORCE_SWIZZLE
#include "glm/glm.hpp"
#include "glm/gtx/transform.hpp"
#include "glm/gtc/quaternion.hpp"
#include "glm/gtx/quaternion.hpp"
#include "glm/ext/quaternion_geometric.hpp"
//#include "glm/ext/quaternion_trigonometric.hpp"
#include "glm/gtc/type_ptr.hpp"
#include "glm/gtx/norm.hpp"
//#include "glm/ext.hpp"
#include "glm/gtx/string_cast.hpp"
#include "glm/gtx/closest_point.hpp"
#include "glm/gtx/intersect.hpp"
#include "imgui.h"
#include "imgui_impl_glut.h"
#include "imgui_impl_opengl2.h"
#include <vector>
#include <iostream>
#include <cstdint>
#include <algorithm>
#include <cfloat>
#include <array>
#include <fstream>

#if !(USE_DOUBLE)
typedef float     Float;
typedef glm::vec3 Vec3;
typedef glm::quat Quat;
typedef glm::mat3 Mat3;
typedef glm::mat4 Mat4;
#else
typedef double       Float;
typedef glm::f64vec3 Vec3;
typedef glm::dquat   Quat;
typedef glm::dmat3   Mat3;
typedef glm::dmat4   Mat4;
#endif

#define USE_TEST_SCENE (1)
#define USE_CAPTURE    (0)

namespace {
  Float     fixed_dt          = (Float)(1.0 / 60.0);
  glm::vec3 cam_clamp_pos_min = glm::vec3(-3.0f, +0.0f, -5.5f);
  glm::vec3 cam_clamp_pos_max = glm::vec3(+3.0f, +5.5f, 15.0f);
  glm::vec3 cam_clamp_tgt_min = glm::vec3(-3.0f, -5.5f, +0.0f);
  glm::vec3 cam_clamp_tgt_max = glm::vec3(+3.0f, +5.5f, +0.0f);
};

struct DebugInfo {
  bool    show_depth;
  GLfloat dof;
  GLfloat focus;
  DebugInfo() : show_depth(false),  dof(0.1f), focus(0.0f) {}
};

struct Camera {
  glm::vec3  pos;
  glm::vec3  tgt;
  bool       is_zoom;
  bool       is_move;
  glm::ivec2 zoom_start;
  glm::ivec2 move_start;
  Camera() :
#if USE_TEST_SCENE
    pos(0.0f, 1.6f, 15.0f),
    tgt(0.0f, 3.0f, 0.0f),
#endif
     is_zoom(false), is_move(false), zoom_start(), move_start() {
  }
  void handle_zoom(float d) {
    float scale = 1.0f - 0.03f * d;
    float dist = glm::length(tgt - pos);
    if ((d > (Float)0.0 && dist < 0.2f) || (d < (Float)0.0 && dist > 5.0f)) {
      return;
    }
    pos = tgt + (pos - tgt) * scale;
  }
  void handle_motion(int dx, int dy) {
    float scale = 0.01f;
    float prev = glm::length(tgt - pos);
    glm::vec3 dir_z = tgt - pos;
    glm::vec3 dir_x(dir_z.z, 0.0f, -dir_z.x); 
    dir_x = glm::normalize(dir_x);
    glm::vec3 dir_y = glm::cross(dir_z, dir_x);
    dir_y = glm::normalize(dir_y);
    pos += dir_x * scale * (float)dx;
    pos += dir_y * scale * (float)dy;
    dir_z = tgt - pos;
    dir_z = glm::normalize(dir_z);
    float delta = glm::length(tgt - pos) - prev;
    pos += dir_z * -delta;
  }
  void clamp() {
    pos.x = glm::clamp(pos.x, (float)cam_clamp_pos_min.x, (float)cam_clamp_pos_max.x);
    pos.y = glm::clamp(pos.y, (float)cam_clamp_pos_min.y, (float)cam_clamp_pos_max.y);
    pos.z = glm::clamp(pos.z, (float)cam_clamp_pos_min.z, (float)cam_clamp_pos_max.z);
    tgt.x = glm::clamp(tgt.x, (float)cam_clamp_tgt_min.x, (float)cam_clamp_tgt_max.x);
    tgt.y = glm::clamp(tgt.y, (float)cam_clamp_tgt_min.y, (float)cam_clamp_tgt_max.y);
  }
  void reset() {
#if USE_TEST_SCENE
    pos = glm::vec3(0.0f, 1.6f, 15.0f);
    tgt = glm::vec3(0.0f, 3.0f, 0.0f);
#else
    pos = glm::vec3(0.0f, 1.0f, 4.0f);
    tgt = glm::vec3(0.0f, 1.0f, 0.0f);
#endif
  }
};

struct Params {
  int     substeps;
  bool    show_tets;
  bool    show_tri;
  bool    use_friction;
  float   friction;
  float   dev_comp_ratio;
  float   vol_comp_ratio;
  Params() : substeps(5), show_tets(false), show_tri(true), use_friction(true), friction(1000.0f), dev_comp_ratio(0.0095047523761881f), vol_comp_ratio(0.0f) {}
};

struct SPlane {
  glm::vec4 v;
};

struct Light {
  glm::vec4 v;
};

struct Shadow {
  float v[4][4];
};

struct Context;

class Scene {
public:
  enum {
    eDefault,
    eNum,
  };
  virtual void Update(Context& context, Float dt) = 0;
  virtual void Render(float alpha = 1.0f) = 0;
  virtual ~Scene() {}
};

struct Material {
  GLfloat ambient[4];
  GLfloat diffuse[4];
  GLfloat specular[4];
  GLfloat shininess;
};

// gold
Material mat_gold = {
  { 0.24725f,  0.1995f,   0.0745f,    1.0f },
  { 0.75164f,  0.60648f,  0.22648f,   1.0f },
  { 0.628281f, 0.555802f, 0.366065f,  1.0f },
  51.2f,
};

struct Context {
  std::string   in_fname;
  std::uint32_t frame;
  float         time_sum;
  DebugInfo     debug_info;
  Scene*        scene;
  std::int32_t  scene_num;
  Material      material;
  Camera        camera;
  bool          paused;
  Params        params;
  SPlane        floor;
  Light         light;
  Shadow        floor_shadow;
  GLint         window_w;
  GLint         window_h;
  GLint         vp[4];
  GLdouble      modelview_mtx[16];
  GLdouble      proj_mtx[16];
  Context() : in_fname(), frame(0), time_sum(0.0f), debug_info(), scene(nullptr), scene_num(Scene::eDefault), material(mat_gold), camera(), paused(false), params(), floor(), light(), floor_shadow(), window_w(0), window_h(0), vp(), modelview_mtx(), proj_mtx() {}
};

Context g_Context;

void calc_world_coord(glm::f64vec3* w, int x, int y, double z) {
  GLint realy = g_Context.vp[3] - (GLint)y - 1;
//  printf ("Coordinates at cursor are (%4d, %4d)\n", x, realy);
  gluUnProject((GLdouble)x, (GLdouble)realy, z,
                g_Context.modelview_mtx, g_Context.proj_mtx, g_Context.vp, &w->x, &w->y, &w->z);
}

// emerald
Material mat_emerald = {
  { 0.0215f,  0.1745f,   0.0215f,  1.0f },
  { 0.07568f, 0.61424f,  0.07568f, 1.0f },
  { 0.633f,   0.727811f, 0.633f,   1.0f },
  76.8f
};

// jade
Material mat_jade = {
  { 0.135f,     0.2225f,   0.1575f,   1.0f },
  { 0.54f,      0.89f,     0.63f,     1.0f },
  { 0.316228f,  0.316228f, 0.316228f, 1.0f },
  12.8f
};

// obsidian
Material mat_obsidian = {
  { 0.05375f, 0.05f,      0.06625f,  1.0f },
  { 0.18275f, 0.17f,      0.22525f,  1.0f },
  { 0.332741f, 0.328634f, 0.346435f, 1.0f },
  38.4f
};

// pearl
Material mat_pearl = {
  { 0.25f,     0.20725f,  0.20725f,  1.0f },
  { 1.0f,      0.829f,    0.829f,    1.0f },
  { 0.296648f, 0.296648f, 0.296648f, 1.0f },
  10.24f
};

// ruby
Material mat_ruby  = {
  { 0.1745f,   0.01175f,  0.01175f,  1.0f },
  { 0.61424f,  0.04136f,  0.04136f,  1.0f },
  { 0.727811f, 0.626959f, 0.626959f, 1.0f },
  76.8f
};

// turquoise
Material mat_turquoise = {
  { 0.1f,      0.18725f, 0.1745f,   1.0f },
  { 0.396f,    0.74151f, 0.69102f,  1.0f },
  { 0.297254f, 0.30829f, 0.306678f, 1.0f },
  12.8f,
};

// brass
Material mat_brass = {
  { 0.329412f,  0.223529f, 0.027451f, 1.0f },
  { 0.780392f,  0.568627f, 0.113725f, 1.0f },
  { 0.992157f,  0.941176f, 0.807843f, 1.0f },
  27.89743616f,
};

// bronze
Material mat_bronze = {
  { 0.2125f,   0.1275f,   0.054f,    1.0f },
  { 0.714f,    0.4284f,   0.18144f,  1.0f },
  { 0.393548f, 0.271906f, 0.166721f, 1.0f },
  25.6f,
};

// chrome
Material mat_chrome = {
  { 0.25f,     0.25f,     0.25f,     1.0f },
  { 0.4f,      0.4f,      0.4f,      1.0f },
  { 0.774597f, 0.774597f, 0.774597f, 1.0f },
  76.8f,
};

// copper
Material mat_copper = {
  { 0.19125f,  0.0735f,   0.0225f,   1.0f },
  { 0.7038f,   0.27048f,  0.0828f,   1.0f },
  { 0.256777f, 0.137622f, 0.086014f, 1.0f },
  12.8f,
};

// silver
Material mat_silver = {
  { 0.19225f,  0.19225f,  0.19225f,  1.0f },
  { 0.50754f,  0.50754f,  0.50754f,  1.0f },
  { 0.508273f, 0.508273f, 0.508273f, 1.0f },
  51.2f,
};

// plastic(black)
Material mat_plastic_black = {
  { 0.0f,  0.0f,  0.0f,  1.0f },
  { 0.01f, 0.01f, 0.01f, 1.0f },
  { 0.50f, 0.50f, 0.50f, 1.0f },
  32.0f,
};

// plastic(cyan)
Material mat_plastic_cyan = {
  { 0.0f, 0.1f,        0.06f,    1.0f },
  { 0.0f, 0.50980392f, 0.50980392f, 1.0f },
  { 0.50196078f, 0.50196078f, 0.50196078f, 1.0f },
  32.0f,
};

// rubber(black)
Material mat_rubbr_black = {
  { 0.02f, 0.02f, 0.02f, 1.0f },
  { 0.01f, 0.01f, 0.01f, 1.0f },
  { 0.4f,  0.4f,  0.4f,  1.0f },
  10.0f,
};

// rubber(red)
Material mat_rubbr_red = {
  { 0.05f, 0.0f,  0.0f,  1.0f },
  { 0.5f,  0.4f,  0.4f,  1.0f },
  { 0.7f,  0.04f, 0.04f, 1.0f },
  10.0f,
};

// white
Material mat_white = {
  { 1.0f, 1.0f, 1.0f, 1.0f },
  { 1.0f, 1.0f, 1.0f, 1.0f },
  { 1.0f, 1.0f, 1.0f, 1.0f },
  32.0f,
};

void write_ppm(GLubyte* buff, GLenum format) {
  int w = glutGet(GLUT_WINDOW_WIDTH);
  int h = glutGet(GLUT_WINDOW_HEIGHT);
  int  pix_sz = (format == GL_RGBA) ? 4 : 1;
  char suffix[256];
  sprintf(suffix, (format == GL_RGBA) ? "screen.ppm" : "depth.ppm");
  char filename[1024];
  sprintf(filename, "%08d_%s", g_Context.frame, suffix);
  FILE *fp = fopen(filename, "wb");
  if (fp) {
    fprintf(fp, "P%d\n", (format == GL_RGBA) ? 6 : 5); // 5:Portable graymap(Binary), 6:Portable pixmap(Binary)
    fprintf(fp, "%u %u\n", w, h);
    fprintf(fp, "255\n");
    for(int y = 0; y < h; y++) {
      for(int x = 0; x < w; x++) {
        int index = (h - y - 1) * w * pix_sz + (x * pix_sz);
        if (format == GL_RGBA) {
          int r = buff[index];
          int g = buff[index + 1];
          int b = buff[index + 2];
          int a = buff[index + 3]; // not use here
          putc(r, fp); // binary
          putc(g, fp);
          putc(b, fp);
        } else {
          putc(buff[index], fp);
        }
      }
    }
    fclose(fp);
  }
}

void write_image(GLenum format) {
  GLsizei w = glutGet(GLUT_WINDOW_WIDTH);
  GLsizei h = glutGet(GLUT_WINDOW_HEIGHT);
  GLubyte* buff = (GLubyte*)malloc((size_t)w * (size_t)h * 4); // w x h * RGBA
  glReadBuffer(/*GL_FRONT*/GL_BACK);
  glReadPixels(0, 0, w, h, format, GL_UNSIGNED_BYTE, buff);
  write_ppm(buff, format);
  free(buff);
}

void render_string(std::string& str, int w, int h, GLfloat x0, GLfloat y0) {
  glDisable(GL_LIGHTING);
  glMatrixMode(GL_PROJECTION);
  glPushMatrix();
  glLoadIdentity();
  gluOrtho2D(0, w, h, 0);
  glMatrixMode(GL_MODELVIEW);
  glPushMatrix();
  glLoadIdentity();
  glRasterPos2f(x0, y0);
  int size = (int)str.size();
  for(int i = 0; i < size; ++i){
    glutBitmapCharacter(GLUT_BITMAP_8_BY_13, str[i]);
  }
  glPopMatrix();
  glMatrixMode(GL_PROJECTION);
  glPopMatrix();
  glMatrixMode(GL_MODELVIEW);
}

void render_pipe(GLfloat width, GLfloat length, int slice, GLfloat color[]) {
  glMaterialfv(GL_FRONT, GL_AMBIENT_AND_DIFFUSE, color);
  GLUquadricObj* q;
  q = gluNewQuadric();
  gluQuadricDrawStyle(q, GLU_FILL);
  gluQuadricNormals(q, GLU_SMOOTH);
  gluCylinder(q, width, width, length, slice, 1); // quad, base, top, height, slice, stacks
  glPushMatrix();
    glTranslatef(0.0f, 0.0f, length);
    gluDisk(q, 0.0f, width, slice, 1); // quad, inner, outer, slices, loops(top)
  glPopMatrix();
  gluQuadricOrientation(q, GLU_INSIDE);
  gluDisk(q, 0.0f, width, slice, 1); // quad, inner, outer, slices, loops(bottom)
  gluDeleteQuadric(q); 
}

void render_pipe(const glm::vec3& start, const glm::vec3& end, GLfloat width, int slice, GLfloat color[]) {
  glm::vec3 vec    = end - start;
  GLfloat   length = glm::length(vec);
  GLfloat   ax;
  if (fabs(vec.z) < FLT_EPSILON) {
    ax = 57.2957795f * acos( vec.x / length ); // rotation angle in x-y plane
    if (vec.y <= 0.0f)
      ax = -ax;
  } else {
    ax = 57.2957795f * acos( vec.z / length ); // rotation angle
    if (vec.z <= 0.0f)
      ax = -ax;
  }
  GLfloat rx = -vec.y * vec.z;
  GLfloat ry =  vec.x * vec.z;
  glPushMatrix();
    glTranslatef(start.x, start.y, start.z);
    if (fabs(vec.z) < FLT_EPSILON) {
      glRotatef(90.0f,  0.0f, 1.0f, 0.0f); // Rotate & align with x axis
      glRotatef(   ax, -1.0f, 0.0f, 0.0f); // Rotate to point 2 in x-y plane
    } else {
      glRotatef(   ax,   rx,    ry, 0.0f); // Rotate about rotation vector
    }
    render_pipe(width, length, slice, color);
  glPopMatrix();
}

void render_arrow(const glm::vec3& start, const glm::vec3& end, GLfloat width, int slice, GLfloat height, GLfloat color[]) {
  render_pipe(start, end, width, slice, color);
  glm::vec3 vec        = end - start;
  float     vec_length = glm::length(vec);
  if (vec_length > FLT_MIN) {
    static const glm::vec3 init(0.0f, 0.0f, 1.0f); // glutSolidCone() +z
    glm::vec3 normalized_vec = glm::normalize(vec);
    glm::vec3 diff = normalized_vec - init;
    if (glm::length(diff) > FLT_MIN) {
      glm::vec3 rot_axis  = glm::normalize(glm::cross(init, normalized_vec));
      float     rot_angle = std::acos(glm::dot(init, normalized_vec)) * 57.295f; // 360.0f / (2.0f * 3.14f)
      glm::vec3 cone_pos = end;
      if (vec_length > height){
        cone_pos = start + (vec_length - height) * normalized_vec; // offset cone height
        glPushMatrix();
          glTranslatef(cone_pos.x, cone_pos.y, cone_pos.z);
          glRotatef(rot_angle, rot_axis.x, rot_axis.y, rot_axis.z);
          glutSolidCone(height * 0.25, height, 4, 4); // base, height, slices, stacks
        glPopMatrix();
      }
    }
  }
}

void render_sphere(const glm::vec3& pos, float radius, int slices, GLfloat color[]) {
  glMaterialfv(GL_FRONT, GL_AMBIENT_AND_DIFFUSE, color);
  glPushMatrix();
    glTranslatef(pos.x, pos.y, pos.z);
    glutSolidSphere(radius, slices, slices); // radius, slices, stacks
  glPopMatrix();
}

void set_material(const Material& mat) {
  glMaterialfv(GL_FRONT, GL_AMBIENT,   mat.ambient);
  glMaterialfv(GL_FRONT, GL_DIFFUSE,   mat.diffuse);
  glMaterialfv(GL_FRONT, GL_SPECULAR,  mat.specular);
  glMaterialfv(GL_FRONT, GL_SHININESS, &mat.shininess);
}

void set_material(const Material& in_mat, float alpha) {
  Material mat = in_mat;
  mat.ambient[3]   = alpha;
  mat.diffuse[3]   = alpha;
  mat.specular[3]  = alpha;
  set_material(mat);
}

void render_sphere(const glm::vec3& pos, float radius, int slices) {
  glPushMatrix();
    glTranslatef(pos.x, pos.y, pos.z);
    glutSolidSphere(radius, slices, slices); // radius, slices, stacks
  glPopMatrix();
}

void render_floor(GLfloat w, GLfloat d, int num_w, int num_d) {
  static const GLfloat color[][4] = { { 0.6f, 0.6f, 0.6f, 1.0f },   // white
                                      { 0.3f, 0.3f, 0.3f, 1.0f } }; // gray
  GLfloat center_w = (w * num_w) / 2.0f;
  GLfloat center_d = (d * num_d) / 2.0f;
  glPushMatrix();
    glNormal3f(0.0f, 1.0f, 0.0f); // up vector
    glBegin(GL_QUADS);
    for (int j = 0; j < num_d; ++j) {
      GLfloat dj  = d  * j;
      GLfloat djd = dj + d;
      for (int i = 0; i < num_w; ++i) {
        GLfloat wi  = w  * i;
        GLfloat wiw = wi + w;
        glMaterialfv(GL_FRONT, GL_AMBIENT_AND_DIFFUSE, color[(i + j) & 1]);
        glVertex3f(wi  - center_w,  0.0, dj  - center_d);
        glVertex3f(wi  - center_w,  0.0, djd - center_d);
        glVertex3f(wiw - center_w,  0.0, djd - center_d);
        glVertex3f(wiw - center_w,  0.0, dj  - center_d);
      }
    }
    glEnd();
  glPopMatrix();
}

void init_imgui() {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::StyleColorsDark();
  ImGui_ImplGLUT_Init();
  ImGui_ImplGLUT_InstallFuncs();
  ImGui_ImplOpenGL2_Init();
}

void finalize_imgui() {
  ImGui_ImplOpenGL2_Shutdown();
  ImGui_ImplGLUT_Shutdown();
  ImGui::DestroyContext();
}

void finalize(void) {
  Context& ctx = g_Context;
  if (ctx.scene) {
      delete ctx.scene;
      ctx.scene = nullptr;
  }
  finalize_imgui();
  return;
}

void find_plane(SPlane* plane, const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2) {
  glm::vec3 vec0 = v1 - v0;
  glm::vec3 vec1 = v2 - v0;
  plane->v[0] =   vec0.y * vec1.z - vec0.z * vec1.y;
  plane->v[1] = -(vec0.x * vec1.z - vec0.z * vec1.x);
  plane->v[2] =   vec0.x * vec1.y - vec0.y * vec1.x;
  plane->v[3] = -(plane->v[0] * v0.x + plane->v[1] * v0.y + plane->v[2] * v0.z);
}

void calc_shadow_matrix(Shadow* shadow, const SPlane& plane, const Light& light) {
  GLfloat dot = glm::dot(plane.v, light.v);
  shadow->v[0][0] = dot - light.v[0] * plane.v[0];
  shadow->v[1][0] = 0.f - light.v[0] * plane.v[1];
  shadow->v[2][0] = 0.f - light.v[0] * plane.v[2];
  shadow->v[3][0] = 0.f - light.v[0] * plane.v[3];

  shadow->v[0][1] = 0.f - light.v[1] * plane.v[0];
  shadow->v[1][1] = dot - light.v[1] * plane.v[1];
  shadow->v[2][1] = 0.f - light.v[1] * plane.v[2];
  shadow->v[3][1] = 0.f - light.v[1] * plane.v[3];

  shadow->v[0][2] = 0.f - light.v[2] * plane.v[0];
  shadow->v[1][2] = 0.f - light.v[2] * plane.v[1];
  shadow->v[2][2] = dot - light.v[2] * plane.v[2];
  shadow->v[3][2] = 0.f - light.v[2] * plane.v[3];

  shadow->v[0][3] = 0.f - light.v[3] * plane.v[0];
  shadow->v[1][3] = 0.f - light.v[3] * plane.v[1];
  shadow->v[2][3] = 0.f - light.v[3] * plane.v[2];
  shadow->v[3][3] = dot - light.v[3] * plane.v[3];
}

class SceneDefault : public Scene {
public:
  SceneDefault() {
  }
  ~SceneDefault() {
  }
  virtual void Update(Context& ctx, Float dt) {
    float start_time = (float)glutGet(GLUT_ELAPSED_TIME);
    Float sdt = dt / (Float)ctx.params.substeps;
  }
  virtual void Render(float alpha = 1.0f) {
    auto& ctx = g_Context;
  }
};
void initialize(int argc, char* argv[]) {
#if defined(WIN32)
  _CrtSetDbgFlag( _CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF );
#endif
  Context& ctx = g_Context;
  if (argc > 1) {
    ctx.in_fname = std::string(argv[1]);
  }
  glClearColor(0.2f, 0.2f, 0.2f, 1.0f);
  glClearAccum(0.0f, 0.0f, 0.0f, 0.0f); 
  glClearDepth(1.0);
  glDepthFunc(GL_LESS);
  glEnable(GL_CULL_FACE);
  glCullFace(GL_BACK);
  glEnable(GL_DEPTH_TEST);

  glEnable(GL_LIGHTING);
  glEnable(GL_LIGHT0);
  //glEnable(GL_LIGHT1);

  GLfloat ambient[] = { 0.0, 0.0, 0.0, 1.0 };
  GLfloat diffuse[] = { 1.0, 1.0, 1.0, 1.0 };
  GLfloat specular[] = { 1.0, 1.0, 1.0, 1.0 };
  GLfloat lmodel_ambient[] = { 0.2f, 0.2f, 0.2f, 1.0f };
  GLfloat local_view[] = { 0.0 };

  glLightfv(GL_LIGHT1, GL_AMBIENT, ambient);
  glLightfv(GL_LIGHT1, GL_DIFFUSE, diffuse);
  glLightfv(GL_LIGHT1, GL_DIFFUSE, specular);

  glLightModelfv(GL_LIGHT_MODEL_AMBIENT, lmodel_ambient);
  glLightModelfv(GL_LIGHT_MODEL_LOCAL_VIEWER, local_view);

//  GLfloat light0pos[] = { 0.0, 5.0, 10.0, 1.0 };
  GLfloat light0pos[] = { 15.0, 15.0, 15.0, 1.0 };
//  static const GLfloat light1pos[] = { 5.0, 3.0, 0.0, 1.0 };
  glLightfv(GL_LIGHT0, GL_POSITION, light0pos);
  ctx.light.v[0] = light0pos[0];
  ctx.light.v[1] = light0pos[1];
  ctx.light.v[2] = light0pos[2];
  ctx.light.v[3] = light0pos[3];

  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glEnable(GL_BLEND);
  glEnable(GL_POINT_SMOOTH);
  glEnable(GL_LINE_SMOOTH);
  //glEnable(GL_POLYGON_SMOOTH);
  glHint(GL_POINT_SMOOTH_HINT, GL_NICEST);
  glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
  //glHint(GL_POLYGON_SMOOTH_HINT, GL_NICEST);
  glEnable(GL_AUTO_NORMAL);
  glEnable(GL_NORMALIZE);

  init_imgui();
  atexit(finalize);

  glm::vec3 v0(-1.0f, 0.0f,  0.0f);
  glm::vec3 v1(+1.0f, 0.0f,  0.0f);
  glm::vec3 v2(+1.0f, 0.0f, -1.0f);
  find_plane(&ctx.floor, v0, v1, v2);
  std::srand((unsigned)time( NULL ));
}

void restart() {
  if (g_Context.scene) {
    delete g_Context.scene;
    g_Context.scene = nullptr;
  }
}

void time_step(GLfloat time);

void display_imgui() {
  auto& ctx = g_Context;
  ImGui_ImplOpenGL2_NewFrame();
  ImGui_ImplGLUT_NewFrame();

  {
    ImGui::SetNextWindowPos(ImVec2(  10,  10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(270, 130), ImGuiCond_FirstUseEver);
    ImGui::Begin("Debug");
    ImGui::Checkbox("Show Depth",   &ctx.debug_info.show_depth);
    ImGui::SliderFloat("DoF",       &ctx.debug_info.dof,     0.0f,  0.2f);
    ImGui::SliderFloat("focus",     &ctx.debug_info.focus, - 5.0f,  3.5f);
    ImGui::End();
 
    ImGui::Begin("Params");
    if (ImGui::Button("Restart")) {
      restart();
    }
    ImGui::SameLine();
    if (ImGui::Button("Step")) {
      ctx.paused = true;
      time_step((float)glutGet(GLUT_ELAPSED_TIME) / 1000.0f);
    }
    ImGui::SameLine();
    if (ImGui::Button("Run")) {
      ctx.paused = false;
    }
    ImGui::End();
    ImGui::Begin("Camera");
    if (ImGui::Button("Reset")) {
      ctx.camera.reset();
    }
    ImGui::SliderFloat("Pos X", &ctx.camera.pos.x, cam_clamp_pos_min.x, cam_clamp_pos_max.x);
    ImGui::SliderFloat("Pos Y", &ctx.camera.pos.y, cam_clamp_pos_min.y, cam_clamp_pos_max.y);
    ImGui::SliderFloat("Pos Z", &ctx.camera.pos.z, cam_clamp_pos_min.z, cam_clamp_pos_max.z);
    ImGui::SliderFloat("Tgt X", &ctx.camera.tgt.x, cam_clamp_tgt_min.x, cam_clamp_tgt_max.x);
    ImGui::SliderFloat("Tgt Y", &ctx.camera.tgt.y, cam_clamp_tgt_min.y, cam_clamp_tgt_max.y);
    ImGui::End();
  }

  ImGui::Render();
  ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());
}

void display_axis() {
  glEnable(GL_LIGHTING);
  glEnable(GL_DEPTH_TEST);
  glm::vec3 left( -5.0f, 0.1f, 0.0f);
  glm::vec3 right(+5.0f, 0.1f, 0.0f);
  glm::vec3 bottm( 0.0f, -3.0f, 0.0f);
  glm::vec3 top(   0.0f, +3.0f, 0.0f);
  glm::vec3 back(  0.0f, 0.1f, -3.0f);
  glm::vec3 front( 0.0f, 0.1f, +3.0f);
  GLfloat red[]   = { 0.8f, 0.0f, 0.0f, 1.0f };
  GLfloat green[] = { 0.0f, 0.8f, 0.0f, 1.0f };
  GLfloat blue[]  = { 0.0f, 0.0f, 0.8f, 1.0f };
  render_arrow(left,  right, 0.025f, 8, 0.3f, red);
  render_arrow(bottm, top,   0.025f, 8, 0.3f, green);
  render_arrow(back,  front, 0.025f, 8, 0.3f, blue);
}

void display_string() {
  auto& ctx = g_Context;
/*
  glColor3d(1.0f, 1.0f, 1.0f);
  char debug[128];

  for(auto& joint : ctx.joints) {
    glm::f64vec3 win;
    glm::f64vec3 obj(joint->global_pose1.p);
    gluProject(obj.x, obj.y, obj.z, g_Context.modelview_mtx, g_Context.proj_mtx,  g_Context.vp, &win.x, &win.y, &win.z);
    float realy = (float)g_Context.vp[3] - (float)win.y - 1.0f;
    sprintf(debug, "  f=%1.0fN", std::floor(-joint->force));
    std::string time_text(debug);
    render_string(time_text, glutGet(GLUT_WINDOW_WIDTH), glutGet(GLUT_WINDOW_HEIGHT), (float)win.x, realy);
  }
*/
}

void display_depth() {
  if (g_Context.debug_info.show_depth == false) { return; }
  GLint view[4];
  GLubyte *buffer;
  glGetIntegerv(GL_VIEWPORT, view);
  buffer = (GLubyte *)malloc(size_t(view[2]) * size_t(view[3]));
  glReadPixels(view[0], view[1], view[2], view[3], GL_DEPTH_COMPONENT, GL_UNSIGNED_BYTE, buffer);
  glDisable(GL_DEPTH_TEST);
    glDrawPixels(view[2], view[3], GL_LUMINANCE, GL_UNSIGNED_BYTE, buffer);
  glEnable(GL_DEPTH_TEST);
  free(buffer);
}

void display_actor(float alpha = 1.0f) {
  Material mat[] = {
    mat_emerald, mat_jade, mat_obsidian, mat_pearl, mat_ruby, mat_turquoise, mat_brass, mat_bronze
  };
  for(int i = 0; i < 8; i++) {
    set_material(mat[i], alpha);
    glPushMatrix();
      glTranslatef(-6.0f + 1.0f * (float)i, 0.4f, -3.0f + 2.0f * (float)i);
      float ang = (float)(g_Context.frame % 120) * 3.0f;
      glRotatef(ang, 0.0f, 1.0f, 0.0f);
      glutSolidTeapot(0.5f);
    glPopMatrix();
  }
}

void set_stencil_one_before() {
  glDisable(GL_DEPTH_TEST);
  glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE); // color mask = false
  glEnable(GL_STENCIL_TEST);
  glStencilOp(GL_REPLACE, GL_REPLACE, GL_REPLACE);
  glStencilFunc(GL_ALWAYS, 1, 0xffffffff);
}
void set_stencil_one_after() {
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);     // color mask = true
  glEnable(GL_DEPTH_TEST);
}
void set_stencil_if_one_before() {
  glStencilFunc(GL_EQUAL, 1, 0xffffffff); // draw if ==1
  glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
}
void set_stencil_if_one_after() {
  glDisable(GL_STENCIL_TEST);
}
void set_reflection_before() {
  glDisable(GL_DEPTH_TEST);
  glPushMatrix();
    glScalef(1.0f, -1.0f, 1.0f); // for reflection on plane(y=0.0f)
    glLightfv(GL_LIGHT0, GL_POSITION, &g_Context.light.v[0]);
}
void set_reflection_after() {
  glPopMatrix();
  glLightfv(GL_LIGHT0, GL_POSITION, &g_Context.light.v[0]);
}
void set_shadow_before() {
  glEnable(GL_POLYGON_OFFSET_FILL);
    calc_shadow_matrix(&g_Context.floor_shadow, g_Context.floor, g_Context.light);
    glDisable(GL_LIGHTING);        // force the 50% black
    glColor4f(0.0, 0.0, 0.0, 0.5f);
    glPushMatrix();
      glMultMatrixf((GLfloat*)g_Context.floor_shadow.v);
      glCullFace(GL_FRONT);
}
void set_shadow_after() {
      glCullFace(GL_BACK);
    glPopMatrix();
    glEnable(GL_LIGHTING);
  glDisable(GL_POLYGON_OFFSET_FILL);
  glEnable(GL_DEPTH_TEST);
}

void display(void){
  glClear(GL_ACCUM_BUFFER_BIT);
  int   num_accum = 8;
  struct jitter_point{ GLfloat x, y; };
  jitter_point j8[] = {
    {-0.334818f,  0.435331f},
    { 0.286438f, -0.393495f},
    { 0.459462f,  0.141540f},
    {-0.414498f, -0.192829f},
    {-0.183790f,  0.082102f},
    {-0.079263f, -0.317383f},
    { 0.102254f,  0.299133f},
    { 0.164216f, -0.054399f}
  };
  GLint viewport[4];
  glGetIntegerv (GL_VIEWPORT, viewport);
  for(int i = 0 ; i < num_accum; i++) {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    Context& ctx = g_Context;
    Vec3 pos = ctx.camera.pos;
    Float eye_jitter = (pos.z - ctx.debug_info.focus) / pos.z;
    eye_jitter = (eye_jitter < 0.1f) ? 0.1f : eye_jitter;
    pos.x += ctx.debug_info.dof * j8[i].x * eye_jitter;
    pos.y += ctx.debug_info.dof * j8[i].y * eye_jitter;
    Vec3 tgt = ctx.camera.tgt;
    Vec3 vec = tgt - pos;
    tgt.y = pos.y + vec.y * ((pos.z - ctx.debug_info.focus) / pos.z);
    tgt.z = ctx.debug_info.focus;
    gluLookAt(pos.x, pos.y, pos.z, tgt.x, tgt.y, tgt.z, 0.0, 1.0, 0.0); // pos, tgt, up
#if USE_TEST_SCENE
    display_axis();

    render_floor(1.0f, 1.0f, 24, 28);

    set_stencil_one_before();
      render_floor(1.0f, 1.0f, 24, 28);       // floor pixels just get their stencil set to 1.
    set_stencil_one_after();

    set_stencil_if_one_before();              // draw if stencil == 1

      set_reflection_before();
        display_actor(0.1f);                  // reflection
      set_reflection_after();

      set_shadow_before();
        display_actor();                      // shadow
      set_shadow_after();

    set_stencil_if_one_after();               // draw always

    glCullFace(GL_FRONT);                     // for teapot
      display_actor();                        // actual draw
    glCullFace(GL_BACK);
#endif
    glAccum(GL_ACCUM, 1.0f / num_accum);
  }
  glAccum(GL_RETURN, 1.0f);

  glGetDoublev(GL_MODELVIEW_MATRIX,  g_Context.modelview_mtx); // store current matrix
  glGetDoublev(GL_PROJECTION_MATRIX, g_Context.proj_mtx);

  display_string();
  display_depth();
  display_imgui();

  glutSwapBuffers();
  glutPostRedisplay();
}

void reshape_imgui(int width, int height) {
  ImGuiIO& io = ImGui::GetIO();
  io.DisplaySize.x = (float)width;
  io.DisplaySize.y = (float)height;
}

void reshape(int width, int height){
//  static const GLfloat light0pos[] = { 0.0, 5.0, 10.0, 1.0 };
//  static const GLfloat light1pos[] = { 5.0, 3.0, 0.0, 1.0 };
  glShadeModel(GL_SMOOTH);

  reshape_imgui(width, height);
  glViewport(0, 0, width, height);
  g_Context.window_w = width;
  g_Context.window_h = height;
  glGetIntegerv(GL_VIEWPORT, g_Context.vp);

  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  gluPerspective(70.0, (double)width / (double)height, 0.01f, 100.0f);
  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();

//  gluLookAt(0.0, 1.6, 15.0f, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0); // pos, tgt, up
//  glLightfv(GL_LIGHT0, GL_POSITION, light0pos);
//  glLightfv(GL_LIGHT1, GL_POSITION, light1pos);
}

void keyboard(unsigned char key, int x , int y){
  switch(key) {
  case 's': write_image(GL_RGBA); /*write_objects();*/ break;
  case 'd': write_image(GL_DEPTH_COMPONENT); break;
  case 'r': restart(); break;
  case 27: exit(0); break; // esc
  }
}

void special(int key, int x, int y) {
  auto& ctx = g_Context;
  int previous = ctx.scene_num;
  switch(key) {
  case GLUT_KEY_LEFT:  ctx.scene_num--; break;
  case GLUT_KEY_RIGHT: ctx.scene_num++; break;
  }
  ctx.scene_num = (ctx.scene_num <  0)           ? Scene::eDefault : ctx.scene_num;
  ctx.scene_num = (ctx.scene_num == Scene::eNum) ? Scene::eNum - 1 : ctx.scene_num;
  if (previous != ctx.scene_num) {
    if (ctx.scene){
      delete ctx.scene;
      ctx.scene = nullptr;
    }
  }
}

void time_step(GLfloat time) {
  auto& ctx  = g_Context;
  GLfloat dt = (GLfloat)fixed_dt;//time - ctx.time;
  ctx.scene->Update(ctx, dt);
  //ctx.time = time;
#if USE_CAPTURE
  keyboard('s', 0, 0); // screenshot
#endif
  while(1) {
    if (((float)glutGet(GLUT_ELAPSED_TIME) / 1000.0f - time) > fixed_dt) {
      break; // keep 60fps
    }
  }
  ctx.frame++;
}

void idle(void){
  GLfloat time = (float)glutGet(GLUT_ELAPSED_TIME) / 1000.0f;
  auto&   ctx  = g_Context;
  if (ctx.scene == nullptr) {
    switch(g_Context.scene_num) {
    case Scene::eDefault: ctx.scene = new SceneDefault(); break;
    }
  }
  if (ctx.paused == false) {
    time_step(time);
  }
}

void mouse_left_sub(int state, int x, int y) {
  Context& ctx = g_Context;
  switch(state) {
  case GLUT_DOWN: break;
  case GLUT_UP:   break;
  }
}

void mouse_middle_sub(int state, int x, int y) {
  Context& ctx = g_Context;
  ctx.camera.is_zoom = (state == GLUT_DOWN) ? true : false;
  if (ctx.camera.is_zoom) {
    ctx.camera.zoom_start.x = x;
    ctx.camera.zoom_start.y = y;
  }
}

void mouse_right_sub(int state, int x, int y) {
  Context& ctx = g_Context;
  ctx.camera.is_move = (state == GLUT_DOWN) ? true : false;
  if (ctx.camera.is_move) {
    ctx.camera.move_start.x = x;
    ctx.camera.move_start.y = y;
  }
}

void mouse(int button, int state, int x, int y ) {
  Context& ctx = g_Context;
  switch(button) { // only handle button here
  case GLUT_LEFT_BUTTON:   mouse_left_sub  (state, x, y); break;
  case GLUT_MIDDLE_BUTTON: mouse_middle_sub(state, x, y); break;
  case GLUT_RIGHT_BUTTON:  mouse_right_sub (state, x, y); break;
  }
}

void motion_cam_sub(int x, int y) {
  Context& ctx = g_Context;
  if (ctx.camera.is_zoom) {
    ctx.camera.handle_zoom((float)(y - ctx.camera.zoom_start.y) * 0.1f);
    ctx.camera.zoom_start.x = x;
    ctx.camera.zoom_start.y = y;
  }
  if (ctx.camera.is_move) {
    ctx.camera.handle_motion(x - ctx.camera.move_start.x, y - ctx.camera.move_start.y);
    ctx.camera.clamp();
    ctx.camera.move_start.x = x;
    ctx.camera.move_start.y = y;
  }
}

void motion(int x, int y) {
  Context& ctx = g_Context;
  motion_cam_sub(x, y);
}

int main(int argc, char* argv[]) {
  glutInit(&argc, argv);
  glutInitDisplayMode(GLUT_RGBA | GLUT_DEPTH | GLUT_DOUBLE | GLUT_ACCUM | GLUT_STENCIL);
  //glutInitDisplayMode(GLUT_RGB | GLUT_DEPTH | GLUT_SINGLE | GLUT_ACCUM | GLUT_STENCIL);
  glutInitWindowSize(640, 480);
#ifdef __FREEGLUT_EXT_H__
  glutSetOption(GLUT_ACTION_ON_WINDOW_CLOSE, GLUT_ACTION_GLUTMAINLOOP_RETURNS);
  glutInitContextVersion(4, 3);
  glutInitContextProfile(/*GLUT_CORE_PROFILE*/GLUT_COMPATIBILITY_PROFILE);
  //glutInitContextFlags(GLUT_FORWARD_COMPATIBLE);
#endif

  glutCreateWindow("Water surface wavelets");

  initialize(argc, argv);

  glutDisplayFunc(display);
  glutReshapeFunc(reshape);
  glutIdleFunc(idle);

  //glutMouseFunc(mouse);     // ImGui_ImplGLUT_MouseFunc
  //glutMotionFunc(motion);   // ImGui_ImplGLUT_MotionFunc
  //glutKeyboardFunc(keyboard); // ImGui_ImplGLUT_KeyboardFunc

  glutMainLoop();
  return 0;
}
