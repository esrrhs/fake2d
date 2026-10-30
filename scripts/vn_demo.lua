-- fake2d visual-novel / dating-sim demo (--vn-demo), Tokimeki-style.
--
-- Four heroines, painted backgrounds and expression portraits, branching
-- dialogue with choices, a daily schedule (study / train / style / work /
-- rest), affection, dates, a summer festival and a confession ending.
--
-- Controls: mouse click / Space / Enter advances dialogue, 1-3 (or 1-4) pick
-- choices, F5/F9 save/load at the calendar, Esc backs out of a menu. When the
-- controls are idle an attract player runs the whole story, so headless runs
-- unfold (and finish) on their own.

local SW = 960.0
local SH = 540.0

local ST_TITLE, ST_DIALOGUE, ST_HUB, ST_TOPICS, ST_SUMMARY = 0, 1, 2, 3, 4
local DM_INTRO, DM_CHAT, DM_DATE, DM_FESTIVAL, DM_ENDING = 1, 2, 3, 4, 5

local NEUTRAL, SMILE, BLUSH, SAD = 0, 1, 2, 3
local GAME_DAYS = 14
local FESTIVAL_DAY = 7

local WHO_NARRATOR, WHO_PLAYER = 0, 5

-- Runtime-constant heroine table (read-only, like the font table in text_demo).
local GIRLS = {
    {name = "林夏", pref = 2, r = 0.95, g = 0.55, b = 0.35},
    {name = "苏晴", pref = 1, r = 0.45, g = 0.66, b = 0.94},
    {name = "顾雨", pref = 3, r = 0.76, g = 0.62, b = 0.92},
    {name = "唐心", pref = 3, r = 0.96, g = 0.78, b = 0.38},
}

local FONTS = {
    {path = "/System/Library/Fonts/Hiragino Sans GB.ttc", name = "Hiragino Sans GB"},
    {path = "/System/Library/Fonts/PingFang.ttc", name = "PingFang SC"},
    {path = "/System/Library/Fonts/Supplemental/Songti.ttc", name = "Songti SC"},
    {path = "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc", name = "Noto Sans CJK"},
    {path = "/usr/share/fonts/truetype/wqy/wqy-microhei.ttc", name = "WenQuanYi Micro Hei"},
    {path = "C:\\Windows\\Fonts\\msyh.ttc", name = "Microsoft YaHei"},
    {path = "C:\\Windows\\Fonts\\simhei.ttf", name = "SimHei"},
}

local bg_gate, bg_classroom, bg_library, bg_gym = 0 + 0, 0 + 0, 0 + 0, 0 + 0
local bg_park, bg_cafe, bg_rooftop, bg_festival = 0 + 0, 0 + 0, 0 + 0, 0 + 0
local p_linxia_n, p_linxia_s, p_linxia_b, p_linxia_d = 0 + 0, 0 + 0, 0 + 0, 0 + 0
local p_suqing_n, p_suqing_s, p_suqing_b, p_suqing_d = 0 + 0, 0 + 0, 0 + 0, 0 + 0
local p_guyu_n, p_guyu_s, p_guyu_b, p_guyu_d = 0 + 0, 0 + 0, 0 + 0, 0 + 0
local p_tangxin_n, p_tangxin_s, p_tangxin_b, p_tangxin_d = 0 + 0, 0 + 0, 0 + 0, 0 + 0
local petal_fx = 0 + 0
local font_ok = 0 + 0

local state = ST_TITLE + 0
local frame_no = 0 + 0
local inited = false

-- Calendar / stats.
local day = 1 + 0
local energy = 5 + 0
local money = 100 + 0
local study = 0 + 0
local sport = 0 + 0
local charm = 0 + 0
local aff1, aff2, aff3, aff4 = 0 + 0, 0 + 0, 0 + 0, 0 + 0
local festival_done = false
local save_exists = 0 + 0

-- Dialogue state.
local dlg_mode = 0 + 0
local dlg_girl = 0 + 0
local dlg_topic = 0 + 0
local dlg_line = 0 + 0
local dlg_char = 0.0 + 0.0
local dlg_choice = false
local dlg_after = false
local dlg_after_girl = 0 + 0
local dlg_after_gain = 0 + 0
local dlg_after_expr = 0 + 0
local dlg_fade = 0.0 + 0.0
local dlg_portrait_x = 0.0 + 0.0
local dlg_blip = 0 + 0
local aff_toast_t = 0 + 0
local aff_toast_gain = 0 + 0

-- Menus / AI.
local topics_girl = 0 + 0
local hub_toast = 0 + 0
local hub_toast_t = 0 + 0
local ending_girl = 0 + 0
local ending_success = false
local idle_frames = 0 + 0
local attract = false
local auto_t = 0 + 0
local mouse_x = 0.0 + 0.0
local mouse_y = 0.0 + 0.0

-- ---------------------------------------------------------------------------
-- small helpers
-- ---------------------------------------------------------------------------

local function clampf(v, lo, hi)
    if v < lo then return lo end
    if v > hi then return hi end
    return v
end

local function in_rect(mx, my, x, y, w, h)
    return mx >= x and mx <= x + w and my >= y and my <= y + h
end

local function utf8_len(s)
    local i = 1
    local n = 0
    local len = string.len(s)
    while i <= len do
        local b = string.byte(s, i)
        if b >= 240 then i = i + 4
        elseif b >= 224 then i = i + 3
        elseif b >= 192 then i = i + 2
        else i = i + 1 end
        n = n + 1
    end
    return n
end

local function utf8_prefix(s, chars)
    if chars <= 0 then return "" end
    local i = 1
    local n = 0
    local len = string.len(s)
    while i <= len and n < chars do
        local b = string.byte(s, i)
        if b >= 240 then i = i + 4
        elseif b >= 224 then i = i + 3
        elseif b >= 192 then i = i + 2
        else i = i + 1 end
        n = n + 1
    end
    if i > len then return s end
    return string.sub(s, 1, i - 1)
end

local function get_aff(g)
    if g == 1 then return aff1 end
    if g == 2 then return aff2 end
    if g == 3 then return aff3 end
    return aff4
end

local function set_aff(g, v)
    if v < 0 then v = 0 end
    if v > 100 then v = 100 end
    if g == 1 then aff1 = v
    elseif g == 2 then aff2 = v
    elseif g == 3 then aff3 = v
    else aff4 = v end
end

local function add_aff(g, v)
    set_aff(g, get_aff(g) + v)
end

local function stat_value(which)
    if which == 1 then return study end
    if which == 2 then return sport end
    return charm
end

local function pref_value(g)
    return stat_value(GIRLS[g].pref)
end

local function girl_name(g)
    if g >= 1 and g <= 4 then return GIRLS[g].name end
    return ""
end

local function bg_tex(id)
    if id == 0 then return bg_gate end
    if id == 1 then return bg_classroom end
    if id == 2 then return bg_library end
    if id == 3 then return bg_gym end
    if id == 4 then return bg_park end
    if id == 5 then return bg_cafe end
    if id == 6 then return bg_rooftop end
    return bg_festival
end

local function portrait_tex(g, expr)
    if g == 1 then
        if expr == 0 then return p_linxia_n end
        if expr == 1 then return p_linxia_s end
        if expr == 2 then return p_linxia_b end
        return p_linxia_d
    elseif g == 2 then
        if expr == 0 then return p_suqing_n end
        if expr == 1 then return p_suqing_s end
        if expr == 2 then return p_suqing_b end
        return p_suqing_d
    elseif g == 3 then
        if expr == 0 then return p_guyu_n end
        if expr == 1 then return p_guyu_s end
        if expr == 2 then return p_guyu_b end
        return p_guyu_d
    end
    if expr == 0 then return p_tangxin_n end
    if expr == 1 then return p_tangxin_s end
    if expr == 2 then return p_tangxin_b end
    return p_tangxin_d
end

local function toast_text(code)
    if code == 1 then return "学力 +3" end
    if code == 2 then return "体能 +3" end
    if code == 3 then return "魅力 +3" end
    if code == 4 then return "打工 +25G" end
    if code == 5 then return "体力恢复了" end
    if code == 6 then return "好感 +8" end
    if code == 7 then return "体力不够了，先休息吧" end
    if code == 8 then return "已保存" end
    if code == 9 then return "已读档" end
    if code == 10 then return "好感达到 30 才能约会" end
    return ""
end

local function notify_hub(code)
    hub_toast = code
    hub_toast_t = 140
end

-- ---------------------------------------------------------------------------
-- scene data (files are read-only consts; each call returns a fresh table)
-- ---------------------------------------------------------------------------

local function scene_intro()
    return {
        bg = 0, girl = 0,
        who = {WHO_NARRATOR, WHO_NARRATOR, WHO_PLAYER, WHO_NARRATOR},
        expr = {NEUTRAL, NEUTRAL, NEUTRAL, NEUTRAL},
        lines = {
            "樱花飘落的四月，我转学来到私立樱丘学园。",
            "陌生的教室、陌生的走廊……还有四位会在之后的日子里，改变我生活的女孩。",
            "（深呼吸）……高中生活，就从今天开始吧。",
            "在这个春天，我想留下不会后悔的回忆。",
        },
    }
end

local function scene_linxia(t)
    if t == 1 then
        return {
            bg = 3, girl = 1,
            who = {WHO_NARRATOR, 1, WHO_PLAYER},
            expr = {NEUTRAL, SMILE, NEUTRAL},
            lines = {
                "放学后的操场，林夏还在加练最后一圈。",
                "呼——你今天也来跑步吗？我可以带你热身！",
                "（她把毛巾递过来，眼睛亮晶晶的）",
            },
            opts = {"好啊，请多指教！", "先让我喘口气……", "跑步不适合我。"},
            gains = {12, 7, -4},
        }
    elseif t == 2 then
        return {
            bg = 1, girl = 1,
            who = {WHO_NARRATOR, 1, WHO_PLAYER},
            expr = {NEUTRAL, SMILE, NEUTRAL},
            lines = {
                "体育课自由活动时间，林夏把排球抛给我。",
                "运动会报名表还差一个人，你要不要和我一起参加接力？",
                "（她期待地看着我）",
            },
            opts = {"一起拿第一名吧", "我考虑一下", "我体育很差"},
            gains = {12, 5, -3},
        }
    end
    return {
        bg = 4, girl = 1,
        who = {WHO_NARRATOR, 1, WHO_PLAYER},
        expr = {NEUTRAL, SAD, NEUTRAL},
        lines = {
            "傍晚的公园长椅上，林夏少见地安静下来。",
            "其实……我一直很怕输。跑得越快，就越怕停下来。",
            "（她抱着膝盖，声音轻得几乎听不见）",
        },
        opts = {"你已经很努力了，我陪你", "输赢并不重要", "别想太多"},
        gains = {15, 8, 2},
    }
end

local function scene_suqing(t)
    if t == 1 then
        return {
            bg = 2, girl = 2,
            who = {WHO_NARRATOR, 2, WHO_PLAYER},
            expr = {NEUTRAL, NEUTRAL, NEUTRAL},
            lines = {
                "午休的图书馆，苏晴把一本旧书推到我面前。",
                "这本小说的结尾我一直没读懂……你觉得他们后来重逢了吗？",
                "（她推了推眼镜，认真等着我的答案）",
            },
            opts = {"一定重逢了，因为约定还在", "也许错过也是一种结局", "我只看漫画"},
            gains = {12, 7, -3},
        }
    elseif t == 2 then
        return {
            bg = 2, girl = 2,
            who = {WHO_NARRATOR, 2, WHO_PLAYER},
            expr = {NEUTRAL, SMILE, NEUTRAL},
            lines = {
                "自习课上，苏晴把笔记整理成两份，其中一份放在我桌上。",
                "下周的模拟考，你准备得怎么样？要不要一起复习？",
                "（她小声补充）我……不太擅长讲题，但可以整理重点。",
            },
            opts = {"请务必教我", "我自己先看看", "考试随便啦"},
            gains = {12, 5, -4},
        }
    end
    return {
        bg = 2, girl = 2,
        who = {WHO_NARRATOR, 2, WHO_PLAYER},
        expr = {NEUTRAL, BLUSH, NEUTRAL},
        lines = {
            "放学后的图书馆只剩我们两个人。",
            "我最近在写一篇小说……主角一直在等一个人。你觉得，等待有意义吗？",
            "（夕阳落在她的书页上，她没有抬头）",
        },
        opts = {"值得等的人，一定会来", "要看那个人值不值得", "等待太傻了"},
        gains = {15, 8, -2},
    }
end

local function scene_guyu(t)
    if t == 1 then
        return {
            bg = 1, girl = 3,
            who = {WHO_NARRATOR, 3, WHO_PLAYER},
            expr = {NEUTRAL, SAD, NEUTRAL},
            lines = {
                "美术教室的窗边，顾雨正对着一张空白画布发呆。",
                "我想画一片星空，可是怎么都调不出梦里的颜色……",
                "（她的指尖沾着蓝色颜料）",
            },
            opts = {"把我看到的夜空讲给你听", "蓝色加一点紫会不会更好", "换成照片不就好了"},
            gains = {12, 8, -4},
        }
    elseif t == 2 then
        return {
            bg = 1, girl = 3,
            who = {WHO_NARRATOR, 3, WHO_PLAYER},
            expr = {NEUTRAL, BLUSH, NEUTRAL},
            lines = {
                "文化祭布置，顾雨把一朵纸花递给我。",
                "我负责装饰走廊……你觉得，这里挂一串星星灯会好看吗？",
                "（她紧张地捏着纸花）",
            },
            opts = {"一定会是全校最漂亮的地方", "星星灯太花哨了", "随便挂点什么就行"},
            gains = {12, 4, -3},
        }
    end
    return {
        bg = 4, girl = 3,
        who = {WHO_NARRATOR, 3, WHO_PLAYER},
        expr = {NEUTRAL, BLUSH, NEUTRAL},
        lines = {
            "天台的晚风里，顾雨展开一张画。",
            "画里是两个人站在樱花树下……我一直不敢画脸。",
            "（她把画纸往我这边挪了挪）",
        },
        opts = {"那我们把这张画完成吧", "不画脸也很有意境", "画得有点奇怪"},
        gains = {15, 8, -3},
    }
end

local function scene_tangxin(t)
    if t == 1 then
        return {
            bg = 5, girl = 4,
            who = {WHO_NARRATOR, 4, WHO_PLAYER},
            expr = {NEUTRAL, SMILE, NEUTRAL},
            lines = {
                "放学后的咖啡店，唐心把一盒点心推到我面前。",
                "家里厨师的新品，你尝尝？可别像上次那样客气。",
                "（她托着腮，等着我的评价）",
            },
            opts = {"很好吃，下次我请你", "还能再要一块吗", "太甜了"},
            gains = {12, 7, -4},
        }
    elseif t == 2 then
        return {
            bg = 1, girl = 4,
            who = {WHO_NARRATOR, 4, WHO_PLAYER},
            expr = {NEUTRAL, SAD, NEUTRAL},
            lines = {
                "学生会办公室，唐心正为文化祭预算发愁。",
                "预算又超了……你有什么省钱的点子吗？",
                "（她第一次露出有点苦恼的表情）",
            },
            opts = {"交给我，我陪你一起对账", "少办几个项目吧", "这不是你该操心的事"},
            gains = {12, 5, -5},
        }
    end
    return {
        bg = 5, girl = 4,
        who = {WHO_NARRATOR, 4, WHO_PLAYER},
        expr = {NEUTRAL, SAD, NEUTRAL},
        lines = {
            "傍晚的咖啡店，唐心把一枚旧钢琴谱放在桌上。",
            "我小时候每天都被逼着练琴……现在反而不知道，自己到底喜不喜欢。",
            "（她轻轻按住琴谱的一角）",
        },
        opts = {"那就弹一首只给自己的曲子", "不喜欢就别弹了", "有钱人家的烦恼真特别"},
        gains = {15, 6, -6},
    }
end

local function scene_date(g)
    if g == 1 then
        return {
            bg = 4, girl = 1,
            who = {WHO_NARRATOR, 1, WHO_PLAYER},
            expr = {NEUTRAL, SMILE, NEUTRAL},
            lines = {
                "周末的河堤公园，林夏穿着运动服，比约定早到了十分钟。",
                "你居然真的来了！那……陪我跑一段？",
                "（夕阳把她的影子拉得很长）",
            },
            opts = {"跑到那棵樱花树下", "先散步聊聊吧", "我今天是来休息的"},
            gains = {15, 10, 3},
        }
    elseif g == 2 then
        return {
            bg = 5, girl = 2,
            who = {WHO_NARRATOR, 2, WHO_PLAYER},
            expr = {NEUTRAL, BLUSH, NEUTRAL},
            lines = {
                "周日下午的咖啡店，苏晴把书签夹进书里。",
                "这里很安静，适合读书……也适合和某个人聊天。",
                "（她说完就低下了头）",
            },
            opts = {"以后我们常来吧", "你推荐一本书给我", "我不太会聊天"},
            gains = {15, 10, 3},
        }
    elseif g == 3 then
        return {
            bg = 4, girl = 3,
            who = {WHO_NARRATOR, 3, WHO_PLAYER},
            expr = {NEUTRAL, BLUSH, NEUTRAL},
            lines = {
                "公园的湖边，顾雨支起画架。",
                "我想画下今天的天空……还有站在旁边的你，可以吗？",
                "（她的耳朵有点红）",
            },
            opts = {"当然可以，我当模特", "只画风景就好", "别画我，太尴尬了"},
            gains = {15, 9, 3},
        }
    end
    return {
        bg = 5, girl = 4,
        who = {WHO_NARRATOR, 4, WHO_PLAYER},
        expr = {NEUTRAL, BLUSH, NEUTRAL},
        lines = {
            "傍晚的咖啡店，唐心点了一份双人下午茶。",
            "这家店的新品，一个人吃太浪费了。所以……你明白的吧？",
            "（她别过头，耳朵却红了）",
        },
        opts = {"那我就恭敬不如从命", "下次换我请", "我不太饿"},
        gains = {15, 10, 3},
    }
end

local function scene_festival()
    return {
        bg = 7, girl = 0,
        who = {WHO_NARRATOR, WHO_NARRATOR, WHO_PLAYER},
        expr = {NEUTRAL, NEUTRAL, NEUTRAL},
        lines = {
            "文化祭的夜晚，校园里挂满了灯笼。",
            "人群熙攘，四位女孩都在不远处向我招手。",
            "（要和谁一起逛祭典呢？）",
        },
        opts = {"和林夏去捞金鱼", "和苏晴去看书展", "和顾雨去美术展", "和唐心去买点心"},
        gains = {15, 15, 15, 15},
        targets = {1, 2, 3, 4},
    }
end

local function scene_ending()
    local g = ending_girl
    if g < 1 then g = 1 end
    local n = girl_name(g)
    if ending_success then
        return {
            bg = 6, girl = g,
            who = {WHO_NARRATOR, WHO_PLAYER, g, WHO_NARRATOR},
            expr = {NEUTRAL, NEUTRAL, BLUSH, NEUTRAL},
            lines = {
                "毕业典礼结束后，我把" .. n .. "约到了天台。",
                "那个……我有话想对你说。从转学那天起，我就一直在看着你。",
                "（" .. n .. "愣了一下，然后笑了）我等你这句话，等了很久。",
                "樱花季节结束的时候，我们的故事才刚刚开始。",
            },
        }
    end
    return {
        bg = 6, girl = g,
        who = {WHO_NARRATOR, WHO_PLAYER, WHO_NARRATOR},
        expr = {NEUTRAL, NEUTRAL, SAD},
        lines = {
            "毕业典礼结束了。天台上只剩下我一个人。",
            "那句练习了很多遍的话，最终还是没有说出口。",
            "也许，青春就是这样带着一点遗憾结束的吧。",
        },
    }
end

local function current_scene()
    if dlg_mode == DM_INTRO then return scene_intro() end
    if dlg_mode == DM_CHAT then
        if dlg_girl == 1 then return scene_linxia(dlg_topic) end
        if dlg_girl == 2 then return scene_suqing(dlg_topic) end
        if dlg_girl == 3 then return scene_guyu(dlg_topic) end
        return scene_tangxin(dlg_topic)
    end
    if dlg_mode == DM_DATE then return scene_date(dlg_girl) end
    if dlg_mode == DM_FESTIVAL then return scene_festival() end
    return scene_ending()
end

local function after_text(gain)
    if gain >= 10 then return "她开心地笑了，你们之间的距离似乎近了一些。" end
    if gain >= 4 then return "她轻轻点了点头，气氛变得柔和起来。" end
    return "她移开了视线，空气里有一点点尴尬。"
end

-- ---------------------------------------------------------------------------
-- calendar / save-load
-- ---------------------------------------------------------------------------

local function save_game()
    storage_reset()
    storage_set_num("vn_ver", 1)
    storage_set_num("vn_day", day)
    storage_set_num("vn_energy", energy)
    storage_set_num("vn_money", money)
    storage_set_num("vn_study", study)
    storage_set_num("vn_sport", sport)
    storage_set_num("vn_charm", charm)
    storage_set_num("vn_aff1", aff1)
    storage_set_num("vn_aff2", aff2)
    storage_set_num("vn_aff3", aff3)
    storage_set_num("vn_aff4", aff4)
    storage_set_num("vn_fest", festival_done and 1 or 0)
    if storage_save("vn_quick") then
        save_exists = 1
        notify_hub(8)
        log_info("vn: save")
        return true
    end
    return false
end

local function load_game()
    if not storage_load("vn_quick") then return false end
    if not storage_has("vn_day") then return false end
    day = math.floor(storage_get_num("vn_day", 1.0))
    energy = math.floor(storage_get_num("vn_energy", 5.0))
    money = storage_get_num("vn_money", 100.0)
    study = storage_get_num("vn_study", 0.0)
    sport = storage_get_num("vn_sport", 0.0)
    charm = storage_get_num("vn_charm", 0.0)
    aff1 = storage_get_num("vn_aff1", 0.0)
    aff2 = storage_get_num("vn_aff2", 0.0)
    aff3 = storage_get_num("vn_aff3", 0.0)
    aff4 = storage_get_num("vn_aff4", 0.0)
    festival_done = storage_get_num("vn_fest", 0.0) > 0.5
    save_exists = 1
    state = ST_HUB
    notify_hub(9)
    log_info("vn: load")
    return true
end

local function reset_game()
    day = 1
    energy = 5
    money = 100
    study = 0
    sport = 0
    charm = 0
    aff1 = 0
    aff2 = 0
    aff3 = 0
    aff4 = 0
    festival_done = false
    ending_girl = 0
    ending_success = false
end

-- ---------------------------------------------------------------------------
-- dialogue engine
-- ---------------------------------------------------------------------------

local function begin_scene(mode, g, t)
    dlg_mode = mode
    dlg_girl = g
    dlg_topic = t
    dlg_line = 0
    dlg_char = 0.0
    dlg_choice = false
    dlg_after = false
    dlg_after_girl = 0
    dlg_after_gain = 0
    dlg_after_expr = NEUTRAL
    dlg_fade = 1.0
    dlg_portrait_x = 48.0
    aff_toast_t = 0
    state = ST_DIALOGUE
    auto_t = 0
    if mode == DM_INTRO then
        log_info("vn: intro")
    elseif mode == DM_CHAT then
        log_info(string.format("vn: chat %s topic %d", girl_name(g), t))
    elseif mode == DM_DATE then
        log_info(string.format("vn: date %s", girl_name(g)))
    elseif mode == DM_FESTIVAL then
        log_info("vn: festival")
    elseif mode == DM_ENDING then
        if ending_success then
            log_info(string.format("vn: ending %s success (frame %d)", girl_name(ending_girl), frame_no))
        else
            log_info(string.format("vn: solo ending (frame %d)", frame_no))
        end
    end
end

local function enter_hub()
    if day > GAME_DAYS then
        state = ST_DIALOGUE
        ending_girl = 1
        local best = aff1
        if aff2 > best then best = aff2; ending_girl = 2 end
        if aff3 > best then best = aff3; ending_girl = 3 end
        if aff4 > best then best = aff4; ending_girl = 4 end
        ending_success = best >= 60
        begin_scene(DM_ENDING, ending_girl, 0)
        return
    end
    if not festival_done and day >= FESTIVAL_DAY then
        festival_done = true
        begin_scene(DM_FESTIVAL, 0, 0)
        return
    end
    state = ST_HUB
    auto_t = 0
end

local function advance_day()
    day = day + 1
    log_info(string.format("vn: day %d", day))
end

local function finish_scene()
    if dlg_mode == DM_ENDING then
        state = ST_SUMMARY
        auto_t = 0
        return
    end
    enter_hub()
end

local function pick_option(k)
    local sc = current_scene()
    if sc.opts == nil then return end
    local gains = sc.gains
    local target = sc.girl
    if sc.targets ~= nil then target = sc.targets[k] end
    if target < 1 then target = 1 end
    local gain = gains[k]
    local bonus = 0
    if pref_value(target) >= 10 and gain >= 8 then bonus = 2 end
    local total = gain + bonus
    add_aff(target, total)
    dlg_after = true
    dlg_after_girl = target
    dlg_after_gain = total
    if total >= 10 then dlg_after_expr = SMILE
    elseif total >= 4 then dlg_after_expr = NEUTRAL
    else dlg_after_expr = SAD end
    dlg_char = 0.0
    dlg_choice = false
    aff_toast_gain = total
    aff_toast_t = 90
    auto_t = 0
    if total >= 10 then
        audio_play("vn_heart", 0.4)
    elseif total >= 0 then
        audio_play("vn_aff", 0.35)
    else
        audio_play("vn_sad", 0.35)
    end
end

local function best_option()
    local sc = current_scene()
    local gains = sc.gains
    if gains == nil then return 1 end
    local best = 1
    local i = 2
    while i <= #gains do
        if gains[i] > gains[best] then best = i end
        i = i + 1
    end
    return best
end

local function advance_dialogue()
    if dlg_choice then return end
    if dlg_after then
        local text = after_text(dlg_after_gain)
        if dlg_char < utf8_len(text) then
            dlg_char = utf8_len(text) + 0.0
            return
        end
        finish_scene()
        return
    end
    local sc = current_scene()
    local text = sc.lines[dlg_line + 1]
    if text == nil then return end
    local total = utf8_len(text)
    if dlg_char < total then
        dlg_char = total + 0.0
        return
    end
    dlg_line = dlg_line + 1
    dlg_char = 0.0
    local count = #sc.lines
    if dlg_line >= count then
        if sc.opts ~= nil then
            dlg_choice = true
            dlg_line = count - 1
            auto_t = 0
        else
            finish_scene()
        end
    end
end

local function update_dialogue()
    if dlg_fade > 0.0 then dlg_fade = clampf(dlg_fade - 0.045, 0.0, 1.0) end
    if dlg_portrait_x > 0.0 then dlg_portrait_x = clampf(dlg_portrait_x - 3.0, 0.0, 48.0) end
    if aff_toast_t > 0 then aff_toast_t = aff_toast_t - 1 end

    local sc = current_scene()
    local text = ""
    local speed = 0.55
    if attract then speed = 1.8 end
    if dlg_after then
        text = after_text(dlg_after_gain)
    else
        text = sc.lines[dlg_line + 1]
        if text == nil then text = "" end
    end
    local total = utf8_len(text)
    if dlg_char < total then
        local before = math.floor(dlg_char)
        dlg_char = dlg_char + speed
        if math.floor(dlg_char) > before then
            dlg_blip = dlg_blip + 1
            if dlg_blip % 4 == 0 then audio_play("vn_blip", 0.08) end
        end
    end

    if attract then
        auto_t = auto_t + 1
        if dlg_choice then
            if auto_t >= 34 then
                pick_option(best_option())
                auto_t = 0
            end
        elseif dlg_char >= total then
            if auto_t >= 26 then
                advance_dialogue()
                auto_t = 0
            end
        else
            auto_t = 0
        end
    end
end

-- ---------------------------------------------------------------------------
-- hub actions
-- ---------------------------------------------------------------------------

local function do_action(kind)
    if kind == 5 then
        energy = math.min(energy + 2, 5)
        notify_hub(5)
        advance_day()
        enter_hub()
        return
    end
    if energy <= 0 then
        notify_hub(7)
        return
    end
    if kind == 1 then study = math.min(study + 3, 40)
    elseif kind == 2 then sport = math.min(sport + 3, 40)
    elseif kind == 3 then charm = math.min(charm + 3, 40)
    elseif kind == 4 then money = money + 25
    end
    energy = energy - 1
    notify_hub(kind)
    audio_play("vn_click", 0.22)
    advance_day()
    enter_hub()
end

local function start_chat(g, t)
    if energy <= 0 then
        notify_hub(7)
        return false
    end
    energy = energy - 1
    advance_day()
    begin_scene(DM_CHAT, g, t)
    return true
end

local function start_date(g)
    if get_aff(g) < 30 then
        notify_hub(10)
        return false
    end
    if energy <= 0 then
        notify_hub(7)
        return false
    end
    energy = energy - 1
    advance_day()
    begin_scene(DM_DATE, g, 0)
    return true
end

local function give_gift(g)
    if money < 20 then
        notify_hub(1)
        return
    end
    money = money - 20
    add_aff(g, 8)
    notify_hub(6)
    audio_play("vn_aff", 0.35)
end

local function begin_ending()
    ending_girl = 1
    local best = aff1
    if aff2 > best then best = aff2; ending_girl = 2 end
    if aff3 > best then best = aff3; ending_girl = 3 end
    if aff4 > best then best = aff4; ending_girl = 4 end
    ending_success = best >= 60
    begin_scene(DM_ENDING, ending_girl, 0)
end

local function start_new_game()
    reset_game()
    audio_play("vn_click", 0.3)
    begin_scene(DM_INTRO, 0, 0)
end

-- ---------------------------------------------------------------------------
-- input
-- ---------------------------------------------------------------------------

local function any_activity()
    if input_mouse_pressed(0) then return true end
    if input_key_pressed("space") or input_key_pressed("enter") then return true end
    if input_key_pressed("escape") or input_key_pressed("f5") or input_key_pressed("f9") then return true end
    if input_key_pressed("1") or input_key_pressed("2") or input_key_pressed("3") or
       input_key_pressed("4") then return true end
    return false
end

local function update_title_input(clicked)
    local bx = 720.0
    local by = 190.0
    if clicked and in_rect(mouse_x, mouse_y, bx, by, 210.0, 48.0) then
        start_new_game()
    elseif clicked and save_exists > 0 and in_rect(mouse_x, mouse_y, bx, by + 60.0, 210.0, 48.0) then
        if not load_game() then start_new_game() end
    elseif clicked and in_rect(mouse_x, mouse_y, bx, by + 120.0, 210.0, 48.0) then
        window_quit()
    end
end

local function update_hub_input(clicked)
    if clicked then
        local i = 0
        while i < 5 do
            if in_rect(mouse_x, mouse_y, 16.0 + i * 94.0, 452.0, 90.0, 64.0) then
                do_action(i + 1)
                return
            end
            i = i + 1
        end
        i = 0
        while i < 4 do
            local y = 70.0 + i * 96.0
            if in_rect(mouse_x, mouse_y, 540.0, y, 404.0, 86.0) then
                if in_rect(mouse_x, mouse_y, 826.0, y + 48.0, 104.0, 30.0) then
                    start_date(i + 1)
                else
                    topics_girl = i + 1
                    state = ST_TOPICS
                    audio_play("vn_click", 0.22)
                end
                return
            end
            i = i + 1
        end
        if day >= 10 and in_rect(mouse_x, mouse_y, 744.0, 12.0, 204.0, 28.0) then
            begin_ending()
            return
        end
        if in_rect(mouse_x, mouse_y, 600.0, 12.0, 64.0, 28.0) then
            save_game()
            return
        end
        if in_rect(mouse_x, mouse_y, 670.0, 12.0, 64.0, 28.0) then
            load_game()
            return
        end
    end
end

local function update_topics_input(clicked)
    local g = topics_girl
    if clicked then
        local i = 1
        while i <= 3 do
            if in_rect(mouse_x, mouse_y, 392.0, 116.0 + (i - 1) * 74.0, 388.0, 60.0) then
                if i == 3 and get_aff(g) < 20 then
                    audio_play("vn_sad", 0.2)
                else
                    start_chat(g, i)
                end
                return
            end
            i = i + 1
        end
        if in_rect(mouse_x, mouse_y, 392.0, 348.0, 186.0, 46.0) then
            give_gift(g)
            return
        end
        if in_rect(mouse_x, mouse_y, 594.0, 348.0, 186.0, 46.0) then
            start_date(g)
            return
        end
        if in_rect(mouse_x, mouse_y, 392.0, 406.0, 388.0, 40.0) then
            state = ST_HUB
            return
        end
    end
    if input_key_pressed("escape") then state = ST_HUB end
end

local function update_summary_input(clicked)
    if clicked then
        if in_rect(mouse_x, mouse_y, 60.0, 460.0, 200.0, 46.0) then
            start_new_game()
        elseif in_rect(mouse_x, mouse_y, 280.0, 460.0, 200.0, 46.0) then
            state = ST_TITLE
        end
    end
end

local function update_dialogue_input(clicked)
    if dlg_choice then
        local sc = current_scene()
        local n = #sc.opts
        local i = 0
        while i < n do
            if clicked and in_rect(mouse_x, mouse_y, 500.0, 118.0 + i * 64.0, 400.0, 52.0) then
                pick_option(i + 1)
                return
            end
            i = i + 1
        end
        i = 1
        while i <= n do
            if input_key_pressed(string.format("%d", i)) then
                pick_option(i)
                return
            end
            i = i + 1
        end
        return
    end
    if clicked or input_key_pressed("space") or input_key_pressed("enter") then
        advance_dialogue()
    end
end

-- ---------------------------------------------------------------------------
-- attract AI
-- ---------------------------------------------------------------------------

local function ai_hub()
    if day > GAME_DAYS then
        begin_ending()
        return
    end
    if energy <= 0 then
        do_action(5)
        return
    end
    local focus = 1
    if day % 4 == 0 and get_aff(focus) >= 30 then
        if start_date(focus) then return end
    end
    if day % 2 == 0 then
        local t = 1
        if get_aff(focus) >= 20 then t = 3 elseif day % 6 == 0 then t = 2 end
        if start_chat(focus, t) then return end
    end
    local k = 1
    if day % 3 == 0 then k = 2
    elseif day % 5 == 0 then k = 3 end
    do_action(k)
end

local function update_ai()
    if state == ST_TITLE then
        if attract then
            auto_t = auto_t + 1
            if auto_t >= 100 then
                auto_t = 0
                start_new_game()
            end
        end
        return
    end
    if state == ST_HUB and attract then
        auto_t = auto_t + 1
        if auto_t >= 14 then
            auto_t = 0
            ai_hub()
        end
    elseif state == ST_SUMMARY and attract then
        auto_t = auto_t + 1
        if auto_t >= 260 then
            auto_t = 0
            start_new_game()
        end
    end
end

-- ---------------------------------------------------------------------------
-- drawing helpers
-- ---------------------------------------------------------------------------

local function panel(x, y, w, h, r, g, b, a)
    draw_quad(x, y, w, h, r, g, b, a)
end

local function frame(x, y, w, h, r, g, b, a)
    draw_rect_outline(x, y, w, h, r, g, b, a)
end

local function label(text, x, y, s, r, g, b, a)
    draw_text(text, x, y, s, r, g, b, a)
end

local function label_center(text, cx, y, s, r, g, b, a)
    local w = text_width(text, s)
    draw_text(text, cx - w * 0.5, y, s, r, g, b, a)
end

local function rounded_button(key_hover, x, y, w, h, text, s, enabled)
    local hover = in_rect(mouse_x, mouse_y, x, y, w, h)
    local base = 0.10
    if hover and enabled then base = 0.17 end
    panel(x, y, w, h, base, base + 0.02, base + 0.04, 0.9)
    if enabled then
        frame(x, y, w, h, 0.90, 0.78, 0.46, hover and 1.0 or 0.65)
        label_center(text, x + w * 0.5, y + h * 0.5 - 11.0, s, 0.96, 0.92, 0.82, 1.0)
    else
        frame(x, y, w, h, 0.38, 0.36, 0.34, 0.5)
        label_center(text, x + w * 0.5, y + h * 0.5 - 11.0, s, 0.52, 0.50, 0.48, 1.0)
    end
end

local function draw_background(id)
    local tex = bg_tex(id)
    if tex ~= 0 then
        sprite_draw(tex, 0.0, 0.0, SW, SH)
    else
        draw_quad(0.0, 0.0, SW, SH, 0.08, 0.10, 0.16, 1.0)
    end
end

local function draw_portrait(g, expr, x, y, scale)
    if g < 1 or g > 4 then return end
    local tex = portrait_tex(g, expr)
    if tex ~= 0 then
        local w = 320.0 * scale
        local h = 480.0 * scale
        panel(x + 12.0, y + h - 38.0, w - 24.0, 30.0, 0.0, 0.0, 0.0, 0.25)
        sprite_draw(tex, x, y, w, h)
    end
end

local function draw_bust(g, expr, x, y, size)
    if g < 1 or g > 4 then return end
    local tex = portrait_tex(g, expr)
    if tex ~= 0 then
        sprite_draw_region(tex, 0.0, 0.0, 320.0, 320.0, x, y, size, size)
    end
end

local function draw_petals()
    if state == ST_TITLE or (state == ST_DIALOGUE and (dlg_mode == DM_INTRO or dlg_mode == DM_FESTIVAL)) then
        if frame_no % 18 == 0 then
            local x = rng_float() * SW
            part_emit(petal_fx, 2, x, -10.0)
        end
    end
    part_draw()
end

-- ---------------------------------------------------------------------------
-- title / hub / topics / summary
-- ---------------------------------------------------------------------------

local function draw_title()
    draw_background(0)
    draw_petals()
    panel(0.0, 0.0, SW, SH, 0.05, 0.03, 0.08, 0.30)
    panel(0.0, 36.0, SW, 118.0, 0.04, 0.02, 0.07, 0.58)
    frame(0.0, 36.0, SW, 118.0, 0.88, 0.74, 0.46, 0.45)
    label_center("心 动 回 忆", SW * 0.5, 58.0, 1.15, 1.0, 0.92, 0.76, 1.0)
    label_center("SAKURA DAYS  ·  A FAKE2D VISUAL NOVEL", SW * 0.5, 112.0, 0.38,
                 0.95, 0.82, 0.86, 0.95)
    label_center("和四位女孩一起，留下不会后悔的春天。", SW * 0.5, 140.0, 0.42,
                 0.88, 0.90, 0.96, 0.9)

    local i = 0
    while i < 4 do
        draw_bust(i + 1, SMILE, 10.0 + i * 176.0, 370.0, 170.0)
        i = i + 1
    end

    local bx = 720.0
    rounded_button(0, bx, 190.0, 210.0, 48.0, "开 始 游 戏", 0.48, true)
    rounded_button(0, bx, 250.0, 210.0, 48.0, "继 续 游 戏", 0.48, save_exists > 0)
    rounded_button(0, bx, 310.0, 210.0, 48.0, "退 出", 0.48, true)
    panel(0.0, 156.0, 360.0, 26.0, 0.04, 0.03, 0.07, 0.55)
    label("点击 / Space 确认　F5 存档　F9 读档", 22.0, 161.0, 0.30,
          0.90, 0.84, 0.86, 0.95)
end

local function draw_energy_hearts()
    local i = 0
    while i < 5 do
        local x = 470.0 + i * 26.0
        if i < energy then
            draw_quad(x, 12.0, 16.0, 12.0, 1.0, 0.24, 0.42, 1.0)
            draw_quad(x + 2.0, 9.0, 12.0, 9.0, 1.0, 0.24, 0.42, 1.0)
            draw_quad(x + 2.0, 21.0, 12.0, 5.0, 1.0, 0.24, 0.42, 1.0)
        else
            draw_quad(x, 12.0, 16.0, 12.0, 0.32, 0.18, 0.24, 1.0)
            draw_quad(x + 2.0, 9.0, 12.0, 9.0, 0.32, 0.18, 0.24, 1.0)
            draw_quad(x + 2.0, 21.0, 12.0, 5.0, 0.32, 0.18, 0.24, 1.0)
        end
        i = i + 1
    end
    label("体力", 408.0, 8.0, 0.30, 0.84, 0.72, 0.78, 1.0)
    label(string.format("%d/5", energy), 408.0, 27.0, 0.24,
          0.92, 0.82, 0.84, 1.0)
end

local function draw_stats()
    panel(16.0, 62.0, 250.0, 138.0, 0.05, 0.03, 0.08, 0.82)
    frame(16.0, 62.0, 250.0, 138.0, 0.86, 0.72, 0.44, 0.7)
    label("能 力", 30.0, 72.0, 0.36, 0.96, 0.86, 0.56, 1.0)
    local i = 0
    while i < 3 do
        local v = stat_value(i + 1)
        local name = "学力"
        local cr, cg, cb = 0.40, 0.68, 0.96
        if i == 1 then name = "体能"; cr, cg, cb = 0.46, 0.86, 0.52 end
        if i == 2 then name = "魅力"; cr, cg, cb = 0.96, 0.62, 0.80 end
        label(name, 30.0, 102.0 + i * 30.0, 0.30, 0.84, 0.86, 0.90, 1.0)
        panel(88.0, 106.0 + i * 30.0, 140.0, 10.0, 0.10, 0.08, 0.12, 1.0)
        panel(88.0, 106.0 + i * 30.0, 140.0 * (v / 40.0), 10.0, cr, cg, cb, 1.0)
        frame(88.0, 106.0 + i * 30.0, 140.0, 10.0, 0.36, 0.32, 0.36, 0.8)
        label(string.format("%d", math.floor(v)), 236.0, 101.0 + i * 30.0, 0.27,
              0.86, 0.86, 0.82, 1.0)
        i = i + 1
    end
end

local function draw_action_bar()
    local names = {"学习", "锻炼", "打扮", "打工", "休息"}
    local subs = {"学力+3", "体能+3", "魅力+3", "+25G", "体力+2"}
    local i = 0
    while i < 5 do
        local x = 16.0 + i * 94.0
        local y = 452.0
        local enabled = (energy > 0) or i == 4
        rounded_button(0, x, y, 90.0, 64.0, names[i + 1], 0.46, enabled)
        label(subs[i + 1], x + 8.0, y + 44.0, 0.25, 0.82, 0.88, 0.80, 0.95)
        i = i + 1
    end
    label("每项行动消耗 1 点体力并推进一天", 16.0, 426.0, 0.28,
          0.76, 0.70, 0.76, 0.9)
end

local function draw_girl_cards()
    local i = 0
    while i < 4 do
        local g = i + 1
        local y = 70.0 + i * 96.0
        local hover = in_rect(mouse_x, mouse_y, 540.0, y, 404.0, 86.0)
        panel(540.0, y, 404.0, 86.0, 0.05, 0.03, 0.08, hover and 0.9 or 0.78)
        local gr = GIRLS[g].r
        local gg = GIRLS[g].g
        local gb = GIRLS[g].b
        frame(540.0, y, 404.0, 86.0, gr, gg, gb, hover and 1.0 or 0.55)
        local expr = NEUTRAL
        local a = get_aff(g)
        if a >= 60 then expr = BLUSH elseif a >= 30 then expr = SMILE end
        local ptex = portrait_tex(g, expr)
        if ptex ~= 0 then
            sprite_draw_region(ptex, 0.0, 0.0, 320.0, 300.0,
                               546.0, y + 8.0, 64.0, 60.0)
        end
        label(GIRLS[g].name, 618.0, y + 8.0, 0.42, 0.96, 0.90, 0.84, 1.0)
        local trait = "学力"
        if GIRLS[g].pref == 2 then trait = "体能" end
        if GIRLS[g].pref == 3 then trait = "魅力" end
        label("偏好: " .. trait, 618.0, y + 30.0, 0.24, 0.70, 0.76, 0.78, 0.9)
        panel(618.0, y + 48.0, 190.0, 8.0, 0.12, 0.08, 0.12, 1.0)
        panel(618.0, y + 48.0, 190.0 * (a / 100.0), 8.0, 0.92, 0.48, 0.62, 1.0)
        frame(618.0, y + 48.0, 190.0, 8.0, 0.40, 0.30, 0.34, 0.8)
        label(string.format("%d", math.floor(a)), 812.0, y + 43.0, 0.24,
              0.94, 0.74, 0.78, 1.0)
        rounded_button(0, 826.0, y + 48.0, 104.0, 30.0, "约 会", 0.26, a >= 30 and energy > 0)
        label("点击卡片聊天", 646.0, y + 64.0, 0.22, 0.62, 0.58, 0.64, 0.9)
        i = i + 1
    end
end

local function draw_hub()
    draw_background(1)
    panel(0.0, 0.0, SW, SH, 0.02, 0.02, 0.04, 0.20)
    panel(0.0, 0.0, SW, 52.0, 0.04, 0.03, 0.07, 0.92)
    panel(0.0, 50.0, SW, 2.0, 0.88, 0.74, 0.42, 0.8)
    label("SAKURA DAYS", 18.0, 10.0, 0.44, 0.98, 0.90, 0.72, 1.0)
    label(string.format("第 %d 天 / %d", day, GAME_DAYS), 210.0, 10.0, 0.40,
          0.94, 0.90, 0.82, 1.0)
    label("¥ " .. string.format("%d", math.floor(money)), 340.0, 10.0, 0.34,
          0.96, 0.86, 0.48, 1.0)
    draw_energy_hearts()
    panel(282.0, 64.0, 240.0, 66.0, 0.04, 0.03, 0.07, 0.76)
    frame(282.0, 64.0, 240.0, 66.0, 0.86, 0.72, 0.44, 0.6)
    label_center("今天要做些什么？", 402.0, 72.0, 0.44, 0.96, 0.90, 0.78, 1.0)
    label_center("和喜欢的女孩说说话，也让自己的能力成长起来。", 402.0, 104.0, 0.28,
                 0.82, 0.80, 0.82, 0.95)

    draw_stats()
    draw_action_bar()
    draw_girl_cards()

    if day >= 10 then
        rounded_button(0, 744.0, 12.0, 204.0, 28.0, "毕 业 典 礼", 0.26, true)
    end
    rounded_button(0, 600.0, 12.0, 64.0, 28.0, "存 档", 0.24, true)
    rounded_button(0, 670.0, 12.0, 64.0, 28.0, "读 档", 0.24, save_exists > 0)

    if hub_toast_t > 0 then
        local text = toast_text(hub_toast)
        local w = text_width(text, 0.42) + 32.0
        panel((SW - w) * 0.5, 188.0, w, 34.0, 0.22, 0.10, 0.16, 0.92)
        frame((SW - w) * 0.5, 188.0, w, 34.0, 0.92, 0.62, 0.72, 0.9)
        label_center(text, SW * 0.5, 198.0, 0.42, 1.0, 0.86, 0.88, 1.0)
    end
end

local function draw_topics()
    draw_background(1)
    panel(0.0, 0.0, SW, SH, 0.02, 0.02, 0.04, 0.30)
    panel(0.0, 0.0, SW, SH, 0.03, 0.02, 0.05, 0.62)
    panel(120.0, 62.0, 720.0, 408.0, 0.05, 0.03, 0.08, 0.95)
    frame(120.0, 62.0, 720.0, 408.0, 0.92, 0.76, 0.46, 0.85)

    local g = topics_girl
    local a = get_aff(g)
    local expr = NEUTRAL
    if a >= 60 then expr = BLUSH elseif a >= 30 then expr = SMILE end
    draw_bust(g, expr, 140.0, 120.0, 230.0)
    label(GIRLS[g].name, 370.0, 84.0, 0.56, 0.98, 0.90, 0.82, 1.0)
    label(string.format("好感度 %d / 100", math.floor(a)), 370.0, 130.0, 0.34,
          0.94, 0.72, 0.80, 1.0)
    panel(370.0, 160.0, 220.0, 8.0, 0.12, 0.08, 0.12, 1.0)
    panel(370.0, 160.0, 220.0 * (a / 100.0), 8.0, 0.92, 0.48, 0.62, 1.0)
    frame(370.0, 160.0, 220.0, 8.0, 0.40, 0.30, 0.34, 0.8)

    local topics = {"兴 趣", "校 园", "心 事"}
    local i = 0
    while i < 3 do
        local enabled = energy > 0
        if i == 2 and get_aff(g) < 20 then enabled = false end
        local t = topics[i + 1]
        if i == 2 and get_aff(g) < 20 then t = "心 事（好感 20 解锁）" end
        rounded_button(0, 392.0, 116.0 + i * 74.0, 388.0, 60.0, t, 0.38, enabled)
        i = i + 1
    end
    rounded_button(0, 392.0, 348.0, 186.0, 46.0, "送礼物 ¥20", 0.30, money >= 20)
    rounded_button(0, 594.0, 348.0, 186.0, 46.0, "约 会", 0.30, a >= 30 and energy > 0)
    rounded_button(0, 392.0, 406.0, 388.0, 40.0, "返 回", 0.30, true)
    label("聊天/约会消耗 1 点体力并推进一天", 150.0, 438.0, 0.24,
          0.70, 0.66, 0.72, 0.9)
end

local function draw_dialogue()
    local sc = current_scene()
    draw_background(sc.bg)
    draw_petals()

    local g = sc.girl
    local expr = NEUTRAL
    local who = WHO_NARRATOR
    local text = ""
    if dlg_after then
        g = dlg_after_girl
        expr = dlg_after_expr
        who = g
        text = after_text(dlg_after_gain)
    else
        who = sc.who[dlg_line + 1]
        if who == nil then who = WHO_NARRATOR end
        expr = sc.expr[dlg_line + 1]
        if expr == nil then expr = NEUTRAL end
        text = sc.lines[dlg_line + 1]
        if text == nil then text = "" end
    end

    if g >= 1 then
        local bob = math.sin(frame_no * 0.02) * 3.0
        draw_portrait(g, expr, 24.0 + dlg_portrait_x, 34.0 + bob, 1.0)
    end

    local name = "旁白"
    local nr, ng, nb = 0.72, 0.72, 0.78
    if who == WHO_PLAYER then
        name = "我"
        nr, ng, nb = 0.78, 0.88, 0.98
    elseif who >= 1 and who <= 4 then
        name = GIRLS[who].name
        nr, ng, nb = GIRLS[who].r, GIRLS[who].g, GIRLS[who].b
    end

    panel(24.0, 352.0, 912.0, 170.0, 0.04, 0.03, 0.07, 0.92)
    frame(24.0, 352.0, 912.0, 170.0, 0.90, 0.76, 0.46, 0.8)
    if who ~= WHO_NARRATOR then
        local nw = text_width(name, 0.44) + 30.0
        panel(50.0, 334.0, nw, 34.0, nr, ng, nb, 0.92)
        frame(50.0, 334.0, nw, 34.0, 1.0, 1.0, 1.0, 0.5)
        label(name, 60.0, 341.0, 0.44, 1.0, 1.0, 1.0, 1.0)
    end

    local total = utf8_len(text)
    local shown = math.floor(dlg_char)
    if shown > total then shown = total end
    local visible = utf8_prefix(text, shown)
    draw_text_ex(visible, 56.0, 382.0, 0.78, 0.96, 0.94, 0.90, 1.0, 0, 856.0)

    if dlg_choice then
        local opts = sc.opts
        local i = 0
        while i < #opts do
            local y = 118.0 + i * 64.0
            local hover = in_rect(mouse_x, mouse_y, 500.0, y, 400.0, 52.0)
            panel(500.0, y, 400.0, 52.0, hover and 0.24 or 0.10, 0.08, 0.14, 0.94)
            frame(500.0, y, 400.0, 52.0, 0.94, 0.78, 0.48, hover and 1.0 or 0.7)
            label(string.format("%d. ", i + 1), 514.0, y + 14.0, 0.34,
                  0.92, 0.82, 0.56, 1.0)
            label(opts[i + 1], 548.0, y + 12.0, 0.40, 0.98, 0.94, 0.88, 1.0)
            i = i + 1
        end
    else
        local complete = shown >= total
        if complete then
            if math.floor(frame_no / 30) % 2 == 0 then
                label("▼ 点击继续", 828.0, 496.0, 0.28, 0.92, 0.84, 0.66, 0.95)
            end
        end
    end

    if aff_toast_t > 0 then
        local text2 = string.format("好感 %+d", aff_toast_gain)
        local w = text_width(text2, 0.44) + 28.0
        panel(330.0 - w * 0.5, 306.0, w, 32.0, 0.30, 0.08, 0.16, 0.92)
        frame(330.0 - w * 0.5, 306.0, w, 32.0, 1.0, 0.62, 0.76, 0.9)
        label_center(text2, 330.0, 314.0, 0.44, 1.0, 0.82, 0.88, 1.0)
    end

    if dlg_fade > 0.0 then
        panel(0.0, 0.0, SW, SH, 0.0, 0.0, 0.0, dlg_fade * 0.65)
    end
end

local function draw_summary()
    draw_background(6)
    panel(0.0, 0.0, SW, SH, 0.04, 0.03, 0.07, 0.72)
    local g = ending_girl
    if g < 1 then g = 1 end
    local expr = SAD
    if ending_success then expr = SMILE end
    draw_bust(g, expr, 540.0, 150.0, 380.0)
    panel(40.0, 48.0, 500.0, 392.0, 0.04, 0.03, 0.07, 0.9)
    frame(40.0, 48.0, 500.0, 392.0, 0.94, 0.78, 0.48, 0.85)

    if ending_success then
        label("GOOD END", 64.0, 68.0, 0.62, 1.0, 0.86, 0.52, 1.0)
        label("春天最后的告白，成功了。", 64.0, 120.0, 0.42, 0.96, 0.90, 0.84, 1.0)
        draw_text_ex("毕业典礼结束后，天台上只剩我们两个人。樱花落在她的肩上，她没有躲开。",
                     64.0, 160.0, 0.34, 0.88, 0.86, 0.86, 1.0, 0, 450.0)
    else
        label("NORMAL END", 64.0, 68.0, 0.62, 0.80, 0.84, 0.92, 1.0)
        label("春天结束了，那句话留在风里。", 64.0, 120.0, 0.42, 0.86, 0.88, 0.90, 1.0)
        draw_text_ex("也许下一次，我会更勇敢一点。青春带着一点遗憾，也依然值得纪念。",
                     64.0, 160.0, 0.34, 0.82, 0.82, 0.86, 1.0, 0, 450.0)
    end

    label("能力", 64.0, 236.0, 0.34, 0.94, 0.86, 0.62, 1.0)
    label("学力 " .. string.format("%d", math.floor(study)) ..
          "   体能 " .. string.format("%d", math.floor(sport)) ..
          "   魅力 " .. string.format("%d", math.floor(charm)), 64.0, 264.0, 0.30,
          0.82, 0.84, 0.88, 1.0)
    label("好感度", 64.0, 296.0, 0.34, 0.94, 0.86, 0.62, 1.0)
    local i = 0
    while i < 4 do
        local g2 = i + 1
        local a = get_aff(g2)
        local bar = 200.0
        label(GIRLS[g2].name, 64.0, 326.0 + i * 26.0, 0.28, 0.90, 0.86, 0.88, 1.0)
        panel(120.0, 330.0 + i * 26.0, bar, 7.0, 0.12, 0.08, 0.12, 1.0)
        panel(120.0, 330.0 + i * 26.0, bar * (a / 100.0), 7.0, 0.92, 0.48, 0.62, 1.0)
        label(string.format("%d", math.floor(a)), 330.0, 324.0 + i * 26.0, 0.24,
              0.94, 0.74, 0.78, 1.0)
        i = i + 1
    end

    rounded_button(0, 60.0, 460.0, 200.0, 46.0, "重 新 开 始", 0.42, true)
    rounded_button(0, 280.0, 460.0, 200.0, 46.0, "返 回 标 题", 0.42, true)
end

-- ---------------------------------------------------------------------------
-- init / update
-- ---------------------------------------------------------------------------

local function setup_scene()
    bg_gate = sprite_load("assets/vn/bg/gate.png")
    bg_classroom = sprite_load("assets/vn/bg/classroom.png")
    bg_library = sprite_load("assets/vn/bg/library.png")
    bg_gym = sprite_load("assets/vn/bg/gym.png")
    bg_park = sprite_load("assets/vn/bg/park.png")
    bg_cafe = sprite_load("assets/vn/bg/cafe.png")
    bg_rooftop = sprite_load("assets/vn/bg/rooftop.png")
    bg_festival = sprite_load("assets/vn/bg/festival.png")

    p_linxia_n = sprite_load("assets/vn/portraits/linxia_neutral.png")
    p_linxia_s = sprite_load("assets/vn/portraits/linxia_smile.png")
    p_linxia_b = sprite_load("assets/vn/portraits/linxia_blush.png")
    p_linxia_d = sprite_load("assets/vn/portraits/linxia_sad.png")
    p_suqing_n = sprite_load("assets/vn/portraits/suqing_neutral.png")
    p_suqing_s = sprite_load("assets/vn/portraits/suqing_smile.png")
    p_suqing_b = sprite_load("assets/vn/portraits/suqing_blush.png")
    p_suqing_d = sprite_load("assets/vn/portraits/suqing_sad.png")
    p_guyu_n = sprite_load("assets/vn/portraits/guyu_neutral.png")
    p_guyu_s = sprite_load("assets/vn/portraits/guyu_smile.png")
    p_guyu_b = sprite_load("assets/vn/portraits/guyu_blush.png")
    p_guyu_d = sprite_load("assets/vn/portraits/guyu_sad.png")
    p_tangxin_n = sprite_load("assets/vn/portraits/tangxin_neutral.png")
    p_tangxin_s = sprite_load("assets/vn/portraits/tangxin_smile.png")
    p_tangxin_b = sprite_load("assets/vn/portraits/tangxin_blush.png")
    p_tangxin_d = sprite_load("assets/vn/portraits/tangxin_sad.png")

    petal_fx = part_create()
    part_set_lifetime(petal_fx, 3.5, 6.5)
    part_set_speed(petal_fx, 16, 46)
    part_set_direction(petal_fx, 1.2616, 0.5)
    part_set_size(petal_fx, 4, 8)
    part_set_gravity(petal_fx, 0, 10)
    part_set_drag(petal_fx, 0.3)
    part_set_spin(petal_fx, 4)
    part_set_color(petal_fx, 1.0, 0.72, 0.82, 1.0, 0.0)

    local i2 = 1
    while i2 <= 7 do
        if font_load(FONTS[i2].path, 30.0) then
            font_ok = 1
            log_info("vn: font " .. FONTS[i2].name)
            i2 = 99
        else
            i2 = i2 + 1
        end
    end
    if font_ok == 0 then log_info("vn: font fallback (ASCII only)") end

    storage_load("vn_quick")
    save_exists = 0
    if storage_has("vn_day") then save_exists = 1 end

    audio_loop("vn_theme", 0.16, 1.0)
    camera_set_position(0.0, 0.0)
    camera_set_zoom(1.0)
    log_info("vn: story start")
end

local function update_input()
    local clicked = input_mouse_pressed(0)
    if state == ST_TITLE then
        update_title_input(clicked)
    elseif state == ST_HUB then
        update_hub_input(clicked)
    elseif state == ST_TOPICS then
        update_topics_input(clicked)
    elseif state == ST_DIALOGUE then
        update_dialogue_input(clicked)
    elseif state == ST_SUMMARY then
        update_summary_input(clicked)
    end
end

local function assets_ready()
    return bg_gate ~= 0 and p_linxia_n ~= 0
end

function update(dt)
    frame_no = frame_no + 1
    if not inited then
        inited = true
        setup_scene()
    end

    mouse_x = input_mouse_x()
    mouse_y = input_mouse_y()

    if not assets_ready() then
        draw_text("VN ASSETS FAILED TO LOAD", 40.0, 60.0, 0.7, 1.0, 0.4, 0.4, 1.0)
        return 0
    end

    if any_activity() then
        idle_frames = 0
        if attract then attract = false end
    else
        idle_frames = idle_frames + 1
        if idle_frames > 80 then attract = true end
    end

    if state == ST_HUB then
        if input_key_pressed("f5") then save_game() end
        if input_key_pressed("f9") then load_game() end
    end

    update_input()
    if state == ST_DIALOGUE then
        update_dialogue()
    end
    update_ai()
    if hub_toast_t > 0 then hub_toast_t = hub_toast_t - 1 end

    if state == ST_TITLE then
        draw_title()
    elseif state == ST_HUB then
        draw_hub()
    elseif state == ST_TOPICS then
        draw_topics()
    elseif state == ST_DIALOGUE then
        draw_dialogue()
    else
        draw_summary()
    end
    return 0
end
