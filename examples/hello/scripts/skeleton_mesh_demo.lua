-- Soft-skinned mesh deformation -- a flag that bends with its mast.
--
-- Phase 49: the cloth is not a rigid sprite hung off a bone. It is a weighted
-- mesh whose tip verts follow the tip bone and whose root edge is split between
-- mast and tip, so rotating the tip folds the cloth instead of spinning a box.
-- A deform timeline adds a mid-clip flutter on top of the bone motion.
--
-- Controls: LEFT/RIGHT sway the tip, SPACE replay the wave, B bone overlay,
--           ESC quit. Headless attract plays the wave on a loop.

local W, H = 960, 540

local S = new_global_obj("meshskel", "state")

local function reset_state()
    S.frame = 0
    S.rig = 0
    S.flag = 0
    S.x = W * 0.5
    S.y = H * 0.62
    S.bones_on = 0
    S.t = 0.0
    S.log_t = 0.0
end

local function build()
    S.rig = skel_load("assets/skeleton/softskin.json")
    if S.rig == 0 then
        log_info("softskin: the rig failed to load")
        return
    end
    S.flag = skel_create(S.rig)
    skel_set_mix(S.flag, 0.15)
    skel_play(S.flag, "wave", 0.0)
    log_info("softskin: loaded rig=" .. S.rig .. " flag=" .. S.flag ..
                 " mesh=" .. tostring(skel_mesh(S.flag, 1)))
end

local function draw_hud()
    draw_text("SOFT-SKINNED MESH -- WEIGHTED FLAG", 24.0, 40.0, 0.72, 0.95, 0.97, 1.0,
              1.0)
    draw_text("LEFT/RIGHT sway   SPACE wave   B bones   ESC quit", 24.0, 66.0, 0.44,
              0.7, 0.78, 0.88, 1.0)
    local line1 = "mesh verts " .. skel_mesh_vertex_count(S.flag, 1) .. "   tris " ..
                      skel_mesh_triangle_count(S.flag, 1)
    draw_text(line1, 24.0, H - 74.0, 0.5, 0.6, 0.95, 1.0, 1.0)
    local line2 = "draw calls " .. skel_stat(5) .. "   parts " .. skel_stat(4)
    draw_text(line2, 24.0, H - 52.0, 0.5, 0.6, 0.95, 1.0, 1.0)
    if S.bones_on == 1 then
        draw_text("BONES ON", W - 120.0, 66.0, 0.56, 0.4, 1.0, 0.85, 1.0)
    end
end

function update(dt)
    if S.frame == nil then
        reset_state()
        build()
    end
    if S.rig == 0 then
        return 0
    end
    S.frame = S.frame + 1
    S.t = S.t + dt
    S.log_t = S.log_t + dt

    if input_key_pressed("escape") then
        window_quit()
        return 0
    end
    if input_key_pressed("b") then
        if S.bones_on == 0 then
            S.bones_on = 1
        else
            S.bones_on = 0
        end
    end
    if input_key_pressed("space") then
        skel_play(S.flag, "wave", 0.1)
    end

    local sway = 0.0
    if input_key_down("left") then
        sway = sway - 40.0
    end
    if input_key_down("right") then
        sway = sway + 40.0
    end
    if sway ~= 0.0 then
        local tip = skel_bone_index(S.flag, "tip")
        local cur = skel_bone_rot(S.flag, tip)
        skel_set_bone(S.flag, "tip", 32.0, 0.0, cur + sway * dt, 1.0, 1.0)
    end

    if not skel_playing(S.flag) then
        skel_play(S.flag, "wave", 0.2)
    end

    skel_draw(S.flag, S.x, S.y, 3.5, 1.0, 1.0, 1.0, 1.0, 1.0)
    if S.bones_on == 1 then
        skel_draw_bones(S.flag, S.x, S.y, 3.5, 1.0, 0.35, 1.0, 0.85, 0.9)
    end
    draw_hud()

    if S.log_t > 1.0 then
        S.log_t = 0.0
        log_info("softskin: verts=" .. skel_mesh_vertex_count(S.flag, 1) .. " tris=" ..
                     skel_mesh_triangle_count(S.flag, 1) .. " draws=" .. skel_stat(5))
    end
    return 0
end
