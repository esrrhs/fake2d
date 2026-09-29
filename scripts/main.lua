-- fake2d sample entry script
-- Called once per frame as update(dt)

local time = 0.0

function update(dt)
    time = time + dt

    -- Draw background cards / UI panels
    draw_quad(40, 40, 260, 160, 0.15, 0.18, 0.25, 0.9)
    draw_quad(50, 50, 240, 30, 0.22, 0.35, 0.60, 1.0)

    -- Draw colored decorative shapes
    draw_quad(60, 95, 35, 35, 0.9, 0.3, 0.3, 1.0)
    draw_quad(105, 95, 35, 35, 0.3, 0.85, 0.4, 1.0)
    draw_quad(150, 95, 35, 35, 0.3, 0.5, 0.95, 1.0)
    draw_quad(195, 95, 35, 35, 0.95, 0.85, 0.2, 1.0)

    -- Draw an animated bouncing quad
    local bounce_x = 450 + 180 * math.sin(time * 2.0)
    local bounce_y = 200 + 80 * math.cos(time * 3.0)
    draw_quad(bounce_x, bounce_y, 80, 80, 0.9, 0.4, 0.7, 0.95)

    -- Draw ground bar
    draw_quad(0, 500, 960, 40, 0.2, 0.25, 0.3, 1.0)

    return 0
end
