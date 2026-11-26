#include "gtest_support.h"
#include "fake2d/save_store.h"
#include "fake2d/shader.h"
#include "fake2d/input.h"
#include "fake2d/input_recording.h"
#include "fake2d/engine.h"
#include "fake2d/lua_ui.h"
#include "fake2d/renderer.h"
#include "fake2d/tilemap.h"
#include "fake2d/skeleton.h"
#include "fake2d/i18n.h"
#include "fake2d/province_map.h"
#include "fake2d/resource_manager.h"
#include "fake2d/log.h"
#include "fake2d/ease.h"
#include "fake2d/font_set.h"
#include "fake2d/text_utils.h"
#include "platform/window.h"
#include "render/gl.h"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <nlohmann/json.hpp>
#ifdef FAKE2D_WITH_FAKELUA
#include <fakelua.h>
#include "script/script_bindings.h"
#endif
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <csignal>
#include <cstdlib>
#if !defined(_WIN32)
#include <unistd.h>
#include <sys/wait.h>
#include <sys/resource.h>
#endif
#ifdef FAKE2D_TEST_CRASH_PROFILE
extern "C" void __gcov_dump();
extern "C" int __real_raise(int);
extern "C" [[noreturn]] void __real__Exit(int);
extern "C" int __wrap_raise(int sig){__gcov_dump();return __real_raise(sig);}
extern "C" [[noreturn]] void __wrap__Exit(int code){__gcov_dump();__real__Exit(code);}
#endif
using namespace fake2d;
static int checks=0;
static bool near_value(float a,float b){return std::abs(a-b)<.005f;}
struct Fixtures{
    std::filesystem::path dir=std::filesystem::path(".deps")/("round3-tests-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Fixtures(){std::filesystem::create_directories(dir);}
    ~Fixtures(){std::error_code ec;std::filesystem::remove_all(dir,ec);}
    std::string path(const std::string &name)const{return (dir/name).string();}
    std::string write(const std::string &name,std::string_view data)const{std::ofstream out(path(name),std::ios::binary);out.write(data.data(),data.size());out.close();if(!out)throw std::runtime_error("fixture write");return path(name);}
    std::string read(const std::string &name)const{std::ifstream in(path(name),std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
};
// Regress the newly integrated public audit contracts using real recorded
// layout, never fabricated AuditRects or counters.
static void upstream_layout_contracts(Renderer &renderer) {
    Input input; Camera2D camera; camera.SetViewport(128,128);
    UISystem ui; ui.Bind(&input,&camera,&renderer); ui.SetAudit(true);
    auto audit=[&](std::string_view rules,const std::vector<Rect> &children,int kind=0){
        input.InjectFrame({-100,-100},{},{}); ui.NewFrame(); renderer.BeginFrame(0,0,0,1);
        ui.RootBegin(0,0,UISize::Fixed(100),UISize::Fixed(100));
        ui.BeginAlignGroup("upstream",rules);
        for(std::size_t i=0;i<children.size();++i){
            const auto &r=children[i]; ui.RootAnchorBegin("left","top",UISize::Fixed(r.width),UISize::Fixed(r.height),r.x,r.y,0,true);
            if(kind==1){ ui.NextSize(UISize::Grow(),UISize::Fixed(r.height)); ui.Button("member-"+std::to_string(i),""); }
            if(kind==2){ ui.NextSize(UISize::Grow(),UISize::Fixed(r.height)); ui.ProgressBar(.5f,Color::White()); }
            ui.RootEnd();
        }
        ui.EndAlignGroup(); ui.RootEnd(); renderer.EndFrame();
        std::vector<UISystem::LayoutIssue> result;
        for(const auto &issue:ui.AuditLayout()) if(issue.category=="align_group") result.push_back(issue);
        return result;
    };
    for(std::string_view kind:{"button","tab","checkbox","slider","image button","combo","combo_item","text_input","text_area","text input","text area","password","radio","splitter","tree"}) EXPECT_TRUE((UISystem::IsInteractiveKind(kind)) && (++checks, true));
    for(std::string_view kind:{"","panel","tooltip","progress","text","BUTTON"}) EXPECT_TRUE((!UISystem::IsInteractiveKind(kind)) && (++checks, true));
    const std::string parent="center_in_parent|symmetric_x|symmetric_y|fit_parent";
    EXPECT_TRUE((audit(parent,{{40,45,20,10}}).empty()) && (++checks, true));
    auto issues=audit(parent,{{10,20,20,10}});
    if (issues.size()!=4) {
        std::fprintf(stderr,"alignment issues=%zu\n",issues.size());
        for (const auto &entry:ui.AuditRects()) std::fprintf(stderr,"audit %s %s rect=%g,%g,%g,%g\n",entry.kind.c_str(),entry.name.c_str(),entry.rect.x,entry.rect.y,entry.rect.width,entry.rect.height);
        for (const auto &issue:issues) std::fprintf(stderr,"issue %s delta=%g\n",issue.rule.c_str(),issue.delta);
    }
    EXPECT_TRUE((issues.size()==4) && (++checks, true));
    for(const auto &issue:issues){
        EXPECT_TRUE((issue.delta>0&&issue.b_name=="upstream"&&!issue.message.empty()) && (++checks, true));
        if(issue.rule=="center_x_in_parent") EXPECT_TRUE((near_value(issue.delta,30)) && (++checks, true));
        else if(issue.rule=="center_y_in_parent") EXPECT_TRUE((near_value(issue.delta,25)) && (++checks, true));
        else if(issue.rule=="symmetric_x") EXPECT_TRUE((near_value(issue.delta,60)) && (++checks, true));
        else {EXPECT_TRUE((issue.rule=="symmetric_y") && (++checks, true));EXPECT_TRUE((near_value(issue.delta,50)) && (++checks, true));}
    }
    issues=audit("fit_parent",{{95,95,20,20}});
    EXPECT_TRUE((issues.size()==1&&issues[0].rule=="fit_parent"&&near_value(issues[0].delta,15)) && (++checks, true));
    // Explicit axis rules, the combined aliases, and a multi-member union have
    // different semantics; symmetric members need not be individually centered.
    EXPECT_TRUE((audit("center_x_in_parent",{{40,0,20,10}}).empty()) && (++checks, true));
    EXPECT_TRUE((audit("center_y_in_parent",{{0,45,20,10}}).empty()) && (++checks, true));
    EXPECT_TRUE((audit("group_center_in_parent",{{10,40,20,20},{70,40,20,20}}).empty()) && (++checks, true));
    issues=audit("group_center_x_in_parent|group_center_y_in_parent",{{0,0,10,10},{20,20,10,10}});
    EXPECT_TRUE((issues.size()==2) && (++checks, true));
    for(const auto &issue:issues) EXPECT_TRUE((near_value(issue.delta,35)&&issue.a_name=="upstream") && (++checks, true));
    EXPECT_TRUE((audit("equal_spacing_x",{{10,0,10,10},{40,0,10,10},{70,0,10,10}}).empty()) && (++checks, true));
    issues=audit("equal_spacing_x",{{80,0,10,10},{10,0,10,10},{40,0,10,10}});
    EXPECT_TRUE((issues.size()==1&&issues[0].rule=="equal_spacing_x"&&near_value(issues[0].delta,10)) && (++checks, true));
    EXPECT_TRUE((audit("equal_spacing_y",{{0,10,10,10},{0,40,10,10},{0,70,10,10}}).empty()) && (++checks, true));
    issues=audit("equal_spacing_y",{{0,80,10,10},{0,10,10,10},{0,40,10,10}});
    EXPECT_TRUE((issues.size()==1&&issues[0].rule=="equal_spacing_y"&&near_value(issues[0].delta,10)) && (++checks, true));
    EXPECT_TRUE((audit("widgets|center_in_parent",{{40,45,20,10}},1).empty()) && (++checks, true));
    issues=audit("widgets|center_in_parent",{{0,0,20,10}},1);
    EXPECT_TRUE((issues.size()==2&&issues[0].a_name=="member-0") && (++checks, true));
    EXPECT_TRUE((audit("widgets|center_in_parent",{{0,0,20,10}},2).empty()) && (++checks, true));
    issues=audit("include_text|center_in_parent",{{0,0,20,10}},2);
    EXPECT_TRUE((issues.size()==2) && (++checks, true));
    EXPECT_TRUE((audit("widgets|center_in_parent",{}).empty()) && (++checks, true));
}
static void text_utility_contracts(){
    const std::string text="Aé你😀";EXPECT_TRUE((utf8::Decode(text)==std::vector<unsigned>({65,233,0x4f60,0x1f600})&&utf8::Count(text)==4) && (++checks, true));
    EXPECT_TRUE((utf8::Cut(text,0).empty()&&utf8::Cut(text,2)=="Aé"&&utf8::Cut(text,99)==text&&utf8::First(text)=="A"&&utf8::First("").empty()) && (++checks, true));
    EXPECT_TRUE((utf8::Encode(65)=="A"&&utf8::Encode(233)=="é"&&utf8::Encode(0x1f600)=="😀") && (++checks, true));
    EXPECT_TRUE((utf8::Encode(0)==std::string(1,'\0')&&utf8::Decode(std::string("A\0B",3))==std::vector<unsigned>({65,0,66})) && (++checks, true));
    EXPECT_TRUE((Strf("%s:%d %.2f","value",42,.25)=="value:42 0.25"&&Strf("").empty()) && (++checks, true));std::string long_text(1500,'x');EXPECT_TRUE((Strf("%s:%d",long_text.c_str(),42)==long_text+":42") && (++checks, true));
}
static void font_ladder_contracts(){
    const float bases[]={12,0,24,18,48,36};FontSet set(3,2,bases);const FontSet &constant=set;
    EXPECT_TRUE((set.Tiers()==3&&set.Faces()==2&&set.Base(1,0)==0&&set.Base(0,2)==48&&!set.Loaded(0,0)&&set.Scale(0,0,16)==0) && (++checks, true));
    std::vector<std::pair<int,float>> called;
    EXPECT_TRUE((!set.Reload(2,[&](Font &font,int face,float px){called.emplace_back(face,px);return face==1&&px==36&&font.LoadFallback();})) && (++checks, true));
    const std::vector<std::pair<int,float>> expected={{0,24},{0,48},{1,36},{0,96},{1,72}};EXPECT_TRUE((called==expected) && (++checks, true));
    EXPECT_TRUE((set.Pick(1,0)==1&&set.Pick(1,100)==1&&set.Pick(0,0)==0&&set.Loaded(1,1)&&!set.Loaded(1,2)) && (++checks, true));
    EXPECT_TRUE((constant.Slot(1,1)==set.Slot(1,1)&&constant.Slot(1,1)->HasGlyph(U'A')&&set.Scale(1,1,16)==2) && (++checks, true));
    called.clear();EXPECT_TRUE((set.Reload(1.5f,[&](Font &font,int face,float px){called.emplace_back(face,px);return font.LoadFallback();})) && (++checks, true));
    const std::vector<std::pair<int,float>> scaled={{0,18},{0,36},{1,27},{0,72},{1,54}};EXPECT_TRUE((called==scaled&&set.Pick(-1,20)==1&&set.Pick(99,50)==2&&set.Pick(1,0)==1) && (++checks, true));
    EXPECT_TRUE((!set.Loaded(1,0)&&set.Loaded(0,2)&&set.Scale(0,2,24)==3) && (++checks, true));
}
static void interpolation_contracts(){
    const std::pair<const char *,float> midpoints[]={{"in_quad",.25f},{"out_quad",.75f},{"inout_quad",.5f},{"in_cubic",.125f},{"out_cubic",.875f},{"inout_cubic",.5f},{"out_back",1.0876975f},{"out_elastic",1.015625f},{"unknown",.5f}};
    for(const auto &[name,value]:midpoints){EXPECT_TRUE((near_value(ease::ByName(name,.5f),value)) && (++checks, true));EXPECT_TRUE((near_value(ease::ByName(name,-1),0)&&near_value(ease::ByName(name,2),1)) && (++checks, true));}
    EXPECT_TRUE((ease::InOutQuad(.25f)==.125f&&ease::InOutQuad(.75f)==.875f&&ease::InOutCubic(.25f)==.0625f&&ease::InOutCubic(.75f)==.9375f) && (++checks, true));
    EXPECT_TRUE((ease::SmoothStep(2,6,-1)==2&&ease::SmoothStep(2,6,2)==6&&ease::SmoothStep(2,6,.5f)==4) && (++checks, true));
    EXPECT_TRUE((near_value(ease::Approach(10,20,std::log(2.f),1),15)&&ease::Approach(10,20,0,1)==10) && (++checks, true));
    EXPECT_TRUE((near_value(ease::Approach(ease::Approach(1,10,2,.25f),10,2,.75f),ease::Approach(1,10,2,1))) && (++checks, true));
    Vec2 v{3,-4};v*=2;EXPECT_TRUE((v.x==6&&v.y==-8&&v.Length()==10) && (++checks, true));Rect r{1,2,3,4};EXPECT_TRUE((r.Left()==1&&r.Top()==2&&r.Right()==4&&r.Bottom()==6) && (++checks, true));
    const std::pair<Color(*)(),std::uint32_t> palette[]={{Color::Black,0xff000000u},{Color::Red,0xff0000ffu},{Color::Green,0xff00ff00u},{Color::Blue,0xffff0000u},{Color::Yellow,0xff00ffffu},{Color::Magenta,0xffff00ffu}};
    for(const auto &[factory,packed]:palette)EXPECT_TRUE((factory().ToRGBA8()==packed) && (++checks, true));
    EXPECT_TRUE((Color::FromRGBA8(255,0,255,0).ToRGBA8()==0x00ff00ffu) && (++checks, true));Color clamped{1.5f,-1,.5f,2};EXPECT_TRUE((clamped.ToRGBA8()==0xff7f00ffu) && (++checks, true));
}
static void ui_extra_contracts(Fixtures &f,Renderer &renderer){
    UITextureCache cache;EXPECT_TRUE((!cache.GetOrLoad("",f.dir.string(),"")) && (++checks, true));auto bad=f.write("corrupt-ui.ppm","broken");EXPECT_TRUE((!cache.GetOrLoad("corrupt-ui.ppm",f.dir.string(),"")) && (++checks, true));
    f.write("corrupt-ui.ppm",std::string("P6\n2 2\n255\n")+std::string(12,char(255)));EXPECT_TRUE((!cache.GetOrLoad("corrupt-ui.ppm",f.dir.string(),"")) && (++checks, true));cache.Clear();EXPECT_TRUE((cache.GetOrLoad("corrupt-ui.ppm",f.dir.string(),"")->Width()==2) && (++checks, true));
    // Prefix eviction must release successful entries and negative-cache entries,
    // without disturbing differently named textures or failures. Reload from disk
    // (with a different shape) proves this is not merely an ownership no-op.
    f.write("screen-a.ppm",std::string("P6\n2 2\n255\n")+std::string(12,char(255)));
    EXPECT_TRUE((cache.GetOrLoad("screen-a.ppm",f.dir.string(),"")->Width()==2) && (++checks, true));
    EXPECT_TRUE((!cache.GetOrLoad("screen-missing.ppm",f.dir.string(),"")) && (++checks, true));
    EXPECT_TRUE((!cache.GetOrLoad("other-missing.ppm",f.dir.string(),"")) && (++checks, true));
    f.write("screen-a.ppm",std::string("P6\n3 1\n255\n")+std::string(9,char(255)));
    f.write("screen-missing.ppm",std::string("P6\n1 1\n255\n")+std::string(3,char(255)));
    f.write("other-missing.ppm",std::string("P6\n1 1\n255\n")+std::string(3,char(255)));
    cache.EvictPrefix("SCREEN");EXPECT_TRUE((cache.GetOrLoad("screen-a.ppm",f.dir.string(),"")->Width()==2) && (++checks, true));
    cache.EvictPrefix("screen-");
    EXPECT_TRUE((cache.GetOrLoad("screen-a.ppm",f.dir.string(),"")->Width()==3) && (++checks, true));
    EXPECT_TRUE((cache.GetOrLoad("screen-missing.ppm",f.dir.string(),"")->Height()==1) && (++checks, true));
    EXPECT_TRUE((!cache.GetOrLoad("other-missing.ppm",f.dir.string(),"")) && (++checks, true));
    EXPECT_TRUE((cache.GetOrLoad("corrupt-ui.ppm",f.dir.string(),"")->Width()==2) && (++checks, true));
    cache.EvictPrefix("");EXPECT_TRUE((cache.GetOrLoad("other-missing.ppm",f.dir.string(),"")->Width()==1) && (++checks, true));
    UITipTracker tips;tips.BeginFrame();tips.SetFallbackTip("fallback");tips.PushScroll("outer",{0,0,80,80},2);tips.PushScroll("inner",{50,0,50,50},3);
    auto clipped=tips.ToScreenRect({50,5,60,60});EXPECT_TRUE((clipped.x==50&&clipped.y==0&&clipped.width==30&&clipped.height==50&&tips.CurrentScrollOffset()==5) && (++checks, true));
    tips.RecordClippedEntry({50,5,60,60},"nested");tips.RecordEntry({0,0,0,2},"invalid");tips.RecordClippedEntry({0,0,2,2},"");tips.RecordOccluder({0,0,-1,2});
    EXPECT_TRUE((tips.Poll({60,5})=="nested"&&tips.PolledRect().width==30) && (++checks, true));EXPECT_TRUE((tips.Poll({90,5},"override")=="override"&&tips.PolledRect().x==80&&tips.PolledRect().width==20) && (++checks, true));
    EXPECT_TRUE((tips.Poll({90,5})=="fallback"&&tips.Poll({-1001,0}).empty()&&tips.PolledRect().width==0) && (++checks, true));tips.PopScroll();tips.PopScroll();tips.BeginFrame();EXPECT_TRUE((tips.Poll({0,0}).empty()&&tips.FallbackTip().empty()) && (++checks, true));
    Camera2D camera(128,128);Input input;UISystem ui;ui.Bind(&input,&camera,&renderer);
    EXPECT_TRUE((ui.Tween("in",0,1,0,"in")==0&&ui.Tween("in",1,1,.25f,"in")==.0625f) && (++checks, true));
    EXPECT_TRUE((ui.Tween("out",0,1,0,"out")==0&&ui.Tween("out",1,1,.25f,"out")==.4375f) && (++checks, true));
    auto slider=[&](float x,bool down,float value){input.InjectFrame({x,5},{},down?std::vector<std::int64_t>{0}:std::vector<std::int64_t>{});ui.NewFrame();renderer.BeginFrame(0,0,0,1);ui.RootBegin(0,0,UISize::Fixed(100),UISize::Fixed(20));ui.NextSize(UISize::Grow(),UISize::Fixed(20));float next=ui.Slider("grab",value,0,1);ui.RootEnd();renderer.EndFrame();return next;};
    EXPECT_TRUE((slider(47,true,.5f)==.5f&&ui.ClickCount()==1) && (++checks, true));EXPECT_TRUE((slider(69,true,.5f)==.75f&&slider(69,false,.75f)==.75f) && (++checks, true));
    auto scroll=[&](float y,bool down){input.InjectFrame({95,y},{},down?std::vector<std::int64_t>{0}:std::vector<std::int64_t>{});ui.NewFrame();renderer.BeginFrame(0,0,0,1);ui.RootBegin(0,0,UISize::Fixed(100),UISize::Fixed(100));ui.ScrollBoxBegin("bar",UISize::Fixed(60));ui.Spacer(240);ui.ScrollBoxEnd();ui.RootEnd();float value=ui.ScrollOffset("bar");renderer.EndFrame();return value;};
    EXPECT_TRUE((scroll(5,true)==0&&ui.ScrollContentHeight("bar")==240) && (++checks, true));EXPECT_TRUE((near_value(scroll(23,true),90)&&near_value(scroll(23,false),90)) && (++checks, true));
    const int target=renderer.GetRenderTargets().Create(128,128);EXPECT_TRUE((target>0) && (++checks, true));ui.NewFrame();renderer.BeginFrame(0,0,0,1);EXPECT_TRUE((renderer.BeginRenderTarget(target)) && (++checks, true));renderer.ClearActiveTarget({0,0,0,1});
    ui.RootBegin(10,20,UISize::Fixed(60),UISize::Fixed(40));ui.CanvasBegin(UISize::Grow(),UISize::Grow());ui.DisabledBegin();ui.DrawSpriteAt(&Texture2D::White(),1,2,8,8,{1,0,0,1});ui.DisabledEnd();ui.FillRect(30,0,8,8,{0,1,0,1});ui.DrawSpriteAt(nullptr,0,0,5,5);ui.FillRect(0,0,0,1,Color::White());ui.CanvasEnd();ui.RootEnd();renderer.EndRenderTarget();renderer.EndFrame();
    auto *rt=renderer.GetRenderTargets().Get(target);Color pixel;EXPECT_TRUE((rt->ReadPixel(12,23,pixel)&&near_value(pixel.r,.4f)&&pixel.g<.01f) && (++checks, true));EXPECT_TRUE((rt->ReadPixel(42,22,pixel)&&pixel.g>.99f&&pixel.r<.01f) && (++checks, true));EXPECT_TRUE((rt->ReadPixel(2,2,pixel)&&pixel.r<.01f&&pixel.g<.01f) && (++checks, true));renderer.GetRenderTargets().Destroy(target);
}
// Verify exported accessibility output of real controls, including redaction.
static void ci_ui_semantics(Renderer &renderer){
    Camera2D camera(640,480);Input input;UISystem ui;ui.Bind(&input,&camera,&renderer);
    input.InjectFrame({-1000,-1000},{},{});ui.NewFrame();renderer.BeginFrame(0,0,0,1);
    EXPECT_TRUE((ui.SemanticTree().role=="application"&&ui.SemanticNodes().empty()) && (++checks, true));
    EXPECT_TRUE((!ui.MenuBegin("outside","Outside")&&!ui.MenuItem("outside","Outside")) && (++checks, true));ui.MenuSeparator();
    auto invalid=ui.VirtualBegin("invalid",0,0);EXPECT_TRUE((invalid.first==0&&invalid.count==0) && (++checks, true));ui.VirtualEnd();ui.VirtualEnd();
    EXPECT_TRUE((ui.ScopedKey("").empty()&&ui.ScopedKey("/absolute")=="absolute") && (++checks, true));
    ui.PushID("scope");EXPECT_TRUE((ui.ScopedKey("child")=="scope/child") && (++checks, true));
    EXPECT_TRUE((ui.DroppedType("missing").empty()&&ui.DroppedPayload("/missing").empty()) && (++checks, true));ui.PopID();
    ui.RootBegin(0,0,UISize::Fixed(600),UISize::Fixed(1500),0,false);ui.VStackBegin(0,0);
    EXPECT_TRUE((!ui.Button("button","Button")&&!ui.ImageButton("image-button",&Texture2D::White(),{0,0,1,1},"Image")) && (++checks, true));
    EXPECT_TRUE((!ui.Checkbox("check",true,"Check")&&!ui.RadioButton("radio",true,"Radio")) && (++checks, true));
    EXPECT_TRUE((ui.Slider("slider",.25f,0,1)==.25f) && (++checks, true));ui.SetTextValue("text","visible");ui.SetTextValue("secret","must-never-appear");
    EXPECT_TRUE((!ui.TextInput("text")&&!ui.TextPassword("secret")) && (++checks, true));
    EXPECT_TRUE((!ui.Tab("tab","Tab",true)&&!ui.Tree("tree","Tree",false)) && (++checks, true));
    EXPECT_TRUE((!ui.ComboBegin("combo","Combo",false)&&!ui.ComboItem("option","Option",true)) && (++checks, true));
    ui.ProgressBar(.75f,Color::Green());ui.Text("Label");ui.RichText("Rich");ui.Image(&Texture2D::White(),{0,0,1,1});
    EXPECT_TRUE((ui.Splitter("split",40,10)==40) && (++checks, true));
    ui.DragSourceBegin("source","inventory","item-42");ui.Text("Drag");ui.DragSourceEnd();
    ui.DropTargetBegin("target","inventory");ui.Text("Drop");ui.DropTargetEnd();
    ui.MenuBarBegin();EXPECT_TRUE((!ui.MenuBegin("menu","Menu")) && (++checks, true));ui.MenuBarEnd();EXPECT_TRUE((!ui.MenuItem("item","Item","Ctrl+I",true)) && (++checks, true));
    ui.ScrollBoxBegin("scroll",UISize::Fixed(20));ui.Text("Scroll");ui.ScrollBoxEnd();
    ui.TableBegin("table",{UISize::Fixed(40)},0,0);ui.TableRowBegin();ui.TableCellBegin();ui.Text("Cell");ui.TableCellEnd();ui.TableRowEnd();ui.TableEnd();
    const std::string escaped="quote\" slash\\ newline\nreturn\rtab\t";
    ui.NextSemantics("status",escaped,"value",escaped);ui.CanvasBegin(UISize::Fixed(10),UISize::Fixed(10));ui.CanvasEnd();
    ui.RequestFocus("text");ui.StackEnd();ui.RootEnd();renderer.EndFrame();
    auto roots=ui.SemanticNodes();EXPECT_TRUE((roots.size()==1&&roots[0].role=="window") && (++checks, true));
    std::unordered_map<std::string,UISemanticNode> keyed;std::unordered_map<std::string,int> roles;bool custom=false;
    std::function<void(const UISemanticNode&)> walk=[&](const UISemanticNode &n){
        ++roles[n.role];if(!n.key.empty())keyed[n.key]=n;
        if(n.role=="status"){EXPECT_TRUE((n.label==escaped&&n.hint==escaped&&n.value=="value") && (++checks, true));custom=true;}
        for(const auto &c:n.children)walk(c);
    };walk(ui.SemanticTree());EXPECT_TRUE((custom) && (++checks, true));
    const std::pair<const char*,const char*> expected[]={{"button","button"},{"image-button","button"},{"check","checkbox"},{"radio","radio"},{"slider","slider"},{"text","text_field"},{"secret","text_field"},{"tab","tab"},{"tree","tree_item"},{"combo","combobox"},{"option","option"},{"split","splitter"},{"source","draggable"},{"target","drop_zone"},{"menu","menu"},{"item","menuitem"},{"scroll","scroll_view"},{"table","table"}};
    for(const auto &[key,role]:expected){EXPECT_TRUE((keyed.count(key)==1) && (++checks, true));EXPECT_TRUE((keyed.at(key).role==role) && (++checks, true));}
    EXPECT_TRUE((keyed.at("check").checked&&keyed.at("radio").selected) && (++checks, true));
    EXPECT_TRUE((keyed.at("slider").actions==std::vector<std::string>{"adjust"}) && (++checks, true));
    EXPECT_TRUE((keyed.at("text").value=="visible"&&keyed.at("text").focused) && (++checks, true));EXPECT_TRUE((keyed.at("secret").value=="***") && (++checks, true));
    for(const auto *role:{"progressbar","text","image","menubar","row","cell"})EXPECT_TRUE((roles[role]>0) && (++checks, true));
    const std::string encoded=ui.SemanticJson();auto parsed=nlohmann::json::parse(encoded);
    EXPECT_TRUE((parsed["role"]=="application"&&parsed["children"].size()==1) && (++checks, true));EXPECT_TRUE((encoded.find("must-never-appear")==std::string::npos) && (++checks, true));
    EXPECT_TRUE((encoded.find("\\n")!=std::string::npos&&encoded.find("\\r")!=std::string::npos&&encoded.find("\\t")!=std::string::npos) && (++checks, true));
    ui.ClearFocus();EXPECT_TRUE((ui.FocusedTextInput().empty()) && (++checks, true));ui.NewFrame();ui.RequestFocus("text");EXPECT_TRUE((ui.FocusedTextInput()=="text") && (++checks, true));ui.ClearFocus();
    EXPECT_TRUE((ui.SemanticNodes().empty()&&!ui.IsDragging()&&!ui.IsDropHovered("target")) && (++checks, true));
}
static void ci_ui_geometry_and_pixels(Renderer &renderer){
    Camera2D camera(128,128);Input input;UISystem ui;ui.Bind(&input,&camera,&renderer);
    auto begin=[&](){input.InjectFrame({-1000,-1000},{},{});ui.NewFrame();renderer.BeginFrame(0,0,0,1);};
    begin();ui.RootBegin(0,0,UISize::Percent(.5f),UISize::Percent(.25f),0,false);ui.RememberBox("percent-root");ui.RootEnd();
    EXPECT_TRUE((ui.NamedRect("percent-root").width==64&&ui.NamedRect("percent-root").height==32) && (++checks, true));
    ui.RootBegin(0,0,UISize::Grow(),UISize::Grow(),0,false);ui.RememberBox("grow-root");ui.RootEnd();
    EXPECT_TRUE((ui.NamedRect("grow-root").width==128&&ui.NamedRect("grow-root").height==128) && (++checks, true));
    renderer.EndFrame();
    auto target=[&](const Rect &r){ui.RootBegin(r.x,r.y,UISize::Fixed(r.width),UISize::Fixed(r.height),0,false);ui.RememberBox("anchor");ui.RootEnd();};
    auto anchored=[&](const char *placement,float x,float y,float width=10){
        ui.AnchorToBegin("anchor",placement,2,UISize::Fixed(width),UISize::Fixed(8));ui.RememberBox("popup");ui.AnchorToEnd();
        auto r=ui.NamedRect("popup");EXPECT_TRUE((near_value(r.x,x)&&near_value(r.y,y)&&r.width==width&&r.height==8) && (++checks, true));
    };
    begin();target({4,4,20,10});anchored("top_start",4,16);anchored("top_center",9,16);anchored("top_end",14,16);anchored("left_start",26,4);anchored("right_start",26,4);renderer.EndFrame();
    begin();target({110,50,10,10});anchored("right_start",88,50,20);renderer.EndFrame();
    begin();target({50,116,10,10});anchored("bottom_start",50,106,20);renderer.EndFrame();
    begin();ui.AnchorToBegin("absent","bottom_start",2,UISize::Fixed(10),UISize::Fixed(8));ui.RememberBox("absent-popup");ui.AnchorToEnd();
    EXPECT_TRUE((ui.NamedRect("absent-popup").x==64&&ui.NamedRect("absent-popup").y==66) && (++checks, true));renderer.EndFrame();
    begin();ui.RootBegin(4,4,UISize::Fixed(20),UISize::Fixed(10),0,false);ui.NextSize(UISize::Fixed(20),UISize::Fixed(10));ui.Button("previous","Previous");ui.RootEnd();renderer.EndFrame();
    begin();ui.AnchorToBegin("previous","bottom_start",2,UISize::Fixed(10),UISize::Fixed(8));ui.RememberBox("prior-popup");ui.AnchorToEnd();
    EXPECT_TRUE((ui.NamedRect("prior-popup").x==4&&ui.NamedRect("prior-popup").y==16) && (++checks, true));renderer.EndFrame();
    const int rt=renderer.GetRenderTargets().Create(128,128);EXPECT_TRUE((rt>0) && (++checks, true));begin();EXPECT_TRUE((renderer.BeginRenderTarget(rt)) && (++checks, true));renderer.ClearActiveTarget(Color::Black());
    ui.RootBegin(0,0,UISize::Fixed(32),UISize::Fixed(16),0,false);ui.NextGradient(Color::Red(),Color::Green(),true);ui.NextBorder(Color::Blue(),1);ui.NextRadius(0);ui.CanvasBegin(UISize::Grow(),UISize::Grow());ui.CanvasEnd();ui.RootEnd();
    ui.RootBegin(40,0,UISize::Fixed(16),UISize::Fixed(32),0,false);ui.NextGradient(Color::Red(),Color::Blue(),false);ui.CanvasBegin(UISize::Grow(),UISize::Grow());ui.CanvasEnd();ui.RootEnd();
    ui.RootBegin(80,0,UISize::Fixed(16),UISize::Fixed(16),0,false);ui.NextVisible(false);ui.NextBg(Color::Green());ui.CanvasBegin(UISize::Grow(),UISize::Grow());ui.CanvasEnd();ui.RootEnd();
    ui.RootBegin(0,40,UISize::Fixed(100),UISize::Fixed(40),0,false);ui.NextDirection(UILayoutDirection::RTL);ui.HStackBegin(0,0);
    ui.NextSize(UISize::Fixed(10),UISize::Fixed(10));ui.NextEnabled(false);ui.NextTooltip("Disabled hint");ui.Button("disabled","Disabled");
    ui.NextSize(UISize::Fixed(10),UISize::Fixed(10));ui.Button("enabled","Enabled");ui.StackEnd();ui.RootEnd();
    EXPECT_TRUE((ui.WidgetRect("disabled").x>ui.WidgetRect("enabled").x) && (++checks, true));
    bool disabled=false;std::function<void(const UISemanticNode&)> find=[&](const UISemanticNode &n){if(n.key=="disabled"){EXPECT_TRUE((!n.enabled&&n.hint=="Disabled hint") && (++checks, true));disabled=true;}for(const auto &c:n.children)find(c);};find(ui.SemanticTree());EXPECT_TRUE((disabled) && (++checks, true));
    renderer.EndRenderTarget();renderer.EndFrame();Color c;auto *surface=renderer.GetRenderTargets().Get(rt);
    EXPECT_TRUE((surface->ReadPixel(16,8,c)&&c.r>.3f&&c.r<.7f&&c.g>.3f&&c.g<.7f&&c.b<.05f) && (++checks, true));
    EXPECT_TRUE((surface->ReadPixel(0,8,c)&&c.b>.95f&&c.r<.05f) && (++checks, true));
    EXPECT_TRUE((surface->ReadPixel(48,16,c)&&c.r>.3f&&c.r<.7f&&c.b>.3f&&c.b<.7f&&c.g<.05f) && (++checks, true));
    EXPECT_TRUE((surface->ReadPixel(88,8,c)&&c.r<.01f&&c.g<.01f&&c.b<.01f) && (++checks, true));
    renderer.GetRenderTargets().Destroy(rt);EXPECT_TRUE((!renderer.GetRenderTargets().Get(rt)) && (++checks, true));
}
static void ci_ui_inspector(Renderer &renderer){
    Camera2D camera(640,480);Input input;UISystem ui;ui.Bind(&input,&camera,&renderer);ui.SetAudit(true);ui.SetInspector(true);
    const int target=renderer.GetRenderTargets().Create(640,480);EXPECT_TRUE((target>0) && (++checks, true));
    auto frame=[&](Vec2 mouse,bool down){
        input.InjectFrame(mouse,{},down?std::vector<std::int64_t>{0}:std::vector<std::int64_t>{});ui.NewFrame();
        renderer.BeginFrame(0,0,0,1);EXPECT_TRUE((renderer.BeginRenderTarget(target)) && (++checks, true));renderer.ClearActiveTarget(Color::Black());
        ui.RootBegin(400,320,UISize::Fixed(120),UISize::Fixed(100),0,false);
        ui.NextMargin(4);ui.NextPad(2,2,2,2);ui.NextSize(UISize::Fixed(80),UISize::Fixed(40));ui.Button("inspect","Inspect");ui.RootEnd();
        renderer.EndRenderTarget();renderer.EndFrame();
    };
    frame({430,350},true);EXPECT_TRUE((ui.IsInspectorEnabled()) && (++checks, true));auto *surface=renderer.GetRenderTargets().Get(target);Color c;
    EXPECT_TRUE((surface->ReadPixel(22,20,c)&&c.b>.15f) && (++checks, true)); // actual Inspector header paint
    EXPECT_TRUE((surface->ReadPixel(402,330,c)&&c.r>.08f) && (++checks, true)); // actual highlighted margin outside widget
    frame({-1000,-1000},false);const float before=ui.ThemeMetric("corner_radius");
    // With a selected box, first metric row starts at y=112. Exercise the real
    // hit-tested theme stepper rather than calling SetThemeMetric in the test.
    frame({337,120},true);EXPECT_TRUE((ui.ThemeMetric("corner_radius")==before+2) && (++checks, true));
    frame({-1000,-1000},false);frame({120,212},true); // issue panel toggle
    frame({-1000,-1000},false);EXPECT_TRUE((surface->ReadPixel(20,300,c)&&c.b>.05f) && (++checks, true));
    frame({260,212},true); // semantic panel toggle
    frame({-1000,-1000},false);EXPECT_TRUE((surface->ReadPixel(20,400,c)&&c.b>.05f) && (++checks, true));
    // Drag the header and validate its new physical location and cleared old one.
    frame({40,24},true);frame({80,64},true);frame({-1000,-1000},false);
    EXPECT_TRUE((surface->ReadPixel(62,60,c)&&c.b>.15f) && (++checks, true));
    EXPECT_TRUE((surface->ReadPixel(22,20,c)&&c.r<.01f&&c.g<.01f&&c.b<.01f) && (++checks, true));
    ui.SetInspector(false);frame({-1000,-1000},false);
    EXPECT_TRUE((surface->ReadPixel(62,60,c)&&c.r<.01f&&c.g<.01f&&c.b<.01f) && (++checks, true));
    renderer.GetRenderTargets().Destroy(target);EXPECT_TRUE((!renderer.GetRenderTargets().Get(target)) && (++checks, true));
}
static void ci_ui_drag_lifecycle(Renderer &renderer){
    for(const auto &accept:std::vector<std::string>{"*","other,item","other",""}){
        Camera2D camera(128,128);Input input;UISystem ui;ui.Bind(&input,&camera,&renderer);
        auto frame=[&](float x,bool down,bool escape=false){
            input.InjectFrame({x,10},escape?std::vector<std::string_view>{"escape"}:std::vector<std::string_view>{},down?std::vector<std::int64_t>{0}:std::vector<std::int64_t>{});
            ui.NewFrame();renderer.BeginFrame(0,0,0,1);ui.BeginFrameUI();ui.RootBegin(0,0,UISize::Fixed(100),UISize::Fixed(30),0,false);ui.HStackBegin(10,0);
            ui.NextSize(UISize::Fixed(40),UISize::Fixed(20));ui.DragSourceBegin("source","item","payload-42");ui.Text("Source");ui.DragSourceEnd();
            ui.NextSize(UISize::Fixed(40),UISize::Fixed(20));ui.DropTargetBegin("target",accept);ui.Text("Target");ui.DropTargetEnd();
            ui.StackEnd();ui.RootEnd();EXPECT_TRUE((ui.EndFrameUI()) && (++checks, true));renderer.EndFrame();
        };
        frame(10,false);frame(10,true);EXPECT_TRUE((!ui.IsDragging()) && (++checks, true));frame(30,true);
        EXPECT_TRUE((ui.IsDragging()&&ui.ActiveDragType()=="item"&&ui.ActiveDragPayload()=="payload-42"&&ui.ActiveDragKey()=="source") && (++checks, true));
        frame(65,true);const bool accepts=accept!="other";EXPECT_TRUE((ui.IsDropHovered("target")==accepts) && (++checks, true));
        frame(65,false);EXPECT_TRUE((!ui.IsDragging()) && (++checks, true));
        EXPECT_TRUE((ui.DroppedPayload("target")== (accepts?"payload-42":"")) && (++checks, true));EXPECT_TRUE((ui.DroppedType("target")== (accepts?"item":"")) && (++checks, true));
        frame(10,false);EXPECT_TRUE((ui.DroppedPayload("target").empty()) && (++checks, true));
        frame(10,true);frame(30,true);EXPECT_TRUE((ui.IsDragging()) && (++checks, true));frame(65,true,true);
        EXPECT_TRUE((!ui.IsDragging()&&ui.ActiveDragType().empty()&&ui.ActiveDragPayload().empty()&&!ui.IsDropHovered("target")) && (++checks, true));
        frame(65,false);EXPECT_TRUE((ui.DroppedPayload("target").empty()) && (++checks, true));
        frame(10,true);frame(10,false);EXPECT_TRUE((!ui.IsDragging()&&ui.DroppedType("target").empty()) && (++checks, true));
    }
}
static void ci_ui_menu_lifecycle(Renderer &renderer){
    Camera2D camera(128,128);Input input;UISystem ui;ui.Bind(&input,&camera,&renderer);bool action=false;
    auto frame=[&](Vec2 mouse,bool down){
        input.InjectFrame(mouse,{},down?std::vector<std::int64_t>{0}:std::vector<std::int64_t>{});ui.NewFrame();renderer.BeginFrame(0,0,0,1);
        ui.RootBegin(0,0,UISize::Fixed(128),UISize::Fixed(100),0,false);ui.MenuBarBegin();const bool opened=ui.MenuBegin("file","File");
        action=false;if(opened){action=ui.MenuItem("action","Action");ui.MenuEnd();}ui.MenuBarEnd();ui.RootEnd();renderer.EndFrame();return opened;
    };
    EXPECT_TRUE((!frame({10,5},false)) && (++checks, true));EXPECT_TRUE((!frame({10,5},true)) && (++checks, true));EXPECT_TRUE((frame({10,5},false)) && (++checks, true));
    EXPECT_TRUE((ui.WidgetRect("action").width>0) && (++checks, true));EXPECT_TRUE((frame({10,5},true)) && (++checks, true));EXPECT_TRUE((!frame({10,5},false)) && (++checks, true));
    EXPECT_TRUE((!frame({10,5},true)) && (++checks, true));EXPECT_TRUE((frame({10,5},false)) && (++checks, true));auto r=ui.WidgetRect("action");Vec2 mid{r.x+r.width/2,r.y+r.height/2};
    EXPECT_TRUE((frame(mid,true)) && (++checks, true));EXPECT_TRUE((frame(mid,false)) && (++checks, true));EXPECT_TRUE((action) && (++checks, true));EXPECT_TRUE((!frame({-1000,-1000},false)) && (++checks, true));
    // An unbalanced recorded frame is rejected until the actual containers close.
    ui.NewFrame();renderer.BeginFrame(0,0,0,1);ui.TableColLimits(0,1,2);EXPECT_TRUE((ui.SemanticNodes().empty()) && (++checks, true));ui.BeginFrameUI();
    ui.RootBegin(0,0,UISize::Fixed(100),UISize::Fixed(40),0,false);ui.RememberBox("balanced");ui.HStackBegin();ui.CanvasBegin(UISize::Fixed(10),UISize::Fixed(10));
    EXPECT_TRUE((!ui.EndFrameUI()) && (++checks, true));ui.CanvasEnd();ui.StackEnd();ui.RootEnd();EXPECT_TRUE((ui.EndFrameUI()) && (++checks, true));
    EXPECT_TRUE((!ui.InAutoLayout()&&ui.NamedRect("balanced").width==100) && (++checks, true));ui.StackEnd();EXPECT_TRUE((!ui.InAutoLayout()) && (++checks, true));renderer.EndFrame();
}
static void ci_ui_flex_boundaries(Renderer &renderer){
    Camera2D camera(128,128);Input input;UISystem ui;ui.Bind(&input,&camera,&renderer);input.InjectFrame({-1000,-1000},{},{});ui.NewFrame();renderer.BeginFrame(0,0,0,1);
    for(bool horizontal:{false,true}){
        ui.RootBegin(0,0,UISize::Fixed(100),UISize::Fixed(100),0,false);
        ui.NextSize(UISize::Grow(),UISize::Grow());if(horizontal)ui.HStackBegin(0,0);else ui.VStackBegin(0,0);
        ui.CanvasBegin(UISize::Percent(.25f),UISize::Percent(.25f));ui.RememberBox("quarter");ui.CanvasEnd();
        ui.CanvasBegin(UISize::Grow(),UISize::Grow());ui.RememberBox("remaining");ui.CanvasEnd();ui.StackEnd();ui.RootEnd();
        auto q=ui.NamedRect("quarter"),rest=ui.NamedRect("remaining");EXPECT_TRUE((q.width==25&&q.height==25) && (++checks, true));EXPECT_TRUE((horizontal?rest.width==75:rest.height==75) && (++checks, true));
        ui.RootBegin(0,0,UISize::Fixed(100),UISize::Fixed(100),0,false);ui.NextSize(UISize::Grow(),UISize::Grow());if(horizontal)ui.HStackBegin(0,0);else ui.VStackBegin(0,0);
        for(int i=0;i<2;++i){ui.NextShrink(1);ui.CanvasBegin(UISize::Fixed(80),UISize::Fixed(80));ui.RememberBox(i==0?"shrink-a":"shrink-b");ui.CanvasEnd();}ui.StackEnd();ui.RootEnd();
        auto a=ui.NamedRect("shrink-a"),b=ui.NamedRect("shrink-b");EXPECT_TRUE((horizontal?(a.width==50&&b.width==50):(a.height==50&&b.height==50)) && (++checks, true));
    }
    ui.RootBegin(0,0,UISize::Fixed(100),UISize::Fixed(40),0,false);ui.NextDirection(UILayoutDirection::RTL);ui.TableBegin("percent-table",{UISize::Percent(.25f),UISize::Grow()},0,0);ui.TableRowBegin();
    ui.TableCellBegin();ui.RememberBox("pct-cell");ui.Text("A");ui.TableCellEnd();ui.TableCellBegin();ui.RememberBox("rest-cell");ui.Text("B");ui.TableCellEnd();ui.TableRowEnd();ui.TableEnd();ui.RootEnd();
    EXPECT_TRUE((ui.NamedRect("pct-cell").width==25&&ui.NamedRect("pct-cell").x==75&&ui.NamedRect("rest-cell").width==75) && (++checks, true));
    for(bool fixed:{false,true}){
        ui.RootBegin(0,0,UISize::Fixed(100),UISize::Fixed(100),0,false);ui.NextWidth(fixed?UISize::Fixed(40):UISize::Auto());const float side=fixed?1.0f:200.0f;ui.Image(&Texture2D::White(),{0,0,side,side});ui.Remember("intrinsic-image");ui.RootEnd();
        auto r=ui.NamedRect("intrinsic-image");EXPECT_TRUE((r.width==(fixed?40:100)&&r.height==r.width) && (++checks, true));
    }
    renderer.EndFrame();
}
static void ci_ui_scroll_visibility(Renderer &renderer){
    Camera2D camera(128,128);Input input;UISystem ui;ui.Bind(&input,&camera,&renderer);
    auto frame=[&](){
        input.InjectFrame({-1000,-1000},{},{});ui.NewFrame();renderer.BeginFrame(0,0,0,1);ui.RootBegin(0,0,UISize::Fixed(100),UISize::Fixed(100),0,false);
        ui.ScrollBoxBegin("reveal",UISize::Fixed(40),UISize::Fixed(40),UIScrollDir::Both);ui.CanvasBegin(UISize::Fixed(200),UISize::Fixed(200));
        ui.NextSize(UISize::Fixed(10),UISize::Fixed(10));ui.Button("near","Near");
        ui.NextMargin(100,100,0,0);ui.NextSize(UISize::Fixed(10),UISize::Fixed(10));ui.Button("far","Far");ui.CanvasEnd();ui.ScrollBoxEnd();ui.RootEnd();renderer.EndFrame();
    };
    frame();EXPECT_TRUE((ui.ScrollOffset("reveal")==0&&ui.ScrollOffsetX("reveal")==0) && (++checks, true));ui.ScrollToWidget("",true);
    ui.ScrollToWidget("far",true);EXPECT_TRUE((ui.ScrollOffset("reveal")==0&&ui.ScrollOffsetX("reveal")==0) && (++checks, true));
    frame();EXPECT_TRUE((ui.ScrollOffset("reveal")>0&&ui.ScrollOffset("reveal")<200&&ui.ScrollOffsetX("reveal")>0&&ui.ScrollOffsetX("reveal")<200) && (++checks, true));
    ui.ScrollToWidget("near",false);EXPECT_TRUE((ui.ScrollOffset("reveal")==0&&ui.ScrollOffsetX("reveal")==0) && (++checks, true));frame();
    EXPECT_TRUE((ui.WidgetRect("near").x==0&&ui.WidgetRect("near").y==0) && (++checks, true));
    auto virtual_frame=[&](){
        input.InjectFrame({-1000,-1000},{},{});ui.NewFrame();renderer.BeginFrame(0,0,0,1);ui.RootBegin(0,0,UISize::Fixed(100),UISize::Fixed(100),0,false);ui.ScrollBoxBegin("virtual-reveal",UISize::Fixed(40));
        auto range=ui.VirtualBegin("virtual-reveal",100,10,1);for(int i=0;i<range.count;++i)ui.Spacer(10);ui.VirtualEnd();ui.ScrollBoxEnd();ui.RootEnd();renderer.EndFrame();return range;
    };
    EXPECT_TRUE((virtual_frame().first==0) && (++checks, true));ui.SetScrollOffset("virtual-reveal",500);auto r=virtual_frame();EXPECT_TRUE((r.first==49&&r.count>0&&r.count<10) && (++checks, true));EXPECT_TRUE((ui.ScrollContentHeight("virtual-reveal")==1000) && (++checks, true));
}
static void log_crash_contracts(Fixtures &f){
#if !defined(_WIN32)
    const int signals[]={SIGSEGV,SIGABRT,SIGFPE,SIGILL,SIGBUS};
    const char *reasons[]={"SIGSEGV","SIGABRT","SIGFPE","SIGILL","SIGBUS","std::terminate","deliberate exception","std::terminate"};
    for(int scenario=0;scenario<9;++scenario){
        auto dir=f.path("child-log-"+std::to_string(scenario));auto pid=fork();EXPECT_TRUE((pid>=0) && (++checks, true));
        if(pid==0){
            rlimit limit{0,0};setrlimit(RLIMIT_CORE,&limit);setenv("FAKE2D_NO_DIALOG","1",1);
            log::Config cfg;cfg.dir=dir;cfg.app_name="CrashContract";cfg.version="test-v1";cfg.echo_stderr=false;cfg.terminate_handler=true;
            cfg.crash_dialog_title="Expected test crash";cfg.crash_dialog_body_prefix="prefix";cfg.crash_dialog_body_suffix="suffix";
            if(scenario==8){cfg.disable_crash_env="FAKE2D_DEEP_DISABLE_CRASH";cfg.no_dialog_env="FAKE2D_DEEP_NO_DIALOG";setenv(cfg.disable_crash_env.c_str(),"1",1);std::signal(SIGABRT,SIG_DFL);}
            if(!log::Init(cfg))std::_Exit(91);
            if(scenario<5)kill(getpid(),signals[scenario]);
            if(scenario==5)std::terminate();
            if(scenario==6){try{throw std::runtime_error("deliberate exception");}catch(...){std::terminate();}}
            if(scenario==7){try{throw 42;}catch(...){std::terminate();}}
            if(scenario==8){
                if(std::signal(SIGABRT,SIG_DFL)!=SIG_DFL)std::_Exit(92);
                log::Warn("warning contract");log::ErrorDialog("suppressed","generic");unsetenv("FAKE2D_NO_DIALOG");setenv(cfg.no_dialog_env.c_str(),"1",1);log::ErrorDialog("suppressed","custom");log::Shutdown();std::exit(0);
            }
            std::_Exit(93);
        }
        int status=0;EXPECT_TRUE((waitpid(pid,&status,0)==pid) && (++checks, true));
        if(scenario<5)EXPECT_TRUE((WIFSIGNALED(status)&&WTERMSIG(status)==signals[scenario]) && (++checks, true));
        else EXPECT_TRUE((WIFEXITED(status)&&WEXITSTATUS(status)==(scenario==8?0:3)) && (++checks, true));
        std::ifstream in(std::filesystem::path(dir)/"game.log");std::string logtext{std::istreambuf_iterator<char>(in),{}};
        if(scenario==8){EXPECT_TRUE((logtext.find("[warn] warning contract")!=std::string::npos&&logtext.find("suppressed: custom")!=std::string::npos) && (++checks, true));continue;}
        EXPECT_TRUE((logtext.find(reasons[scenario])!=std::string::npos) && (++checks, true));
        int reports=0;for(const auto &entry:std::filesystem::directory_iterator(dir))if(entry.path().filename().string().starts_with("crash-")){
            ++reports;std::ifstream crash(entry.path());std::string text{std::istreambuf_iterator<char>(crash),{}};
            EXPECT_TRUE((text.find("CrashContract test-v1 crash report")!=std::string::npos&&text.find(reasons[scenario])!=std::string::npos&&text.find("backtrace:")!=std::string::npos&&text.find("game.log")!=std::string::npos) && (++checks, true));
        }EXPECT_TRUE((reports==1) && (++checks, true));
    }
#else
    (void)f; // POSIX fatal-signal protocol is tested only on the POSIX platform.
#endif
}
static void province_map_contracts(Fixtures &f){
    ProvinceMap map;EXPECT_TRUE((!map.Load(f.path("absent-provinces.ppm"),3)) && (++checks, true));
    std::string ppm="P6\n3 3\n255\n";for(int id:{1,1,0,2,2,0,2,3,9}){ppm+=char(id);ppm+=char(0);ppm+=char(0);}
    auto path=f.write("provinces.ppm",ppm);EXPECT_TRUE((map.Load(path,3)) && (++checks, true));
    EXPECT_TRUE((map.width==3&&map.height==3&&map.ids.size()==9&&map.pixels==std::vector<int>({2,3,1})) && (++checks, true));
    EXPECT_TRUE((near_value(map.centroid_x[0],.5f)&&map.centroid_y[0]==0&&near_value(map.centroid_x[1],1.f/3)&&near_value(map.centroid_y[1],4.f/3)) && (++checks, true));
    EXPECT_TRUE((map.neighbors[0]==std::vector<int>({1})&&map.neighbors[1]==std::vector<int>({0,2})&&map.neighbors[2]==std::vector<int>({1})) && (++checks, true));
    float x=-9,y=-9;EXPECT_TRUE((map.CoastAnchor(0,0,&x,&y)&&x==10&&y==0) && (++checks, true));
    EXPECT_TRUE((!map.Load(f.path("absent-provinces.ppm"),3)&&map.width==3&&map.ids.back()==9) && (++checks, true));
    f.write("inland.ppm",std::string("P6\n1 1\n255\n")+char(1)+char(0)+char(0));
    EXPECT_TRUE((map.Load(f.path("inland.ppm"),2)&&map.pixels==std::vector<int>({1,0})&&map.centroid_x[1]==0&&map.neighbors[0].empty()) && (++checks, true));
    x=-9;y=-8;EXPECT_TRUE((!map.CoastAnchor(0,0,&x,&y)&&x==-9&&y==-8) && (++checks, true));
    EXPECT_TRUE((map.Load(path,0)&&map.pixels.empty()&&map.neighbors.empty()&&map.ids.size()==9) && (++checks, true));
    for(int which=0;which<4;++which){int w=which==1?1:2,h=which==0?1:2;auto ids=which<2?std::vector<int>{1,2}:which==2?std::vector<int>{0,0,1,2}:std::vector<int>{0,1,0,2};
        std::string edge="P6\n"+std::to_string(w)+" "+std::to_string(h)+"\n255\n";for(int id:ids){edge+=char(id);edge+=char(0);edge+=char(0);}
        EXPECT_TRUE((map.Load(f.write("province-edge-"+std::to_string(which)+".ppm",edge),2)&&map.neighbors==std::vector<std::vector<int>>({{1},{0}})) && (++checks, true));
    }
}
static void i18n_view_contracts(Fixtures &f){
    I18n tr;EXPECT_TRUE((tr.TranslateSegmented("城")=="城"&&tr.TranslateSegmentedView("城")=="城") && (++checks, true));
    std::filesystem::create_directory(f.path("packs"));
    auto file=f.write("packs/xx.tsv","# name: Test   \r\n城\tcity\r\n城市\tmetro\r\nesc\t\\t\\r\\\\\\q\r\nbroken\r\n");
    EXPECT_TRUE((tr.LoadDirectory(f.path("packs"),"absent")&&tr.Language()=="xx"&&tr.LanguageCount()==1) && (++checks, true));
    EXPECT_TRUE((tr.Translate("esc")==std::string("\t\r\\\\q")&&tr.GetLanguage(0)->name=="Test") && (++checks, true));
    EXPECT_TRUE((tr.LoadFile(f.path("absent.tsv"),"en")==0&&!tr.SetLanguage("absent")) && (++checks, true));
    EXPECT_TRUE((tr.TranslateSegmented("城市城")=="metro city") && (++checks, true));
    EXPECT_TRUE((tr.TranslateSegmentedView("城市城")=="metro city"&&tr.TranslateSegmentedView("城市城")=="metro city") && (++checks, true));
    EXPECT_TRUE((tr.TranslateSegmentedView("城")=="city"&&tr.TranslateSegmentedView("ascii")=="ascii") && (++checks, true));
    EXPECT_TRUE((tr.TranslateSegmented("é😀城")=="é😀city") && (++checks, true));
    std::string malformed=std::string(1,char(0xff))+"城";EXPECT_TRUE((tr.TranslateSegmented(malformed)==std::string(1,char(0xff))+"city") && (++checks, true));
    for(const auto &item:std::vector<std::pair<std::string,std::string>>{{"：",":"},{"；",";"},{"（"," ("},{"）",")"},{"「","\""},{"」","\""},{"『","\""},{"』","\""},{"【","["},{"】","]"},{"　",""},{"？","?"}})EXPECT_TRUE((tr.TranslateSegmented("城"+item.first)=="city"+item.second) && (++checks, true));
    EXPECT_TRUE((tr.LoadFile(f.write("packs/empty-ja.tsv","# name: Empty\n"),"ja")==0&&tr.SetLanguage("ja")) && (++checks, true));
    EXPECT_TRUE((tr.TranslateSegmentedView("城")=="城") && (++checks, true));tr.Clear();EXPECT_TRUE((tr.LanguageCount()==0&&tr.Language().empty()&&tr.MissingCount()==0&&tr.RejectedCount()==0&&tr.TranslateSegmentedView("城")=="城") && (++checks, true));
    EXPECT_TRUE((tr.LoadFile(file,"en")==3&&tr.SetLanguage("en")) && (++checks, true));
    auto stable=tr.TranslateSegmentedView("城0");EXPECT_TRUE((stable=="city0") && (++checks, true));
    for(int i=1;i<60000;++i){auto key="城"+std::to_string(i);if(tr.TranslateSegmentedView(key)!="city"+std::to_string(i))throw std::runtime_error("segmented memo value");}
    std::string capped="城60000";auto fallback=tr.TranslateSegmentedView(capped);EXPECT_TRUE((fallback==capped&&fallback.data()==capped.data()&&stable=="city0") && (++checks, true));
    EXPECT_TRUE((tr.TranslateSegmented("城1")=="city1") && (++checks, true));
}
static void resource_atlas_contracts(Fixtures &f){
    ResourceManager resources;const auto &constant=resources;
    auto atlas=f.write("resource-atlas.json",R"({"meta":{"image":"resource-sheet.ppm"},"frames":{"a":{"frame":{"x":0,"y":0,"w":2,"h":2}}}})");
    f.write("resource-sheet.ppm",std::string("P6\n2 2\n255\n")+std::string(12,char(255)));
    EXPECT_TRUE((resources.LoadAtlas(f.path("absent-atlas.json"))==kInvalidAtlasHandle) && (++checks, true));
    EXPECT_TRUE((resources.LoadAtlas(f.write("invalid-atlas.json","{}"))==kInvalidAtlasHandle) && (++checks, true));
    EXPECT_TRUE((resources.LoadAtlas(f.write("missing-sheet.json",R"({"meta":{"image":"absent.ppm"},"frames":{"a":{"frame":{"w":2,"h":2}}}})"))==kInvalidAtlasHandle) && (++checks, true));
    auto a=resources.LoadAtlas(atlas);EXPECT_TRUE((a!=kInvalidAtlasHandle&&constant.GetAtlas(a)&&constant.GetAtlas(a)->GetRegion("a")) && (++checks, true));
    auto sheet=constant.AtlasTexture(a);EXPECT_TRUE((sheet!=kInvalidTextureHandle&&constant.GetTexture(sheet)->Width()==2) && (++checks, true));
    auto again=resources.LoadAtlas(atlas);EXPECT_TRUE((again==a) && (++checks, true));resources.Retain(a);resources.Release(a);resources.Release(again);EXPECT_TRUE((constant.GetAtlas(a)) && (++checks, true));
    resources.Retain(sheet);resources.Release(a);EXPECT_TRUE((!constant.GetAtlas(a)&&constant.AtlasTexture(a)==kInvalidTextureHandle&&constant.GetTexture(sheet)) && (++checks, true));
    resources.Release(sheet);EXPECT_TRUE((!constant.GetTexture(sheet)) && (++checks, true));resources.Release(a);resources.Retain(a);EXPECT_TRUE((!constant.GetAtlas(a)) && (++checks, true));
    std::array<std::uint8_t,4> pixel={20,40,60,255};auto t=resources.CreateTexture("memory",1,1,pixel.data());auto same=resources.CreateTexture("memory",1,1,pixel.data());
    EXPECT_TRUE((t!=kInvalidTextureHandle&&same==t) && (++checks, true));resources.Release(t);EXPECT_TRUE((resources.GetTexture(same)) && (++checks, true));resources.Release(same);EXPECT_TRUE((!resources.GetTexture(same)) && (++checks, true));
    resources.SetStrictLoads(true);EXPECT_TRUE((resources.StrictLoads()&&resources.LoadTexture(f.path("absent-texture.ppm"))==kInvalidTextureHandle&&resources.FailedLoads()==1) && (++checks, true));
}

static void storage_tests(Fixtures &f){
    SaveStore db;const std::string dir=f.dir.string();
    EXPECT_TRUE((!SaveStore::ValidSlotName("")&&!SaveStore::ValidSlotName(std::string(33,'a'))) && (++checks, true));
    for(const char *bad:{"../escape","a/b","a.b","白","a b"})EXPECT_TRUE((!db.LoadSlot(bad,dir)&&!db.SaveSlot(bad,dir)) && (++checks, true));
    db.SetNumber("number",7.5);db.SetString("text","你好");
    EXPECT_TRUE((db.SaveSlot("kv",dir)) && (++checks, true));db.Reset();EXPECT_TRUE((db.LoadSlot("kv",dir)) && (++checks, true));
    EXPECT_TRUE((db.GetNumber("number",-1)==7.5&&db.GetString("text","")=="你好"&&db.Size()==2) && (++checks, true));
    EXPECT_TRUE((db.GetNumber("text",-1)==-1&&db.GetString("number","fallback")=="fallback") && (++checks, true));
    EXPECT_TRUE((db.GetString("absent","fallback")=="fallback") && (++checks, true));db.Erase("number");EXPECT_TRUE((!db.Has("number")) && (++checks, true));
    for(const char *bad:{"{","{}","[]","{\"values\":[]}"}){
        f.write("broken.json",bad);EXPECT_TRUE((!db.LoadSlot("broken",dir)) && (++checks, true));EXPECT_TRUE((db.GetString("text","")=="你好") && (++checks, true));
    }
    f.write("mixed.json",R"({"values":{"n":{"n":12},"s":{"s":"yes"},"plain_n":3.25,"plain_s":"old","junk":{},"null":null,"bool":true}})");
    EXPECT_TRUE((db.LoadSlot("mixed",dir)&&db.Size()==4) && (++checks, true));EXPECT_TRUE((db.GetNumber("n",0)==12&&db.GetNumber("plain_n",0)==3.25) && (++checks, true));
    EXPECT_TRUE((db.GetString("s","")=="yes"&&db.GetString("plain_s","")=="old") && (++checks, true));
    EXPECT_TRUE((db.LoadSlot("missing",dir)&&db.Size()==0) && (++checks, true));
    f.write("not-a-dir","file");EXPECT_TRUE((!db.SaveSlot("x",f.path("not-a-dir"))) && (++checks, true));
    std::filesystem::create_directory(f.path("blocked.json"));EXPECT_TRUE((!db.SaveSlot("blocked",dir)) && (++checks, true));EXPECT_TRUE((!std::filesystem::exists(f.path("blocked.tmp"))) && (++checks, true));
    EXPECT_TRUE((SaveStore::Crc32("123456789")==0xcbf43926u&&SaveStore::Crc32("")==0) && (++checks, true));
    EXPECT_TRUE((SaveStore::ParseHeaderValue("a=1\r\nignored\nempty=\na=2\nlast=z","a")=="1") && (++checks, true));
    EXPECT_TRUE((SaveStore::ParseHeaderValue("a=1\r\nlast=z","last")=="z"&&SaveStore::ParseHeaderValue("x=3","missing").empty()) && (++checks, true));
    std::string error,payload;SaveStore::ContainerInfo ci;const char magic[4]={'T','E','S','T'};
    EXPECT_TRUE((SaveStore::SaveContainer(f.path("container"),magic,2,"k=v",std::string("a\0b",3),&error)) && (++checks, true));
    EXPECT_TRUE((SaveStore::LoadContainer(f.path("container"),magic,2,payload,ci,&error)) && (++checks, true));
    EXPECT_TRUE((!ci.legacy&&ci.container_ver==2&&ci.header=="k=v"&&ci.payload_bytes==3&&payload==std::string("a\0b",3)) && (++checks, true));
    EXPECT_TRUE((!SaveStore::SaveContainer(f.path("x"),nullptr,1,"","",&error)&&error=="invalid header") && (++checks, true));
    EXPECT_TRUE((!SaveStore::SaveContainer(f.path("x"),magic,1,std::string(16*1024*1024+1,'x'),"",&error)) && (++checks, true));
    EXPECT_TRUE((!SaveStore::LoadContainer(f.path("absent"),magic,2,payload,ci,&error)&&error=="unreadable"&&payload.empty()) && (++checks, true));
    f.write("empty","");EXPECT_TRUE((!SaveStore::LoadContainer(f.path("empty"),magic,2,payload,ci,&error)&&error=="empty file") && (++checks, true));
    auto bytes=f.read("container");bytes[4]=3;f.write("container-new",bytes);
    EXPECT_TRUE((!SaveStore::LoadContainer(f.path("container-new"),magic,2,payload,ci,&error)&&error=="newer save format") && (++checks, true));
    bytes=f.read("container");bytes[8]=char(127);f.write("container-short",bytes);
    EXPECT_TRUE((!SaveStore::LoadContainer(f.path("container-short"),magic,2,payload,ci,&error)&&error=="truncated") && (++checks, true));
    bytes=f.read("container");bytes.back()='X';f.write("container-crc",bytes);
    EXPECT_TRUE((!SaveStore::LoadContainer(f.path("container-crc"),magic,2,payload,ci,&error)&&error.find("checksum")!=std::string::npos) && (++checks, true));
    f.write("legacy","legacy-data");EXPECT_TRUE((SaveStore::LoadContainer(f.path("legacy"),magic,2,payload,ci,&error)&&ci.legacy&&payload=="legacy-data") && (++checks, true));
    EXPECT_TRUE((SaveStore::LoadContainer(f.path("container"),nullptr,0,payload,ci)&&ci.legacy&&payload==f.read("container")) && (++checks, true));
    EXPECT_TRUE((!SaveStore::AtomicWriteFile(f.path("not-a-dir/x"),"bad",&error)&&error=="open temp failed") && (++checks, true));
    std::filesystem::create_directory(f.path("replace-dir"));
    EXPECT_TRUE((SaveStore::AtomicWriteFile(f.path("replace-dir"),"replacement",&error)&&f.read("replace-dir")=="replacement") && (++checks, true));
    std::filesystem::create_directory(f.path("keep-dir"));f.write("keep-dir/marker","keep");
    EXPECT_TRUE((!SaveStore::AtomicWriteFile(f.path("keep-dir"),"bad",&error)) && (++checks, true));
    EXPECT_TRUE((f.read("keep-dir/marker")=="keep"&&!std::filesystem::exists(f.path("keep-dir.tmp"))) && (++checks, true));
    EXPECT_TRUE((db.SaveBlob("blob",dir,"first","h=1",9)) && (++checks, true));EXPECT_TRUE((db.SaveBlob("blob",dir,"second","h=2",10)) && (++checks, true));
    f.write("blob.sav","F2SV"+std::string(16,'\0'));SaveStore::BlobInfo bi;
    EXPECT_TRUE((db.LoadBlob("blob",dir,payload,bi)&&bi.restored_from_backup&&payload=="first"&&bi.game_version==9) && (++checks, true));
    EXPECT_TRUE((db.ReadBlobInfo("blob",dir,bi)&&bi.restored_from_backup) && (++checks, true));
    EXPECT_TRUE((!db.LoadBlob("../bad",dir,payload,bi)&&!db.ReadBlobInfo("../bad",dir,bi)) && (++checks, true));
    EXPECT_TRUE((!db.SaveBlob("../bad",dir,"x")&&!db.SaveBlob("x",f.path("not-a-dir"),"x")) && (++checks, true));
    EXPECT_TRUE((db.SaveBlob("oversized",dir,"x",std::string(1024*1024+1,'x'))==false) && (++checks, true));
    f.write("future.sav",std::string("F2SV")+std::string(16,'\0'));EXPECT_TRUE((!db.LoadBlob("future",dir,payload,bi)) && (++checks, true));
    f.write("bare.sav","old-body");EXPECT_TRUE((db.LoadBlob("bare",dir,payload,bi)&&payload=="old-body"&&bi.game_version==0) && (++checks, true));
    EXPECT_TRUE((!SaveStore::RenameSlot("../bad","ok",dir)&&!SaveStore::RenameSlot("kv","../bad",dir)) && (++checks, true));
    EXPECT_TRUE((SaveStore::RenameSlot("kv","renamed",dir)&&!std::filesystem::exists(f.path("kv.json"))) && (++checks, true));
    EXPECT_TRUE((!SaveStore::RenameSlot("mixed","renamed",dir)) && (++checks, true));
    f.write("unrelated.txt","x");f.write("invalid.name.json","x");
    auto slots=SaveStore::ListSlots(dir);EXPECT_TRUE((std::is_sorted(slots.begin(),slots.end())&&std::find(slots.begin(),slots.end(),"renamed")!=slots.end()) && (++checks, true));
    EXPECT_TRUE((std::find(slots.begin(),slots.end(),"invalid.name")==slots.end()&&SaveStore::ListSlots(f.path("absent-dir")).empty()) && (++checks, true));
    EXPECT_TRUE((SaveStore::DeleteSlot("renamed",dir)&&!std::filesystem::exists(f.path("renamed.json"))) && (++checks, true));
    EXPECT_TRUE((!SaveStore::DeleteSlot("../bad",dir)) && (++checks, true));
}
static void shader_tests(){
#ifdef _WIN32
    auto get_uniform=reinterpret_cast<void(APIENTRY*)(GLuint,GLint,GLfloat*)>(glfwGetProcAddress("glGetUniformfv"));
    auto get_attribute=reinterpret_cast<GLint(APIENTRY*)(GLuint,const GLchar*)>(glfwGetProcAddress("glGetAttribLocation"));
    EXPECT_TRUE((get_uniform&&get_attribute) && (++checks, true));
#else
    auto get_uniform=&glGetUniformfv;auto get_attribute=&glGetAttribLocation;
#endif
    const std::string vert="#version 330 core\nlayout (location=0) in vec2 a_pos;\nout vec2 uv;\nvoid main(){uv=a_pos;gl_Position=vec4(a_pos,0,1);}";
    const std::string frag="#version 330 core\nin vec2 uv;\nout vec4 color;\nuniform vec4 tint;\nvoid main(){color=tint+vec4(uv,0,0)*0.001;}";
    Shader sh;EXPECT_TRUE((!sh.IsValid()) && (++checks, true));EXPECT_TRUE((!sh.LoadFromSource("bad shader",frag)&&!sh.IsValid()) && (++checks, true));
    EXPECT_TRUE((!sh.LoadFromSource(vert,"bad shader")&&!sh.IsValid()) && (++checks, true));
    EXPECT_TRUE((!sh.LoadFromSource(vert,"#version 330 core\nin vec3 uv;\nout vec4 color;\nvoid main(){color=vec4(uv,1);}")&&!sh.IsValid()) && (++checks, true));
    // Apple's software GL (macOS CGL headless) leaves a sticky glGetError()
    // after expected compile/link failures. Drain it before the success path
    // so the closing GL_NO_ERROR check reflects later operations only.
    while(glGetError()!=GL_NO_ERROR){}
    EXPECT_TRUE((sh.LoadFromSource(vert,frag)) && (++checks, true));auto original=sh.Id();Shader moved(std::move(sh));EXPECT_TRUE((!sh.IsValid()&&moved.Id()==original) && (++checks, true));
    Shader assigned;assigned=std::move(moved);EXPECT_TRUE((assigned.Id()==original&&!moved.IsValid()) && (++checks, true));assigned=std::move(assigned);EXPECT_TRUE((assigned.Id()==original) && (++checks, true));
    assigned.Bind();assigned.SetVec4("tint",{.25f,.5f,.75f,1});float v[4]={};
    auto loc=glGetUniformLocation(assigned.Id(),"tint");EXPECT_TRUE((loc>=0) && (++checks, true));get_uniform(assigned.Id(),loc,v);EXPECT_TRUE((v[0]==.25f&&v[1]==.5f&&v[2]==.75f&&v[3]==1) && (++checks, true));
    assigned.SetFloat("missing",1);assigned.SetFloat("missing",2);assigned.Unbind();assigned.Destroy();assigned.Destroy();EXPECT_TRUE((!assigned.IsValid()) && (++checks, true));
    struct LegacyScope{bool old=glcaps::legacy_mode;LegacyScope(){glcaps::legacy_mode=true;}~LegacyScope(){glcaps::legacy_mode=old;}};
    {
        LegacyScope legacy;
        const std::string lv="#version 330 core\n  layout (location=0) in vec2 a_pos;\n smooth out vec2 v_uv;\nuniform mat4 matrix;\nvoid main(){v_uv=a_pos;gl_Position=matrix*vec4(a_pos,0,1);}";
        const std::string lf="#version 330 core\nsmooth in vec2 v_uv;\n  out vec4 output_color;\nlayout(location=1) uniform vec4 tint;\nuniform sampler2D image;\nvoid main(){float pretexture=0.0;output_color=texture (image,v_uv)*tint+vec4(pretexture);}";
        EXPECT_TRUE((sh.LoadFromSource(lv,lf)) && (++checks, true));EXPECT_TRUE((get_attribute(sh.Id(),"a_pos")==0) && (++checks, true));
        sh.Bind();sh.SetInt("image",0);sh.SetVec4("tint",{.2f,.4f,.6f,.8f});sh.SetMat4("matrix",Mat4::Identity());
        get_uniform(sh.Id(),glGetUniformLocation(sh.Id(),"tint"),v);EXPECT_TRUE((near_value(v[2],.6f)) && (++checks, true));sh.Unbind();
        EXPECT_TRUE((sh.LoadFromSource("#version 120\nattribute vec2 a_pos;\nvoid main(){gl_Position=vec4(a_pos,0,1);}","void main(){gl_FragColor=vec4(1);}")) && (++checks, true));
    }
    const GLenum shader_err=glGetError();
    if(shader_err!=GL_NO_ERROR)std::fprintf(stderr,"coverage deep shader_tests glGetError()=0x%x\n",shader_err);
    EXPECT_TRUE((shader_err==GL_NO_ERROR) && (++checks, true));
}
static void input_tests(platform::Window &window){
    Input in;in.Attach(nullptr);in.SetClipboard("你好 clipboard");EXPECT_TRUE((in.Clipboard()=="你好 clipboard") && (++checks, true));
    const std::vector<std::string_view> keys={"a","z","0","9","space","return","tab","insert","delete","home","end","caps_lock","page_up","page_down","left_shift","right_shift","left_control","right_control","left_alt","right_alt","left_super","right_super","menu","up","down","left","right","-","=","[","]","\\",";","'","`",",",".","/","f1","f12"};
    in.InjectFrame({10,20},keys,{0,2,7,-1,100},3,{65,0x4f60});
    for(auto key:keys)EXPECT_TRUE((in.KeyDown(key)&&in.KeyPressed(key)) && (++checks, true));
    for(auto key:{"unknown","F1","f0","f13","fzz","f999",""})EXPECT_TRUE((!in.KeyDown(key)&&!in.KeyPressed(key)&&!in.KeyReleased(key)) && (++checks, true));
    EXPECT_TRUE((in.MouseDown(7)&&!in.MouseDown(-1)&&!in.MouseDown(8)&&in.HasTextInput()&&in.TextCodepoints().size()==2) && (++checks, true));
    in.BindAction("","a");in.BindAction("go","");in.BindAction("go","a");in.BindAction("go","a");
    EXPECT_TRUE((in.ActionDown("go")&&in.ActionPressed("go")&&!in.ActionDown("missing")&&!in.ActionPressed("missing")&&!in.ActionReleased("missing")) && (++checks, true));
    in.InjectFrame({12,15},{},{},0,{});EXPECT_TRUE((in.ActionReleased("go")&&in.MouseReleased(7)&&in.MouseDelta().x==2&&in.MouseDelta().y==-5) && (++checks, true));
    in.ClearActions("go");EXPECT_TRUE((!in.ActionReleased("go")) && (++checks, true));in.BindAction("other","z");in.InjectFrame({}, {"a"},{ });EXPECT_TRUE((!in.ActionDown("other")&&!in.ActionPressed("other")&&!in.ActionReleased("other")) && (++checks, true));
    in.ClearActions();in.AiHoldKey("left",true);in.AiHoldButton(1,true);in.AiHoldMouse({8,9},true);in.NewFrame(nullptr);
    EXPECT_TRUE((in.KeyDown("left")&&in.MouseDown(1)&&in.MousePosition().x==8) && (++checks, true));in.AiClear();in.NewFrame(nullptr);EXPECT_TRUE((!in.KeyDown("left")&&!in.MouseDown(1)&&in.KeyReleased("left")&&in.MouseReleased(1)) && (++checks, true));
    in.NewFrame(nullptr);EXPECT_TRUE((!in.KeyReleased("left")&&!in.MouseReleased(1)) && (++checks, true));
    in.InjectFrame({}, {"left"},{1});in.AiHoldKey("left",true);in.AiHoldButton(1,true);in.NewFrame(nullptr);
    in.AiClear();in.NewFrame(nullptr);EXPECT_TRUE((in.KeyDown("left")&&in.MouseDown(1)) && (++checks, true)); // physical base survives
    in.InjectFrame({}, {},{});in.NewFrame(nullptr);EXPECT_TRUE((!in.KeyDown("left")&&!in.MouseDown(1)) && (++checks, true));
    in.AiHoldKey("right",true);in.NewFrame(nullptr);in.AiHoldKey("right",false);in.NewFrame(nullptr);EXPECT_TRUE((in.KeyReleased("right")) && (++checks, true));
    InputFrameSnapshot snap;snap.pad_connected[0]=1;for(int b=0;b<Input::kSnapshotPadButtonCount;++b)snap.SetPadButton(0,b,true);
    snap.pad_axes[0][0]=.1f;snap.pad_axes[0][1]=.1f;snap.pad_axes[0][2]=.6f;snap.pad_axes[0][3]=.8f;snap.pad_axes[0][4]=2;snap.pad_axes[0][5]=-2;
    in.ApplySnapshot(snap);
    for(auto name:{"a","b","x","y","left_bumper","right_bumper","back","start","guide","left_thumb","right_thumb","dpad_up","dpad_right","dpad_down","dpad_left"})EXPECT_TRUE((in.GamepadButtonDown(0,name)&&in.GamepadButtonPressed(0,name)) && (++checks, true));
    EXPECT_TRUE((in.GamepadAxis(0,"left_x")==0&&in.GamepadAxis(0,"left_y")==0) && (++checks, true));
    EXPECT_TRUE((near_value(in.GamepadAxis(0,"right_x"),.6f)&&near_value(in.GamepadAxis(0,"right_y"),.8f)) && (++checks, true));
    EXPECT_TRUE((in.GamepadAxis(0,"left_trigger")==1&&in.GamepadAxis(0,"right_trigger")==0&&in.GamepadAxis(0,"unknown")==0) && (++checks, true));
    EXPECT_TRUE((!in.GamepadConnected(-1)&&!in.GamepadConnected(16)&&!in.GamepadButtonDown(0,"unknown")&&in.GamepadName(0).empty()) && (++checks, true));
    for(int b=0;b<Input::kSnapshotPadButtonCount;++b)snap.SetPadButton(0,b,false);
    snap.pad_axes[0][0]=.6f;snap.pad_axes[0][1]=.8f;in.ApplySnapshot(snap);
    EXPECT_TRUE((in.GamepadButtonReleased(0,"a")&&near_value(in.GamepadAxis(0,"left_x"),.6f)&&near_value(in.GamepadAxis(0,"left_y"),.8f)) && (++checks, true));
    EXPECT_TRUE((!in.GamepadButtonReleased(-1,"a")&&!in.GamepadButtonReleased(0,"unknown")) && (++checks, true));
    snap.pad_connected[0]=0;in.ApplySnapshot(snap);EXPECT_TRUE((!in.GamepadConnected(0)&&in.GamepadAxis(0,"right_x")==0) && (++checks, true));
    if(auto *win=window.Handle()){
        in.Attach(win);auto scroll=glfwSetScrollCallback(win,nullptr);auto text=glfwSetCharCallback(win,nullptr);auto mouse=glfwSetMouseButtonCallback(win,nullptr);auto key=glfwSetKeyCallback(win,nullptr);
        EXPECT_TRUE((scroll&&text&&mouse&&key) && (++checks, true));glfwSetScrollCallback(win,scroll);glfwSetCharCallback(win,text);glfwSetMouseButtonCallback(win,mouse);glfwSetKeyCallback(win,key);
        glfwShowWindow(win);scroll(win,0,2.5);text(win,0x4f60);mouse(win,0,GLFW_PRESS,0);mouse(win,-1,GLFW_PRESS,0);key(win,GLFW_KEY_F1,0,GLFW_PRESS,0);key(win,-1,0,GLFW_PRESS,0);
        in.NewFrame(win);EXPECT_TRUE((in.MouseWheel()==2.5&&in.TextCodepoints()==std::vector<unsigned int>{0x4f60}) && (++checks, true));EXPECT_TRUE((in.KeyPressed("f1")&&in.MousePressed(0)) && (++checks, true));
        in.NewFrame(win);EXPECT_TRUE((in.KeyReleased("f1")&&in.MouseReleased(0)&&in.MouseWheel()==0&&!in.HasTextInput()) && (++checks, true));
        in.SetClipboard("native clipboard");EXPECT_TRUE((in.Clipboard()=="native clipboard") && (++checks, true));
        glfwHideWindow(win);in.Attach(nullptr);
    }else{
        EXPECT_TRUE((window.GetRenderMode()==platform::RenderMode::CGLHeadlessFBO) && (++checks, true));
    }
}
static void font_deep(Fixtures &f,Renderer &renderer){
    Font font;const auto primary=renderer.FontFacePath(0);const auto corrupt=f.write("invalid.ttf",std::string(64,'\0'));
    EXPECT_TRUE((!font.LoadFromTTF(f.path("absent.ttf"),16)&&!font.LoadFromTTF(corrupt,16)) && (++checks, true));
    EXPECT_TRUE((!font.AddFallback(corrupt,16)&&font.FacePath(0).empty()) && (++checks, true));
    if(!primary.empty()){
        EXPECT_TRUE((font.LoadFromTTF(primary,16)&&font.HasVectorFont()&&font.HasGlyph(U'A')) && (++checks, true));
        const float width=font.MeasureText("AV");EXPECT_TRUE((width>0&&font.MeasureText("AV",false)>=width) && (++checks, true));
        EXPECT_TRUE((!font.HasGlyph(char32_t(0x10ffff))) && (++checks, true)); // negative result cached before adding a face
        const auto copy=f.path("fallback.ttf");std::filesystem::copy_file(primary,copy);
        EXPECT_TRUE((font.AddFallback(copy,16)&&font.AddFallback(copy,16)&&font.AddFallback(primary,16)&&font.AddFallback(corrupt,16)) && (++checks, true));
        EXPECT_TRUE((font.FaceCount()==1&&!font.HasGlyph(char32_t(0x10fffe))&&font.FaceCount()==2&&font.FacePath(1)==copy) && (++checks, true));
        EXPECT_TRUE((!font.LoadFromTTF(corrupt,16)&&font.HasGlyph(U'A')&&near_value(font.MeasureText("AV"),width)) && (++checks, true));
        font.ClearFallbacks();EXPECT_TRUE((font.FaceCount()==1&&font.FacePath(1).empty()&&font.HasGlyph(U'A')) && (++checks, true));
    }else EXPECT_TRUE((font.LoadFallback()&&font.HasGlyph(U'A')&&!font.HasGlyph(U'你')) && (++checks, true));
    font.Reset();EXPECT_TRUE((!font.HasVectorFont()&&font.FaceCount()==0&&font.LineHeight()==0) && (++checks, true));font.ClearFallbacks();
}
static void tilemap_deep(Fixtures &f,const nlohmann::json &map,Renderer &renderer,ResourceManager &resources){
    using nlohmann::json;
    Tilemap empty;EXPECT_TRUE((!empty.SolidAtPixel(0,0)&&!empty.SolidAtPixel(-1,-1)) && (++checks, true));
    for(const std::string bad:{"{","[]","{}",R"({"orientation":"isometric"})",R"({"orientation":"orthogonal","width":0})",R"({"orientation":"orthogonal","width":1,"height":1,"tilewidth":8,"tileheight":8})"}){Tilemap rejected;EXPECT_TRUE((!rejected.LoadFromString(bad,f.dir.string(),resources)) && (++checks, true));}
    EXPECT_TRUE((!empty.LoadFromFile(f.path("missing-map"),resources)) && (++checks, true));
    Tilemap loaded;EXPECT_TRUE((loaded.LoadFromString(map.dump(),f.dir.string(),resources)) && (++checks, true));
    EXPECT_TRUE((loaded.SolidAtPixel(1,1)&&!loaded.SolidAtPixel(-1,1)&&!loaded.SolidAtPixel(100,100)) && (++checks, true));
    EXPECT_TRUE((loaded.GroundYAt(4,4)==0&&loaded.GroundYAt(-1,0)==-1&&loaded.GroundYAt(100,0)==-1) && (++checks, true)); // object layers are data, not gid arrays
    auto properties=map;properties["layers"][1]["objects"][0]["properties"]=json::array({42,json::object(),{{"name","flag"},{"value",true}},{{"name","n"},{"value",19.5}},{{"name","i"},{"value",3}},{{"name","bad"},{"value",nullptr}}});
    Tilemap inferred;EXPECT_TRUE((inferred.LoadFromString(properties.dump(),f.dir.string(),resources)) && (++checks, true));const auto *objects=inferred.ObjectsAt(1);EXPECT_TRUE((objects&&objects->size()==1&&objects->front().PropBool("flag")&&objects->front().PropNum("n")==19.5&&objects->front().PropNum("i")==3&&!objects->front().HasProperty("bad")) && (++checks, true));
    auto tile_only=map;tile_only["layers"].erase(2);tile_only["layers"][0]["data"][0]=0;Tilemap parallax;EXPECT_TRUE((parallax.LoadFromString(tile_only.dump(),f.dir.string(),resources)) && (++checks, true));
    auto draw=[&](Tilemap &subject,bool scaled,int red_x,int red_y,int black_x){
        int target=renderer.GetRenderTargets().Create(128,128);EXPECT_TRUE((target>0) && (++checks, true));renderer.BeginFrame(0,0,0,1);EXPECT_TRUE((renderer.BeginRenderTarget(target)) && (++checks, true));renderer.ClearActiveTarget({0,0,0,1});
        if(scaled)subject.DrawScaled(renderer,1);else subject.Draw(renderer);renderer.EndRenderTarget();renderer.EndFrame();Color pixel;auto *rt=renderer.GetRenderTargets().Get(target);
        if(!rt->ReadPixel(red_x,red_y,pixel)||pixel.r<.95f||pixel.g>.05f)std::fprintf(stderr,"map red pixel %d,%d = %.3f %.3f %.3f\n",red_x,red_y,pixel.r,pixel.g,pixel.b);
        EXPECT_TRUE((rt->ReadPixel(red_x,red_y,pixel)&&pixel.r>.95f&&pixel.g<.05f) && (++checks, true));if(black_x>=0)EXPECT_TRUE((rt->ReadPixel(black_x,red_y,pixel)&&pixel.r<.05f) && (++checks, true));renderer.GetRenderTargets().Destroy(target);
    };
    draw(parallax,false,10,2,20);draw(parallax,true,18,2,10);draw(loaded,false,100,100,-1);
    PhysicsWorld colliders;auto bodies=loaded.CreateStaticColliders(colliders);
    EXPECT_TRUE((bodies.size()==1&&colliders.BodyCount()==1) && (++checks, true));
    auto *body=colliders.Get(bodies.front());EXPECT_TRUE((body&&body->type==BodyType::Static&&body->position.x==4&&body->position.y==4&&body->half_extents.x==4) && (++checks, true));
    colliders.DestroyBody(bodies.front());EXPECT_TRUE((colliders.BodyCount()==0) && (++checks, true));
    for(const std::string dir:{"up","down"})for(int rise:{0,16}){
        auto doc=map;doc["tilesets"][0]["tiles"][0]["properties"]=json::array({{{"name","slope"},{"type","string"},{"value",dir}},{{"name","slope_rise"},{"type","int"},{"value",rise}}});
        Tilemap slope;EXPECT_TRUE((slope.LoadFromString(doc.dump(),f.dir.string(),resources)) && (++checks, true));
        EXPECT_TRUE((slope.SlopeDirAt(0,0)==(dir=="up"?1:2)&&slope.SlopeRiseAt(0,0)==(rise?rise:8)) && (++checks, true));
        const float expected=dir=="up"?(rise?12:6):(rise?4:2);
        EXPECT_TRUE((near_value(slope.GroundYAt(2,8),expected)) && (++checks, true));
        EXPECT_TRUE((slope.SlopeDirAt(-1,0)==0&&slope.SlopeRiseAt(-1,0)==0&&slope.SlopeDirAt(0,2)==0) && (++checks, true));
        doc["layers"][0]["visible"]=false;Tilemap hidden;EXPECT_TRUE((hidden.LoadFromString(doc.dump(),f.dir.string(),resources)&&hidden.SlopeDirAt(0,0)==0&&hidden.SlopeRiseAt(0,0)==0) && (++checks, true));
    }
    TilemapLibrary library;const auto path=f.write("library-contract.json",map.dump());std::vector<int> ids;
    const auto &view=library;EXPECT_TRUE((view.Get(0)==nullptr&&view.Get(99)==nullptr) && (++checks, true));
    for(std::size_t i=0;i<TilemapLibrary::kMaxMaps;++i){int id=library.Load(path,resources);EXPECT_TRUE((id>0&&view.Get(id)&&view.Get(id)->IsValid()) && (++checks, true));ids.push_back(id);}
    EXPECT_TRUE((library.Load(path,resources)==0) && (++checks, true));library.Destroy(ids.front());EXPECT_TRUE((view.Get(ids.front())==nullptr) && (++checks, true));
    EXPECT_TRUE((library.Load(path,resources)>0) && (++checks, true));library.Clear();EXPECT_TRUE((view.Get(ids.back())==nullptr) && (++checks, true));

}
static void skeleton_deep(Fixtures &f,Renderer &renderer){
    using nlohmann::json;ResourceManager resources;SkeletonSystem sk;int sequence=0;
    f.write("white.ppm",std::string("P6\n2 2\n255\n")+std::string(12,char(255)));
    auto rig=json::parse(R"({"skeleton":{"texture":"white.ppm"},"bones":[{"name":"root"},{"name":"tip","parent":"root","x":10}],"regions":{"pixel":{"x":0,"y":0,"w":2,"h":2}},"attachments":[{"name":"art","region":"pixel","w":20,"h":20}],"slots":[{"name":"body","bone":"root","attachment":"art"}],"animations":{"fade":{"loop":false,"duration":1,"slots":{"body":{"color":[{"time":0,"color":"ffFFff80"},{"time":1,"color":"ffffff40"}]}},"events":[{"time":0.2,"name":"early","data":2},{"time":0.8,"name":"late","data":8}]}}})");
    auto load=[&](const json &doc){std::string error;int asset=sk.LoadAsset(resources,f.write("deep-rig-"+std::to_string(sequence++)+".json",doc.dump()),&error);if(asset==0)std::fprintf(stderr,"rig load: %s\n",error.c_str());EXPECT_TRUE((asset>0) && (++checks, true));return asset;};
    auto reject=[&](const json &doc,const std::string &expected){std::string error;int asset=sk.LoadAsset(resources,f.write("bad-deep-rig-"+std::to_string(sequence++)+".json",doc.dump()),&error);if(asset||error.find(expected)==std::string::npos)std::fprintf(stderr,"expected rejection %s, got %s\n",expected.c_str(),error.c_str());EXPECT_TRUE((asset==0&&error.find(expected)!=std::string::npos) && (++checks, true));};
    auto mesh=rig;mesh["attachments"][0]=json::parse(R"({"name":"art","type":"mesh","uvs":[0,0,1,0,0,1],"triangles":[0,1,2],"vertices":[0,0,10,0,0,10]})");
    auto bad=mesh;bad["attachments"][0]["uvs"]=json::array({0,0,1});bad["attachments"][0]["vertices"]=json::array({1,0,0,0,1});reject(bad,"even non-empty");
    bad=mesh;bad["attachments"][0]["uvs"]=std::vector<int>((SkeletonSystem::kMaxMeshVertices+1)*2,0);bad["attachments"][0]["vertices"]=bad["attachments"][0]["uvs"];reject(bad,"vertices");
    bad=mesh;bad["attachments"][0]["triangles"]=std::vector<int>(SkeletonSystem::kMaxMeshIndices+3,0);reject(bad,"triangle indices");
    bad=rig;bad["animations"]["fade"]["bones"]={{"root",42}};reject(bad,"bad block");
    bad=mesh;bad["animations"]["fade"]["deform"]={{"body",{{"art",{{"absolute",true},{"keys",42}}}}}};reject(bad,"bad deform track");
    bad=rig;bad["animations"]=json::object();for(int i=0;i<=SkeletonSystem::kMaxClips;++i)bad["animations"]["a"+std::to_string(i)]=json::object();reject(bad,"animations");
    bad=rig;bad["slots"][0]["attachments"]=json::array();for(int i=0;i<=SkeletonSystem::kMaxAttachmentsPerSlot;++i){auto att=rig["attachments"][0];att["name"]="attachment"+std::to_string(i);bad["attachments"].push_back(att);bad["slots"][0]["attachments"].push_back({{"name",att["name"]}});}reject(bad,"too many attachments");
    bad=rig;bad["attachments"][0]={{"name","link"},{"type","linkedmesh"},{"parent","missing"}};bad["slots"][0]["attachment"]="link";reject(bad,"unknown mesh");
    bad=rig;bad["skins"]={{"bad",{{"body","absent"}}}};reject(bad,"no attachment named");
    bad=rig;bad["skins"]={{"bad",{{"body",{{"art",{{"type","mesh"},{"uvs",json::array({0})}}}}}}}};reject(bad,"skin 'bad' slot 'body': mesh attachment 'art': 'vertices' must be a non-empty array");
    bad=rig;bad["skins"]=json::array({{{"name","array-skin"},{"attachments",{{"body","art"}}}}});int array_skin=sk.Create(load(bad));EXPECT_TRUE((sk.SetSkin(array_skin,"array-skin")==0&&sk.SlotAttachmentCount(array_skin,0)==1&&sk.AttachmentName(array_skin,0)=="art") && (++checks, true));
    bad=rig;bad["skins"]={{"nested",{{"body",{{"embedded",{{"region","pixel"},{"w",8},{"h",8}}}}}}}};bad["attachments"].push_back({{"name","copy"},{"type","linkedmesh"},{"parent","missing"},{"skin","nested"}});reject(bad,"in skin 'nested'");
    bad=mesh;bad["attachments"].push_back({{"name","copy"},{"type","linkedmesh"},{"parent","art"},{"skin","absent"}});bad["slots"][0]["attachments"]=json::array({{{"name","copy"}}});int linked=sk.Create(load(bad));EXPECT_TRUE((sk.SetSlotAttachment(linked,0,"copy")) && (++checks, true));Vec2 linked_vertex;EXPECT_TRUE((sk.AttachmentIsMesh(linked,1)&&sk.MeshVertexWorld(linked,1,1,linked_vertex)&&linked_vertex.x==10) && (++checks, true));
    int asset=load(rig),id=sk.Create(asset);EXPECT_TRUE((id>0&&sk.Play(id,"fade",0)) && (++checks, true));sk.SetTime(id,.5f);
    int target=renderer.GetRenderTargets().Create(128,128);EXPECT_TRUE((target>0) && (++checks, true));renderer.BeginFrame(0,0,0,1);EXPECT_TRUE((renderer.BeginRenderTarget(target)) && (++checks, true));renderer.ClearActiveTarget({0,0,0,1});sk.Draw(renderer,id,{40,40},1,1,Color::White());renderer.EndRenderTarget();renderer.EndFrame();
    Color pixel;EXPECT_TRUE((renderer.GetRenderTargets().Get(target)->ReadPixel(40,40,pixel)&&pixel.r>.35f&&pixel.r<.4f&&near_value(pixel.r,pixel.g)) && (++checks, true));renderer.GetRenderTargets().Destroy(target);
    sk.SetTime(id,1);sk.SetSpeed(id,-1);sk.Update(.5f);EXPECT_TRUE((near_value(sk.Time(id),.5f)&&sk.EventCount(id)==1&&sk.EventName(id,0)=="late"&&sk.EventData(id,0)==8) && (++checks, true));
    sk.Update(.5f);EXPECT_TRUE((sk.Time(id)==0&&!sk.IsPlaying(id)&&sk.Finished(id)&&!sk.Finished(id)) && (++checks, true));
    bad=rig;bad["animations"]["fade"]["loop"]=true;int loop=sk.Create(load(bad));EXPECT_TRUE((sk.Play(loop,"fade",0)) && (++checks, true));sk.SetTime(loop,.1f);sk.SetSpeed(loop,-1);sk.Update(.2f);EXPECT_TRUE((near_value(sk.Time(loop),.9f)&&sk.EventCount(loop)==0) && (++checks, true));
    sk.SetTime(loop,.9f);sk.Update(.8f);EXPECT_TRUE((near_value(sk.Time(loop),.1f)&&sk.EventCount(loop)==2) && (++checks, true));
    bad=rig;bad["slots"][0]["color"]="gg000000";bad["bones"][1]["x"]="not numeric";int defaults=sk.Create(load(bad));EXPECT_TRUE((sk.BonePosition(defaults,1).x==0) && (++checks, true));
    for(bool weighted:{false,true}){
        auto absolute=mesh;if(weighted)absolute["attachments"][0]["vertices"]=json::array({1,0,0,0,1,1,1,0,0,1,1,0,0,10,1});
        absolute["animations"]={{"absolute",{{"duration",1},{"deform",{{"body",{{"art",{{"absolute",true},{"keys",json::array({{{"time",0},{"vertices",json::array({2,4,10,0,0,10})}}})}}}}}}}}}};
        int inst=sk.Create(load(absolute));auto initial=sk.MeshBindVertex(inst,0,0);EXPECT_TRUE((initial.x==0&&initial.y==0) && (++checks, true));EXPECT_TRUE((sk.Play(inst,"absolute",0)) && (++checks, true));sk.SetTime(inst,0);Vec2 point;EXPECT_TRUE((sk.MeshVertexWorld(inst,0,0,point)&&near_value(point.x,2)&&near_value(point.y,4)) && (++checks, true));
        auto bind=sk.MeshBindVertex(inst,0,0);EXPECT_TRUE((near_value(bind.x,2)&&near_value(bind.y,4)) && (++checks, true));EXPECT_TRUE((sk.SampleDeformAt(inst,0,0)) && (++checks, true));EXPECT_TRUE((sk.MeshVertexWorld(inst,0,0,point)&&near_value(point.x,2)) && (++checks, true));
        sk.SetPoseWeight(inst,.5f);EXPECT_TRUE((sk.MeshVertexWorld(inst,0,0,point)&&near_value(point.x,1)&&near_value(point.y,2)) && (++checks, true));
    }
    sk.Clear();EXPECT_TRUE((sk.InstanceCount()==0) && (++checks, true));
}
static void ui_input_rich_edges(Renderer &renderer){
    Camera2D camera(128,128);Input input;UISystem rich;rich.Bind(&input,&camera,&renderer);
    rich.SetRichIcon("mark",&Texture2D::White(),{0,0,1,1});
    auto icon_frame=[&](bool down){input.InjectFrame({4,4},{},down?std::vector<std::int64_t>{0}:std::vector<std::int64_t>{});rich.NewFrame();renderer.BeginFrame(0,0,0,1);rich.RootBegin(0,0,UISize::Fixed(60),UISize::Fixed(40));auto link=rich.RichText("[link=go][img=mark][/link]",1);rich.RootEnd();renderer.EndFrame();return link;};
    EXPECT_TRUE((icon_frame(true).empty()) && (++checks, true));EXPECT_TRUE((icon_frame(false)=="go") && (++checks, true));EXPECT_TRUE((icon_frame(false).empty()) && (++checks, true));
    std::string many;for(unsigned cp=0x1000;cp<0x1000+4100;++cp){many+=char(0xe0|(cp>>12));many+=char(0x80|((cp>>6)&63));many+=char(0x80|(cp&63));}
    input.InjectFrame({-100,-100},{},{});rich.NewFrame();renderer.BeginFrame(0,0,0,1);rich.RootBegin(0,0,UISize::Fixed(60),UISize::Fixed(100));
    auto many_link=rich.RichText(many,1);
    rich.RememberBox("rich-many");
    rich.RootEnd(); // Two-Pass measurement publishes glyph counters here.
    EXPECT_TRUE((many_link.empty()&&rich.Stats().rich_glyphs==4100) && (++checks, true));
    rich.RootBegin(0,0,UISize::Fixed(60),UISize::Fixed(100));
    auto extra_link=rich.RichText("A",1);rich.RootEnd();
    EXPECT_TRUE((extra_link.empty()&&rich.Stats().rich_glyphs==4101) && (++checks, true));EXPECT_TRUE((rich.NamedRect("rich-many").height>0) && (++checks, true));renderer.EndFrame();
    for(bool deferred:{false,true}){
        Input pad_input;UISystem pad;pad.Bind(&pad_input,&camera,&renderer);float value=.5f;
        auto frame=[&](float axis,bool tab=false){InputFrameSnapshot snap;snap.mouse_x=-100;snap.mouse_y=-100;snap.pad_connected[0]=1;snap.pad_axes[0][0]=axis;snap.SetKey(GLFW_KEY_TAB,tab);pad_input.ApplySnapshot(snap);pad.NewFrame();renderer.BeginFrame(0,0,0,1);
            if(deferred)EXPECT_TRUE((pad.BeginFrameUI()) && (++checks, true));pad.RootBegin(0,0,UISize::Fixed(100),UISize::Fixed(30));pad.NextSize(UISize::Grow(),UISize::Fixed(20));value=pad.Slider("pad-slider",value,0,1);pad.RootEnd();if(deferred){EXPECT_TRUE((pad.EndFrameUI()) && (++checks, true));value=pad.ResultNumber("pad-slider",-1);}renderer.EndFrame();return value;};
        EXPECT_TRUE((frame(0)==.5f) && (++checks, true));EXPECT_TRUE((frame(0,true)==.5f&&pad.FocusedWidget()=="pad-slider") && (++checks, true));
        const float right=frame(1);EXPECT_TRUE((right>.5f&&right<=.6f) && (++checks, true));EXPECT_TRUE((frame(1)==right) && (++checks, true));EXPECT_TRUE((frame(0)==right) && (++checks, true));EXPECT_TRUE((near_value(frame(-1),.5f)) && (++checks, true));
    }
}

static void ui_tests(Renderer &renderer){
    UISystem ui;Input input;Camera2D camera(128,128);
    EXPECT_TRUE((!ui.TextArea("none")&&!ui.TextPassword("none")&&!ui.Undo("none")&&!ui.Redo("none")) && (++checks, true));
    EXPECT_TRUE((ui.TextValue("none").empty()&&ui.Selection("none",true)==0&&ui.VariableListNext()==-1&&ui.VariableListRowEnd()==0) && (++checks, true));
    EXPECT_TRUE((ui.VariableListHeight("none","id")==0&&ui.VariableListTotal("none")==0&&!ui.VariableListMeasured("none","id")) && (++checks, true));
    EXPECT_TRUE((!ui.SetVariableRowText("none","id","x")&&!ui.VariableListBegin("none",20)&&!ui.ListView("none",UISize::Fixed(10))&&!ui.ListViewBegin("none")&&!ui.ListViewEnd()) && (++checks, true));
    EXPECT_TRUE((!ui.TextInput("none")&&!ui.Button("none","x")&&ui.RichText("none").empty()) && (++checks, true));
    ui.Bind(&input,&camera,&renderer);
    auto edit=[&](const std::vector<std::string_view>&keys=std::vector<std::string_view>{},const std::vector<unsigned int>&text=std::vector<unsigned int>{},const std::vector<std::int64_t>&buttons=std::vector<std::int64_t>{},std::size_t maximum=0){
        input.InjectFrame({8,8},keys,buttons,0,text);ui.NewFrame();renderer.BeginFrame(0,0,0,1);ui.RootBegin(0,0,UISize::Fixed(120),UISize::Fixed(100));
        ui.NextHeight(UISize::Fixed(70));
        bool changed=ui.TextArea("edit",maximum);ui.RootEnd();renderer.EndFrame();return changed;
    };
    auto press=[&](std::vector<std::string_view> keys){edit();return edit(keys);};
    edit({}, {},{0});edit(); // actual pointer focus; no private state access
    ui.SetTextValue("edit","Aé你😀");press({"end"});
    EXPECT_TRUE((press({"backspace"})&&ui.TextValue("edit")=="Aé你") && (++checks, true));
    EXPECT_TRUE((press({"backspace"})&&ui.TextValue("edit")=="Aé") && (++checks, true));
    press({"home"});EXPECT_TRUE((ui.Selection("edit",false)==0) && (++checks, true));
    EXPECT_TRUE((press({"delete"})&&ui.TextValue("edit")=="é") && (++checks, true));
    ui.SetTextValue("edit","");EXPECT_TRUE((edit({}, {0xe9,0x1f600})&&ui.TextValue("edit")=="é😀") && (++checks, true));
    ui.SetTextValue("edit","");EXPECT_TRUE((edit({}, {0xe9,0x1f600},{},1)&&ui.TextValue("edit")=="é") && (++checks, true));
    press({"left_control","a"});EXPECT_TRUE((ui.Selection("edit",false)==0&&ui.Selection("edit",true)==2) && (++checks, true));
    press({"left_control","c"});EXPECT_TRUE((input.Clipboard()=="é") && (++checks, true));
    EXPECT_TRUE((press({"left_control","x"})&&ui.TextValue("edit").empty()) && (++checks, true));
    EXPECT_TRUE((press({"left_control","v"})&&ui.TextValue("edit")=="é") && (++checks, true));
    ui.SetTextValue("edit","a\nb");press({"home"});EXPECT_TRUE((ui.Selection("edit",false)==2) && (++checks, true));press({"left_control","home"});EXPECT_TRUE((ui.Selection("edit",false)==0) && (++checks, true));
    press({"down"});EXPECT_TRUE((ui.Selection("edit",false)==2) && (++checks, true));press({"up"});EXPECT_TRUE((ui.Selection("edit",false)==0) && (++checks, true));
    ui.SetTextValue("edit","");
    for(std::size_t i=0;i<130;++i)EXPECT_TRUE((edit({}, {'x'})&&ui.TextValue("edit").size()==i+1) && (++checks, true));
    for(std::size_t i=0;i<128;++i)EXPECT_TRUE((ui.Undo("edit")&&ui.TextValue("edit").size()==129-i) && (++checks, true));
    EXPECT_TRUE((!ui.Undo("edit")&&ui.TextValue("edit")=="xx") && (++checks, true));
    for(int i=0;i<128;++i)EXPECT_TRUE((ui.Redo("edit")) && (++checks, true));EXPECT_TRUE((!ui.Redo("edit")&&ui.TextValue("edit").size()==130) && (++checks, true));
    double now=1;ui.SetInteractionClock([&]{return now;});
    for(const auto &sample:std::vector<std::pair<std::string,std::size_t>>{{"你好吗",9},{"，你好",3},{"😀éab",8}}){
        ui.SetTextValue("edit",sample.first);edit();edit({}, {},{0});now+=.1;edit();edit({}, {},{0});
        EXPECT_TRUE((ui.Selection("edit",false)==0&&ui.Selection("edit",true)==sample.second) && (++checks, true));edit();now+=1;
    }
    ui.SetAudit(true);ui.NewFrame();input.InjectFrame({100,100},{},{});renderer.BeginFrame(0,0,0,1);
    ui.SetTipDelay(0);ui.RootBegin(90,90,UISize::Fixed(20),UISize::Fixed(20));EXPECT_TRUE((!ui.Button("hover","")) && (++checks, true));ui.Tip("tooltip near corner");ui.RootEnd();ui.DrawTooltip();EXPECT_TRUE((!ui.InvisibleButton("invisible",{90,90,20,20})) && (++checks, true));
    bool tooltip=false;for(const auto &r:ui.AuditRects())if(r.kind=="tooltip"){tooltip=true;EXPECT_TRUE((r.rect.x>=0&&r.rect.y>=0&&r.rect.x+r.rect.width<=128&&r.rect.y+r.rect.height<=128) && (++checks, true));}EXPECT_TRUE((tooltip) && (++checks, true));
    for(const std::string h:{"left","center","right","stretch"})for(const std::string v:{"top","center","bottom","stretch"}){
        ui.RootAnchorBegin(h,v,UISize::Fixed(20),UISize::Fixed(10),5,7,0,false);ui.Panel({1,0,0,1});ui.RememberBox("anchor-rect");ui.RootEnd();auto r=ui.NamedRect("anchor-rect");
        const float x=h=="center"?59:h=="right"?103:5;
        const float y=v=="center"?66:v=="bottom"?111:7;
        EXPECT_TRUE((near_value(r.x,x)&&near_value(r.y,y)&&near_value(r.width,h=="stretch"?118:20)&&near_value(r.height,v=="stretch"?114:10)) && (++checks, true));
    }
    ui.BeginAlignGroup("contradiction","left|right|center_x|width|top|bottom|center_y|height|no_overlap");
    ui.RootBegin(10,10,UISize::Fixed(20),UISize::Fixed(20));ui.Button("one","");ui.RootEnd();ui.RootBegin(15,15,UISize::Fixed(30),UISize::Fixed(30));ui.Button("two","");ui.RootEnd();ui.EndAlignGroup();
    ui.RootBegin(200,200,UISize::Fixed(20),UISize::Fixed(20));ui.Button("outside","");ui.RootEnd();
    auto issues=ui.AuditLayout();EXPECT_TRUE((ui.LayoutIssueCount()==issues.size()) && (++checks, true));bool overlap=false,viewport=false;
    for(auto &issue:issues){if(issue.category=="align_group"&&issue.rule=="no_overlap")overlap=true;if(issue.category=="viewport")viewport=true;}
    EXPECT_TRUE((overlap&&viewport) && (++checks, true));renderer.EndFrame();
    // Public layout remains usable before backend binding; input-dependent
    // controls must preserve caller values and must not manufacture activation.
    UISystem detached;detached.NewFrame();detached.RootBegin(0,0,UISize::Fixed(100),UISize::Fixed(400));
    EXPECT_TRUE((!detached.Button("detached","x")) && (++checks, true));
    EXPECT_TRUE((detached.Slider("detached-slider",.375f,0,1)==.375f) && (++checks, true));
    EXPECT_TRUE((!detached.Checkbox("detached-check",true,std::string_view("x"))) && (++checks, true));
    EXPECT_TRUE((!detached.RadioButton("detached-radio",true,std::string_view("x"))) && (++checks, true));
    EXPECT_TRUE((!detached.ComboBegin("detached-combo","x",true)) && (++checks, true));
    EXPECT_TRUE((!detached.ComboItem("detached-item","x",true)) && (++checks, true));
    EXPECT_TRUE((!detached.Window("detached-window","x")) && (++checks, true));
    EXPECT_TRUE((!detached.Tab("detached-tab","x",true)) && (++checks, true));
    EXPECT_TRUE((!detached.Tree("detached-tree","x",true)) && (++checks, true));
    EXPECT_TRUE((detached.DrawTextPx(0,0,"x",16)==0&&detached.DrawWrappedTextPx(0,0,20,"x",16)==0) && (++checks, true));
    EXPECT_TRUE((detached.MeasureTextPx("x",16)==0&&detached.MeasureWrappedTextPx("x",16,20)==0) && (++checks, true));
    detached.RootEnd();

    // Exercise the supported text-provider extension using real font drawing,
    // verifying dispatch, scaling, face identity, return values and deferral.
    UISystem hooked;hooked.Bind(&input,&camera,&renderer);
    EXPECT_TRUE((hooked.SetScale(64,64)) && (++checks, true));
    int drawn=0,wrapped=0,measured=0,measured_wrapped=0;
    UISystem::TextHooks hooks;
    hooks.measure=[&](std::string_view text,float px,int face){EXPECT_TRUE((face==7&&px==16) && (++checks, true));++measured;return renderer.MeasureText(text,px/32);};
    hooks.measure_wrapped=[&](std::string_view text,float px,float width,int face){EXPECT_TRUE((face==7&&px==16&&width==30) && (++checks, true));++measured_wrapped;return renderer.GetFont().MeasureTextEx(text,px/32,width);};
    hooks.draw=[&](SpriteBatch &batch,std::string_view text,float x,float y,float px,const Color &color,int align,int face){EXPECT_TRUE((face==7&&align==2&&x==8&&y==12&&px==32) && (++checks, true));++drawn;return renderer.GetFont().DrawText(batch,text,x,y,px/32,color);};
    hooks.draw_wrapped=[&](SpriteBatch &batch,std::string_view text,float x,float y,float px,float width,const Color &color,int face){EXPECT_TRUE((face==7&&x==8&&y==40&&px==32&&width==60) && (++checks, true));++wrapped;return renderer.GetFont().DrawTextEx(batch,text,x,y,px/32,0,width,color);};
    hooked.SetTextHooks(std::move(hooks));input.InjectFrame({-100,-100},{},{});hooked.NewFrame();renderer.BeginFrame(0,0,0,1);
    hooked.RootBegin(0,0,UISize::Fixed(64),UISize::Fixed(64));hooked.CanvasBegin(UISize::Grow(),UISize::Grow());
    const float measured_width=renderer.MeasureText("ABC",.5f);
    EXPECT_TRUE((measured_width>0&&hooked.MeasureTextPx("ABC",16,7)==measured_width) && (++checks, true));
    EXPECT_TRUE((hooked.DrawTextPx(4,6,"ABC",16,Color::White(),2,7)==measured_width) && (++checks, true));
    const float measured_height=renderer.GetFont().MeasureTextEx("AB CD",.5f,30);
    EXPECT_TRUE((measured_height>0&&hooked.DrawWrappedTextPx(4,20,30,"AB CD",16,Color::White(),7)==measured_height) && (++checks, true));
    EXPECT_TRUE((drawn==0&&wrapped==0) && (++checks, true));hooked.CanvasEnd();hooked.RootEnd();renderer.EndFrame();
    EXPECT_TRUE((drawn==1&&wrapped==1&&measured==2&&measured_wrapped==1) && (++checks, true));
    hooked.SetTextHooks({});EXPECT_TRUE((hooked.MeasureTextPx("ABC",16)==measured_width) && (++checks, true));

    // System-font bootstrap is platform-dependent: success must mean a real
    // usable face; failure on hosts without CJK fonts must remain honest.
    { Renderer system_fonts;const bool found=EnsureSystemCjkFonts(system_fonts,"",18);
      EXPECT_TRUE((found==system_fonts.GetFont().IsValid()) && (++checks, true));
      if(found)EXPECT_TRUE((system_fonts.FontFaceCount()>0&&system_fonts.MeasureText("A",1)>0) && (++checks, true)); }
    UISystem menu;menu.Bind(&input,&camera,&renderer);
    auto popup_frame=[&](const std::vector<std::string_view>& keys=std::vector<std::string_view>{}){
        input.InjectFrame({-100,-100},keys,{});menu.NewFrame();renderer.BeginFrame(0,0,0,1);
        int chosen=menu.Popup("hot-popup",{"First","Second"},60,12);renderer.EndFrame();return chosen;
    };
    menu.OpenPopup("hot-popup",10,10);EXPECT_TRUE((popup_frame()==-1) && (++checks, true));
    EXPECT_TRUE((popup_frame({"up"})==-1&&menu.FocusedWidget()=="hot-popup/1") && (++checks, true));
    EXPECT_TRUE((popup_frame({"down"})==-1&&menu.FocusedWidget()=="hot-popup/0") && (++checks, true));
    EXPECT_TRUE((popup_frame({"enter"})==0&&!menu.IsModal()) && (++checks, true));
    EXPECT_TRUE((popup_frame()==-1) && (++checks, true));

    // Public textured-UI primitives must honor box padding, disabled alpha,
    // and both immediate and explicit-frame deferred command lifetimes.
    for(bool deferred:{false,true}){
        UISystem textured;textured.Bind(&input,&camera,&renderer);input.InjectFrame({-100,-100},{},{});textured.NewFrame();
        int target=renderer.GetRenderTargets().Create(128,128);EXPECT_TRUE((target>0) && (++checks, true));
        renderer.BeginFrame(0,0,0,1);EXPECT_TRUE((renderer.BeginRenderTarget(target)) && (++checks, true));renderer.ClearActiveTarget({0,0,0,1});
        if(deferred)EXPECT_TRUE((textured.BeginFrameUI()) && (++checks, true));
        textured.RootBegin(8,8,UISize::Fixed(40),UISize::Fixed(40),2);
        textured.CanvasBegin(UISize::Grow(),UISize::Grow());
        textured.DrawNineSliceAt(&Texture2D::White(),.25f,{1,0,0,1});
        // The old LastRect assertion described the padded drawing content,
        // not the outer 40px root. Retain that exact 36px geometry check.
        textured.RememberBox("slice-rect");
        textured.CanvasEnd();
        textured.DrawNineSliceAt(nullptr,1);
        textured.RootEnd();
        if(deferred)EXPECT_TRUE((textured.EndFrameUI()) && (++checks, true));
        auto rect=textured.NamedRect("slice-rect");EXPECT_TRUE((rect.x==10&&rect.y==10&&rect.width==36&&rect.height==36) && (++checks, true));
        textured.DisabledBegin();
        textured.DrawSpriteRect(&Texture2D::White(),{0,0,1,1},{60,8,20,20},{0,1,0,1});
        textured.DisabledEnd();
        textured.DrawSpriteRect(nullptr,{0,0,1,1},{90,8,20,20});
        textured.DrawSpriteRect(&Texture2D::White(),{0,0,1,1},{90,8,0,20});
        renderer.EndRenderTarget();renderer.EndFrame();Color pixel;auto *surface=renderer.GetRenderTargets().Get(target);
        EXPECT_TRUE((surface->ReadPixel(12,12,pixel)&&pixel.r>.99f&&pixel.g<.01f) && (++checks, true));
        EXPECT_TRUE((surface->ReadPixel(68,16,pixel)&&pixel.r<.01f&&pixel.g>.39f&&pixel.g<.41f) && (++checks, true));
        EXPECT_TRUE((surface->ReadPixel(96,16,pixel)&&pixel.r<.01f&&pixel.g<.01f&&pixel.b<.01f) && (++checks, true));
        renderer.GetRenderTargets().Destroy(target);EXPECT_TRUE((!renderer.GetRenderTargets().Get(target)) && (++checks, true));
    }
}
static void shared_white_context_regression(Renderer &renderer){
    const int target=renderer.GetRenderTargets().Create(128,128);EXPECT_TRUE((target>0) && (++checks, true));
    renderer.BeginFrame(0,0,0,1);EXPECT_TRUE((renderer.BeginRenderTarget(target)) && (++checks, true));renderer.ClearActiveTarget({0,0,0,1});
    renderer.DrawQuad({8,8,16,16},Color::White());renderer.EndRenderTarget();renderer.EndFrame();
    Color c;auto *rt=renderer.GetRenderTargets().Get(target);
    EXPECT_TRUE((rt->ReadPixel(12,12,c)&&c.r>.99f&&c.g>.99f&&c.b>.99f) && (++checks, true));
    EXPECT_TRUE((rt->ReadPixel(2,2,c)&&c.r<.01f&&c.g<.01f&&c.b<.01f) && (++checks, true));
    renderer.GetRenderTargets().Destroy(target);EXPECT_TRUE((!renderer.GetRenderTargets().Get(target)) && (++checks, true));
}
#ifdef FAKE2D_WITH_FAKELUA
static void lua_ui_deep(Engine &engine,Fixtures &f){
    auto &ui=engine.GetUI();auto &renderer=engine.GetRenderer();engine.GetInput().InjectFrame({5,5},{},{});ui.NewFrame();
    ColorPool colors;UITipTracker tips;UITextureCache textures;tips.BeginFrame();LuaUIHost host;host.engine=&engine;host.colors=&colors;host.tips=&tips;host.textures=&textures;host.root_dir=f.dir.string();host.texture_subdir="";
    fakelua::FakeluaStateGuard state;auto *st=state.GetState();RegisterCommonUILuaApi(st,&host);RegisterLayoutUILuaApi(st,&host);
    fakelua::RegisterNativeFunction(st,"deep_expect",false,std::function<void(fakelua::State*,bool,std::string_view)>([](auto*,bool ok,std::string_view label){if(!ok)std::fprintf(stderr,"Lua UI assertion: %.*s\n",int(label.size()),label.data());EXPECT_TRUE((ok) && (++checks, true));}));
    fakelua::CompileConfig cc;cc.debug_mode=false;fakelua::DisableAllJit(cc);cc.disable_jit[fakelua::JIT_INTERP]=false;fakelua::CompileFile(st,"tests/coverage_deep_ui.lua",cc);
    int target=renderer.GetRenderTargets().Create(128,128);EXPECT_TRUE((target>0) && (++checks, true));renderer.BeginFrame(0,0,0,1);EXPECT_TRUE((renderer.BeginRenderTarget(target)) && (++checks, true));renderer.ClearActiveTarget({0,0,0,1});int result=0;fakelua::Call(st,fakelua::JIT_INTERP,"deep_ui_contract",result);renderer.EndRenderTarget();renderer.EndFrame();EXPECT_TRUE((result==123) && (++checks, true));
    Color pixel;auto *rt=renderer.GetRenderTargets().Get(target);for(auto point:std::vector<Vec2>{{10,8},{43,20},{43,60}}){EXPECT_TRUE((rt->ReadPixel(int(point.x),int(point.y),pixel)) && (++checks, true));if(pixel.r<.95f||pixel.g>.05f||pixel.b>.05f)std::fprintf(stderr,"UI pixel %.0f,%.0f = %.3f %.3f %.3f\n",point.x,point.y,pixel.r,pixel.g,pixel.b);EXPECT_TRUE((pixel.r>.95f&&pixel.g<.05f&&pixel.b<.05f) && (++checks, true));}
    EXPECT_TRUE((rt->ReadPixel(85,16,pixel)&&near_value(pixel.r,ui.ThemeColor("panel").r)) && (++checks, true));renderer.GetRenderTargets().Destroy(target);
    fakelua::CompileFile(st,"tests/ci_ui_bindings.lua",cc);
    engine.GetInput().InjectFrame({-1000,-1000},{},{});ui.NewFrame();
    renderer.BeginFrame(0,0,0,1);result=0;
    fakelua::Call(st,fakelua::JIT_INTERP,"ci_ui_layout_contract",result,false);EXPECT_TRUE((result==126) && (++checks, true));
    EXPECT_TRUE((ui.ScrollContentHeight("ci-virtual")>=1000) && (++checks, true));
    renderer.EndFrame();
    host.engine=nullptr;result=0;fakelua::Call(st,fakelua::JIT_INTERP,"deep_ui_detached",result);EXPECT_TRUE((result==124) && (++checks, true));textures.Clear();
}
static void binding_tests(Fixtures &f){
    using nlohmann::json;
    std::string ppm="P6\n16 8\n255\n";for(int i=0;i<128;++i){ppm+=char(255);ppm+=char(0);ppm+=char(0);}f.write("tiles.ppm",ppm);
    auto map=json::parse(R"({"width":4,"height":3,"orientation":"orthogonal","tilewidth":8,"tileheight":8,
      "tilesets":[{"firstgid":1,"tilewidth":8,"tileheight":8,"tilecount":2,"columns":2,"image":"tiles.ppm","imagewidth":16,"imageheight":8,
      "tiles":[{"id":0,"animation":[{"tileid":0,"duration":100},{"tileid":1,"duration":200}],"properties":[{"name":"solid","type":"bool","value":true}]},
      {"id":1,"properties":[{"name":"oneway","type":"bool","value":true}]}]}],
      "layers":[{"type":"tilelayer","name":"ground","width":4,"height":3,"data":[1,0,2,0,0,0,0,0,0,0,0,0],"parallaxx":0.5,"parallaxy":0.75},
       {"type":"objectgroup","name":"objects","objects":[{"id":7,"name":"spawn","class":"Enemy","x":12,"y":13,"width":14,"height":15,"rotation":30,"visible":true,
       "polygon":[{"x":1,"y":2},{"x":4,"y":5},{"x":6,"y":0}],"properties":[{"name":"flag","type":"bool","value":true},{"name":"hp","type":"float","value":42},{"name":"title","type":"string","value":"boss"}]}]},
       {"type":"imagelayer","name":"sky","image":"tiles.ppm","offsetx":3,"offsety":4,"opacity":0.4,"repeatx":true,"repeaty":true}]})");
    const auto mapfile=f.write("map.json",map.dump());
    f.write("en.tsv","# name: English\n你好\tHello\nbad %s\tbad %d\n");f.write("zh.tsv","# name: 中文\n你好\t你好\n");f.write("unsafe.dat","bad %s\tbad %d\n");
    std::filesystem::create_directories(f.path("mods/addon"));f.write("mods/addon/mod.json",R"({"name":"addon","version":"1.2","load_order":7,"enabled":true})");
    f.write("entry.lua","function update(dt) return 0 end\n");
    EngineConfig cfg;cfg.width=128;cfg.height=128;cfg.headless=true;cfg.vsync=false;cfg.script_jit=false;cfg.script_entry=f.path("entry.lua");cfg.save_dir=f.path("script-saves");cfg.localization_dir=f.path("no-langs");cfg.fixed_sim_dt=true;cfg.pin_rng=true;cfg.rng_seed=17;
    Engine engine;EXPECT_TRUE((!engine.IsRunning()&&engine.Run(1)==1) && (++checks, true));EXPECT_TRUE((engine.Init(cfg)) && (++checks, true));
    const Engine &constant=engine;EXPECT_TRUE((constant.IsRunning()&&constant.InterpAlpha()==1) && (++checks, true));
    EXPECT_TRUE((&constant.GetInput()==&engine.GetInput()&&&constant.GetResources()==&engine.GetResources()&&&constant.GetSkeletons()==&engine.GetSkeletons()) && (++checks, true));
    EXPECT_TRUE((&constant.GetAnimations()==&engine.GetAnimations()&&&constant.GetTilemaps()==&engine.GetTilemaps()&&&constant.GetUI()==&engine.GetUI()) && (++checks, true));
    EXPECT_TRUE((&constant.GetI18n()==&engine.GetI18n()&&&constant.GetAchievements()==&engine.GetAchievements()&&&constant.GetMods()==&engine.GetMods()&&&constant.GetSaves()==&engine.GetSaves()) && (++checks, true));
    engine.SetFullscreen(false);EXPECT_TRUE((!constant.IsFullscreen()) && (++checks, true));shared_white_context_regression(engine.GetRenderer());tilemap_deep(f,map,engine.GetRenderer(),engine.GetResources());EXPECT_TRUE((engine.GetRenderer().GetFont().LoadFallback()) && (++checks, true));
    fakelua::FakeluaStateGuard state;auto *st=state.GetState();EXPECT_TRUE((st) && (++checks, true));RegisterScriptApi(st,&engine);
    fakelua::RegisterNativeFunction(st,"deep_expect",false,std::function<void(fakelua::State*,bool,std::string_view)>([](auto*,bool ok,std::string_view label){
        if(!ok)std::fprintf(stderr,"Lua deep assertion: %.*s\n",int(label.size()),label.data());EXPECT_TRUE((ok) && (++checks, true));
    }));
    fakelua::RegisterNativeFunction(st,"deep_step",false,std::function<void(fakelua::State*,double)>([&](auto*,double dt){engine.GetPhysics().Step(float(dt));engine.GetAnimations().Update(float(dt));}));
    fakelua::CompileConfig cc;cc.debug_mode=false;fakelua::DisableAllJit(cc);cc.disable_jit[fakelua::JIT_INTERP]=false;
    fakelua::CompileFile(st,"tests/coverage_deep_bindings.lua",cc);
    int result=0;fakelua::Call(st,fakelua::JIT_INTERP,"deep_contract",result,f.dir.string(),mapfile);EXPECT_TRUE((result==321) && (++checks, true));
    for(const auto &invalid:std::vector<std::string>{std::string(1,char(0xff)),std::string(1,char(0xc2)),std::string("\xe4\xb8",2),std::string("\xc2x",2)}){result=0;fakelua::Call(st,fakelua::JIT_INTERP,"deep_bad_glyph",result,invalid);EXPECT_TRUE((result==1) && (++checks, true));}
    EXPECT_TRUE((engine.GetSaves().GetString("cross-language","")=="verified"&&engine.GetEntities().Count()==0) && (++checks, true));
    EXPECT_TRUE((engine.GetAnimations().FrameIndex(999999)==-1) && (++checks, true)); // stale handle remains harmless
    fakelua::RegisterNativeFunction(st,"hot_begin",false,std::function<void(fakelua::State*,double,double,bool)>([&](auto*,double x,double y,bool down){engine.GetInput().InjectFrame({float(x),float(y)},{},down?std::vector<std::int64_t>{0}:std::vector<std::int64_t>{});engine.GetUI().NewFrame();engine.GetRenderer().BeginFrame(0,0,0,1);}));
    fakelua::RegisterNativeFunction(st,"hot_end",false,std::function<void(fakelua::State*)>([&](auto*){engine.GetRenderer().EndFrame();}));
    fakelua::CompileFile(st,"tests/coverage_hot_bindings.lua",cc);result=0;fakelua::Call(st,fakelua::JIT_INTERP,"hot_contract",result,f.dir.string(),mapfile);EXPECT_TRUE((result==777) && (++checks, true));
    fakelua::CompileFile(st,"tests/ci_ui_bindings.lua",cc);
    engine.GetInput().InjectFrame({-1000,-1000},{},{});engine.GetUI().NewFrame();
    engine.GetRenderer().BeginFrame(0,0,0,1);int ci_result=0;
    fakelua::Call(st,fakelua::JIT_INTERP,"ci_ui_layout_contract",ci_result,true);EXPECT_TRUE((ci_result==126) && (++checks, true));
    engine.GetRenderer().EndFrame();
    lua_ui_deep(engine,f);int frozen=0;engine.SetFrameCallback([&](Engine &e){if(e.FrameIndex()==0){EXPECT_TRUE((e.DeltaTime()==0) && (++checks, true));++frozen;}});
    EXPECT_TRUE((engine.Run(2)==0&&engine.FrameIndex()==2&&frozen==1&&!engine.IsRunning()) && (++checks, true));engine.Shutdown();
    auto run_config=[&](EngineConfig options,int expected){Engine e;EXPECT_TRUE((e.Init(options)) && (++checks, true));EXPECT_TRUE((e.Run(2)==expected&&!e.IsRunning()) && (++checks, true));};
    auto recorded=cfg;recorded.record_path=f.path("engine-input.bin");recorded.hot_reload=true;recorded.min_width=64;recorded.min_height=64;run_config(recorded,0);
    InputReplay recording;EXPECT_TRUE((recording.Load(recorded.record_path)&&recording.Size()==2) && (++checks, true));
    auto replayed=cfg;replayed.replay_path=recorded.record_path;replayed.fixed_dt=0;run_config(replayed,0);
    auto missing=cfg;missing.replay_path=f.path("absent-replay.bin");{Engine e;EXPECT_TRUE((!e.Init(missing)) && (++checks, true));}
    auto contradictory=recorded;contradictory.replay_path=recorded.record_path;{Engine e;EXPECT_TRUE((!e.Init(contradictory)) && (++checks, true));}
    auto bad_record=recorded;bad_record.record_path=f.path("missing-parent/record.bin");run_config(bad_record,1);
    auto bad_script=cfg;bad_script.script_entry=f.write("compile-bad.lua","function broken(\n");run_config(bad_script,0);
    auto failed_update=cfg;failed_update.script_entry=f.write("update-error.lua","function update(dt) error(\"intentional update failure\") end\n");run_config(failed_update,1);
    {Engine e;EXPECT_TRUE((e.Init(cfg)) && (++checks, true));e.RequestRelaunch("next-demo");EXPECT_TRUE((e.PendingRelaunch()=="next-demo"&&e.Run(1)==0&&e.FrameIndex()==0) && (++checks, true));}

}
#endif
static void sprite_batch_primitives(Renderer &renderer) {
    auto &batch=renderer.GetSpriteBatch();
    const int target=renderer.GetRenderTargets().Create(128,128);
    EXPECT_TRUE((target>0) && (++checks, true));
    auto begin=[&]{renderer.BeginFrame(0,0,0,1);EXPECT_TRUE((renderer.BeginRenderTarget(target)) && (++checks, true));renderer.ClearActiveTarget({0,0,0,1});batch.ResetStats();};
    auto end=[&]{renderer.EndRenderTarget();renderer.EndFrame();};
    auto pixel=[&](int x,int y){Color value;EXPECT_TRUE((renderer.GetRenderTargets().Get(target)->ReadPixel(x,y,value)) && (++checks, true));return value;};
    auto black=[&](int x,int y){auto c=pixel(x,y);EXPECT_TRUE((c.r<.01f&&c.g<.01f&&c.b<.01f) && (++checks, true));};
    begin();
    batch.DrawFrame({4,4,24,24},{1,0,0,1},3);
    batch.DrawFrame({40,4,24,24},{0,1,0,1},0);
    batch.DrawFrame({40,4,0,24},{0,1,0,1},2);
    batch.DrawFrame({40,4,24,-1},{0,1,0,1},2);
    end();
    for(auto pt:std::vector<Vec2>{{6,6},{16,5},{5,16},{26,16},{16,26}}){auto c=pixel(int(pt.x),int(pt.y));EXPECT_TRUE((c.r>.99f&&c.g<.01f&&c.b<.01f) && (++checks, true));}
    black(16,16);black(3,3);black(45,10);EXPECT_TRUE((batch.QuadCount()==4) && (++checks, true));
    begin();
    batch.DrawGradientV({4,4,24,32},{1,0,0,1},{0,0,1,1},4);
    batch.DrawGradientV({40,60,16,1},{1,0,0,1},{0,0,1,1},8);
    batch.DrawGradientV({70,4,10,10},{1,1,1,1},{1,1,1,1},0);
    batch.DrawGradientV({70,4,-1,10},{1,1,1,1},{1,1,1,1},4);
    batch.DrawGradientV({70,4,10,0},{1,1,1,1},{1,1,1,1},4);
    end();
    for(int i=0;i<4;++i){auto c=pixel(12,8+8*i);const float t=(float(i)+.5f)/4;EXPECT_TRUE((near_value(c.r,1-t)&&near_value(c.b,t)&&c.g<.01f) && (++checks, true));}
    auto thin=pixel(42,60);EXPECT_TRUE((near_value(thin.r,.5625f)&&near_value(thin.b,.4375f)) && (++checks, true));
    black(42,59);black(42,61);black(75,8);EXPECT_TRUE((batch.QuadCount()==5) && (++checks, true));
    begin();
    batch.DrawBezierTapered({8,96},{40,96},{72,96},{104,96},12,2,Color::White());
    batch.DrawBezierTapered({80,48},{80,48},{80,48},{80,48},6,6,Color::White());
    end();
    for(auto pt:std::vector<Vec2>{{12,91},{12,96},{96,96},{80,48}}){auto c=pixel(int(pt.x),int(pt.y));EXPECT_TRUE((c.r>.99f&&c.g>.99f&&c.b>.99f) && (++checks, true));}
    black(96,91);black(80,43);black(80,52);
    renderer.GetRenderTargets().Destroy(target);EXPECT_TRUE((!renderer.GetRenderTargets().Get(target)) && (++checks, true));
}


static void sprite_mesh_capacity_regression(Renderer &renderer) {
    const int target=renderer.GetRenderTargets().Create(128,128);EXPECT_TRUE((target>0) && (++checks, true));
    std::vector<Vertex2D> vertices;
    for(int i=0;i<3;++i){vertices.push_back({{6,6},{0,0},{1,0,0,1}});vertices.push_back({{30,6},{0,0},{1,0,0,1}});vertices.push_back({{6,30},{0,0},{1,0,0,1}});}
    const std::uint16_t indices[]={0,1,2,3,4,5,6,7,8},bad_indices[]={0,1,99};
    for(bool sorted:{false,true}){
        renderer.BeginFrame(0,0,0,1);EXPECT_TRUE((renderer.BeginRenderTarget(target)) && (++checks, true));renderer.ClearActiveTarget({0,0,0,1});
        SpriteBatch small;EXPECT_TRUE((small.Init(1)) && (++checks, true));small.SetSorted(sorted);small.Begin(renderer.ActiveCamera().ViewProjectionMatrix(),Shader::GetDefault2D());
        EXPECT_TRUE((!small.HasPendingDraws()) && (++checks, true));
        small.DrawTriangles(Texture2D::White(),vertices.data(),vertices.size(),bad_indices,3);
        small.DrawTriangles(Texture2D::White(),nullptr,vertices.size(),indices,9);
        small.DrawTriangles(Texture2D::White(),vertices.data(),vertices.size(),indices,2);
        EXPECT_TRUE((!small.HasPendingDraws()&&small.QuadCount()==0&&small.DrawCallCount()==0) && (++checks, true));
        small.DrawTriangles(Texture2D::White(),vertices.data(),vertices.size(),indices,9);
        EXPECT_TRUE((small.HasPendingDraws()) && (++checks, true));
        small.DrawQuad({64,64,16,16},{0,0,1,1});small.End();EXPECT_TRUE((!small.HasPendingDraws()) && (++checks, true));
        renderer.EndRenderTarget();renderer.EndFrame();
        Color c;auto *rt=renderer.GetRenderTargets().Get(target);
        EXPECT_TRUE((rt->ReadPixel(12,12,c)&&c.r>.99f&&c.g<.01f&&c.b<.01f) && (++checks, true));
        EXPECT_TRUE((rt->ReadPixel(68,68,c)&&c.b>.99f&&c.g<.01f&&c.r<.01f) && (++checks, true));
        EXPECT_TRUE((rt->ReadPixel(100,100,c)&&c.r<.01f&&c.g<.01f&&c.b<.01f) && (++checks, true));
        EXPECT_TRUE((small.QuadCount()==1&&small.DrawCallCount()==2) && (++checks, true));
    }
    renderer.GetRenderTargets().Destroy(target);EXPECT_TRUE((!renderer.GetRenderTargets().Get(target)) && (++checks, true));
}

static void skeleton_public_asset_tests(){
    using S=SkeletonSystem;S sys;S::Asset base;base.bones.emplace_back();base.bones[0].name="root";
    S::Attachment triangle;triangle.kind=S::AttachmentKind::Mesh;triangle.name="triangle";triangle.uvs={0,0,1,0,0,1};triangle.triangles={0,1,2};triangle.bind_xy={0,0,10,0,0,10};triangle.hull=3;
    base.attachments.push_back(triangle);S::Slot slot;slot.name="body";slot.attachment_count=1;base.slots.push_back(slot);
    auto invalid=[&](S::Attachment att){auto bad=base;bad.attachments[0]=std::move(att);EXPECT_TRUE((sys.AddAsset(std::move(bad))==S::kInvalid) && (++checks, true));};
    auto att=triangle;att.weight_first={0,0,0};att.weight_count={-1,0,0};invalid(att);
    att=triangle;att.weight_first={0,0,0};att.weight_count={1,0,0};invalid(att);
    att.influences={{99,0,0,1}};invalid(att);
    att.influences={{0,0,0,1},{0,1,1,1}};invalid(att);
    att=triangle;att.bind_xy={0,0};invalid(att);
    int asset=sys.AddAsset(base);EXPECT_TRUE((asset==1) && (++checks, true)); // rejections must not consume an asset slot
    auto *stored=sys.GetAsset(asset);EXPECT_TRUE((stored&&stored->attachments[0].geom&&stored->attachments[0].geom->weight_count==std::vector<int>({0,0,0})) && (++checks, true));
    auto weighted=base;att=triangle;att.weight_first={9,9,9};att.weight_count={1,1,1};att.influences={{0,0,0,1},{0,10,0,1},{0,0,10,1}};att.bind_xy.clear();weighted.attachments[0]=att;
    int wa=sys.AddAsset(weighted);EXPECT_TRUE((wa>0) && (++checks, true));stored=sys.GetAsset(wa);EXPECT_TRUE((stored&&stored->attachments[0].geom->weight_first==std::vector<int>({0,1,2})&&stored->attachments[0].size.x==1&&stored->attachments[0].size.y==1) && (++checks, true));
    int instance=sys.Create(wa);Vec2 point;EXPECT_TRUE((instance>0&&sys.MeshVertexWorld(instance,0,1,point)&&point.x==10&&point.y==0) && (++checks, true));
    EXPECT_TRUE((sys.MeshBindVertex(instance,0,99).LengthSq()==0&&!sys.MeshVertexWorld(instance,0,99,point)) && (++checks, true));
    EXPECT_TRUE((sys.BonePosition(instance,99).LengthSq()==0&&sys.BoneRotation(instance,99)==0&&sys.BoneLength(instance,99)==0) && (++checks, true));
    EXPECT_TRUE((!sys.SetBone(instance,99,0,0,0,1,1)&&!sys.ResetBone(instance,99)) && (++checks, true));
    EXPECT_TRUE((!sys.SetSlotVisible(instance,99,true)&&!sys.SetSlotAttachmentIndex(instance,0,99)) && (++checks, true));
    EXPECT_TRUE((!sys.SampleDeformAt(instance,99,0)&&!sys.MeshHullVertex(instance,0,99,point)) && (++checks, true));
    EXPECT_TRUE((sys.MeshHullWorld(instance,0,{0,0},1,1,nullptr,0)==0) && (++checks, true));
    sys.Destroy(instance);EXPECT_TRUE((sys.BonePosition(instance,0).LengthSq()==0&&sys.MeshBindVertex(instance,0,0).LengthSq()==0) && (++checks, true));
    EXPECT_TRUE((!sys.MeshVertexWorld(instance,0,0,point)&&sys.MeshWeightCount(instance,0,0)==0) && (++checks, true));
    sys.RemoveAsset(wa);sys.RemoveAsset(wa);EXPECT_TRUE((!sys.HasAsset(wa)) && (++checks, true));
    S capped;for(int i=0;i<S::kMaxAssets;++i)EXPECT_TRUE((capped.AddAsset(base)==i+1) && (++checks, true));
    EXPECT_TRUE((capped.AddAsset(base)==0) && (++checks, true));int last=0;for(int i=0;i<S::kMaxInstances;++i){last=capped.Create(1);EXPECT_TRUE((last>0) && (++checks, true));}
    EXPECT_TRUE((capped.Create(1)==0&&capped.InstanceCount()==S::kMaxInstances) && (++checks, true));
    capped.RemoveAsset(1);EXPECT_TRUE((capped.InstanceCount()==0&&!capped.Has(last)) && (++checks, true));capped.Clear();EXPECT_TRUE((capped.InstanceCount()==0) && (++checks, true));
}

static void physics_edge_tests(){
    PhysicsWorld world;world.SetGravity({0,0});BodyConfig cfg;cfg.radius=1;cfg.position={40,0};
    auto circle=world.CreateBody(cfg);cfg.position={0,0};cfg.type=BodyType::Static;auto stationary=world.CreateBody(cfg);
    cfg.position={20,0};cfg.type=BodyType::Dynamic;auto dead=world.CreateBody(cfg);world.DestroyBody(dead);
    cfg.radius=0;cfg.position={80,0};const Vec2 points[]={{0,0},{0,4},{4,0}};cfg.SetVertices(points,3);auto polygon=world.CreateBody(cfg);
    EXPECT_TRUE((world.PointTest({0,0})==kInvalidBody&&world.PointTest({20,0})==kInvalidBody) && (++checks, true));
    EXPECT_TRUE((world.PointTest({40,0})==circle&&world.PointTest({81,1})==polygon&&world.PointTest({100,100})==kInvalidBody) && (++checks, true));
    EXPECT_TRUE((world.WorldVertexCount(polygon)==3&&near_value(world.WorldVertex(polygon,0).x,80)) && (++checks, true));
    EXPECT_TRUE((world.SmoothRotation(kInvalidBody)==0) && (++checks, true));world.SetInterpAlpha(1);world.Get(circle)->rotation=.4f;EXPECT_TRUE((world.SmoothRotation(circle)==.4f) && (++checks, true));
    world.SetSleepTolerances(-1,-1,-1);EXPECT_TRUE((world.SleepLinearTolerance()==.01f&&world.SleepAngularTolerance()==.001f&&world.SleepTime()==0) && (++checks, true));
    world.Step(.001f);EXPECT_TRUE((world.IsSleeping(circle)) && (++checks, true));world.WakeBody(circle);EXPECT_TRUE((!world.IsSleeping(circle)) && (++checks, true));
    JointConfig joint;EXPECT_TRUE((world.CreateJoint(joint,circle,circle)==kInvalidJoint&&world.CreateJoint(joint,circle,dead)==kInvalidJoint) && (++checks, true));
    auto other_static=world.CreateBody(*world.Get(stationary));EXPECT_TRUE((world.CreateJoint(joint,stationary,other_static)==kInvalidJoint) && (++checks, true));
    const auto &constant=world;EXPECT_TRUE((constant.GetJoint(kInvalidJoint)==nullptr) && (++checks, true));
    std::vector<JointId> joints;for(std::size_t i=0;i<PhysicsWorld::kMaxJoints;++i){auto id=world.CreateJoint(joint,circle,polygon);EXPECT_TRUE((id!=kInvalidJoint) && (++checks, true));joints.push_back(id);}
    EXPECT_TRUE((world.JointCount()==PhysicsWorld::kMaxJoints&&world.CreateJoint(joint,circle,polygon)==kInvalidJoint) && (++checks, true));
    EXPECT_TRUE((constant.GetJoint(joints.front())&&constant.GetJoint(joints.front())->type==JointType::Distance) && (++checks, true));
    world.DestroyBody(polygon);EXPECT_TRUE((world.JointCount()==0&&constant.GetJoint(joints.front())==nullptr) && (++checks, true));
    world.DestroyBody(polygon);world.DestroyJoint(joints.front());EXPECT_TRUE((world.JointCount()==0) && (++checks, true));
    world.Clear();cfg=BodyConfig{};cfg.radius=1;cfg.sensor=true;auto a=world.CreateBody(cfg),b=world.CreateBody(cfg);
    world.Step(0);EXPECT_TRUE((world.Contacts().size()==1&&world.Contacts().front().normal.x==0&&world.Contacts().front().normal.y==-1&&world.Contacts().front().point.x==0) && (++checks, true));
    auto link=world.CreateJoint(joint,a,b);EXPECT_TRUE((link!=kInvalidJoint) && (++checks, true));world.Step(.001f);EXPECT_TRUE((world.Contacts().empty()&&world.StayedContacts().empty()) && (++checks, true));
    world.DestroyJoint(link);world.Step(0);EXPECT_TRUE((world.Contacts().size()==1) && (++checks, true));
    world.Clear();EXPECT_TRUE((world.BodyCount()==0&&world.JointCount()==0) && (++checks, true));
}

// Google Test migration registration; helper/test bodies above are unchanged.
static fake2d::test::FilesEnvironment<Fixtures> *deep_files;
void RegisterFake2dTestEnvironment() {
    deep_files = new fake2d::test::FilesEnvironment<Fixtures>;
    testing::AddGlobalTestEnvironment(deep_files);
}
// Keep all fork/crash checks before graphics contexts/driver threads, as before.
TEST(DeepCore, TextUtilities) { text_utility_contracts(); }
TEST(DeepCore, Interpolation) { interpolation_contracts(); }
TEST(DeepCore, Storage) { storage_tests(*deep_files->files); }
TEST(DeepCore, ProvinceMap) { province_map_contracts(*deep_files->files); }
TEST(DeepCore, I18nViews) { i18n_view_contracts(*deep_files->files); }
TEST(DeepCore, LogAndCrash) { log_crash_contracts(*deep_files->files); }
TEST(DeepCore, PhysicsEdges) { physics_edge_tests(); }
TEST(DeepCore, SkeletonPublicAssets) { skeleton_public_asset_tests(); }
class DeepGraphics : public testing::Test {
protected:
    struct Context { platform::Window window; Renderer renderer; };
    inline static std::unique_ptr<Context> context;
    static void SetUpTestSuite() {
        context = std::make_unique<Context>();
        platform::WindowDesc desc; desc.width=128; desc.height=128; desc.headless=true;
        ASSERT_TRUE(context->window.Create(desc) && (++checks, true));
        ASSERT_TRUE(context->renderer.Init(128,128) && (++checks, true));
    }
    static void TearDownTestSuite() { context.reset(); }
};
TEST_F(DeepGraphics, SharedWhiteContext) { shared_white_context_regression(context->renderer); }
TEST_F(DeepGraphics, SpritePrimitives) { sprite_batch_primitives(context->renderer); }
TEST_F(DeepGraphics, SpriteMeshCapacity) { sprite_mesh_capacity_regression(context->renderer); }
TEST_F(DeepGraphics, Shaders) { shader_tests(); }
TEST_F(DeepGraphics, ResourceAtlas) { resource_atlas_contracts(*deep_files->files); }
TEST_F(DeepGraphics, FontLadder) { font_ladder_contracts(); }
TEST_F(DeepGraphics, UIExtra) { ui_extra_contracts(*deep_files->files,context->renderer); }
TEST_F(DeepGraphics, semantics) { ci_ui_semantics(context->renderer); }
TEST_F(DeepGraphics, geometry_and_pixels) { ci_ui_geometry_and_pixels(context->renderer); }
TEST_F(DeepGraphics, inspector) { ci_ui_inspector(context->renderer); }
TEST_F(DeepGraphics, drag_lifecycle) { ci_ui_drag_lifecycle(context->renderer); }
TEST_F(DeepGraphics, menu_lifecycle) { ci_ui_menu_lifecycle(context->renderer); }
TEST_F(DeepGraphics, flex_boundaries) { ci_ui_flex_boundaries(context->renderer); }
TEST_F(DeepGraphics, scroll_visibility) { ci_ui_scroll_visibility(context->renderer); }
TEST_F(DeepGraphics, UpstreamLayout) { upstream_layout_contracts(context->renderer); }
TEST_F(DeepGraphics, Input) { input_tests(context->window); }
TEST_F(DeepGraphics, UI) { ui_tests(context->renderer); }
TEST_F(DeepGraphics, UIInputAndRichText) { ui_input_rich_edges(context->renderer); }
TEST_F(DeepGraphics, Skeleton) { skeleton_deep(*deep_files->files,context->renderer); }
TEST_F(DeepGraphics, Font) { font_deep(*deep_files->files,context->renderer); }
#ifdef FAKE2D_WITH_FAKELUA
// This Engine owns its own context, after the shared graphics suite is destroyed.
TEST(DeepBindings, EngineAndLua) { binding_tests(*deep_files->files); }
#endif

int Fake2dAssertionCount() { return checks; }
