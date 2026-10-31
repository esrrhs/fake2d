-- 2D skeletal animation -- one rig, every pose, no sprite sheets.
--
-- The soldier is not a sequence of pictures: he is a bone hierarchy with parts
-- hung off it. One clip stores which bones turn by how much and when, so the
-- engine interpolates the pose every frame and redraws the parts. That is why
-- the same eight pieces give an idle that breathes, a run that plants its feet,
-- a shot that kicks the gun up, a hit that snaps the body back and a death that
-- folds him to the ground -- and why switching between them cross-fades instead
-- of popping.
--
-- What is on screen:
--   * the hero, aimed at the mouse with one-bone IK on the gun arm,
--   * a rank of soldiers marching behind him, all instances of the *same* rig,
--   * events: every footstep kicks dust, every shot flashes and drops a casing,
--   * B overlays the skeleton so you can see the bones steering the art,
--   * N swaps the skin, which is attachment substitution rather than new art.
--
-- The HUD prints what the pass costs. Every part of every soldier comes off one
-- texture, so the batch coalesces a crowd into a couple of draw calls.
--
-- Controls: LEFT/RIGHT move, SPACE shoot, H take a hit, K die, R revive,
--           B bone overlay, N skin, ESC quit.
-- Headless: with no input the attract sequence walks, aims, shoots, takes a hit
--           and dies on its own, and logs the rig stats once per second.

local W, H = 960, 540

-- How many extra rigs march behind the hero. They are the same asset instance
-- after instance, which is the point. Their ids live in flat fields
-- (`S["soldier_" .. i]`) rather than a table: a table stored on a NativeObject
-- is deep-const, so `S.soldiers[i] = id` is dropped and reads back as nil — the
-- nil then reaches `skel_draw`'s numeric argument and the call fails.
local CROWD = 6
local GROUND = 402.0
local HERO_SPEED = 190.0

-- Where the marching rank stands. File-level tables are read-only, which is
-- exactly right for a layout that never changes.
local RANK_SPOTS = {
    {x = 150.0, y = 356.0, s = 0.52},
    {x = 300.0, y = 350.0, s = 0.48},
    {x = 470.0, y = 358.0, s = 0.55},
    {x = 640.0, y = 350.0, s = 0.47},
    {x = 790.0, y = 358.0, s = 0.53},
    {x = 225.0, y = 336.0, s = 0.40},
    {x = 385.0, y = 332.0, s = 0.38},
    {x = 555.0, y = 338.0, s = 0.42},
    {x = 715.0, y = 332.0, s = 0.39},
    {x = 860.0, y = 338.0, s = 0.41},
}


local S = new_global_obj("skeleton_demo", "state")

local function reset_state()
    S.frame = 0
    S.t = 0.0
    S.fps = 0.0
    S.logged = 0
    S.hero_x = 420.0
    S.hero_dir = 1
    S.walking = 0
    S.clip = "idle"
    S.bones_on = 0
    S.skin = 0
    S.dead = 0
    S.cool = 0.0
    S.mix = 0.22
    S.quiet = 0.0
    S.attract = 0.0
    S.attract_dir = 1
    S.soldier_n = 0
    S.tex_back = 0
    S.tex_floor = 0
end

local function build()
    S.rig = skel_load("assets/skeleton/soldier.json")
    if S.rig == 0 then
        log_info("skeleton: the rig failed to load")
        return
    end
    -- Footstep dust, muzzle flash and ejected casings all live in the engine's
    -- particle pool rather than in script-side tables: fewer objects for the
    -- script to own, and the emitter's look is set up once.
    S.dust = part_create()
    part_set_lifetime(S.dust, 0.25, 0.5)
    part_set_speed(S.dust, 12, 55)
    part_set_direction(S.dust, -3.14, -1.57)
    part_set_size(S.dust, 9, 0)
    part_set_gravity(S.dust, 0, -30)
    part_set_drag(S.dust, 3.0)
    part_set_color(S.dust, 0.62, 0.56, 0.46, 0.6, 0.0)

    S.flash = part_create()
    part_set_lifetime(S.flash, 0.06, 0.13)
    part_set_speed(S.flash, 0, 40)
    part_set_direction(S.flash, -1.57, 1.57)
    part_set_size(S.flash, 30, 4)
    part_set_gravity(S.flash, 0, 0)
    part_set_drag(S.flash, 0)
    part_set_color(S.flash, 1.0, 0.86, 0.42, 1.0, 0.0)

    S.casing = part_create()
    part_set_lifetime(S.casing, 0.5, 0.9)
    part_set_speed(S.casing, 90, 170)
    part_set_direction(S.casing, -2.4, -1.2)
    part_set_size(S.casing, 5, 0)
    part_set_gravity(S.casing, 0, 620)
    part_set_drag(S.casing, 0.4)
    part_set_color(S.casing, 0.78, 0.62, 0.28, 1.0, 0.55)

    -- Loaded once: `sprite_load` takes a reference each time it is called, so
    -- calling it every frame leaks one per frame.
    S.tex_back = sprite_load("assets/art/limbo_bg.png")
    S.tex_floor = sprite_load("assets/art/stone_tex.png")
    S.hero = skel_create(S.rig)
    skel_set_mix(S.hero, S.mix)
    skel_set_clip(S.hero, "idle")
    S.gun_bone = skel_bone_index(S.hero, "gun")
    S.hand_bone = skel_bone_index(S.hero, "hand_r")

    -- The rank behind the hero: one asset, one instance each, and the same
    -- twelve parts over and over, which is what a rig is for.
    for i = 1, CROWD do
        local id = skel_create(S.rig)
        S["soldier_" .. i] = id
        S.soldier_n = i
        -- Same clip, different clock and speed, so the rank never marches in
        -- lockstep -- the cheapest thing that makes a crowd look alive.
        skel_set_clip(id, "run")
        skel_set_time(id, i * 0.09)
        skel_set_speed(id, 0.82 + (i % 4) * 0.09)
        skel_set_mix(id, 0.3)
    end
end

--- Play a clip only on a real change, so a cross-fade is not restarted every
--- frame by the caller that is merely reporting what should be playing.
local function want_clip(name, mix)
    if S.clip == name then
        return
    end
    if skel_play(S.hero, name, mix) then
        S.clip = name
    end
end

local function draw_room(tex_back, tex_floor)
    draw_quad(0.0, 0.0, W, H, 0.10, 0.11, 0.15, 1.0)
    sprite_draw(tex_back, 0.0, 0.0, W, GROUND - 60.0)
    draw_quad(0.0, GROUND - 66.0, W, 12.0, 0.28, 0.24, 0.21, 1.0)
    sprite_draw(tex_floor, 0.0, GROUND - 56.0, 256.0, 256.0)
    sprite_draw(tex_floor, 256.0, GROUND - 56.0, 256.0, 256.0)
    sprite_draw(tex_floor, 512.0, GROUND - 56.0, 256.0, 256.0)
    sprite_draw(tex_floor, 768.0, GROUND - 56.0, 256.0, 256.0)
    draw_quad(0.0, GROUND - 2.0, W, 3.0, 0.74, 0.70, 0.62, 1.0)
end


local function drain_events(id)
    local n = skel_event_count(id)
    for i = 0, n - 1 do
        local name = skel_event_name(id, i)
        local data = skel_event_data(id, i)
        local foot = S.foot_r
        if data < 1.5 then
            foot = S.foot_l
        end
        if name == "footstep" then
            part_emit(S.dust, 3, skel_bone_x(id, foot), skel_bone_y(id, foot) + 26.0)
        elseif name == "muzzle" then
            part_emit(S.flash, 4, skel_bone_x(id, S.gun_tip) + 44.0,
                      skel_bone_y(id, S.gun_tip))
        elseif name == "shell" then
            part_emit(S.casing, 1, skel_bone_x(id, S.hand_bone),
                      skel_bone_y(id, S.hand_bone))
        end
    end
end

local function draw_hero()
    local flip = S.hero_dir < 0 and -1.0 or 1.0
    skel_draw(S.hero, S.hero_x, GROUND, 1.0, flip, 1.0, 1.0, 1.0, 1.0)
    if S.bones_on == 1 then
        skel_draw_bones(S.hero, S.hero_x, GROUND, 1.0, flip, 0.35, 1.0, 0.85, 0.9)
    end
end

local function draw_hud()
    draw_text("2D SKELETAL ANIMATION -- ONE RIG, EVERY POSE", 24.0, 40.0, 0.72, 0.95,
              0.97, 1.0, 1.0)
    draw_text("MOVE  SPACE shoot  H hit  K die  R revive  B bones  N skin", 24.0, 66.0,
              0.44, 0.7, 0.78, 0.88, 1.0)

    -- Each line is built in one short statement. Splitting a concatenation over
    -- several source lines, in the same function as another such expression,
    -- takes the GCC JIT down a bad path (docs/SCRIPTING.md "Known FakeLua
    -- pitfalls"), which is why this reads more verbosely than it needs to.
    local skin = "standard"
    if S.skin == 1 then
        skin = "alert"
    end
    local line1 = "clip " .. S.clip .. "   facing " .. S.hero_dir .. "   skin " .. skin
    draw_text(line1, 24.0, H - 96.0, 0.5, 0.6, 0.95, 1.0, 1.0)
    local line2 = "instances " .. skel_stat(2) .. "   bones posed " .. skel_stat(3)
    draw_text(line2, 24.0, H - 74.0, 0.5, 0.6, 0.95, 1.0, 1.0)
    local line3 = "parts drawn " .. skel_stat(4) .. "   draw calls " .. skel_stat(5)
    draw_text(line3, 24.0, H - 52.0, 0.5, 0.6, 0.95, 1.0, 1.0)
    local line4 = "clips " .. skel_stat(7) .. "   events " .. skel_stat(6)
    draw_text(line4, 24.0, H - 30.0, 0.5, 0.6, 0.95, 1.0, 1.0)

    local fps = string.format("%.1f fps", S.fps)
    local fw = text_width(fps, 0.56)
    draw_text(fps, W - fw - 24.0, H - 52.0, 0.56, 0.75, 1.0, 0.8, 1.0)
    if S.dead == 1 then
        draw_text("K.I.A. -- press R", W - 190.0, 40.0, 0.56, 1.0, 0.5, 0.45, 1.0)
    end
    if S.bones_on == 1 then
        draw_text("BONES ON", W - 120.0, 66.0, 0.56, 0.4, 1.0, 0.85, 1.0)
    end
end

local function render_crowd()
    for i = 1, S.soldier_n do
        local id = S["soldier_" .. i]
        local spot = RANK_SPOTS[i]
        skel_draw(id, spot.x, spot.y + 90.0, spot.s, 1.0, 0.82, 0.86, 0.94, 1.0)
        if S.bones_on == 1 then
            skel_draw_bones(id, spot.x, spot.y + 90.0, spot.s, 1.0, 0.3, 0.9, 0.8, 0.55)
        end
    end
end
local function render_scene()
    draw_room(S.tex_back, S.tex_floor)

    for i = 1, S.soldier_n do
        local id = S["soldier_" .. i]
        local spot = RANK_SPOTS[i]
        local flip = 1.0
        skel_draw(id, spot.x, spot.y + 90.0, spot.s, flip, 0.82, 0.86, 0.94, 1.0)
        if S.bones_on == 1 then
            skel_draw_bones(id, spot.x, spot.y + 90.0, spot.s, flip, 0.3, 0.9, 0.8, 0.55)
        end
    end
    draw_hero()
    part_draw()
    draw_hud()

end

--- Every phase is its own function on purpose: `update` below stays short, and
--- the script engine handles a small function that touches a few globals far
--- better than one that touches fifty (docs/SCRIPTING.md, "Known FakeLua
--- pitfalls").
local function handle_keys(dt)
    if input_key_pressed("b") then
        S.bones_on = 1 - S.bones_on
    end
    if input_key_pressed("n") then
        S.skin = 1 - S.skin
        local which = "standard"
        if S.skin == 1 then
            which = "alert"
        end
        skel_skin(S.hero, which)
        for i = 1, S.soldier_n do
            skel_skin(S["soldier_" .. i], which)
        end
    end
    if input_key_pressed("r") and S.dead == 1 then
        S.dead = 0
        skel_reset_pose(S.hero)
        want_clip("idle", 0.35)
    end
    if input_key_pressed("h") and S.dead == 0 then
        want_clip("hit", 0.05)
    end
    if input_key_pressed("k") and S.dead == 0 then
        S.dead = 1
        want_clip("death", 0.2)
    end
end

--- Left alone, the hero works through the whole clip set so every pose,
--- cross-fade and event plays out without anyone at the keyboard.
local function attract_step(dt)
    if S.quiet < 1.4 or S.frame < 90 then
        return
    end
    S.attract = S.attract + dt
    if S.attract < 2.2 then
        return
    end
    S.attract = 0.0
    S.quiet = 0.0
    if S.dead == 1 then
        S.dead = 0
        skel_reset_pose(S.hero)
        want_clip("idle", 0.4)
    elseif S.clip == "idle" then
        want_clip("run", S.mix)
    elseif S.clip == "run" then
        want_clip("shoot", 0.06)
    elseif S.clip == "shoot" then
        want_clip("hit", 0.05)
    elseif S.clip == "hit" then
        S.dead = 1
        want_clip("death", 0.2)
    else
        want_clip("idle", S.mix)
    end
end

local function walk_the_hero(dt)
    S.walking = 0
    if S.dead == 1 or S.clip == "hit" then
        if skel_finished(S.hero) and S.dead == 0 then
            want_clip("idle", S.mix)
        end
        return
    end
    if input_key_down("right") or input_key_down("left") then
        S.quiet = 0.0
    end
    if input_key_down("right") or S.attract_dir > 0 then
        S.hero_x = S.hero_x + HERO_SPEED * dt
        S.hero_dir = 1
        S.walking = 1
    end
    if input_key_down("left") or S.attract_dir < 0 then
        S.hero_x = S.hero_x - HERO_SPEED * dt
        S.hero_dir = -1
        S.walking = 1
    end
    if S.attract_dir > 0 and S.hero_x > 700.0 then
        S.attract_dir = -1
    elseif S.attract_dir < 0 and S.hero_x < 220.0 then
        S.attract_dir = 1
    end
    if S.hero_x < 60.0 then
        S.hero_x = 60.0
    end
    if S.hero_x > W - 60.0 then
        S.hero_x = W - 60.0
    end
    if S.walking == 1 then
        want_clip("run", S.mix)
    else
        want_clip("idle", S.mix)
    end
    S.cool = S.cool - dt
    if input_key_pressed("space") and S.cool <= 0.0 then
        S.cool = 0.34
        want_clip("shoot", 0.06)
    elseif S.clip == "shoot" and skel_finished(S.hero) then
        want_clip("idle", S.mix)
    end
end

--- One-bone aim on the gun: because the gun hangs off the hand, which hangs off
--- the elbow, one call moves the whole arm.
local function aim_the_gun()
    if S.dead == 1 then
        return
    end
    local mx = input_mouse_x()
    local my = input_mouse_y()
    if mx > 0.0 or my > 0.0 then
        skel_aim(S.hero, "gun", mx, my)
    end
end


local function report_once()
    if S.logged == 0 and S.frame == 60 then
        S.logged = 1
        log_info("skeleton: " .. skel_stat(2) .. " instances, " .. skel_stat(3) ..
                     " bones posed, " .. skel_stat(4) .. " parts drawn, " ..
                     skel_stat(5) .. " draw calls")
        log_number(91000 + skel_stat(5))
    end
end
function update(dt)
    if S.frame == nil then
        reset_state()
        build()
        if S.rig ~= 0 then
            S.foot_l = skel_bone_index(S.hero, "knee_l")
            S.foot_r = skel_bone_index(S.hero, "knee_r")
            S.gun_tip = skel_bone_index(S.hero, "gun")
        end
    end
    if S.rig == 0 then
        return 0
    end
    S.frame = S.frame + 1
    S.t = S.t + dt
    S.fps = S.fps * 0.94 + (1.0 / (dt + 0.0001)) * 0.06
    if input_key_pressed("escape") then
        window_quit()
        return 0
    end
    handle_keys(dt)
    attract_step(dt)
    walk_the_hero(dt)
    aim_the_gun()
    drain_events(S.hero)
    render_scene()
    report_once()
    return 0
end
