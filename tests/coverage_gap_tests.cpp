#include "gtest_support.h"
#include "fake2d/atlas.h"
#include "fake2d/character.h"
#include "fake2d/engine.h"
#include "fake2d/log.h"
#include "fake2d/lua_ui.h"
#include "fake2d/node.h"
#include "fake2d/province_overlay.h"
#include "fake2d/rule_host.h"
#include "fake2d/scene.h"
#include "fake2d/skeleton.h"
#include <nlohmann/json.hpp>
#include "fake2d/ui_test_runner.h"
#include "platform/window.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#ifdef FAKE2D_WITH_FAKELUA
#include <fakelua.h>
#include "script/script_bindings.h"
#endif
using namespace fake2d;
static int checks=0;
static bool close(float a,float b){return std::abs(a-b)<.02f;}
struct Fixtures {
    std::filesystem::path dir=std::filesystem::path(".deps")/("gap-tests-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Fixtures(){std::filesystem::create_directories(dir);}
    ~Fixtures(){std::error_code ec;std::filesystem::remove_all(dir,ec);}
    std::string write(const std::string &name,const std::string &data)const{
        auto p=dir/name;std::ofstream out(p,std::ios::binary);out<<data;out.close();
        if(!out)throw std::runtime_error("fixture write");return p.string();
    }
    std::string read(const std::string &name)const{std::ifstream in(dir/name);return {std::istreambuf_iterator<char>(in),{}};}
};
static void bounds_regression(){
    ProvinceOverlay map;
    const int n=128;
    std::vector<std::uint16_t> ids(n*n,1);
    EXPECT_TRUE((map.Build(n,n,ids,1,1)) && (++checks, true));
    EXPECT_TRUE((map.BBoxes().size()==1) && (++checks, true));
    const auto &bb=map.BBoxes()[0];
    EXPECT_TRUE((close(bb.min_x,.5f)&&close(bb.min_y,.5f)) && (++checks, true));
    EXPECT_TRUE((close(bb.max_x,n-.5f)&&close(bb.max_y,n-.5f)) && (++checks, true));
    map.ComputeBorderDistance({0},ids);
    std::vector<int> component{0};Vec2 center{64,64};double area=n*n;
    TerritoryLabelRequest req;req.width=n;req.height=n;req.region_count=1;
    req.region_ids=ids.data();req.border_dist=map.BorderDistance().data();
    req.bboxes=map.BBoxes().data();req.bbox_count=1;req.region_centroids=&center;
    req.region_areas=&area;req.component_regions=&component;req.text_candidates={"Test"};
    auto label=PlaceContiguousLabel(req);
    EXPECT_TRUE((label.valid&&label.text=="Test"&&label.size>=11) && (++checks, true));
}
static std::string atlas_json(const std::string &image="sheet.ppm"){
 return R"({"frames":{"normal":{"frame":{"x":0,"y":0,"w":2,"h":2}},"rot":{"frame":{"x":0,"y":0,"w":2,"h":2},"rotated":true,"spriteSourceSize":{"x":3,"y":4},"sourceSize":{"w":8,"h":9},"pivot":{"x":0.25,"y":0.75}}},"meta":{"image":")"+image+R"("}})";
}
static void atlas_tests(Fixtures &f,Texture2D &sheet){
    Atlas atlas;std::string image;
    for(const auto &bad:{"not json","{}","{\"frames\":42}","{\"frames\":[]}","{\"frames\":{\"bad\":{}}}",
        "{\"frames\":[{\"filename\":\"x\",\"frame\":{\"x\":\"bad\"}}]}",
        "{\"frames\":{\"a\":{\"frame\":{\"w\":1}},\"z\":{\"frame\":{\"w\":\"bad\"}}}}"}){
        EXPECT_TRUE((!atlas.Parse(bad,image)) && (++checks, true));EXPECT_TRUE((image.empty()) && (++checks, true));EXPECT_TRUE((!atlas.IsValid()) && (++checks, true));EXPECT_TRUE((!atlas.GetRegion("a")) && (++checks, true));
    }
    EXPECT_TRUE((atlas.Parse(atlas_json(),image)&&image=="sheet.ppm") && (++checks, true));
    EXPECT_TRUE((!atlas.IsValid()) && (++checks, true));auto *r=atlas.GetRegion("rot");EXPECT_TRUE((r&&r->rotated) && (++checks, true));
    EXPECT_TRUE((r->offset.x==3&&r->offset.y==4&&r->source_size.x==8&&r->source_size.y==9) && (++checks, true));
    EXPECT_TRUE((r->pivot.x==.25f&&r->pivot.y==.75f) && (++checks, true));
    Rect rect;EXPECT_TRUE((atlas.GetRegionRect("normal",rect)&&rect.width==2) && (++checks, true));
    rect={1,2,3,4};EXPECT_TRUE((!atlas.GetRegionRect("missing",rect)&&rect.width==3) && (++checks, true));
    Texture2D invalid;EXPECT_TRUE((!atlas.LoadFromString(atlas_json(),invalid)) && (++checks, true));
    EXPECT_TRUE((!atlas.LoadFromString("{}",sheet)) && (++checks, true));
    EXPECT_TRUE((atlas.LoadFromString(atlas_json(),sheet)&&&atlas.GetTexture()==&sheet) && (++checks, true));
    EXPECT_TRUE((atlas.Parse(R"({"frames":[{"filename":"item","frame":{"w":3,"h":5},"sourceSize":null},{"filename":"bad","frame":null}]})",image)) && (++checks, true));
    r=atlas.GetRegion("item");EXPECT_TRUE((r&&r->source_size.x==3&&r->source_size.y==5&&r->pivot.x==.5f) && (++checks, true));
    EXPECT_TRUE((atlas.Parse(R"({"frames":{"x":{"frame":{"w":3,"h":5},"rotated":true}}})",image)) && (++checks, true));
    r=atlas.GetRegion("x");EXPECT_TRUE((r&&r->frame.width==5&&r->frame.height==3) && (++checks, true));
    atlas.BindTexture(sheet);EXPECT_TRUE((atlas.IsValid()) && (++checks, true));
    EXPECT_TRUE((!atlas.LoadFromFile((f.dir/"missing.json").string())) && (++checks, true));
    EXPECT_TRUE((!atlas.LoadFromFile(f.write("bad.json","not json"))) && (++checks, true));
    EXPECT_TRUE((!atlas.LoadFromFile(f.write("no-image.json",R"({"frames":{"x":{"frame":{}}}})"))) && (++checks, true));
    EXPECT_TRUE((!atlas.LoadFromFile(f.write("missing-image.json",atlas_json("missing.png")))) && (++checks, true));
    f.write("sheet.ppm",std::string("P6\n2 2\n255\n")+std::string(12,'\xff'));
    EXPECT_TRUE((atlas.LoadFromFile(f.write("atlas.json",atlas_json()))) && (++checks, true));
    EXPECT_TRUE((atlas.GetTexture().IsValid()&&atlas.GetTexture().Width()==2) && (++checks, true));
}
struct CornerProbe:QuadNode{using QuadNode::ComputeWorldCorners;};
struct TrackedNode:Node {int &dead;explicit TrackedNode(int &d):dead(d){}~TrackedNode()override{++dead;}};
static void node_tests(Renderer &renderer,Texture2D &sheet){
    Node root("root");root.GetTransform().SetPosition(10,20);
    auto child=std::make_unique<Node>("child");auto *c=child.get();c->GetTransform().SetPosition(1,2);
    auto grand=std::make_unique<Node>("grand");auto *g=grand.get();g->GetTransform().SetPosition(3,4);
    c->AddChild(std::move(grand));root.AddChild(std::move(child));root.AddChild(nullptr);
    auto pos=g->WorldMatrix().TransformPoint({0,0});EXPECT_TRUE((close(pos.x,14)&&close(pos.y,26)) && (++checks, true));
    root.GetTransform().SetPosition(100,200);
    pos=g->WorldMatrix().TransformPoint({0,0});EXPECT_TRUE((close(pos.x,104)&&close(pos.y,206)) && (++checks, true));
    pos=g->WorldMatrix().TransformPoint({0,0});EXPECT_TRUE((close(pos.x,104)) && (++checks, true));
    std::vector<std::string> names;root.Visit([&](Node *n){names.push_back(n->Name());});
    EXPECT_TRUE(((names==std::vector<std::string>{"root","child","grand"})) && (++checks, true));
    root.SetVisibleRecursive(false);EXPECT_TRUE((!root.Visible()&&!c->Visible()&&!g->Visible()) && (++checks, true));
    root.SetVisibleRecursive(true);EXPECT_TRUE((root.Visible()&&g->Visible()) && (++checks, true));
    EXPECT_TRUE((!root.RemoveChild(nullptr)) && (++checks, true));
    auto detached=root.RemoveChild(c);EXPECT_TRUE((detached&&c->Parent()==nullptr&&root.Children().empty()) && (++checks, true));
    pos=g->WorldMatrix().TransformPoint({0,0});EXPECT_TRUE((close(pos.x,4)&&close(pos.y,6)) && (++checks, true));
    Node other("other");other.GetTransform().SetPosition(50,60);(void)other.WorldMatrix();
    other.AddChild(std::move(detached));pos=g->WorldMatrix().TransformPoint({0,0});
    EXPECT_TRUE((close(pos.x,54)&&close(pos.y,66)) && (++checks, true));
    root.RemoveFromParent();int dead=0;auto dying=std::make_unique<TrackedNode>(dead);auto *d=dying.get();
    root.AddChild(std::move(dying));d->RemoveFromParent();EXPECT_TRUE((dead==1&&root.Children().empty()) && (++checks, true));
    CornerProbe probe;probe.SetSize(10,20);probe.SetPivot({.5f,.5f});probe.GetTransform().SetPosition(30,40);probe.GetTransform().SetScale(2);
    Vec2 corners[4];probe.ComputeWorldCorners(corners);EXPECT_TRUE((close(corners[0].x,20)&&close(corners[0].y,20)) && (++checks, true));
    EXPECT_TRUE((close(corners[2].x,40)&&close(corners[2].y,60)) && (++checks, true));
    Atlas atlas;EXPECT_TRUE((atlas.LoadFromString(atlas_json(),sheet)) && (++checks, true));
    SpriteNode sprite; sprite.Draw(renderer);sprite.SetTexture(nullptr);sprite.Draw(renderer);
    sprite.SetAtlasRegion(nullptr,"normal");sprite.SetAtlasRegion(&atlas,"missing");
    sprite.SetTexture(&sheet);EXPECT_TRUE((sprite.TextureRegion().width==2) && (++checks, true));
    sprite.SetTextureRegion({0,0,0,0});sprite.Draw(renderer);
    sprite.SetSize(16,16);sprite.SetTextureRegion({0,0,2,2});sprite.GetTransform().SetPosition(20,0);
    const int target=renderer.GetRenderTargets().Create(64,64);EXPECT_TRUE((target>0) && (++checks, true));
    renderer.BeginFrame(0,0,0,1);EXPECT_TRUE((renderer.BeginRenderTarget(target)) && (++checks, true));
    renderer.ClearActiveTarget({0,0,0,1});sprite.Draw(renderer);
    sprite.SetAtlasRegion(&atlas,"rot");sprite.GetTransform().SetPosition(20,20);sprite.Draw(renderer);
    RectNode box;box.SetSize(10,10);box.SetTint({0,0,1,1});box.Draw(renderer);
    renderer.EndRenderTarget();renderer.EndFrame();Color pixel;
    auto *rt=renderer.GetRenderTargets().Get(target);EXPECT_TRUE((rt&&rt->ReadPixel(5,5,pixel)&&pixel.b>.9f&&pixel.r<.1f) && (++checks, true));
    EXPECT_TRUE((rt->ReadPixel(22,2,pixel)&&pixel.r>.9f&&pixel.g<.1f) && (++checks, true));
    EXPECT_TRUE((rt->ReadPixel(22,22,pixel)&&pixel.g>.9f&&pixel.r<.1f) && (++checks, true));
    Scene scene;std::vector<int> order;
    struct Ordered:Node {int id;std::vector<int>&order;Ordered(int i,std::vector<int>&o):id(i),order(o){}void OnDraw(Renderer&)override{order.push_back(id);}};
    for(auto spec:std::vector<std::pair<int,int>>{{1,2},{2,0},{3,2},{4,-1}}){
        auto n=std::make_unique<Ordered>(spec.first,order);n->SetLayer(spec.second);n->SetZ(0);if(spec.first==4)n->SetVisible(false);scene.Root().AddChild(std::move(n));}
    renderer.BeginFrame(0,0,0,1);scene.Draw(renderer);renderer.EndFrame();
    EXPECT_TRUE(((order==std::vector<int>{1,2,3})) && (++checks, true));EXPECT_TRUE((!renderer.GetSpriteBatch().IsSorted()) && (++checks, true));int visits=0;scene.Visit([&](Node*){++visits;});EXPECT_TRUE((visits==5) && (++checks, true));
    renderer.GetRenderTargets().Destroy(target);
}
static std::string map_json(int cols,int rows,const std::vector<int>&data){
    std::string json="{\"width\":"+std::to_string(cols)+",\"height\":"+std::to_string(rows)+R"(,"orientation":"orthogonal","tilewidth":16,"tileheight":16,"tilesets":[{"firstgid":1,"tilewidth":16,"tileheight":16,"tilecount":2,"columns":2,"tiles":[{"id":0,"properties":[{"name":"solid","type":"bool","value":true}]},{"id":1,"properties":[{"name":"oneway","type":"bool","value":true}]}]}],"layers":[{"type":"tilelayer","name":"ground","data":[)";
    for(std::size_t i=0;i<data.size();++i){if(i)json+=',';json+=std::to_string(data[i]);}return json+"]}]}";
}
static void character_tests(){
    ResourceManager resources;Tilemap map;std::vector<int> data(20*12,0);
    for(int x=0;x<20;++x)data[10*20+x]=1;
    for(int y=2;y<10;++y){data[y*20]=1;data[y*20+12]=1;}
    data[5*20+4]=2;data[5*20+5]=2;data[5*20+6]=2;data[5*20+8]=1; // solid ceiling
    EXPECT_TRUE((map.LoadFromString(map_json(20,12,data),".",resources)) && (++checks, true));
    EXPECT_TRUE((map.SolidAt(2,10)&&map.OneWayAt(4,5)) && (++checks, true));
    CharacterController ch;ch.Spawn({32,16});EXPECT_TRUE((!ch.Update(.01f,{}).on_ground&&ch.Position().x==32) && (++checks, true));
    ch.SetMap(&map);auto tuning=ch.Tuning();tuning.half_extents={4,7};ch.SetTuning(tuning);
    auto p=ch.Position();ch.Update(0,{});EXPECT_TRUE((ch.Position().x==p.x&&ch.Position().y==p.y) && (++checks, true));
    bool landed=false;for(int i=0;i<120;++i){auto frame=ch.Update(1.f/120,{});landed|=frame.landed;}
    EXPECT_TRUE((landed&&ch.OnGround()&&close(ch.Position().y,146)) && (++checks, true));
    ch.Spawn({32,20});EXPECT_TRUE((!ch.OnGround()&&ch.Velocity().y==0) && (++checks, true));
    EXPECT_TRUE((!ch.Update(.01f,{0,true,true}).jumped) && (++checks, true)); // grounded state cannot survive teleport
    ch.Spawn({32,146});ch.Update(.01f,{});EXPECT_TRUE((ch.OnGround()) && (++checks, true));
    ch.Update(.02f,{1,false,false});EXPECT_TRUE((ch.Velocity().x>0&&ch.Facing()==1) && (++checks, true));
    ch.Update(.02f,{});EXPECT_TRUE((ch.Velocity().x>=0) && (++checks, true));
    ch.Update(.03f,{-1,false,false});EXPECT_TRUE((ch.Facing()==-1&&ch.Velocity().x<0) && (++checks, true));
    ch.Update(.1f,{});EXPECT_TRUE((ch.Velocity().x==0) && (++checks, true));
    auto jump=ch.Update(.01f,{0,true,true});EXPECT_TRUE((jump.jumped&&ch.Velocity().y<0) && (++checks, true));
    float vy=ch.Velocity().y;ch.Update(.01f,{0,false,false});EXPECT_TRUE((ch.Velocity().y>vy*.6f) && (++checks, true));
    ch.Spawn({32,20});EXPECT_TRUE((!ch.OnGround()&&ch.Velocity().y==0) && (++checks, true));
    auto after_spawn=ch.Update(.01f,{0,true,true});EXPECT_TRUE((!after_spawn.jumped) && (++checks, true)); // no stale coyote/buffer
    ch.Spawn({25,130});for(int i=0;i<60;++i)ch.Update(.02f,{-1,false,false});
    EXPECT_TRUE((ch.Position().x>=16&&ch.Position().x<17) && (++checks, true));
    ch.Spawn({180,130});for(int i=0;i<60;++i)ch.Update(.02f,{1,false,false});
    EXPECT_TRUE((ch.Position().x<=184&&ch.Position().x>183) && (++checks, true));
    ch.Spawn({72,20});for(int i=0;i<120;++i)ch.Update(.01f,{});
    EXPECT_TRUE((ch.OnGround()&&close(ch.Position().y,66)) && (++checks, true)); // one-way catches from above
    auto j=ch.Update(.01f,{0,true,true});EXPECT_TRUE((j.jumped) && (++checks, true)); // passes upward through one-way
    ch.Spawn({72,82});for(int i=0;i<120;++i)ch.Update(.01f,{});
    EXPECT_TRUE((close(ch.Position().y,146)) && (++checks, true)); // spawn below one-way must not be pulled onto it
    ch.Spawn({72,146});for(int i=0;i<2;++i)ch.Update(.01f,{});
    for(int i=0;i<120;++i)ch.Update(.01f,{0,i==0,true});
    EXPECT_TRUE((ch.Position().y<=146&&ch.Bounds().width==8&&ch.Center().x==76) && (++checks, true));
    ch.Spawn({128,146});ch.Update(.01f,{});EXPECT_TRUE((ch.Update(.01f,{0,true,true}).jumped) && (++checks, true));
    float highest=ch.Position().y;bool ceiling_stop=false;
    for(int i=0;i<45;++i){ch.Update(.01f,{0,false,true});highest=std::min(highest,ch.Position().y);ceiling_stop|=ch.Velocity().y==0&&!ch.OnGround();}
    EXPECT_TRUE((ceiling_stop&&highest>=96-.02f) && (++checks, true)); // solid underside cancels ascent
    ch.Spawn({32,20});ch.Update(1.0f,{});EXPECT_TRUE((ch.OnGround()&&close(ch.Position().y,146)) && (++checks, true)); // substep anti-tunnelling
}
static void ui_runner_tests(Renderer &renderer){
    Engine engine;Camera2D camera;camera.SetViewport(128,128);auto &ui=engine.GetUI();auto &input=engine.GetInput();ui.Bind(&input,&camera,&renderer);
    int prepared=0,resets=0,after=0,clicks=0;std::size_t errors=0;bool fail_click=false;
    UITestTarget target;target.name="good";target.prepare=[&]{++prepared;};target.reset_state=[&]{++resets;};
    target.render_frame=[&]{renderer.BeginFrame(0,0,0,1);EXPECT_TRUE((ui.BeginFrameUI()) && (++checks, true));ui.RootBegin(1,1,UISize::Fixed(100),UISize::Fixed(70));ui.Button("button","");ui.RootEnd();EXPECT_TRUE((ui.EndFrameUI()) && (++checks, true));renderer.EndFrame();if(ui.Clicked("button")){++clicks;if(fail_click)++errors;}};
    target.after_click=[&]{++after;};
    UITestConfig cfg;cfg.filter_pattern="good";cfg.get_error_count=[&]{return errors;};
    UITestTarget skip=target;skip.name="skip";skip.prepare=[] {throw std::runtime_error("filter failed");};
    UITestCustomCheck custom{"good-custom",[&]{++prepared;},[]{return std::size_t(0);}};
    auto report=RunUITests(engine,{target,skip},{custom,{"skip-custom",[]{throw std::runtime_error("filter");},{}}},cfg);
    EXPECT_TRUE((report.Passed()&&report.targets_tested==2&&report.buttons_tested==1) && (++checks, true));
    std::fprintf(stderr,"UI input observations: clicks=%d after=%d resets=%d prepared=%d\n",clicks,after,resets,prepared);
    EXPECT_TRUE((clicks==1&&after==2&&resets==4&&prepared==2&&report.elapsed_seconds>=0) && (++checks, true));
    fail_click=true;report=RunUITests(engine,{target},{},cfg);EXPECT_TRUE((!report.Passed()&&report.script_errors==1&&report.buttons_tested==1) && (++checks, true));
    cfg.filter_pattern.clear();cfg.get_error_count={};
    UITestTarget overflow{"overflow",{},{},[&]{renderer.BeginFrame(0,0,0,1);ui.RootBegin(0,0,UISize::Fixed(20),UISize::Fixed(10));ui.Button("too-tall","");ui.RootEnd();renderer.EndFrame();},{}};
    report=RunUITests(engine,{overflow},{},cfg);EXPECT_TRUE((!report.Passed()&&report.violations>0) && (++checks, true));
    report=RunUITests(engine,{{"empty",{},{},{},{}}},{{"errors",{},[]{return std::size_t(3);}},{"null",{},{}}},cfg);
    EXPECT_TRUE((report.targets_tested==3&&report.script_errors==3&&!report.Passed()) && (++checks, true));
    report=RunUITests(engine,{}, {},cfg);EXPECT_TRUE((report.Passed()&&report.targets_tested==0) && (++checks, true));
    // Exercise the upstream audit/click runner through real UI rendering and
    // injected press/release, including deliberate violations as negative tests.
    cfg.audit_layout=false;
    auto make_target=[&](std::string name,std::function<void()> draw){
        UITestTarget t;t.name=std::move(name);
        t.render_frame=[&,draw]{renderer.BeginFrame(0,0,0,1);EXPECT_TRUE((ui.BeginFrameUI()) && (++checks, true));draw();EXPECT_TRUE((ui.EndFrameUI()) && (++checks, true));renderer.EndFrame();};
        return t;
    };
    auto run=[&](const UITestTarget &t){return RunUITests(engine,{t},{},cfg);};
    auto disabled=make_target("disabled",[&]{ui.RootBegin(0,0,UISize::Fixed(100),UISize::Fixed(30));ui.DisabledBegin();ui.Button("disabled-button","");ui.DisabledEnd();ui.RootEnd();});
    int count_before=ui.ClickCount(); report=run(disabled);
    EXPECT_TRUE((report.Passed()&&report.buttons_tested==1&&report.disabled_tested==1&&ui.ClickCount()==count_before) && (++checks, true));
    auto offscreen=make_target("offscreen",[&]{ui.RootBegin(200,200,UISize::Fixed(20),UISize::Fixed(20));ui.Button("outside-viewport","");ui.RootEnd();});
    report=run(offscreen);EXPECT_TRUE((!report.Passed()&&report.click_failures==1&&report.buttons_tested==0) && (++checks, true));
    cfg.test_clicks=false;report=run(offscreen);EXPECT_TRUE((report.Passed()&&report.buttons_tested==0) && (++checks, true));cfg.test_clicks=true;
    auto occluded=make_target("occluded",[&]{for(auto key:{"covered-button","covering-button"}){ui.RootBegin(0,0,UISize::Fixed(100),UISize::Fixed(30));ui.Button(key,"");ui.RootEnd();}});
    report=run(occluded);EXPECT_TRUE((!report.Passed()&&report.click_failures==1&&report.buttons_tested==1) && (++checks, true));
    auto modal=make_target("modal",[&]{ui.RootBegin(0,0,UISize::Fixed(100),UISize::Fixed(30));ui.Button("under-modal","");ui.RootEnd();ui.SetModal(true);ui.RootBegin(0,0,UISize::Fixed(100),UISize::Fixed(30));ui.Panel(Color::Black());ui.RootEnd();ui.SetModal(false);});
    report=run(modal);EXPECT_TRUE((report.Passed()&&report.buttons_tested==0&&report.click_failures==0) && (++checks, true));
    // A genuinely miswired input backend renders a visible button but cannot
    // receive the engine's injected clicks. Audit must catch it; explicit policy
    // may opt this intentionally passive widget out, without inventing clicks.
    Input passive_input;ui.Bind(&passive_input,&camera,&renderer);
    auto blocked=make_target("blocked",[&]{ui.RootBegin(0,0,UISize::Fixed(100),UISize::Fixed(30));ui.Button("blocked-button","");ui.RootEnd();});
    report=run(blocked);EXPECT_TRUE((!report.Passed()&&report.click_failures==1) && (++checks, true));
    int allowed=0;cfg.allow_no_click=[&](std::string_view target,std::string_view widget,std::string_view kind){EXPECT_TRUE((target=="blocked"&&widget=="blocked-button"&&kind=="button") && (++checks, true));++allowed;return true;};
    count_before=ui.ClickCount();report=run(blocked);EXPECT_TRUE((report.Passed()&&allowed==1&&ui.ClickCount()==count_before) && (++checks, true));cfg.allow_no_click={};ui.Bind(&input,&camera,&renderer);
    auto editors=make_target("editors",[&]{ui.RootBegin(0,0,UISize::Fixed(100),UISize::Fixed(100));ui.VStackBegin(4);ui.Checkbox("check-probe",false,std::string_view(""));ui.TextInput("focus-probe");ui.StackEnd();ui.RootEnd();});
    report=run(editors);EXPECT_TRUE((report.Passed()&&report.buttons_tested==2&&ui.FocusedTextInput()=="focus-probe") && (++checks, true));
    bool removed=false;int first_clicks=0,second_clicks=0;
    auto disappearing=make_target("disappearing",[&]{ui.RootBegin(0,0,UISize::Fixed(100),UISize::Fixed(80));ui.VStackBegin(4);ui.Button("remove-first","");if(!removed){ui.Button("removed-second","");}ui.StackEnd();ui.RootEnd();});
    disappearing.after_click=[&]{if(ui.Clicked("remove-first")){removed=true;++first_clicks;}if(ui.Clicked("removed-second"))++second_clicks;};
    report=run(disappearing);EXPECT_TRUE((report.Passed()&&report.buttons_tested==2&&removed&&first_clicks==1&&second_clicks==0) && (++checks, true));
    bool moved=false;first_clicks=0;second_clicks=0;
    auto moving=make_target("moving",[&]{ui.RootBegin(0,0,UISize::Fixed(20),UISize::Auto());ui.Button("move-first","");ui.RootEnd();ui.RootBegin(moved?80:0,40,UISize::Fixed(20),UISize::Auto());ui.Button("moving-second","");ui.RootEnd();});
    moving.after_click=[&]{if(ui.Clicked("move-first")){moved=true;++first_clicks;}if(ui.Clicked("moving-second"))++second_clicks;};
    report=run(moving);EXPECT_TRUE((report.Passed()&&report.buttons_tested==2&&moved&&first_clicks==1&&second_clicks==1) && (++checks, true));
    auto scroll=make_target("scroll",[&]{ui.RootBegin(0,0,UISize::Fixed(100),UISize::Fixed(30));ui.ScrollBoxBegin("runner-scroll",UISize::Fixed(20));for(int i=0;i<5;++i)ui.Button("scroll-item-"+std::to_string(i),"");ui.ScrollBoxEnd();ui.RootEnd();});
    report=run(scroll);EXPECT_TRUE((report.Passed()&&report.skipped_offscreen>0&&report.buttons_tested>0) && (++checks, true));

}
static void skeleton_tests(Fixtures &f, Renderer &renderer){
    using nlohmann::json;
    ResourceManager resources;SkeletonSystem sk;
    auto rig=json::parse(R"({"skeleton":{"texture":"sheet.ppm"},"bones":[{"name":"root","length":10},{"name":"tip","parent":"root","x":10}],"regions":{"pixel":{"x":0,"y":0,"w":2,"h":2}},"attachments":[{"name":"art","region":"pixel","w":10,"h":10}],"slots":[{"name":"body","bone":"root","attachment":"art"}],"animations":{"turn":{"loop":false,"duration":1,"bones":{"root":{"rotate":[{"time":0,"angle":0},{"time":1,"angle":90}]}}}}})");
    int fixture=0;
    auto reject=[&](const json &doc,const std::string &why){
        std::string error;auto file=f.write("reject-"+std::to_string(fixture++)+".json",doc.dump());
        int rejected=sk.LoadAsset(resources,file,&error);
        if(rejected!=SkeletonSystem::kInvalid)std::fprintf(stderr,"unexpected accepted fixture: %s\n",why.c_str());
        EXPECT_TRUE((rejected==SkeletonSystem::kInvalid) && (++checks, true));
        if(error.find(why)==std::string::npos)std::fprintf(stderr,"expected %s, got %s\n",why.c_str(),error.c_str());
        EXPECT_TRUE((error.find(why)!=std::string::npos) && (++checks, true));EXPECT_TRUE((sk.InstanceCount()==0) && (++checks, true));
    };
    auto change=[&](const char *path,json value,const char *why){auto doc=rig;doc[json::json_pointer(path)]=std::move(value);reject(doc,why);};
    std::string error;EXPECT_TRUE((sk.LoadAsset(resources,(f.dir/"absent-rig").string(),&error)==SkeletonSystem::kInvalid&&error.find("cannot open")!=std::string::npos) && (++checks, true));
    EXPECT_TRUE((sk.LoadAsset(resources,f.write("syntax-rig.json","{"),&error)==SkeletonSystem::kInvalid&&error.find("invalid JSON")!=std::string::npos) && (++checks, true));
    reject(json::array(),"not an object");change("/bones",json::array(),"non-empty");
    change("/bones/1/name","root","unique");change("/bones/0",42,"unique");
    change("/bones/1/parent","absent","unknown parent");
    auto too_many=rig;too_many["bones"]=json::array();for(int i=0;i<=SkeletonSystem::kMaxBones;++i)too_many["bones"].push_back({{"name","b"+std::to_string(i)}});reject(too_many,"exceeds the limit");
    change("/attachments",42,"must be an array");change("/attachments/0/name","","needs a 'name'");
    change("/attachments/0/region","missing","region' that exists");change("/attachments/0/type","unknown","unsupported type");
    change("/attachments/0/type","linkedmesh","needs a 'parent'");
    change("/slots",42,"slots' must be an array");change("/slots/0/bone","absent","real 'bone'");
    change("/slots/0/attachments",json::array({{{"name","absent"}}}),"unknown attachment");
    change("/skins",42,"skins' must be");change("/skins",json::array({42}),"entries must be objects");
    change("/skins",json::array({json::object()}),"needs a 'name'");
    change("/skins",json::array({{{"name","skin"},{"attachments",42}}}),"attachments must be an object");
    change("/skins",{{"bad",42}},"must be an object");
    change("/skins",{{"skin",{{"unknown-slot","art"}}}},"unknown slot");
    change("/animations",42,"animations' must be an object");change("/animations/turn",42,"must be an object");
    change("/animations/turn/bones",{{"absent",json::object()}},"unknown bone");
    change("/animations/turn/slots",{{"absent",json::object()}},"unknown slot");
    change("/animations/turn/events",json::array({{{"time",.1}}}),"event with no name");
    change("/animations/turn/deform",{{"absent",json::object()}},"unknown slot");
    change("/animations/turn/deform",{{"body",42}},"must be an object");
    change("/animations/turn/deform",{{"body",{{"art",json::array()}}}},"unknown mesh attachment");
    change("/skeleton/texture","","needs 'skeleton.texture'");change("/skeleton/texture","missing.png","cannot load sheet");
    auto mesh=rig;mesh["attachments"][0]=json::parse(R"({"name":"art","type":"mesh","uvs":[0,0,1,0,0,1],"triangles":[0,1,2],"vertices":[0,0,10,0,0,10]})");
    auto badmesh=[&](const char *key,json value,const char *why){auto doc=mesh;doc["attachments"][0][key]=std::move(value);reject(doc,why);};
    badmesh("uvs",json::array(),"needs a 'uvs'");badmesh("uvs",json::array({"bad"}),"non-numeric UV");
    badmesh("triangles",42,"needs 'triangles'");badmesh("triangles",json::array({"bad"}),"non-numeric triangle");
    badmesh("triangles",json::array({0,1}),"multiple of 3");badmesh("triangles",json::array({0,1,9}),"out of range");
    badmesh("vertices",json::array(),"non-empty array");badmesh("vertices",json::array({"bad"}),"must be numbers");
    badmesh("vertices",json::array({0}),"bone count");badmesh("vertices",json::array({1}),"truncated");
    badmesh("vertices",json::array({1,99,0,0,1}),"out of range");
    badmesh("vertices",json::array({1,0,0,0,1}),"describe 1 vertices");
    badmesh("vertices",json::array({1,0,0,0,-1,1,0,10,0,1,1,0,0,10,1}),"negative weight");
    badmesh("hull",4,"hull' must be");
    auto bad=mesh;bad["animations"]["turn"]["deform"]={{"body",{{"art",json::array({{{"vertices",42}}})}}}};reject(bad,"without a 'vertices' array");
    bad=mesh;bad["animations"]["turn"]["deform"]={{"body",{{"art",json::array({{{"vertices",json::array({"x"})}}})}}}};reject(bad,"non-numeric deform");
    int asset=sk.LoadAsset(resources,f.write("valid-rig.json",rig.dump()),&error);EXPECT_TRUE((asset>0&&sk.HasAsset(asset)) && (++checks, true));
    int id=sk.Create(asset);EXPECT_TRUE((id>0&&sk.AssetOf(id)==asset&&sk.InstanceCount()==1) && (++checks, true));
    EXPECT_TRUE((sk.BoneCount(id)==2&&sk.BoneName(id,1)=="tip"&&sk.BoneParent(id,1)==0) && (++checks, true));
    EXPECT_TRUE((sk.SlotCount(id)==1&&sk.SlotName(id,0)=="body"&&sk.SlotBone(id,0)==0) && (++checks, true));
    EXPECT_TRUE((sk.AttachmentName(id,0)=="art"&&sk.SlotAttachmentCount(id,0)==1) && (++checks, true));
    EXPECT_TRUE((sk.Play(id,"turn",0)&&sk.ClipName(id)=="turn"&&sk.ClipCount(id)==1) && (++checks, true));
    sk.SetTime(id,.5f);EXPECT_TRUE((close(sk.BoneRotation(id,0),45)) && (++checks, true));sk.Pause(id);EXPECT_TRUE((!sk.IsPlaying(id)) && (++checks, true));
    sk.Resume(id);EXPECT_TRUE((sk.IsPlaying(id)) && (++checks, true));sk.SetSpeed(id,-1);sk.Update(.25f);EXPECT_TRUE((close(sk.Time(id),.25f)) && (++checks, true));
    sk.SetTime(id,-10);EXPECT_TRUE((sk.Time(id)==0) && (++checks, true));sk.SetTime(id,10);EXPECT_TRUE((sk.Time(id)==1) && (++checks, true));
    sk.SetSpeed(id,1);sk.Update(.1f);EXPECT_TRUE((sk.Finished(id)&&!sk.Finished(id)) && (++checks, true));
    EXPECT_TRUE((sk.SetClip(id,"turn")) && (++checks, true));sk.SetPoseWeight(id,0);EXPECT_TRUE((sk.PoseWeight(id)==0&&close(sk.BoneRotation(id,0),0)) && (++checks, true));
    sk.SetPoseWeight(id,1);sk.SetMix(id,.2f);EXPECT_TRUE((sk.SetBoneByName(id,"tip",20,0,0,1,1)) && (++checks, true));
    EXPECT_TRUE((close(sk.BonePosition(id,1).x,20)) && (++checks, true));EXPECT_TRUE((sk.ResetBone(id,1)&&close(sk.BonePosition(id,1).x,10)) && (++checks, true));
    EXPECT_TRUE((sk.SetSlotVisible(id,0,false)&&sk.SetSlotColor(id,0,{1,.5f,.2f,1})) && (++checks, true));EXPECT_TRUE((sk.SetSlotAttachmentIndex(id,0,0)) && (++checks, true));
    EXPECT_TRUE((sk.SetSkin(id,"missing")==0) && (++checks, true));EXPECT_TRUE((sk.AimBoneByName(id,"root",{0,20})) && (++checks, true));EXPECT_TRUE((close(sk.BoneRotation(id,0),90)) && (++checks, true));
    EXPECT_TRUE((!sk.SolveTwoBoneByName(id,"root","tip","absent",{0,0},1)) && (++checks, true));
    EXPECT_TRUE((sk.SetSlotVisible(id,0,true)) && (++checks, true));Rect bounds;EXPECT_TRUE((sk.Bounds(id,bounds)&&sk.BoundsWorld(id,{20,20},2,-1,bounds)) && (++checks, true));
    for(int invalid:{-1,0,99999}){
        EXPECT_TRUE((!sk.Has(invalid)&&!sk.HasAsset(invalid)&&sk.GetAsset(invalid)==nullptr) && (++checks, true));
        EXPECT_TRUE((sk.Create(invalid)==SkeletonSystem::kInvalid&&!sk.Play(invalid,"turn")&&!sk.PlayIndex(invalid,0)) && (++checks, true));
        EXPECT_TRUE((!sk.IsPlaying(invalid)&&!sk.Finished(invalid)&&sk.BoneCount(invalid)==0&&sk.SlotCount(invalid)==0) && (++checks, true));
        EXPECT_TRUE((sk.BoneIndex(invalid,"root")==-1&&sk.BoneName(invalid,0).empty()&&sk.BoneParent(invalid,0)==-1) && (++checks, true));
        EXPECT_TRUE((sk.BonePosition(invalid,0).x==0&&sk.BoneRotation(invalid,0)==0&&sk.BoneLength(invalid,0)==0) && (++checks, true));
        EXPECT_TRUE((!sk.SetBone(invalid,0,0,0,0,1,1)&&!sk.ResetBone(invalid,0)) && (++checks, true));
        EXPECT_TRUE((sk.SlotIndex(invalid,"body")==-1&&sk.SlotName(invalid,0).empty()&&sk.SlotBone(invalid,0)==-1) && (++checks, true));
        EXPECT_TRUE((!sk.SetSlotColor(invalid,0,{})&&!sk.SetSlotVisible(invalid,0,true)&&!sk.SetSlotAttachment(invalid,0,"art")) && (++checks, true));
        EXPECT_TRUE((!sk.SetSlotAttachmentIndex(invalid,0,0)&&sk.SlotAttachmentCount(invalid,0)==0&&sk.SetSkin(invalid,"x")==0) && (++checks, true));
        EXPECT_TRUE((sk.AttachmentCount(invalid)==0&&sk.AttachmentName(invalid,0).empty()&&!sk.AttachmentIsMesh(invalid,0)) && (++checks, true));
        EXPECT_TRUE((sk.MeshVertexCount(invalid,0)==0&&sk.MeshTriangleCount(invalid,0)==0&&sk.MeshWeightCount(invalid,0,0)==0) && (++checks, true));
        int bone=123;float weight=123;Vec2 out;EXPECT_TRUE((!sk.MeshWeight(invalid,0,0,0,bone,weight)&&!sk.MeshVertexWorld(invalid,0,0,out)) && (++checks, true));
        EXPECT_TRUE((sk.MeshBindVertex(invalid,0,0).x==0&&!sk.SampleDeformAt(invalid,0,0)&&sk.MeshBindKey(invalid,0)==0&&!sk.MeshSharesBind(invalid,0,0)) && (++checks, true));
        EXPECT_TRUE((sk.MeshHullCount(invalid,0)==0&&!sk.MeshHullVertex(invalid,0,0,out)&&sk.MeshHullWorld(invalid,0,{},1,1,&out,1)==0) && (++checks, true));
        EXPECT_TRUE((!sk.Bounds(invalid,bounds)&&!sk.BoundsWorld(invalid,{},1,1,bounds)&&!sk.AimBone(invalid,0,{})) && (++checks, true));
        EXPECT_TRUE((sk.EventCount(invalid)==0&&sk.EventName(invalid,0).empty()&&sk.EventData(invalid,0)==0) && (++checks, true));
    }
    EXPECT_TRUE((!sk.Play(id,"missing")&&!sk.PlayIndex(id,99)&&!sk.SetBone(id,99,0,0,0,1,1)) && (++checks, true));
    EXPECT_TRUE((sk.BoneName(id,99).empty()&&sk.BoneParent(id,99)==-1&&sk.BoneLength(id,99)==0) && (++checks, true));
    EXPECT_TRUE((!sk.ResetBone(id,99)&&!sk.SetSlotVisible(id,99,true)&&!sk.SetSlotAttachmentIndex(id,0,99)) && (++checks, true));
    EXPECT_TRUE((!sk.AttachmentIsMesh(id,0)&&sk.MeshVertexCount(id,0)==0&&!sk.SampleDeformAt(id,0,0)) && (++checks, true));
    int mid=sk.LoadAsset(resources,f.write("mesh-rig.json",mesh.dump()),&error);EXPECT_TRUE((mid>0) && (++checks, true));int mi=sk.Create(mid);
    Vec2 vertex;EXPECT_TRUE((sk.AttachmentIsMesh(mi,0)&&sk.MeshVertexCount(mi,0)==3&&sk.MeshTriangleCount(mi,0)==1) && (++checks, true));
    EXPECT_TRUE((sk.MeshVertexWorld(mi,0,1,vertex)&&close(vertex.x,10)&&close(vertex.y,0)) && (++checks, true));
    EXPECT_TRUE((sk.MeshHullWorld(mi,0,{20,30},2,-1,&vertex,1)==1&&close(vertex.x,20)&&close(vertex.y,30)) && (++checks, true));
    EXPECT_TRUE((!sk.MeshHullVertex(mi,0,99,vertex)&&sk.MeshHullWorld(mi,0,{},1,1,nullptr,0)==0) && (++checks, true));
    sk.RemoveAsset(asset);EXPECT_TRUE((!sk.Has(id)&&!sk.HasAsset(asset)&&sk.Has(mi)) && (++checks, true));
    sk.Destroy(mi);EXPECT_TRUE((!sk.Has(mi)&&sk.InstanceCount()==0) && (++checks, true));sk.Clear();EXPECT_TRUE((sk.InstanceCount()==0) && (++checks, true));
    (void)renderer;
}
#ifdef FAKE2D_WITH_FAKELUA
static void layout_binding_tests(Fixtures &f,Renderer &renderer){
    Engine engine;auto &ui=engine.GetUI();Camera2D camera(64,64);auto &input=engine.GetInput();
    input.InjectFrame({5,5},{"l","escape"},{2});ui.Bind(&input,&camera,&renderer);ui.NewFrame();
    ColorPool colors;UITipTracker tips;UITextureCache textures;tips.BeginFrame();int sprites=0;
    LuaUIHost host;host.engine=&engine;host.colors=&colors;host.tips=&tips;host.textures=&textures;
    host.root_dir=f.dir.string();host.texture_subdir="";host.before_sprite_draw=[&]{++sprites;};
    UIButtonStylePreset style;style.has_active_variant=true;style.active.button={0,1,0,1};host.button_styles["test"]=style;
    fakelua::FakeluaStateGuard state;auto *st=state.GetState();RegisterCommonUILuaApi(st,&host);RegisterLayoutUILuaApi(st,&host);
    fakelua::RegisterNativeFunction(st,"gap_expect",false,
        std::function<void(fakelua::State*,bool,std::string_view)>([](auto*,bool ok,std::string_view label){
            if(!ok)std::fprintf(stderr,"Lua layout assertion: %.*s\n",int(label.size()),label.data());EXPECT_TRUE((ok) && (++checks, true));
        }));
    fakelua::CompileConfig cfg;cfg.debug_mode=false;fakelua::DisableAllJit(cfg);cfg.disable_jit[fakelua::JIT_INTERP]=false;
    fakelua::CompileFile(st,"tests/coverage_layout_bindings.lua",cfg);
    auto previous=ui.ThemeColor("button");const int target=renderer.GetRenderTargets().Create(64,64);EXPECT_TRUE((target>0) && (++checks, true));
    renderer.BeginFrame(0,0,0,1);EXPECT_TRUE((renderer.BeginRenderTarget(target)) && (++checks, true));renderer.ClearActiveTarget({0,0,0,1});
    int result=0;fakelua::Call(st,fakelua::JIT_INTERP,"layout_contract",result);
    renderer.EndRenderTarget();renderer.EndFrame();EXPECT_TRUE((result==123&&sprites==1&&ui.IsModal()) && (++checks, true));
    EXPECT_TRUE((ui.ThemeColor("button").r==previous.r&&ui.ThemeColor("button").g==previous.g) && (++checks, true));
    Color pixel;auto *rt=renderer.GetRenderTargets().Get(target);EXPECT_TRUE((rt&&rt->ReadPixel(3,3,pixel)&&pixel.r>.9f&&pixel.b<.1f) && (++checks, true));
    EXPECT_TRUE((rt->ReadPixel(19,3,pixel)&&pixel.b>.9f&&pixel.r<.1f) && (++checks, true));
    EXPECT_TRUE((rt->ReadPixel(40,3,pixel)&&pixel.r>.9f&&pixel.b<.1f) && (++checks, true));
    EXPECT_TRUE((rt->ReadPixel(43,23,pixel)&&pixel.r>.9f&&pixel.b<.1f) && (++checks, true));
    EXPECT_TRUE((rt->ReadPixel(39,23,pixel)&&pixel.b>.9f&&pixel.r<.1f) && (++checks, true));
    renderer.GetRenderTargets().Destroy(target);ui.SetModal(false);textures.Clear();
}
static void skeleton_binding_tests(Fixtures &f){
    Engine engine;fakelua::FakeluaStateGuard state;auto *st=state.GetState();EXPECT_TRUE((st) && (++checks, true));
    RegisterScriptApi(nullptr,&engine);RegisterScriptApi(st,nullptr);RegisterScriptApi(st,&engine);
    fakelua::RegisterNativeFunction(st,"gap_expect",false,
        std::function<void(fakelua::State*,bool,std::string_view)>([](auto*,bool ok,std::string_view label){
            if(!ok)std::fprintf(stderr,"Lua binding assertion: %.*s\n",int(label.size()),label.data());EXPECT_TRUE((ok) && (++checks, true));
        }));
    fakelua::CompileConfig cfg;cfg.debug_mode=false;fakelua::DisableAllJit(cfg);cfg.disable_jit[fakelua::JIT_INTERP]=false;
    fakelua::CompileFile(st,"tests/coverage_skeleton_bindings.lua",cfg);
    int result=0;fakelua::Call(st,fakelua::JIT_INTERP,"binding_roundtrip",result,(f.dir/"valid-rig.json").string(),(f.dir/"mesh-rig.json").string());
    EXPECT_TRUE((result==123&&engine.GetSkeletons().InstanceCount()==0) && (++checks, true));
}
static void rule_tests(Fixtures &f){
    bool finalized=false;int registrations=0,notes=0;
    log::Config lc;lc.dir=(f.dir/"logs").string();lc.echo_stderr=false;lc.crash_reports=false;lc.rotate=false;EXPECT_TRUE((log::Init(lc)) && (++checks, true));
    {
        RuleHost host;EXPECT_TRUE((!host.Opened()&&host.State()==nullptr) && (++checks, true));
        EXPECT_TRUE((host.Open([&](void *ptr){++registrations;auto *st=static_cast<fakelua::State*>(ptr);
            auto &mgr=fakelua::GetNativeObjectManager(st);auto *obj=mgr.GlobalCreate("owned","Owned",1);obj->SetFinalizer([&](auto*){finalized=true;});
            fakelua::RegisterNativeFunction(st,"md_note",false,std::function<void(fakelua::State*)>([&](auto*){++notes;}));
            fakelua::RegisterNativeFunction(st,"boom",false,std::function<void(fakelua::State*)>([](auto*){throw std::runtime_error("fixture failure");}));
        })) && (++checks, true));
        EXPECT_TRUE((host.Opened()&&host.State()) && (++checks, true));EXPECT_TRUE((host.Open({})&&registrations==1) && (++checks, true));
        auto one=f.write("rule_01.lua","function md_answer() return 42.0 end\n");
        auto two=f.write("rule_02.lua","function u_known() return 5.0 end\n");
        f.write("rule_a.bak.lua","broken");f.write("rule_bad.lua~","broken");f.write("other.lua","broken");
        auto first=(f.dir/"common.lua").string();auto files=RuleHost::DiscoverChunks(f.dir.string(),"rule_",first);
        EXPECT_TRUE(((files==std::vector<std::string>{first,one,two})) && (++checks, true));
        EXPECT_TRUE((RuleHost::DiscoverChunks((f.dir/"absent").string(),"rule_",first)==std::vector<std::string>{first}) && (++checks, true));
        EXPECT_TRUE((host.CompileUnit(one,true,"test")&&host.HasEntry("md_answer",true)&&host.HasEntry("md_answer",false)) && (++checks, true));
        double value=0;fakelua::Call(static_cast<fakelua::State*>(host.State()),fakelua::JIT_INTERP,"md_answer",value);EXPECT_TRUE((value==42) && (++checks, true));
        EXPECT_TRUE((!host.HasEntry("not_here",true)) && (++checks, true));
        auto bad=f.write("bad.lua","function ( broken");
        EXPECT_TRUE((!host.CompileUnit(bad,true,"test")) && (++checks, true));EXPECT_TRUE((!host.CompileUnit((f.dir/"missing").string(),true,"test")) && (++checks, true));
        bool interp=true;EXPECT_TRUE((host.CompileUnitWithFallback(two,interp,"test")&&interp) && (++checks, true));
        interp=false;EXPECT_TRUE((!host.CompileUnitWithFallback(bad,interp,"test")&&interp) && (++checks, true));
        int visited=0,retries=0;interp=true;
        auto tag=[](const std::string &p){return std::filesystem::path(p).filename().string();};
        EXPECT_TRUE((host.CompileSetWithFallback({one,two},interp,"test",[&](auto&){++visited;},[&]{++retries;},tag)) && (++checks, true));
        EXPECT_TRUE((visited==2&&retries==0) && (++checks, true));
        EXPECT_TRUE((!host.CompileSetWithFallback({bad},interp,"test",{},[&]{++retries;},tag)&&retries==1) && (++checks, true));
        auto recover=f.write("recover.lua","function (");interp=false;
        EXPECT_TRUE((host.CompileSetWithFallback({recover},interp,"test",{},[&]{f.write("recover.lua","function recovered() return 9.0 end");},tag)) && (++checks, true));
        EXPECT_TRUE((interp&&host.HasEntry("recovered",true)) && (++checks, true));
        auto prelude=f.write("prelude.lua","function push_ok() md_note() return 0.0 end");
        EXPECT_TRUE((host.CompilePrelude(prelude,"push_ok","test")&&notes==1) && (++checks, true));
        EXPECT_TRUE((!host.CompilePrelude(prelude,"missing_push","test")) && (++checks, true));
        EXPECT_TRUE((!host.CompilePrelude(bad,"push_ok","test")) && (++checks, true));
        auto throwing=f.write("throw.lua","function push_throws() boom() return 0.0 end");
        EXPECT_TRUE((!host.CompilePrelude(throwing,"push_throws","test")) && (++checks, true));
        host.SetNativeChecker([](const char *n){return std::string(n)=="md_note";});
        auto lint=f.write("lint.lua","function lint_probe() md_missing() u_missing() u_known() md_note() return 0.0 end\n-- some_md_skip()\n");
        host.LintUnit(lint,"direct-lint");host.LintUnit((f.dir/"absent.lua").string(),"missing");
        auto logfile=f.read("logs/game.log");
        EXPECT_TRUE((logfile.find("direct-lint calls unregistered 'md_missing'")!=std::string::npos) && (++checks, true));
        EXPECT_TRUE((logfile.find("direct-lint calls unregistered 'u_missing'")!=std::string::npos) && (++checks, true));
        EXPECT_TRUE((logfile.find("direct-lint calls unregistered 'md_note'")==std::string::npos&&logfile.find("direct-lint calls unregistered 'u_known'")==std::string::npos) && (++checks, true));
    }
    EXPECT_TRUE((finalized) && (++checks, true));log::Shutdown();
    RuleHost no_reg;EXPECT_TRUE((no_reg.Open({})) && (++checks, true));
}
#endif
// Google Test migration registration; helper/test bodies above are unchanged.
static fake2d::test::FilesEnvironment<Fixtures> *gap_files;
void RegisterFake2dTestEnvironment() {
    gap_files = new fake2d::test::FilesEnvironment<Fixtures>;
    testing::AddGlobalTestEnvironment(gap_files);
}
#ifdef FAKE2D_WITH_FAKELUA
TEST(GapCore, RuleHost) { rule_tests(*gap_files->files); }
#endif
TEST(GapCore, CharacterController) { character_tests(); }
class GapGraphics : public testing::Test {
protected:
    struct Context {
        // Reverse destruction: sheet, renderer, then still-current GL window.
        platform::Window window;
        Renderer renderer;
        std::unique_ptr<Texture2D> sheet;
    };
    inline static std::unique_ptr<Context> context;
    static void SetUpTestSuite() {
        context = std::make_unique<Context>();
        platform::WindowDesc desc; desc.width=64; desc.height=64; desc.headless=true;
        ASSERT_TRUE(context->window.Create(desc) && (++checks, true));
        ASSERT_TRUE(context->renderer.Init(64,64) && (++checks, true));
    }
    static Texture2D &Sheet() {
        if (!context->sheet) {
            const std::uint8_t rgba[]={255,0,0,255,0,255,0,255,0,0,255,255,255,255,255,255};
            context->sheet = std::make_unique<Texture2D>();
            EXPECT_TRUE(context->sheet->Create(2,2,rgba,4,TextureFilter::Nearest) && (++checks, true));
        }
        return *context->sheet;
    }
    static void TearDownTestSuite() { context.reset(); }
};
TEST_F(GapGraphics, UITestRunner) { ui_runner_tests(context->renderer); }
TEST_F(GapGraphics, BoundsAndAtlas) {
    auto &sheet=Sheet(); bounds_regression(); atlas_tests(*gap_files->files,sheet);
}
TEST_F(GapGraphics, Node) { node_tests(context->renderer,Sheet()); }
TEST_F(GapGraphics, SkeletonNativeAndLua) {
    skeleton_tests(*gap_files->files,context->renderer);
#ifdef FAKE2D_WITH_FAKELUA
    // Lua roundtrip consumes the exact rigs generated by the native checks.
    skeleton_binding_tests(*gap_files->files);
#endif
}
#ifdef FAKE2D_WITH_FAKELUA
TEST_F(GapGraphics, LayoutBindings) { layout_binding_tests(*gap_files->files,context->renderer); }
#endif

int Fake2dAssertionCount() { return checks; }
