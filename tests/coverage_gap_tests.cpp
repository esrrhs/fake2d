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
#define REQUIRE(x) do { if (!(x)) {std::fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); throw std::runtime_error(#x);} ++checks; } while(false)
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
    REQUIRE(map.Build(n,n,ids,1,1));
    REQUIRE(map.BBoxes().size()==1);
    const auto &bb=map.BBoxes()[0];
    REQUIRE(close(bb.min_x,.5f)&&close(bb.min_y,.5f));
    REQUIRE(close(bb.max_x,n-.5f)&&close(bb.max_y,n-.5f));
    map.ComputeBorderDistance({0},ids);
    std::vector<int> component{0};Vec2 center{64,64};double area=n*n;
    TerritoryLabelRequest req;req.width=n;req.height=n;req.region_count=1;
    req.region_ids=ids.data();req.border_dist=map.BorderDistance().data();
    req.bboxes=map.BBoxes().data();req.bbox_count=1;req.region_centroids=&center;
    req.region_areas=&area;req.component_regions=&component;req.text_candidates={"Test"};
    auto label=PlaceContiguousLabel(req);
    REQUIRE(label.valid&&label.text=="Test"&&label.size>=11);
}
static std::string atlas_json(const std::string &image="sheet.ppm"){
 return R"({"frames":{"normal":{"frame":{"x":0,"y":0,"w":2,"h":2}},"rot":{"frame":{"x":0,"y":0,"w":2,"h":2},"rotated":true,"spriteSourceSize":{"x":3,"y":4},"sourceSize":{"w":8,"h":9},"pivot":{"x":0.25,"y":0.75}}},"meta":{"image":")"+image+R"("}})";
}
static void atlas_tests(Fixtures &f,Texture2D &sheet){
    Atlas atlas;std::string image;
    for(const auto &bad:{"not json","{}","{\"frames\":42}","{\"frames\":[]}","{\"frames\":{\"bad\":{}}}",
        "{\"frames\":[{\"filename\":\"x\",\"frame\":{\"x\":\"bad\"}}]}",
        "{\"frames\":{\"a\":{\"frame\":{\"w\":1}},\"z\":{\"frame\":{\"w\":\"bad\"}}}}"}){
        REQUIRE(!atlas.Parse(bad,image));REQUIRE(image.empty());REQUIRE(!atlas.IsValid());REQUIRE(!atlas.GetRegion("a"));
    }
    REQUIRE(atlas.Parse(atlas_json(),image)&&image=="sheet.ppm");
    REQUIRE(!atlas.IsValid());auto *r=atlas.GetRegion("rot");REQUIRE(r&&r->rotated);
    REQUIRE(r->offset.x==3&&r->offset.y==4&&r->source_size.x==8&&r->source_size.y==9);
    REQUIRE(r->pivot.x==.25f&&r->pivot.y==.75f);
    Rect rect;REQUIRE(atlas.GetRegionRect("normal",rect)&&rect.width==2);
    rect={1,2,3,4};REQUIRE(!atlas.GetRegionRect("missing",rect)&&rect.width==3);
    Texture2D invalid;REQUIRE(!atlas.LoadFromString(atlas_json(),invalid));
    REQUIRE(!atlas.LoadFromString("{}",sheet));
    REQUIRE(atlas.LoadFromString(atlas_json(),sheet)&&&atlas.GetTexture()==&sheet);
    REQUIRE(atlas.Parse(R"({"frames":[{"filename":"item","frame":{"w":3,"h":5},"sourceSize":null},{"filename":"bad","frame":null}]})",image));
    r=atlas.GetRegion("item");REQUIRE(r&&r->source_size.x==3&&r->source_size.y==5&&r->pivot.x==.5f);
    REQUIRE(atlas.Parse(R"({"frames":{"x":{"frame":{"w":3,"h":5},"rotated":true}}})",image));
    r=atlas.GetRegion("x");REQUIRE(r&&r->frame.width==5&&r->frame.height==3);
    atlas.BindTexture(sheet);REQUIRE(atlas.IsValid());
    REQUIRE(!atlas.LoadFromFile((f.dir/"missing.json").string()));
    REQUIRE(!atlas.LoadFromFile(f.write("bad.json","not json")));
    REQUIRE(!atlas.LoadFromFile(f.write("no-image.json",R"({"frames":{"x":{"frame":{}}}})")));
    REQUIRE(!atlas.LoadFromFile(f.write("missing-image.json",atlas_json("missing.png"))));
    f.write("sheet.ppm",std::string("P6\n2 2\n255\n")+std::string(12,'\xff'));
    REQUIRE(atlas.LoadFromFile(f.write("atlas.json",atlas_json())));
    REQUIRE(atlas.GetTexture().IsValid()&&atlas.GetTexture().Width()==2);
}
struct CornerProbe:QuadNode{using QuadNode::ComputeWorldCorners;};
struct TrackedNode:Node {int &dead;explicit TrackedNode(int &d):dead(d){}~TrackedNode()override{++dead;}};
static void node_tests(Renderer &renderer,Texture2D &sheet){
    Node root("root");root.GetTransform().SetPosition(10,20);
    auto child=std::make_unique<Node>("child");auto *c=child.get();c->GetTransform().SetPosition(1,2);
    auto grand=std::make_unique<Node>("grand");auto *g=grand.get();g->GetTransform().SetPosition(3,4);
    c->AddChild(std::move(grand));root.AddChild(std::move(child));root.AddChild(nullptr);
    auto pos=g->WorldMatrix().TransformPoint({0,0});REQUIRE(close(pos.x,14)&&close(pos.y,26));
    root.GetTransform().SetPosition(100,200);
    pos=g->WorldMatrix().TransformPoint({0,0});REQUIRE(close(pos.x,104)&&close(pos.y,206));
    pos=g->WorldMatrix().TransformPoint({0,0});REQUIRE(close(pos.x,104));
    std::vector<std::string> names;root.Visit([&](Node *n){names.push_back(n->Name());});
    REQUIRE((names==std::vector<std::string>{"root","child","grand"}));
    root.SetVisibleRecursive(false);REQUIRE(!root.Visible()&&!c->Visible()&&!g->Visible());
    root.SetVisibleRecursive(true);REQUIRE(root.Visible()&&g->Visible());
    REQUIRE(!root.RemoveChild(nullptr));
    auto detached=root.RemoveChild(c);REQUIRE(detached&&c->Parent()==nullptr&&root.Children().empty());
    pos=g->WorldMatrix().TransformPoint({0,0});REQUIRE(close(pos.x,4)&&close(pos.y,6));
    Node other("other");other.GetTransform().SetPosition(50,60);(void)other.WorldMatrix();
    other.AddChild(std::move(detached));pos=g->WorldMatrix().TransformPoint({0,0});
    REQUIRE(close(pos.x,54)&&close(pos.y,66));
    root.RemoveFromParent();int dead=0;auto dying=std::make_unique<TrackedNode>(dead);auto *d=dying.get();
    root.AddChild(std::move(dying));d->RemoveFromParent();REQUIRE(dead==1&&root.Children().empty());
    CornerProbe probe;probe.SetSize(10,20);probe.SetPivot({.5f,.5f});probe.GetTransform().SetPosition(30,40);probe.GetTransform().SetScale(2);
    Vec2 corners[4];probe.ComputeWorldCorners(corners);REQUIRE(close(corners[0].x,20)&&close(corners[0].y,20));
    REQUIRE(close(corners[2].x,40)&&close(corners[2].y,60));
    Atlas atlas;REQUIRE(atlas.LoadFromString(atlas_json(),sheet));
    SpriteNode sprite; sprite.Draw(renderer);sprite.SetTexture(nullptr);sprite.Draw(renderer);
    sprite.SetAtlasRegion(nullptr,"normal");sprite.SetAtlasRegion(&atlas,"missing");
    sprite.SetTexture(&sheet);REQUIRE(sprite.TextureRegion().width==2);
    sprite.SetTextureRegion({0,0,0,0});sprite.Draw(renderer);
    sprite.SetSize(16,16);sprite.SetTextureRegion({0,0,2,2});sprite.GetTransform().SetPosition(20,0);
    const int target=renderer.GetRenderTargets().Create(64,64);REQUIRE(target>0);
    renderer.BeginFrame(0,0,0,1);REQUIRE(renderer.BeginRenderTarget(target));
    renderer.ClearActiveTarget({0,0,0,1});sprite.Draw(renderer);
    sprite.SetAtlasRegion(&atlas,"rot");sprite.GetTransform().SetPosition(20,20);sprite.Draw(renderer);
    RectNode box;box.SetSize(10,10);box.SetTint({0,0,1,1});box.Draw(renderer);
    renderer.EndRenderTarget();renderer.EndFrame();Color pixel;
    auto *rt=renderer.GetRenderTargets().Get(target);REQUIRE(rt&&rt->ReadPixel(5,5,pixel)&&pixel.b>.9f&&pixel.r<.1f);
    REQUIRE(rt->ReadPixel(22,2,pixel)&&pixel.r>.9f&&pixel.g<.1f);
    REQUIRE(rt->ReadPixel(22,22,pixel)&&pixel.g>.9f&&pixel.r<.1f);
    Scene scene;std::vector<int> order;
    struct Ordered:Node {int id;std::vector<int>&order;Ordered(int i,std::vector<int>&o):id(i),order(o){}void OnDraw(Renderer&)override{order.push_back(id);}};
    for(auto spec:std::vector<std::pair<int,int>>{{1,2},{2,0},{3,2},{4,-1}}){
        auto n=std::make_unique<Ordered>(spec.first,order);n->SetLayer(spec.second);n->SetZ(0);if(spec.first==4)n->SetVisible(false);scene.Root().AddChild(std::move(n));}
    renderer.BeginFrame(0,0,0,1);scene.Draw(renderer);renderer.EndFrame();
    REQUIRE((order==std::vector<int>{1,2,3}));REQUIRE(!renderer.GetSpriteBatch().IsSorted());int visits=0;scene.Visit([&](Node*){++visits;});REQUIRE(visits==5);
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
    REQUIRE(map.LoadFromString(map_json(20,12,data),".",resources));
    REQUIRE(map.SolidAt(2,10)&&map.OneWayAt(4,5));
    CharacterController ch;ch.Spawn({32,16});REQUIRE(!ch.Update(.01f,{}).on_ground&&ch.Position().x==32);
    ch.SetMap(&map);auto tuning=ch.Tuning();tuning.half_extents={4,7};ch.SetTuning(tuning);
    auto p=ch.Position();ch.Update(0,{});REQUIRE(ch.Position().x==p.x&&ch.Position().y==p.y);
    bool landed=false;for(int i=0;i<120;++i){auto frame=ch.Update(1.f/120,{});landed|=frame.landed;}
    REQUIRE(landed&&ch.OnGround()&&close(ch.Position().y,146));
    ch.Spawn({32,20});REQUIRE(!ch.OnGround()&&ch.Velocity().y==0);
    REQUIRE(!ch.Update(.01f,{0,true,true}).jumped); // grounded state cannot survive teleport
    ch.Spawn({32,146});ch.Update(.01f,{});REQUIRE(ch.OnGround());
    ch.Update(.02f,{1,false,false});REQUIRE(ch.Velocity().x>0&&ch.Facing()==1);
    ch.Update(.02f,{});REQUIRE(ch.Velocity().x>=0);
    ch.Update(.03f,{-1,false,false});REQUIRE(ch.Facing()==-1&&ch.Velocity().x<0);
    ch.Update(.1f,{});REQUIRE(ch.Velocity().x==0);
    auto jump=ch.Update(.01f,{0,true,true});REQUIRE(jump.jumped&&ch.Velocity().y<0);
    float vy=ch.Velocity().y;ch.Update(.01f,{0,false,false});REQUIRE(ch.Velocity().y>vy*.6f);
    ch.Spawn({32,20});REQUIRE(!ch.OnGround()&&ch.Velocity().y==0);
    auto after_spawn=ch.Update(.01f,{0,true,true});REQUIRE(!after_spawn.jumped); // no stale coyote/buffer
    ch.Spawn({25,130});for(int i=0;i<60;++i)ch.Update(.02f,{-1,false,false});
    REQUIRE(ch.Position().x>=16&&ch.Position().x<17);
    ch.Spawn({180,130});for(int i=0;i<60;++i)ch.Update(.02f,{1,false,false});
    REQUIRE(ch.Position().x<=184&&ch.Position().x>183);
    ch.Spawn({72,20});for(int i=0;i<120;++i)ch.Update(.01f,{});
    REQUIRE(ch.OnGround()&&close(ch.Position().y,66)); // one-way catches from above
    auto j=ch.Update(.01f,{0,true,true});REQUIRE(j.jumped); // passes upward through one-way
    ch.Spawn({72,82});for(int i=0;i<120;++i)ch.Update(.01f,{});
    REQUIRE(close(ch.Position().y,146)); // spawn below one-way must not be pulled onto it
    ch.Spawn({72,146});for(int i=0;i<2;++i)ch.Update(.01f,{});
    for(int i=0;i<120;++i)ch.Update(.01f,{0,i==0,true});
    REQUIRE(ch.Position().y<=146&&ch.Bounds().width==8&&ch.Center().x==76);
    ch.Spawn({128,146});ch.Update(.01f,{});REQUIRE(ch.Update(.01f,{0,true,true}).jumped);
    float highest=ch.Position().y;bool ceiling_stop=false;
    for(int i=0;i<45;++i){ch.Update(.01f,{0,false,true});highest=std::min(highest,ch.Position().y);ceiling_stop|=ch.Velocity().y==0&&!ch.OnGround();}
    REQUIRE(ceiling_stop&&highest>=96-.02f); // solid underside cancels ascent
    ch.Spawn({32,20});ch.Update(1.0f,{});REQUIRE(ch.OnGround()&&close(ch.Position().y,146)); // substep anti-tunnelling
}
static void ui_runner_tests(Renderer &renderer){
    Engine engine;Camera2D camera;camera.SetViewport(128,128);auto &ui=engine.GetUI();auto &input=engine.GetInput();ui.Bind(&input,&camera,&renderer);
    int prepared=0,resets=0,after=0,clicks=0;std::size_t errors=0;bool fail_click=false;
    UITestTarget target;target.name="good";target.prepare=[&]{++prepared;};target.reset_state=[&]{++resets;};
    target.render_frame=[&]{renderer.BeginFrame(0,0,0,1);REQUIRE(ui.BeginFrameUI());ui.RootBegin(1,1,UISize::Fixed(100),UISize::Fixed(70));ui.Button("button","");ui.RootEnd();REQUIRE(ui.EndFrameUI());renderer.EndFrame();if(ui.Clicked("button")){++clicks;if(fail_click)++errors;}};
    target.after_click=[&]{++after;};
    UITestConfig cfg;cfg.filter_pattern="good";cfg.get_error_count=[&]{return errors;};
    UITestTarget skip=target;skip.name="skip";skip.prepare=[] {throw std::runtime_error("filter failed");};
    UITestCustomCheck custom{"good-custom",[&]{++prepared;},[]{return std::size_t(0);}};
    auto report=RunUITests(engine,{target,skip},{custom,{"skip-custom",[]{throw std::runtime_error("filter");},{}}},cfg);
    REQUIRE(report.Passed()&&report.targets_tested==2&&report.buttons_tested==1);
    std::fprintf(stderr,"UI input observations: clicks=%d after=%d resets=%d prepared=%d\n",clicks,after,resets,prepared);
    REQUIRE(clicks==1&&after==2&&resets==4&&prepared==2&&report.elapsed_seconds>=0);
    fail_click=true;report=RunUITests(engine,{target},{},cfg);REQUIRE(!report.Passed()&&report.script_errors==1&&report.buttons_tested==1);
    cfg.filter_pattern.clear();cfg.get_error_count={};
    UITestTarget overflow{"overflow",{},{},[&]{renderer.BeginFrame(0,0,0,1);ui.RootBegin(0,0,UISize::Fixed(20),UISize::Fixed(10));ui.Button("too-tall","");ui.RootEnd();renderer.EndFrame();},{}};
    report=RunUITests(engine,{overflow},{},cfg);REQUIRE(!report.Passed()&&report.violations>0);
    report=RunUITests(engine,{{"empty",{},{},{},{}}},{{"errors",{},[]{return std::size_t(3);}},{"null",{},{}}},cfg);
    REQUIRE(report.targets_tested==3&&report.script_errors==3&&!report.Passed());
    report=RunUITests(engine,{}, {},cfg);REQUIRE(report.Passed()&&report.targets_tested==0);
    // Exercise the upstream audit/click runner through real UI rendering and
    // injected press/release, including deliberate violations as negative tests.
    cfg.audit_layout=false;
    auto make_target=[&](std::string name,std::function<void()> draw){
        UITestTarget t;t.name=std::move(name);
        t.render_frame=[&,draw]{renderer.BeginFrame(0,0,0,1);REQUIRE(ui.BeginFrameUI());draw();REQUIRE(ui.EndFrameUI());renderer.EndFrame();};
        return t;
    };
    auto run=[&](const UITestTarget &t){return RunUITests(engine,{t},{},cfg);};
    auto disabled=make_target("disabled",[&]{ui.RootBegin(0,0,UISize::Fixed(100),UISize::Fixed(30));ui.DisabledBegin();ui.Button("disabled-button","");ui.DisabledEnd();ui.RootEnd();});
    int count_before=ui.ClickCount(); report=run(disabled);
    REQUIRE(report.Passed()&&report.buttons_tested==1&&report.disabled_tested==1&&ui.ClickCount()==count_before);
    auto offscreen=make_target("offscreen",[&]{ui.RootBegin(200,200,UISize::Fixed(20),UISize::Fixed(20));ui.Button("outside-viewport","");ui.RootEnd();});
    report=run(offscreen);REQUIRE(!report.Passed()&&report.click_failures==1&&report.buttons_tested==0);
    cfg.test_clicks=false;report=run(offscreen);REQUIRE(report.Passed()&&report.buttons_tested==0);cfg.test_clicks=true;
    auto occluded=make_target("occluded",[&]{for(auto key:{"covered-button","covering-button"}){ui.RootBegin(0,0,UISize::Fixed(100),UISize::Fixed(30));ui.Button(key,"");ui.RootEnd();}});
    report=run(occluded);REQUIRE(!report.Passed()&&report.click_failures==1&&report.buttons_tested==1);
    auto modal=make_target("modal",[&]{ui.RootBegin(0,0,UISize::Fixed(100),UISize::Fixed(30));ui.Button("under-modal","");ui.RootEnd();ui.SetModal(true);ui.RootBegin(0,0,UISize::Fixed(100),UISize::Fixed(30));ui.Panel(Color::Black());ui.RootEnd();ui.SetModal(false);});
    report=run(modal);REQUIRE(report.Passed()&&report.buttons_tested==0&&report.click_failures==0);
    // A genuinely miswired input backend renders a visible button but cannot
    // receive the engine's injected clicks. Audit must catch it; explicit policy
    // may opt this intentionally passive widget out, without inventing clicks.
    Input passive_input;ui.Bind(&passive_input,&camera,&renderer);
    auto blocked=make_target("blocked",[&]{ui.RootBegin(0,0,UISize::Fixed(100),UISize::Fixed(30));ui.Button("blocked-button","");ui.RootEnd();});
    report=run(blocked);REQUIRE(!report.Passed()&&report.click_failures==1);
    int allowed=0;cfg.allow_no_click=[&](std::string_view target,std::string_view widget,std::string_view kind){REQUIRE(target=="blocked"&&widget=="blocked-button"&&kind=="button");++allowed;return true;};
    count_before=ui.ClickCount();report=run(blocked);REQUIRE(report.Passed()&&allowed==1&&ui.ClickCount()==count_before);cfg.allow_no_click={};ui.Bind(&input,&camera,&renderer);
    auto editors=make_target("editors",[&]{ui.RootBegin(0,0,UISize::Fixed(100),UISize::Fixed(100));ui.VStackBegin(4);ui.Checkbox("check-probe",false,std::string_view(""));ui.TextInput("focus-probe");ui.StackEnd();ui.RootEnd();});
    report=run(editors);REQUIRE(report.Passed()&&report.buttons_tested==2&&ui.FocusedTextInput()=="focus-probe");
    bool removed=false;int first_clicks=0,second_clicks=0;
    auto disappearing=make_target("disappearing",[&]{ui.RootBegin(0,0,UISize::Fixed(100),UISize::Fixed(80));ui.VStackBegin(4);ui.Button("remove-first","");if(!removed){ui.Button("removed-second","");}ui.StackEnd();ui.RootEnd();});
    disappearing.after_click=[&]{if(ui.Clicked("remove-first")){removed=true;++first_clicks;}if(ui.Clicked("removed-second"))++second_clicks;};
    report=run(disappearing);REQUIRE(report.Passed()&&report.buttons_tested==2&&removed&&first_clicks==1&&second_clicks==0);
    bool moved=false;first_clicks=0;second_clicks=0;
    auto moving=make_target("moving",[&]{ui.RootBegin(0,0,UISize::Fixed(20),UISize::Auto());ui.Button("move-first","");ui.RootEnd();ui.RootBegin(moved?80:0,40,UISize::Fixed(20),UISize::Auto());ui.Button("moving-second","");ui.RootEnd();});
    moving.after_click=[&]{if(ui.Clicked("move-first")){moved=true;++first_clicks;}if(ui.Clicked("moving-second"))++second_clicks;};
    report=run(moving);REQUIRE(report.Passed()&&report.buttons_tested==2&&moved&&first_clicks==1&&second_clicks==1);
    auto scroll=make_target("scroll",[&]{ui.RootBegin(0,0,UISize::Fixed(100),UISize::Fixed(30));ui.ScrollBoxBegin("runner-scroll",UISize::Fixed(20));for(int i=0;i<5;++i)ui.Button("scroll-item-"+std::to_string(i),"");ui.ScrollBoxEnd();ui.RootEnd();});
    report=run(scroll);REQUIRE(report.Passed()&&report.skipped_offscreen>0&&report.buttons_tested>0);

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
        REQUIRE(rejected==SkeletonSystem::kInvalid);
        if(error.find(why)==std::string::npos)std::fprintf(stderr,"expected %s, got %s\n",why.c_str(),error.c_str());
        REQUIRE(error.find(why)!=std::string::npos);REQUIRE(sk.InstanceCount()==0);
    };
    auto change=[&](const char *path,json value,const char *why){auto doc=rig;doc[json::json_pointer(path)]=std::move(value);reject(doc,why);};
    std::string error;REQUIRE(sk.LoadAsset(resources,(f.dir/"absent-rig").string(),&error)==SkeletonSystem::kInvalid&&error.find("cannot open")!=std::string::npos);
    REQUIRE(sk.LoadAsset(resources,f.write("syntax-rig.json","{"),&error)==SkeletonSystem::kInvalid&&error.find("invalid JSON")!=std::string::npos);
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
    int asset=sk.LoadAsset(resources,f.write("valid-rig.json",rig.dump()),&error);REQUIRE(asset>0&&sk.HasAsset(asset));
    int id=sk.Create(asset);REQUIRE(id>0&&sk.AssetOf(id)==asset&&sk.InstanceCount()==1);
    REQUIRE(sk.BoneCount(id)==2&&sk.BoneName(id,1)=="tip"&&sk.BoneParent(id,1)==0);
    REQUIRE(sk.SlotCount(id)==1&&sk.SlotName(id,0)=="body"&&sk.SlotBone(id,0)==0);
    REQUIRE(sk.AttachmentName(id,0)=="art"&&sk.SlotAttachmentCount(id,0)==1);
    REQUIRE(sk.Play(id,"turn",0)&&sk.ClipName(id)=="turn"&&sk.ClipCount(id)==1);
    sk.SetTime(id,.5f);REQUIRE(close(sk.BoneRotation(id,0),45));sk.Pause(id);REQUIRE(!sk.IsPlaying(id));
    sk.Resume(id);REQUIRE(sk.IsPlaying(id));sk.SetSpeed(id,-1);sk.Update(.25f);REQUIRE(close(sk.Time(id),.25f));
    sk.SetTime(id,-10);REQUIRE(sk.Time(id)==0);sk.SetTime(id,10);REQUIRE(sk.Time(id)==1);
    sk.SetSpeed(id,1);sk.Update(.1f);REQUIRE(sk.Finished(id)&&!sk.Finished(id));
    REQUIRE(sk.SetClip(id,"turn"));sk.SetPoseWeight(id,0);REQUIRE(sk.PoseWeight(id)==0&&close(sk.BoneRotation(id,0),0));
    sk.SetPoseWeight(id,1);sk.SetMix(id,.2f);REQUIRE(sk.SetBoneByName(id,"tip",20,0,0,1,1));
    REQUIRE(close(sk.BonePosition(id,1).x,20));REQUIRE(sk.ResetBone(id,1)&&close(sk.BonePosition(id,1).x,10));
    REQUIRE(sk.SetSlotVisible(id,0,false)&&sk.SetSlotColor(id,0,{1,.5f,.2f,1}));REQUIRE(sk.SetSlotAttachmentIndex(id,0,0));
    REQUIRE(sk.SetSkin(id,"missing")==0);REQUIRE(sk.AimBoneByName(id,"root",{0,20}));REQUIRE(close(sk.BoneRotation(id,0),90));
    REQUIRE(!sk.SolveTwoBoneByName(id,"root","tip","absent",{0,0},1));
    REQUIRE(sk.SetSlotVisible(id,0,true));Rect bounds;REQUIRE(sk.Bounds(id,bounds)&&sk.BoundsWorld(id,{20,20},2,-1,bounds));
    for(int invalid:{-1,0,99999}){
        REQUIRE(!sk.Has(invalid)&&!sk.HasAsset(invalid)&&sk.GetAsset(invalid)==nullptr);
        REQUIRE(sk.Create(invalid)==SkeletonSystem::kInvalid&&!sk.Play(invalid,"turn")&&!sk.PlayIndex(invalid,0));
        REQUIRE(!sk.IsPlaying(invalid)&&!sk.Finished(invalid)&&sk.BoneCount(invalid)==0&&sk.SlotCount(invalid)==0);
        REQUIRE(sk.BoneIndex(invalid,"root")==-1&&sk.BoneName(invalid,0).empty()&&sk.BoneParent(invalid,0)==-1);
        REQUIRE(sk.BonePosition(invalid,0).x==0&&sk.BoneRotation(invalid,0)==0&&sk.BoneLength(invalid,0)==0);
        REQUIRE(!sk.SetBone(invalid,0,0,0,0,1,1)&&!sk.ResetBone(invalid,0));
        REQUIRE(sk.SlotIndex(invalid,"body")==-1&&sk.SlotName(invalid,0).empty()&&sk.SlotBone(invalid,0)==-1);
        REQUIRE(!sk.SetSlotColor(invalid,0,{})&&!sk.SetSlotVisible(invalid,0,true)&&!sk.SetSlotAttachment(invalid,0,"art"));
        REQUIRE(!sk.SetSlotAttachmentIndex(invalid,0,0)&&sk.SlotAttachmentCount(invalid,0)==0&&sk.SetSkin(invalid,"x")==0);
        REQUIRE(sk.AttachmentCount(invalid)==0&&sk.AttachmentName(invalid,0).empty()&&!sk.AttachmentIsMesh(invalid,0));
        REQUIRE(sk.MeshVertexCount(invalid,0)==0&&sk.MeshTriangleCount(invalid,0)==0&&sk.MeshWeightCount(invalid,0,0)==0);
        int bone=123;float weight=123;Vec2 out;REQUIRE(!sk.MeshWeight(invalid,0,0,0,bone,weight)&&!sk.MeshVertexWorld(invalid,0,0,out));
        REQUIRE(sk.MeshBindVertex(invalid,0,0).x==0&&!sk.SampleDeformAt(invalid,0,0)&&sk.MeshBindKey(invalid,0)==0&&!sk.MeshSharesBind(invalid,0,0));
        REQUIRE(sk.MeshHullCount(invalid,0)==0&&!sk.MeshHullVertex(invalid,0,0,out)&&sk.MeshHullWorld(invalid,0,{},1,1,&out,1)==0);
        REQUIRE(!sk.Bounds(invalid,bounds)&&!sk.BoundsWorld(invalid,{},1,1,bounds)&&!sk.AimBone(invalid,0,{}));
        REQUIRE(sk.EventCount(invalid)==0&&sk.EventName(invalid,0).empty()&&sk.EventData(invalid,0)==0);
    }
    REQUIRE(!sk.Play(id,"missing")&&!sk.PlayIndex(id,99)&&!sk.SetBone(id,99,0,0,0,1,1));
    REQUIRE(sk.BoneName(id,99).empty()&&sk.BoneParent(id,99)==-1&&sk.BoneLength(id,99)==0);
    REQUIRE(!sk.ResetBone(id,99)&&!sk.SetSlotVisible(id,99,true)&&!sk.SetSlotAttachmentIndex(id,0,99));
    REQUIRE(!sk.AttachmentIsMesh(id,0)&&sk.MeshVertexCount(id,0)==0&&!sk.SampleDeformAt(id,0,0));
    int mid=sk.LoadAsset(resources,f.write("mesh-rig.json",mesh.dump()),&error);REQUIRE(mid>0);int mi=sk.Create(mid);
    Vec2 vertex;REQUIRE(sk.AttachmentIsMesh(mi,0)&&sk.MeshVertexCount(mi,0)==3&&sk.MeshTriangleCount(mi,0)==1);
    REQUIRE(sk.MeshVertexWorld(mi,0,1,vertex)&&close(vertex.x,10)&&close(vertex.y,0));
    REQUIRE(sk.MeshHullWorld(mi,0,{20,30},2,-1,&vertex,1)==1&&close(vertex.x,20)&&close(vertex.y,30));
    REQUIRE(!sk.MeshHullVertex(mi,0,99,vertex)&&sk.MeshHullWorld(mi,0,{},1,1,nullptr,0)==0);
    sk.RemoveAsset(asset);REQUIRE(!sk.Has(id)&&!sk.HasAsset(asset)&&sk.Has(mi));
    sk.Destroy(mi);REQUIRE(!sk.Has(mi)&&sk.InstanceCount()==0);sk.Clear();REQUIRE(sk.InstanceCount()==0);
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
            if(!ok)std::fprintf(stderr,"Lua layout assertion: %.*s\n",int(label.size()),label.data());REQUIRE(ok);
        }));
    fakelua::CompileConfig cfg;cfg.debug_mode=false;fakelua::DisableAllJit(cfg);cfg.disable_jit[fakelua::JIT_INTERP]=false;
    fakelua::CompileFile(st,"tests/coverage_layout_bindings.lua",cfg);
    auto previous=ui.ThemeColor("button");const int target=renderer.GetRenderTargets().Create(64,64);REQUIRE(target>0);
    renderer.BeginFrame(0,0,0,1);REQUIRE(renderer.BeginRenderTarget(target));renderer.ClearActiveTarget({0,0,0,1});
    int result=0;fakelua::Call(st,fakelua::JIT_INTERP,"layout_contract",result);
    renderer.EndRenderTarget();renderer.EndFrame();REQUIRE(result==123&&sprites==1&&ui.IsModal());
    REQUIRE(ui.ThemeColor("button").r==previous.r&&ui.ThemeColor("button").g==previous.g);
    Color pixel;auto *rt=renderer.GetRenderTargets().Get(target);REQUIRE(rt&&rt->ReadPixel(3,3,pixel)&&pixel.r>.9f&&pixel.b<.1f);
    REQUIRE(rt->ReadPixel(19,3,pixel)&&pixel.b>.9f&&pixel.r<.1f);
    REQUIRE(rt->ReadPixel(40,3,pixel)&&pixel.r>.9f&&pixel.b<.1f);
    REQUIRE(rt->ReadPixel(43,23,pixel)&&pixel.r>.9f&&pixel.b<.1f);
    REQUIRE(rt->ReadPixel(39,23,pixel)&&pixel.b>.9f&&pixel.r<.1f);
    renderer.GetRenderTargets().Destroy(target);ui.SetModal(false);textures.Clear();
}
static void skeleton_binding_tests(Fixtures &f){
    Engine engine;fakelua::FakeluaStateGuard state;auto *st=state.GetState();REQUIRE(st);
    RegisterScriptApi(nullptr,&engine);RegisterScriptApi(st,nullptr);RegisterScriptApi(st,&engine);
    fakelua::RegisterNativeFunction(st,"gap_expect",false,
        std::function<void(fakelua::State*,bool,std::string_view)>([](auto*,bool ok,std::string_view label){
            if(!ok)std::fprintf(stderr,"Lua binding assertion: %.*s\n",int(label.size()),label.data());REQUIRE(ok);
        }));
    fakelua::CompileConfig cfg;cfg.debug_mode=false;fakelua::DisableAllJit(cfg);cfg.disable_jit[fakelua::JIT_INTERP]=false;
    fakelua::CompileFile(st,"tests/coverage_skeleton_bindings.lua",cfg);
    int result=0;fakelua::Call(st,fakelua::JIT_INTERP,"binding_roundtrip",result,(f.dir/"valid-rig.json").string(),(f.dir/"mesh-rig.json").string());
    REQUIRE(result==123&&engine.GetSkeletons().InstanceCount()==0);
}
static void rule_tests(Fixtures &f){
    bool finalized=false;int registrations=0,notes=0;
    log::Config lc;lc.dir=(f.dir/"logs").string();lc.echo_stderr=false;lc.crash_reports=false;lc.rotate=false;REQUIRE(log::Init(lc));
    {
        RuleHost host;REQUIRE(!host.Opened()&&host.State()==nullptr);
        REQUIRE(host.Open([&](void *ptr){++registrations;auto *st=static_cast<fakelua::State*>(ptr);
            auto &mgr=fakelua::GetNativeObjectManager(st);auto *obj=mgr.GlobalCreate("owned","Owned",1);obj->SetFinalizer([&](auto*){finalized=true;});
            fakelua::RegisterNativeFunction(st,"md_note",false,std::function<void(fakelua::State*)>([&](auto*){++notes;}));
            fakelua::RegisterNativeFunction(st,"boom",false,std::function<void(fakelua::State*)>([](auto*){throw std::runtime_error("fixture failure");}));
        }));
        REQUIRE(host.Opened()&&host.State());REQUIRE(host.Open({})&&registrations==1);
        auto one=f.write("rule_01.lua","function md_answer() return 42.0 end\n");
        auto two=f.write("rule_02.lua","function u_known() return 5.0 end\n");
        f.write("rule_a.bak.lua","broken");f.write("rule_bad.lua~","broken");f.write("other.lua","broken");
        auto first=(f.dir/"common.lua").string();auto files=RuleHost::DiscoverChunks(f.dir.string(),"rule_",first);
        REQUIRE((files==std::vector<std::string>{first,one,two}));
        REQUIRE(RuleHost::DiscoverChunks((f.dir/"absent").string(),"rule_",first)==std::vector<std::string>{first});
        REQUIRE(host.CompileUnit(one,true,"test")&&host.HasEntry("md_answer",true)&&host.HasEntry("md_answer",false));
        double value=0;fakelua::Call(static_cast<fakelua::State*>(host.State()),fakelua::JIT_INTERP,"md_answer",value);REQUIRE(value==42);
        REQUIRE(!host.HasEntry("not_here",true));
        auto bad=f.write("bad.lua","function ( broken");
        REQUIRE(!host.CompileUnit(bad,true,"test"));REQUIRE(!host.CompileUnit((f.dir/"missing").string(),true,"test"));
        bool interp=true;REQUIRE(host.CompileUnitWithFallback(two,interp,"test")&&interp);
        interp=false;REQUIRE(!host.CompileUnitWithFallback(bad,interp,"test")&&interp);
        int visited=0,retries=0;interp=true;
        auto tag=[](const std::string &p){return std::filesystem::path(p).filename().string();};
        REQUIRE(host.CompileSetWithFallback({one,two},interp,"test",[&](auto&){++visited;},[&]{++retries;},tag));
        REQUIRE(visited==2&&retries==0);
        REQUIRE(!host.CompileSetWithFallback({bad},interp,"test",{},[&]{++retries;},tag)&&retries==1);
        auto recover=f.write("recover.lua","function (");interp=false;
        REQUIRE(host.CompileSetWithFallback({recover},interp,"test",{},[&]{f.write("recover.lua","function recovered() return 9.0 end");},tag));
        REQUIRE(interp&&host.HasEntry("recovered",true));
        auto prelude=f.write("prelude.lua","function push_ok() md_note() return 0.0 end");
        REQUIRE(host.CompilePrelude(prelude,"push_ok","test")&&notes==1);
        REQUIRE(!host.CompilePrelude(prelude,"missing_push","test"));
        REQUIRE(!host.CompilePrelude(bad,"push_ok","test"));
        auto throwing=f.write("throw.lua","function push_throws() boom() return 0.0 end");
        REQUIRE(!host.CompilePrelude(throwing,"push_throws","test"));
        host.SetNativeChecker([](const char *n){return std::string(n)=="md_note";});
        auto lint=f.write("lint.lua","function lint_probe() md_missing() u_missing() u_known() md_note() return 0.0 end\n-- some_md_skip()\n");
        host.LintUnit(lint,"direct-lint");host.LintUnit((f.dir/"absent.lua").string(),"missing");
        auto logfile=f.read("logs/game.log");
        REQUIRE(logfile.find("direct-lint calls unregistered 'md_missing'")!=std::string::npos);
        REQUIRE(logfile.find("direct-lint calls unregistered 'u_missing'")!=std::string::npos);
        REQUIRE(logfile.find("direct-lint calls unregistered 'md_note'")==std::string::npos&&logfile.find("direct-lint calls unregistered 'u_known'")==std::string::npos);
    }
    REQUIRE(finalized);log::Shutdown();
    RuleHost no_reg;REQUIRE(no_reg.Open({}));
}
#endif
int main(){try{
    Fixtures f;
#ifdef FAKE2D_WITH_FAKELUA
    rule_tests(f);
#endif
    character_tests();
    platform::Window window;platform::WindowDesc desc;desc.width=64;desc.height=64;desc.headless=true;REQUIRE(window.Create(desc));
    Renderer renderer;REQUIRE(renderer.Init(64,64));ui_runner_tests(renderer);
    const std::uint8_t rgba[]={255,0,0,255,0,255,0,255,0,0,255,255,255,255,255,255};
    Texture2D sheet;REQUIRE(sheet.Create(2,2,rgba,4,TextureFilter::Nearest));
    bounds_regression();atlas_tests(f,sheet);node_tests(renderer,sheet);skeleton_tests(f,renderer);
#ifdef FAKE2D_WITH_FAKELUA
    skeleton_binding_tests(f);layout_binding_tests(f,renderer);
#endif
    std::printf("coverage gaps: PASS %d assertions\n",checks);return 0;
}catch(const std::exception &e){std::fprintf(stderr,"coverage gaps FAILED: %s\n",e.what());return 1;}}
