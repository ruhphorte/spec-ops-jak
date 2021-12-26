#include "Tfrag3.h"

#include "third-party/imgui/imgui.h"

Tfrag3::Tfrag3() {
  glGenVertexArrays(1, &m_debug_vao);
  glBindVertexArray(m_debug_vao);
  glGenBuffers(1, &m_debug_verts);
  glBindBuffer(GL_ARRAY_BUFFER, m_debug_verts);
  glBufferData(GL_ARRAY_BUFFER, DEBUG_TRI_COUNT * 3 * sizeof(DebugVertex), nullptr,
               GL_DYNAMIC_DRAW);
  glEnableVertexAttribArray(0);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(0,                                      // location 0 in the shader
                        3,                                      // 3 values per vert
                        GL_FLOAT,                               // floats
                        GL_FALSE,                               // normalized
                        sizeof(DebugVertex),                    // stride
                        (void*)offsetof(DebugVertex, position)  // offset (0)
  );

  glVertexAttribPointer(1,                                  // location 1 in the shader
                        4,                                  // 4 values per vert
                        GL_FLOAT,                           // floats
                        GL_FALSE,                           // normalized
                        sizeof(DebugVertex),                // stride
                        (void*)offsetof(DebugVertex, rgba)  // offset (0)
  );
  glBindVertexArray(0);
}

Tfrag3::~Tfrag3() {
  discard_tree_cache();
  glDeleteVertexArrays(1, &m_debug_vao);
}

void Tfrag3::setup_for_level(const std::vector<tfrag3::TFragmentTreeKind>& tree_kinds,
                             const std::string& level,
                             SharedRenderState* render_state) {
  // make sure we have the level data.
  auto lev_data = render_state->loader.get_tfrag3_level(level);
  if (m_level_name != level) {
    Timer tfrag3_setup_timer;
    fmt::print("new level for tfrag3: {} -> {}\n", m_level_name, level);
    fmt::print("discarding old stuff\n");
    discard_tree_cache();
    fmt::print("level has {} trees\n", lev_data->tfrag_trees.size());
    m_cached_trees.clear();

    size_t idx_buffer_len = 0;
    size_t time_of_day_count = 0;
    size_t vis_temp_len = 0;
    size_t max_draw = 0;

    for (size_t tree_idx = 0; tree_idx < lev_data->tfrag_trees.size(); tree_idx++) {
      const auto& tree = lev_data->tfrag_trees[tree_idx];
      m_cached_trees.emplace_back();
      auto& tree_cache = m_cached_trees.back();

      tree_cache.kind = tree.kind;
      if (std::find(tree_kinds.begin(), tree_kinds.end(), tree.kind) != tree_kinds.end()) {
        max_draw = std::max(tree.draws.size(), max_draw);
        for (auto& draw : tree.draws) {
          idx_buffer_len += draw.vertex_index_stream.size();
        }
        time_of_day_count = std::max(tree.colors.size(), time_of_day_count);
        u32 verts = tree.vertices.size();
        fmt::print("  tree {} has {} verts ({} kB) and {} draws\n", tree_idx, verts,
                   verts * sizeof(tfrag3::PreloadedVertex) / 1024.f, tree.draws.size());
        glGenVertexArrays(1, &tree_cache.vao);
        glBindVertexArray(tree_cache.vao);
        glGenBuffers(1, &tree_cache.vertex_buffer);
        tree_cache.vert_count = verts;
        tree_cache.draws = &tree.draws;  // todo - should we just copy this?
        tree_cache.colors = &tree.colors;
        tree_cache.vis = &tree.bvh;
        tree_cache.tod_cache = swizzle_time_of_day(tree.colors);
        vis_temp_len = std::max(vis_temp_len, tree.bvh.vis_nodes.size());
        glBindBuffer(GL_ARRAY_BUFFER, tree_cache.vertex_buffer);
        glBufferData(GL_ARRAY_BUFFER, verts * sizeof(tfrag3::PreloadedVertex), nullptr,
                     GL_DYNAMIC_DRAW);
        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);
        glEnableVertexAttribArray(2);

        glBufferSubData(GL_ARRAY_BUFFER, 0, verts * sizeof(tfrag3::PreloadedVertex),
                        tree.vertices.data());

        glVertexAttribPointer(0,                                // location 0 in the shader
                              3,                                // 3 values per vert
                              GL_FLOAT,                         // floats
                              GL_FALSE,                         // normalized
                              sizeof(tfrag3::PreloadedVertex),  // stride
                              (void*)offsetof(tfrag3::PreloadedVertex, x)  // offset (0)
        );

        glVertexAttribPointer(1,                                // location 1 in the shader
                              3,                                // 3 values per vert
                              GL_FLOAT,                         // floats
                              GL_FALSE,                         // normalized
                              sizeof(tfrag3::PreloadedVertex),  // stride
                              (void*)offsetof(tfrag3::PreloadedVertex, s)  // offset (0)
        );

        glVertexAttribPointer(2,                                // location 2 in the shader
                              1,                                // 1 values per vert
                              GL_UNSIGNED_SHORT,                // u16
                              GL_FALSE,                         // don't normalize
                              sizeof(tfrag3::PreloadedVertex),  // stride
                              (void*)offsetof(tfrag3::PreloadedVertex, color_index)  // offset (0)
        );
        glBindVertexArray(0);
      }
    }

    fmt::print("TFRAG temporary vis output size: {}\n", vis_temp_len);
    m_cache.vis_temp.resize(vis_temp_len);
    fmt::print("TFRAG max draws/tree: {}\n", max_draw);
    m_cache.draw_idx_temp.resize(max_draw);

    fmt::print("level has {} textures\n", lev_data->textures.size());
    for (auto& tex : lev_data->textures) {
      GLuint gl_tex;
      // fmt::print("  tex: {} x {} {} {}\n", tex.w, tex.h, tex.debug_name, tex.debug_tpage_name);
      glGenTextures(1, &gl_tex);
      glBindTexture(GL_TEXTURE_2D, gl_tex);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tex.w, tex.h, 0, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8_REV,
                   tex.data.data());
      glBindTexture(GL_TEXTURE_2D, 0);
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, gl_tex);
      glGenerateMipmap(GL_TEXTURE_2D);

      float aniso = 0.0f;
      glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY, &aniso);
      glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY, aniso);
      m_textures.push_back(gl_tex);
    }

    fmt::print("level max index stream: {}\n", idx_buffer_len);
    m_cache.index_list.resize(idx_buffer_len);
    m_has_index_buffer = true;
    glGenBuffers(1, &m_index_buffer);
    glActiveTexture(GL_TEXTURE1);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_index_buffer);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx_buffer_len * sizeof(u32), nullptr, GL_DYNAMIC_DRAW);

    fmt::print("level max time of day: {}\n", time_of_day_count);
    assert(time_of_day_count <= TIME_OF_DAY_COLOR_COUNT);
    // regardless of how many we use some fixed max
    // we won't actually interp or upload to gpu the unused ones, but we need a fixed maximum so
    // indexing works properly.
    m_color_result.resize(TIME_OF_DAY_COLOR_COUNT);
    glGenTextures(1, &m_time_of_day_texture);
    m_has_time_of_day_texture = true;
    glBindTexture(GL_TEXTURE_1D, m_time_of_day_texture);
    // just fill with zeros. this lets use use the faster texsubimage later
    glTexImage1D(GL_TEXTURE_1D, 0, GL_RGBA, TIME_OF_DAY_COLOR_COUNT, 0, GL_RGBA,
                 GL_UNSIGNED_INT_8_8_8_8, m_color_result.data());
    glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    m_level_name = level;
    fmt::print("TFRAG3 setup: {:.3f}\n", tfrag3_setup_timer.getSeconds());
  }
}

void Tfrag3::render_tree(const TfragRenderSettings& settings,
                         SharedRenderState* render_state,
                         ScopedProfilerNode& prof) {
  auto& tree = m_cached_trees.at(settings.tree_idx);
  assert(tree.kind != tfrag3::TFragmentTreeKind::INVALID);

  if (m_color_result.size() < tree.colors->size()) {
    m_color_result.resize(tree.colors->size());
  }
  if (m_use_fast_time_of_day) {
    interp_time_of_day_fast(settings.time_of_day_weights, tree.tod_cache, m_color_result.data());
  } else {
    interp_time_of_day_slow(settings.time_of_day_weights, *tree.colors, m_color_result.data());
  }
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_1D, m_time_of_day_texture);
  glTexSubImage1D(GL_TEXTURE_1D, 0, 0, tree.colors->size(), GL_RGBA, GL_UNSIGNED_INT_8_8_8_8_REV,
                  m_color_result.data());

  first_tfrag_draw_setup(settings, render_state);

  glBindVertexArray(tree.vao);
  glBindBuffer(GL_ARRAY_BUFFER, tree.vertex_buffer);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_index_buffer);
  glActiveTexture(GL_TEXTURE0);
  glEnable(GL_PRIMITIVE_RESTART);
  glPrimitiveRestartIndex(UINT32_MAX);

  cull_check_all_slow(settings.planes, tree.vis->vis_nodes, m_cache.vis_temp.data());

  int idx_buffer_ptr = make_index_list_from_vis_string(
      m_cache.draw_idx_temp.data(), m_cache.index_list.data(), *tree.draws, m_cache.vis_temp);

  glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, idx_buffer_ptr * sizeof(u32),
                  m_cache.index_list.data());

  for (size_t draw_idx = 0; draw_idx < tree.draws->size(); draw_idx++) {
    const auto& draw = tree.draws->operator[](draw_idx);
    const auto& indices = m_cache.draw_idx_temp[draw_idx];

    if (indices.second <= indices.first) {
      continue;
    }

    glBindTexture(GL_TEXTURE_2D, m_textures.at(draw.tree_tex_id));
    auto double_draw = setup_tfrag_shader(settings, render_state, draw.mode);
    tree.tris_this_frame += draw.num_triangles;
    tree.draws_this_frame++;
    int draw_size = indices.second - indices.first;
    void* offset = (void*)(indices.first * sizeof(u32));

    prof.add_draw_call();
    prof.add_tri(draw.num_triangles * (float)draw_size / draw.vertex_index_stream.size());

    glDrawElements(GL_TRIANGLE_STRIP, draw_size, GL_UNSIGNED_INT, (void*)offset);

    switch (double_draw.kind) {
      case DoubleDrawKind::NONE:
        break;
      case DoubleDrawKind::AFAIL_NO_DEPTH_WRITE:
        prof.add_draw_call();
        prof.add_tri(draw_size);
        glUniform1f(glGetUniformLocation(render_state->shaders[ShaderId::TFRAG3].id(), "alpha_min"),
                    -10.f);
        glUniform1f(glGetUniformLocation(render_state->shaders[ShaderId::TFRAG3].id(), "alpha_max"),
                    double_draw.aref);
        glDepthMask(GL_FALSE);
        glDrawElements(GL_TRIANGLE_STRIP, draw_size, GL_UNSIGNED_INT, (void*)offset);
        break;
      default:
        assert(false);
    }
  }
  glBindVertexArray(0);
}

/*!
 * Render all trees with settings for the given tree.
 * This is intended to be used only for debugging when we can't easily get commands for all trees
 * working.
 */
void Tfrag3::render_all_trees(const TfragRenderSettings& settings,
                              SharedRenderState* render_state,
                              ScopedProfilerNode& prof) {
  TfragRenderSettings settings_copy = settings;
  for (size_t i = 0; i < m_cached_trees.size(); i++) {
    if (m_cached_trees[i].kind != tfrag3::TFragmentTreeKind::INVALID) {
      settings_copy.tree_idx = i;
      render_tree(settings_copy, render_state, prof);
    }
  }
}

void Tfrag3::render_matching_trees(const std::vector<tfrag3::TFragmentTreeKind>& trees,
                                   const TfragRenderSettings& settings,
                                   SharedRenderState* render_state,
                                   ScopedProfilerNode& prof) {
  TfragRenderSettings settings_copy = settings;
  for (size_t i = 0; i < m_cached_trees.size(); i++) {
    m_cached_trees[i].reset_stats();
    if (!m_cached_trees[i].allowed) {
      continue;
    }
    if (std::find(trees.begin(), trees.end(), m_cached_trees[i].kind) != trees.end() ||
        m_cached_trees[i].forced) {
      m_cached_trees[i].rendered_this_frame = true;
      settings_copy.tree_idx = i;
      render_tree(settings_copy, render_state, prof);
      if (m_cached_trees[i].cull_debug) {
        render_tree_cull_debug(settings_copy, render_state, prof);
      }
    }
  }
}

void Tfrag3::debug_render_all_trees_nolores(const TfragRenderSettings& settings,
                                            SharedRenderState* render_state,
                                            ScopedProfilerNode& prof) {
  TfragRenderSettings settings_copy = settings;
  for (size_t i = 0; i < m_cached_trees.size(); i++) {
    if (m_cached_trees[i].kind != tfrag3::TFragmentTreeKind::INVALID &&
        m_cached_trees[i].kind != tfrag3::TFragmentTreeKind::LOWRES_TRANS &&
        m_cached_trees[i].kind != tfrag3::TFragmentTreeKind::LOWRES) {
      settings_copy.tree_idx = i;
      render_tree(settings_copy, render_state, prof);
    }
  }

  for (size_t i = 0; i < m_cached_trees.size(); i++) {
    if (m_cached_trees[i].kind != tfrag3::TFragmentTreeKind::INVALID &&
        m_cached_trees[i].kind != tfrag3::TFragmentTreeKind::LOWRES_TRANS &&
        m_cached_trees[i].kind != tfrag3::TFragmentTreeKind::LOWRES) {
      settings_copy.tree_idx = i;
      // render_tree_cull_debug(settings_copy, render_state, prof);
    }
  }
}

void Tfrag3::draw_debug_window() {
  for (int i = 0; i < (int)m_cached_trees.size(); i++) {
    auto& tree = m_cached_trees[i];
    if (tree.kind == tfrag3::TFragmentTreeKind::INVALID) {
      continue;
    }
    ImGui::PushID(i);
    ImGui::Text("[%d] %10s", i, tfrag3::tfrag_tree_names[(int)m_cached_trees[i].kind]);
    ImGui::SameLine();
    ImGui::Checkbox("Allow?", &tree.allowed);
    ImGui::SameLine();
    ImGui::Checkbox("Force?", &tree.forced);
    ImGui::SameLine();
    ImGui::Checkbox("cull debug (slow)", &tree.cull_debug);
    ImGui::PopID();
    if (tree.rendered_this_frame) {
      ImGui::Text("  tris: %d draws: %d", tree.tris_this_frame, tree.draws_this_frame);
    }
  }
}

void Tfrag3::discard_tree_cache() {
  for (auto tex : m_textures) {
    glBindTexture(GL_TEXTURE_2D, tex);
    glDeleteTextures(1, &tex);
  }
  m_textures.clear();

  for (auto& tree : m_cached_trees) {
    if (tree.kind != tfrag3::TFragmentTreeKind::INVALID) {
      glDeleteBuffers(1, &tree.vertex_buffer);
      glDeleteVertexArrays(1, &tree.vao);
    }
  }

  if (m_has_index_buffer) {
    glDeleteBuffers(1, &m_index_buffer);
    m_has_index_buffer = false;
  }

  if (m_has_time_of_day_texture) {
    glBindTexture(GL_TEXTURE_1D, m_time_of_day_texture);
    glDeleteTextures(1, &m_time_of_day_texture);
    m_has_time_of_day_texture = false;
  }

  // delete textures and stuff.
  m_cached_trees.clear();
}

namespace {

float frac(float in) {
  return in - (int)in;
}

void debug_vis_draw(int first_root,
                    int tree,
                    int num,
                    int depth,
                    const std::vector<tfrag3::VisNode>& nodes,
                    std::vector<Tfrag3::DebugVertex>& verts_out) {
  for (int ki = 0; ki < num; ki++) {
    auto& node = nodes.at(ki + tree - first_root);
    assert(node.child_id != 0xffff);
    math::Vector4f rgba{frac(0.4 * depth), frac(0.7 * depth), frac(0.2 * depth), 0.06};
    math::Vector3f center = node.bsphere.xyz();
    float rad = node.bsphere.w();
    math::Vector3f corners[8] = {center, center, center, center};
    corners[0].x() += rad;
    corners[1].x() += rad;
    corners[2].x() -= rad;
    corners[3].x() -= rad;

    corners[0].y() += rad;
    corners[1].y() -= rad;
    corners[2].y() += rad;
    corners[3].y() -= rad;

    for (int i = 0; i < 4; i++) {
      corners[i + 4] = corners[i];
      corners[i].z() += rad;
      corners[i + 4].z() -= rad;
    }

    if (true) {
      for (int i : {0, 4}) {
        verts_out.push_back({corners[0 + i], rgba});
        verts_out.push_back({corners[1 + i], rgba});
        verts_out.push_back({corners[2 + i], rgba});

        verts_out.push_back({corners[1 + i], rgba});  // 0
        verts_out.push_back({corners[3 + i], rgba});
        verts_out.push_back({corners[2 + i], rgba});
      }

      for (int i : {2, 6, 7, 2, 3, 7, 0, 4, 5, 0, 5, 1, 0, 6, 4, 0, 6, 2, 1, 3, 7, 1, 5, 7}) {
        verts_out.push_back({corners[i], rgba});
      }

      constexpr int border0[12] = {0, 4, 6, 2, 2, 6, 3, 7, 0, 1, 2, 3};
      constexpr int border1[12] = {1, 5, 7, 3, 0, 4, 1, 5, 4, 5, 6, 7};
      rgba.w() = 1.0;

      for (int i = 0; i < 12; i++) {
        auto p0 = corners[border0[i]];
        auto p1 = corners[border1[i]];
        auto diff = (p1 - p0).normalized();
        math::Vector3f px = diff.z() == 0 ? math::Vector3f{1, 0, 1} : math::Vector3f{0, 1, 1};
        auto off = diff.cross(px) * 2000;

        verts_out.push_back({p0 + off, rgba});
        verts_out.push_back({p0 - off, rgba});
        verts_out.push_back({p1 - off, rgba});

        verts_out.push_back({p0 + off, rgba});
        verts_out.push_back({p1 + off, rgba});
        verts_out.push_back({p1 - off, rgba});
      }
    }

    if (node.flags) {
      debug_vis_draw(first_root, node.child_id, node.num_kids, depth + 1, nodes, verts_out);
    }
  }
}

}  // namespace

void Tfrag3::render_tree_cull_debug(const TfragRenderSettings& settings,
                                    SharedRenderState* render_state,
                                    ScopedProfilerNode& prof) {
  // generate debug verts:
  m_debug_vert_data.clear();
  auto& tree = m_cached_trees.at(settings.tree_idx);

  debug_vis_draw(tree.vis->first_root, tree.vis->first_root, tree.vis->num_roots, 1,
                 tree.vis->vis_nodes, m_debug_vert_data);

  render_state->shaders[ShaderId::TFRAG3_NO_TEX].activate();
  glUniformMatrix4fv(
      glGetUniformLocation(render_state->shaders[ShaderId::TFRAG3_NO_TEX].id(), "camera"), 1,
      GL_FALSE, settings.math_camera.data());
  glUniform4f(
      glGetUniformLocation(render_state->shaders[ShaderId::TFRAG3_NO_TEX].id(), "hvdf_offset"),
      settings.hvdf_offset[0], settings.hvdf_offset[1], settings.hvdf_offset[2],
      settings.hvdf_offset[3]);
  glUniform1f(
      glGetUniformLocation(render_state->shaders[ShaderId::TFRAG3_NO_TEX].id(), "fog_constant"),
      settings.fog_x);
  // glDisable(GL_DEPTH_TEST);
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_GEQUAL);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);  // ?
  glDepthMask(GL_FALSE);

  glBindVertexArray(m_debug_vao);
  glBindBuffer(GL_ARRAY_BUFFER, m_debug_verts);

  int remaining = m_debug_vert_data.size();
  int start = 0;

  while (remaining > 0) {
    int to_do = std::min(DEBUG_TRI_COUNT * 3, remaining);

    glBufferSubData(GL_ARRAY_BUFFER, 0, to_do * sizeof(DebugVertex),
                    m_debug_vert_data.data() + start);
    glDrawArrays(GL_TRIANGLES, 0, to_do);
    prof.add_draw_call();
    prof.add_tri(to_do / 3);

    remaining -= to_do;
    start += to_do;
  }
}
