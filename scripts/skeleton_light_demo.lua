-- Soft-skinned character under the Phase 46 lighting pass.
--
-- Phase 50: the weighted flag casts a real shadow from its mesh hull (synced
-- into a LightSystem occluder each frame) and receives the lantern / torch
-- pools like every other sprite. CPU skinning + existing light APIs — no GPU
-- path required.
--
-- Controls: LEFT/RIGHT sway the tip, SPACE replay the wave, L toggle lights,
--           B bone overlay, ESC quit. Headless attract waves on a loop.

local W, H = 960, 540

local S = new_global_obj("skel_light", "state")

local function reset_state()
    S.frame = 0
    S.rig = 0
    S.flag = 0
    S.x = W * 0.52
    S.y = H * 0.58
    S.scale = 2.2
    S.bones_on = 0
    S.lights_on = 1
    S.t = 0.0
    S.lantern = 0
    S.torch = 0
    S.fill = 0
    S.flag_occ = 0
    S.crate_occ = 0
end

local function build()
    light_set_ambient(0.07, 0.08, 0.12, 1.0)
    light_set_shadow_darkness(0.0)
    light_set_core(0.18, 0.65)

    S.lantern = light_create()
    light_set_color(S.lantern, 1.0, 0.82, 0.5, 1.2)
    light_set_radius(S.lantern, 260.0)
    light_set_position(S.lantern, S.x - 140.0, S.y - 40.0)

    S.torch = light_create()
    light_set_position(S.torch, 140.0, 110.0)
    light_set_color(S.torch, 1.0, 0.42, 0.16, 1.0)
    light_set_radius(S.torch, 200.0)

    S.fill = light_create()
    light_set_position(S.fill, W * 0.5, H * 0.35)
    light_set_color(S.fill, 0.35, 0.45, 0.7, 0.35)
    light_set_radius(S.fill, 520.0)
    light_set_shadows(S.fill, 0)

    -- A static crate so the room has a second caster to compare against.
    S.crate_occ = occluder_add_rect(620.0, 340.0, 70.0, 54.0)

    -- Placeholder poly; skel_sync_occluder rewrites it every frame from the hull.
    S.flag_occ = occluder_add_poly(0.0, 0.0, 10.0, 0.0, 10.0, 10.0, 0.0, 10.0)

    S.rig = skel_load("assets/skeleton/softskin.json")
    if S.rig == 0 then
        log_info("skel-light: the rig failed to load")
        return
    end
    S.flag = skel_create(S.rig)
    skel_set_mix(S.flag, 0.15)
    skel_play(S.flag, "wave", 0.0)
    log_info("skel-light: rig=" .. S.rig .. " flag=" .. S.flag ..
                 " hull=" .. skel_hull_count(S.flag, 1))
end

local function draw_room()
    -- Floor / walls as plain quads so the light multiply has something to hit.
    draw_quad(0.0, 0.0, W, H, 0.12, 0.13, 0.16, 1.0)
    draw_quad(0.0, H - 90.0, W, 90.0, 0.18, 0.16, 0.14, 1.0)
    draw_quad(600.0, 340.0, 90.0, 70.0, 0.35, 0.28, 0.2, 1.0)
    draw_quad(120.0, 70.0, 40.0, 70.0, 0.4, 0.22, 0.12, 1.0)
end

local function draw_hud()
    draw_text("SOFT-SKIN + LIGHTING -- HULL OCCLUDER", 24.0, 36.0, 0.68, 0.95, 0.97, 1.0,
              1.0)
    draw_text("LEFT/RIGHT sway   SPACE wave   L lights   B bones   ESC quit", 24.0,
              60.0, 0.42, 0.7, 0.78, 0.88, 1.0)
    local line = "hull " .. skel_hull_count(S.flag, 1) .. "   lights " ..
                     light_stat(1) .. "   occluders " .. light_stat(4) ..
                     "   shadow quads " .. light_stat(5)
    draw_text(line, 24.0, H - 48.0, 0.48, 0.6, 0.95, 1.0, 1.0)
    if S.lights_on == 0 then
        draw_text("LIGHTS OFF", W - 140.0, 60.0, 0.5, 1.0, 0.45, 0.4, 1.0)
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
    if input_key_pressed("l") then
        if S.lights_on == 0 then
            S.lights_on = 1
        else
            S.lights_on = 0
        end
        light_set_on(S.lights_on)
    end
    if input_key_pressed("space") then
        skel_play(S.flag, "wave", 0.1)
    end

    -- Attract / sway (same tip bone override as skeleton-mesh-demo).
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

    -- Lantern orbits slowly in front of the flag so the hull shadow sweeps.
    local lx = S.x + math.cos(S.t * 0.7) * 160.0
    local ly = S.y - 30.0 + math.sin(S.t * 0.9) * 40.0
    light_set_position(S.lantern, lx, ly)
    light_set_radius(S.torch, 180.0 + math.sin(S.t * 7.0) * 18.0)

    -- Sync the soft-skin hull into the caster *before* drawing so the shadow
    -- matches the pose that lands on screen this frame.
    local wrote = skel_sync_occluder(S.flag, S.flag_occ, 1, S.x, S.y, S.scale, 1.0)
    if S.frame == 60 or S.frame == 180 then
        log_info("skel-light: frame=" .. S.frame .. " hull_wrote=" .. wrote ..
                     " bounds_w=" .. skel_bounds_w(S.flag) .. " lights=" ..
                     light_stat(1) .. " occluders=" .. light_stat(4))
    end

    draw_room()
    skel_draw(S.flag, S.x, S.y, S.scale, 1.0, 1.0, 1.0, 1.0, 1.0)
    if S.bones_on == 1 then
        skel_draw_bones(S.flag, S.x, S.y, S.scale, 1.0, 0.3, 1.0, 0.55, 0.9)
    end
    if S.lights_on == 1 then
        light_render()
    end
    draw_hud()
    return 0
end
