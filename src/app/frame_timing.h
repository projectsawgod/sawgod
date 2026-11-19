// Opt-in presentation diagnostics. Work columns are CPU costs; gpu_ms is a delayed Vulkan timestamp.
// --frame-picture-log also measures final RGB; GPU readback cost is reported separately.
#pragma once
#include "app/picture_motion.h"
#include "engine/gpu_upload_work.h"
#include <array>
#include <cstdint>
#include <cstdio>
#include <string>
namespace sawgod::app {
struct FrameTimingSample {
    uint64_t time = 0, game_frame = 0, picture_frame = 0, generation = 0, production_end = 0;
    int ran = 0, speed = 1, graphics = 0, map = -1, picture_map = -1, width = 0, height = 0, vsync = 0;
    uint64_t presentation_time = 0;
    uint64_t from_frame = 0, to_frame = 0;
    float interpolation = 1;
    bool snapped = true;
    bool pixel_probe = false;
    GpuUploadWork upload_work;
    uint64_t picture_hash = 0, picture_readback = 0;
    std::array<float, 2> ground_geometry{}, party_geometry{};
    PictureMotionSample pixels;
    float refresh = 0, mode7 = 0, pixel_scale = 0;
    bool held = false, paused = false, clock_reset = false, accelerated = false;
    bool mode7_drawn = false;
    int pacing = 0, screen = 0;
    int scene = 0;
    bool battle = false, full_ui = false, field_picture = false;
    std::array<float, 2> scroll{}, origin{};
    uint64_t topology = 0, tiles = 0, palettes = 0;
    // Game/upload/scene/Mode7 collection/project/geometry/effect upload/draw/present/last game tick,
    // atlas+mips, three geometry passes, three flat effect passes, three projected effect passes,
    // focus/UI restoration. All are CPU costs; passes can execute later on the GPU.
    std::array<uint64_t, 22> work{};
    uint64_t present_id = 0, gpu_present_id = 0, gpu_over_budget_count = 0;
    double gpu_ms = std::numeric_limits<double>::quiet_NaN();
    bool gpu_valid = false;
    float audio_queued = 0, synth = 0;
    uint64_t audio_dropped = 0, audio_underruns = 0, audio_trimmed = 0;
    float audio_target = 0;
    bool title_active = false;
};
class FrameTimingLog {
  public:
    ~FrameTimingLog() {
        if (file_)
            std::fclose(file_);
    }
    bool open(const std::string &path) {
        if (path.empty())
            return true;
        file_ = std::fopen(path.c_str(), "w");
        if (!file_)
            return false;
        std::fputs(
            "t_ms,frame_ms,ran,speed,tick_ms,upload_ms,describe_ms,m7_update_ms,m7_project_ms,"
            "m7_draw_ms,m7_effects_ms,draw_ms,present_ms,game_tick_ms,m7_atlas_ms,m7_ground_ms,"
            "m7_ownership_ms,m7_water_ms,flat_scene_ms,flat_bloom_x_ms,flat_composite_ms,"
            "projected_scene_ms,projected_bloom_x_ms,projected_composite_ms,focus_ms,ui_restore_ms,"
            "audio_queued_ms,audio_synth_ms,audio_dropped,game_frame,picture_frame,game_delta,"
            "picture_delta,repeated,dropped,generation,graphics,map,picture_map,width,height,"
            "refresh_hz,vsync,mode7,pixel_scale,scroll_x,scroll_y,scroll_dx,scroll_dy,"
            "origin_x,origin_y,topology_changed,tiles_changed,palettes_changed,held,paused,"
            "clock_reset,accelerated,production_end_ms,pacing,screen,mode7_drawn,scene,battle,full_ui,"
            "interpolation,from_frame,to_frame,snapped,presentation_time_ms,pixel_probe,picture_hash,picture_"
            "readback_ms,geometry_ground_x,geometry_ground_y,geometry_party_x,geometry_party_y,pixel_ground_"
            "x,pixel_ground_y,pixel_ground_dx,pixel_ground_dy,pixel_ground_valid,pixel_ground_error,ground_"
            "patch_x,ground_patch_y,pixel_party_x,pixel_party_y,pixel_party_valid,pixel_party_error,ground_"
            "segment,present_id,gpu_present_id,gpu_ms,gpu_valid,gpu_over_budget_count,field_picture,"
            "scanout_missed_refreshes,audio_underruns,audio_trimmed_frames,audio_target_ms,title_active,"
            "upload_bytes,upload_calls,upload_allocations\n",
            file_);
        return true;
    }
    bool enabled() const { return file_ != nullptr; }
    void write(const FrameTimingSample &s) {
        if (!file_)
            return;
        const bool same = seen_ && s.generation == previous_.generation;
        const auto delta = same && s.picture_frame >= previous_.picture_frame
                               ? s.picture_frame - previous_.picture_frame
                               : 0;
        const auto game_delta =
            same && s.game_frame >= previous_.game_frame ? s.game_frame - previous_.game_frame : 0;
        const bool same_map = same && s.picture_map == previous_.picture_map;
        std::fprintf(file_, "%.6f,%.6f,%d,%d", double(s.time) / 1e6,
                     seen_ ? double(s.time - previous_.time) / 1e6 : 0, s.ran, s.speed);
        for (auto ns : s.work)
            std::fprintf(file_, ",%.6f", double(ns) / 1e6);
        std::fprintf(file_,
                     ",%.3f,%.3f,%llu,%llu,%llu,%llu,%llu,%d,%llu,%llu,%d,%d,%d,%d,%d,"
                     "%.3f,%d,%.3f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%d,%d,%d,%d,%d,%d,%d,%.6f,%d,%d,%d,%d,%"
                     "d,%d,%.6f,%llu,%llu,%d,%.6f",
                     double(s.audio_queued), double(s.synth), (unsigned long long)s.audio_dropped,
                     (unsigned long long)s.game_frame, (unsigned long long)s.picture_frame,
                     (unsigned long long)game_delta, (unsigned long long)delta, int(same && delta == 0),
                     (unsigned long long)(delta > 1 ? delta - 1 : 0), (unsigned long long)s.generation,
                     s.graphics, s.map, s.picture_map, s.width, s.height, double(s.refresh), s.vsync,
                     double(s.mode7), double(s.pixel_scale), double(s.scroll[0]), double(s.scroll[1]),
                     same_map ? double(s.scroll[0] - previous_.scroll[0]) : 0,
                     same_map ? double(s.scroll[1] - previous_.scroll[1]) : 0, double(s.origin[0]),
                     double(s.origin[1]), int(same_map && s.topology != previous_.topology),
                     int(same_map && s.tiles != previous_.tiles),
                     int(same_map && s.palettes != previous_.palettes), int(s.held), int(s.paused),
                     int(s.clock_reset), int(s.accelerated), double(s.production_end) / 1e6, s.pacing,
                     s.screen, int(s.mode7_drawn), s.scene, int(s.battle), int(s.full_ui),
                     double(s.interpolation), (unsigned long long)s.from_frame,
                     (unsigned long long)s.to_frame, int(s.snapped), double(s.presentation_time) / 1e6);
        const float invalid = std::numeric_limits<float>::quiet_NaN();
        std::fprintf(
            file_,
            ",%d,%llu,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%d,%.3f,%d,%d,%.6f,%.6f,%d,%.3f,%llu",
            int(s.pixel_probe), (unsigned long long)s.picture_hash, double(s.picture_readback) / 1e6,
            double(s.pixel_probe && s.mode7_drawn ? s.ground_geometry[0] : invalid),
            double(s.pixel_probe && s.mode7_drawn ? s.ground_geometry[1] : invalid),
            double(s.pixel_probe && s.mode7_drawn ? s.party_geometry[0] : invalid),
            double(s.pixel_probe && s.mode7_drawn ? s.party_geometry[1] : invalid),
            double(s.pixels.ground_match.valid ? s.pixels.ground[0] : invalid),
            double(s.pixels.ground_match.valid ? s.pixels.ground[1] : invalid),
            double(s.pixels.ground_match.valid ? s.pixels.ground_match.x : invalid),
            double(s.pixels.ground_match.valid ? s.pixels.ground_match.y : invalid),
            int(s.pixels.ground_match.valid), double(s.pixels.ground_match.error), s.pixels.patch_x,
            s.pixels.patch_y, double(s.pixels.party_match.valid ? s.pixels.party[0] : invalid),
            double(s.pixels.party_match.valid ? s.pixels.party[1] : invalid), int(s.pixels.party_match.valid),
            double(s.pixels.party_match.error), (unsigned long long)s.pixels.ground_segment);
        std::fprintf(file_, ",%llu,%llu,%.6f,%d,%llu,%d,-1", (unsigned long long)s.present_id,
                     (unsigned long long)s.gpu_present_id, s.gpu_ms, int(s.gpu_valid),
                     (unsigned long long)s.gpu_over_budget_count, int(s.field_picture));
        std::fprintf(file_, ",%llu,%llu,%.3f,%d", (unsigned long long)s.audio_underruns,
                     (unsigned long long)s.audio_trimmed, double(s.audio_target), int(s.title_active));
        std::fprintf(file_, ",%llu,%llu,%llu\n", (unsigned long long)s.upload_work.bytes,
                     (unsigned long long)s.upload_work.calls, (unsigned long long)s.upload_work.allocations);
        previous_ = s;
        seen_ = true;
        // Flush once a second, preserving useful data if a diagnostic run is interrupted.
        if (s.time - flushed_ > 1'000'000'000) {
            std::fflush(file_);
            flushed_ = s.time;
        }
    }

  private:
    FILE *file_ = nullptr;
    bool seen_ = false;
    uint64_t flushed_ = 0;
    FrameTimingSample previous_{};
};
} // namespace sawgod::app
