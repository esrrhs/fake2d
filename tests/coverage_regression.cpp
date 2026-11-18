// Focused, always-on assertions for recently added map/render/snapshot APIs.
#include "fake2d/object_snapshot.h"
#include "fake2d/lua_ui.h"
#include "fake2d/engine.h"
#include <array>
#include <set>
#include "fake2d/procedural_tex.h"
#include "fake2d/province_overlay.h"
#include "platform/window.h"
#include "render/gl.h"
#ifdef FAKE2D_WITH_FAKELUA
#include <fakelua.h>
#endif
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <vector>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); throw std::runtime_error(#x); } ++checks; } while (false)
static int checks = 0;
using namespace fake2d;

static std::vector<unsigned char> pixels(const Texture2D &tex) {
    CHECK(tex.IsValid());
    std::vector<unsigned char> data(static_cast<std::size_t>(tex.Width()) * tex.Height() * 4);
    tex.Bind();
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, data.data());
    CHECK(glGetError() == GL_NO_ERROR);
    return data;
}

static void procedural_tests() {
    using namespace procedural_tex;
    CHECK(!CreateSolidCircle(0).IsValid());
    CHECK(!CreateShadowNineSlice(-1).IsValid());
    CHECK(!CreatePaperGrain(0).IsValid());
    CHECK(!CreateBeveledDisc(-2).IsValid());
    auto circle = CreateSolidCircle(32, 24, 19, 14);
    auto p = pixels(circle);
    CHECK(p[3] == 0);
    CHECK(p[(16 * 32 + 16) * 4 + 3] == 255);
    for (std::size_t i = 0; i < p.size(); i += 4) {
        CHECK(p[i] == 24 && p[i+1] == 19 && p[i+2] == 14);
    }
    auto shadow = CreateShadowNineSlice(32, 2, 15);
    p = pixels(shadow);
    CHECK(p[3] == 0 && p[(16 * 32 + 16) * 4 + 3] == 255);
    auto paper = CreatePaperGrain(32);
    auto paper2 = CreatePaperGrain(32);
    p = pixels(paper);
    CHECK(p == pixels(paper2)); // deterministic output, no hidden RNG state
    bool varied = false;
    for (std::size_t i = 0; i < p.size(); i += 4) {
        CHECK(p[i+3] == 255 && p[i] >= p[i+1] && p[i+1] >= p[i+2]);
        varied |= p[i] != p[0];
    }
    CHECK(varied);
    auto disc = CreateBeveledDisc(32);
    p = pixels(disc);
    CHECK(p[3] == 0 && p[(16 * 32 + 16) * 4 + 3] == 255);
    auto moved = std::move(disc);
    CHECK(moved.IsValid() && !disc.IsValid());
    moved.Destroy(); moved.Destroy();
    CHECK(!moved.IsValid());
}

static void overlay_tests() {
    CHECK(LerpRgb24(0x102030, 0xaabbcc, -1) == 0x102030);
    CHECK(LerpRgb24(0x102030, 0xaabbcc, 2) == 0xaabbcc);
    CHECK(LerpRgb24(0, 0xffffff, .5f) == 0x7f7f7f);
    CHECK(Ramp3Rgb24(.5f, 0, 0x123456, 0xffffff) == 0x123456);
    CHECK(Ramp3Rgb24(1, 0, 0x123456, 0xffffff) == 0xffffff);
    std::vector<std::vector<int>> neighbors{{1, -1, 99}, {0,2}, {1}, {4}, {3}};
    std::vector<int> owners{7,7,8,7,7};
    std::vector<double> scores{2,3,100,10,20}, areas(5,1);
    std::vector<char> seen(5,0);
    auto comp = LargestConnectedComponent({-1,99,0,3}, neighbors, 7, owners, scores, areas, seen);
    std::sort(comp.begin(), comp.end());
    CHECK((comp == std::vector<int>{3,4}));
    CHECK(LargestConnectedComponent({}, neighbors, 7, owners, scores, areas, seen).empty());
    CHECK(!PlaceContiguousLabel({}).valid);
    ProvinceOverlay overlay;
    CHECK(!overlay.Build(0, 20, {}, 1));
    CHECK(!overlay.Build(2, 2, {1}, 1));
    const int n = 128;
    std::vector<std::uint16_t> ids(n*n,1);
    for (int y = 0; y < n; ++y) for (int x = n/2; x < n; ++x) ids[y*n+x] = 2;
    CHECK(overlay.Build(n,n,ids,2,2));
    CHECK(overlay.Width()==n && overlay.Height()==n && overlay.Supersample()==2);
    CHECK(overlay.HidMap().size()==static_cast<std::size_t>(n*n*4));
    CHECK(overlay.BBoxes().size() >= 2 && !overlay.Outlines().empty());
    overlay.ComputeBorderDistance({7,8},ids);
    CHECK(overlay.BorderDistance().size()==ids.size());
    CHECK(overlay.BorderDistance()[n/2*n+n/2] <= overlay.BorderDistance()[n/2*n+n/4]);
    OverlayPolylineSpec river;
    river.points = {{1,20},{20,22},{46,20}};
    OverlayPolylineSpec canal = river; canal.channel_b = true; canal.meander = false;
    overlay.RasterizePolylines({{},river,canal});
    CHECK(overlay.HasPolylines());
    OverlayCompositeParams params;
    params.inner_glow = true; params.occupation_stripes = true;
    overlay.CompositeAndUpload({0x112233,0x8899aa},{1,.5f},{0,1},{1,1},{0xff0000,0x00ff00},params);
    auto px = pixels(overlay.Texture());
    CHECK(px.size()==static_cast<std::size_t>(n*n*16));
    CHECK(std::any_of(px.begin(),px.end(),[](auto x){return x!=0;}));
    overlay.RasterizePolylines({});
    overlay.CompositeAndUpload({0x112233,0x8899aa},{1,.5f},{0,1},{1,1},{0xff0000,0x00ff00},params);
    CHECK(pixels(overlay.Texture()) != px); // empty rasterization clears channel content
    std::vector<std::uint16_t> label_ids(n*n,1);
    CHECK(overlay.Build(n,n,label_ids,1,1));
    CHECK(!overlay.HasPolylines()); // rebuilding invalidates the rasterized channel cache
    overlay.ComputeBorderDistance({0},label_ids);
    std::vector<int> regions{0};
    Vec2 centroid{n/2.0f,n/2.0f}; double area=n*n;
    RegionBBox label_bounds{0,0,static_cast<float>(n-1),static_cast<float>(n-1)};
    TerritoryLabelRequest req;
    req.width=n; req.height=n; req.region_count=1;
    req.region_ids=label_ids.data(); req.border_dist=overlay.BorderDistance().data();
    req.bboxes=&label_bounds; req.bbox_count=1;
    req.region_centroids=&centroid; req.region_areas=&area;
    req.component_regions=&regions; req.text_candidates={"Test"}; req.total_pop=10000;
    auto label=PlaceContiguousLabel(req);
    CHECK(label.valid && label.text=="Test" && label.size>0);
    CHECK(label.x>=0 && label.x<n && label.y>=0 && label.y<n);
}

#ifdef FAKE2D_WITH_FAKELUA
static void lua_ui_tests() {
    ColorPool colors;
    const Color red{1,0,0,.5f};
    const auto id=colors.Id(red);
    CHECK(id>0 && colors.Id(red)==id);
    CHECK(colors.Get(id).r==1 && colors.Get(id).a==.5f);
    CHECK(colors.Get(-1,Color{0,1,0,1}).g==1);
    colors.SetThemeColor("accent",red);
    CHECK(colors.ThemeId("accent")==id);
    CHECK(colors.ThemeId("missing",red)==id);
    colors.Clear(); CHECK(colors.Pack(1,0,0,.5)==1);
    colors.SetThemeColor("accent",red);
    fakelua::FakeluaStateGuard state;
    auto *st=state.GetState(); CHECK(st);
    Engine engine;
    Camera2D camera; camera.SetViewport(640,480);
    Input input;
    engine.GetUI().Bind(&input,&camera,nullptr);
    engine.GetUI().NewFrame();
    LuaUIHost host; host.colors=&colors; host.engine=&engine;
    std::set<std::string> registered;
    host.note_native=[&](const char *name){registered.insert(name);};
    RegisterCommonUILuaApi(nullptr,&host); RegisterCommonUILuaApi(st,nullptr);
    RegisterLayoutUILuaApi(nullptr,&host); RegisterLayoutUILuaApi(st,nullptr);
    CHECK(registered.empty());
    RegisterCommonUILuaApi(st,&host); RegisterLayoutUILuaApi(st,&host);
    CHECK(registered.contains("u_strf") && registered.contains("ui_push_anchor"));
    fakelua::RegisterNativeFunction(st,"expect",false,
        std::function<void(fakelua::State*,bool,long long)>([](fakelua::State*,bool ok,long long stage){
            if (!ok) std::fprintf(stderr,"Lua UI assertion stage %lld failed\n",stage);
            CHECK(ok);
        }));
    fakelua::CompileConfig cfg;
    cfg.debug_mode=false; fakelua::DisableAllJit(cfg); cfg.disable_jit[fakelua::JIT_INTERP]=false;
    fakelua::CompileString(st,R"LUA(
function coverage_ui()
    local r=u_hex(16711680,0.5)
    local b=u_col(0,0,1,1)
    local mix=u_mix(r,b,0.5)
    expect(u_col_get(mix,0)==0.5,1)
    expect(u_col_get(mix,1)==0,2)
    expect(u_col_get(mix,2)==0.5,3)
    expect(u_col_get(mix,3)==0.75,4)
    expect(u_col_get(u_col_a(r,0.25),3)==0.25,5)
    expect(u_col_get(u_dim(r,0.5),0)==0.5,6)
    expect(u_col_get(u_lighten(b,0.5,0.25,0),1)==0.25,7)
    expect(u_col_get(u_lerp_hex(0,16777215,0.5),0)==0.5,8)
    expect(u_col_get(u_theme("accent"),0)==1,9)
    expect(u_utf8_len("你好ab")==4,10)
    expect(u_utf8_first("你好")=="你",11)
    expect(u_utf8_cut("你好ab",3)=="你好a",12)
    expect(u_utf8_cut("abc",-1)=="",13)
    expect(u_strf("%s:%d:%.2f:%%","ok",7,1.25)=="ok:7:1.25:%",14)
    expect(u_strf("missing:%s/%d/%.1f")=="missing:/0/0.0",15)
    expect(u_strf("literal:%q/end:%")=="literal:%q/end:%",16)
    ui_push(10,20,200,120,0)
    expect(ui_avail_w()==200 and ui_avail_h()==120,17)
    ui_push_inset(5,6,7,8)
    expect(ui_avail_w()==188 and ui_avail_h()==106,18)
    ui_remember_box("inset")
    expect(ui_named("inset","x")==15 and ui_named("inset","y")==26,19)
    ui_pop()
    ui_pop()
    ui_push(10,20,200,120,0)
    ui_push_anchor("right","bottom",40,30,2,3)
    ui_remember_box("anchored")
    expect(ui_named("anchored","x")==168 and ui_named("anchored","y")==107,20)
    expect(ui_named("anchored","w")==40 and ui_named("anchored","h")==30,21)
    ui_pop()
    expect(ui_avail_h()==120,22)
    ui_push_fill(10,2)
    expect(ui_avail_w()==196 and ui_avail_h()==106,23)
    ui_pop()
    ui_pop()
    ui_text_set("edit","你好")
    expect(ui_text_get("edit")=="你好",24)
    expect(ui_auto_h("unknown",17)==17,25)
    expect(ui_set_token("gap_md",9),26)
    expect(ui_token("gap_md")==9,27)
    expect(not ui_set_token("not-a-token",9),28)
    return 123
end
)LUA",cfg);
    int result=0;
    fakelua::Call(st,fakelua::JIT_INTERP,"coverage_ui",result);
    CHECK(result==123);
}

static void snapshot_tests() {
    fakelua::FakeluaStateGuard state;
    auto *s=state.GetState(); CHECK(s);
    auto &mgr=fakelua::GetNativeObjectManager(s);
    long long group=mgr.CreateGroup();
    auto *root=mgr.GlobalCreate("coverage_world","World",1); CHECK(root);
    std::int64_t next=10;
    std::vector<fakelua::NativeObject*> items;
    ObjectSnapshotChain::Schema schema;
    schema.root_global="coverage_world";
    schema.is_transient=[](std::string_view, std::string_view key, int){return key.starts_with("tmp_");};
    ObjectSnapshotChain::View view{&root,&group,&next,{{"Item",&items,true}}};
    ObjectSnapshotChain chain;
    CHECK(chain.SnapshotFull(nullptr,schema,view).empty());
    CHECK(!chain.Reset(nullptr,schema,view,2026,11,1));
    root->SetInt("date_y",2026); root->SetInt("date_m",11); root->SetInt("date_d",1);
    root->SetInt("gold",100); root->SetFloat("ratio",1.25); root->SetBool("active",true);
    root->SetString("name","root"); root->SetString("tmp_note","keep");
    auto *child=mgr.Create(group,"Child",2); CHECK(child);
    child->SetInt("value",7); root->SetObject("child",child);
    auto *item=mgr.Create(group,"Item",3); CHECK(item);
    item->SetInt("count",5); items.push_back(item);
    const auto stamp=child->GetVersion();
    CHECK(child->GetInt("value")==7 && child->GetVersion()==stamp);
    CHECK(chain.Reset(s,schema,view,2026,11,1));
    auto full=chain.SnapshotFull(s,schema,view);
    CHECK(!full.empty());
    child->SetInt("value",42);
    CHECK(child->GetVersion()!=stamp); // API whose missing dependency broke CI
    root->SetInt("gold",200); next=20;
    CHECK(chain.CatchUp(s,schema,view,2026,11,2,86400));
    auto delta=chain.SnapshotForSave(s,schema,view,false,86400);
    CHECK(!delta.empty());
    CHECK(!chain.Materialize(s,schema,view).empty());
    auto flat=chain.SnapshotForSave(s,schema,view,true,86400);
    std::string error, warning;
    CHECK(chain.Restore(s,schema,view,delta,&error,&warning));
    CHECK(root->GetInt("gold")==200 && root->GetObject("child")->GetInt("value")==42);
    CHECK(next==20 && items.size()==1 && items[0]->GetInt("count")==5);
    CHECK(chain.Restore(s,schema,view,flat));
    CHECK(root->GetInt("gold")==200);
    CHECK(chain.Restore(s,schema,view,full));
    CHECK(root->GetInt("gold")==100 && root->GetString("tmp_note")=="keep");
    const auto trans=chain.SaveTransients(schema,view,true);
    root->SetString("tmp_note","changed");
    chain.LoadTransients(schema,view,trans,true);
    CHECK(root->GetString("tmp_note")=="keep");
    CHECK(!chain.Restore(s,schema,view,"bad"));
    auto bad_anchor=delta; bad_anchor[16]^=1;
    CHECK(!chain.Restore(s,schema,view,bad_anchor,&error,&warning));
    CHECK(error=="anchor checksum mismatch");
    auto bad_tail=delta; bad_tail.back()^=1; warning.clear();
    CHECK(chain.Restore(s,schema,view,bad_tail,&error,&warning));
    CHECK(!warning.empty() && root->GetInt("gold")==100);
    CHECK(chain.Restore(s,schema,view,delta.substr(0,delta.size()-3),&error,&warning));
    CHECK(root->GetInt("gold")==100);
    items.clear();
    CHECK(chain.CatchUp(s,schema,view,2026,11,3,172800));
    auto removed=chain.SnapshotForSave(s,schema,view,false,172800);
    CHECK(chain.Restore(s,schema,view,removed)); CHECK(items.empty());
    chain.Rebase(view,2026,11,3);
    CHECK(chain.CatchUp(s,schema,view,2026,12,1,172800)); // month reanchor
    chain.Clear(); CHECK(chain.Materialize(s,schema,view).empty());
    mgr.DestroyGroup(group); mgr.GlobalDestroy(schema.root_global);
}
#endif

int main() {
    try {
#ifdef FAKE2D_WITH_FAKELUA
        snapshot_tests();
#endif
        platform::Window window;
        platform::WindowDesc desc; desc.headless=true; desc.width=64; desc.height=64;
        CHECK(window.Create(desc));
        procedural_tests(); overlay_tests();
#ifdef FAKE2D_WITH_FAKELUA
        lua_ui_tests();
#endif
        std::printf("coverage regression: %d assertions passed\n", checks);
        return 0;
    } catch (const std::exception &e) {
        std::fprintf(stderr,"coverage regression failed: %s\n",e.what()); return 1;
    }
}
