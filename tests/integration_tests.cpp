// Existing external-process predicates, now owned by Google Test.
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <regex>
#include <string>
#include <map>
#include <set>
#include <vector>
#include <cmath>
#include <cerrno>
#include <sstream>
#include <cstdio>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif
using Json = nlohmann::json;
namespace fs = std::filesystem;
namespace {
fs::path Utf8(const std::string &s) {
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(s.data()),s.size()));
}
#ifdef _WIN32
std::wstring Wide(const std::string &s) {
    int n=MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), nullptr,0);
    if (!n && !s.empty()) throw std::runtime_error("invalid UTF-8 process argument");
    std::wstring w(n,L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),w.data(),n);return w;
}
std::wstring Quote(const std::wstring &s) {
    std::wstring result=L"\"";size_t slashes=0;
    for(wchar_t c:s) { if(c==L'\\') {++slashes;continue;}
        result.append(slashes*(c==L'"'?2:1),L'\\');slashes=0;
        if(c==L'"')result+=L'\\';result+=c;
    }
    result.append(slashes*2,L'\\');return result+L'"';
}
#endif
int Transport(const fs::path &job,const fs::path &result) {
#ifdef _WIN32
    std::wstring command=Quote(Wide(FAKE2D_PYTHON))+L" "+Quote(Wide(FAKE2D_TRANSPORT))+L" "+Quote(job.native())+L" "+Quote(result.native());
    STARTUPINFOW start{};start.cb=sizeof(start);PROCESS_INFORMATION process{};
    if(!CreateProcessW(nullptr,command.data(),nullptr,nullptr,FALSE,0,nullptr,nullptr,&start,&process))return -static_cast<int>(GetLastError());
    const DWORD waited=WaitForSingleObject(process.hProcess,INFINITE);DWORD code=1;
    if(waited==WAIT_OBJECT_0)GetExitCodeProcess(process.hProcess,&code);
    CloseHandle(process.hThread);CloseHandle(process.hProcess);return static_cast<int>(code);
#else
    pid_t child=fork();if(child<0)return -errno;
    if(child==0){execl(FAKE2D_PYTHON,FAKE2D_PYTHON,FAKE2D_TRANSPORT,job.c_str(),result.c_str(),static_cast<char*>(nullptr));_exit(127);}
    int status=0;while(waitpid(child,&status,0)<0){if(errno!=EINTR)return -errno;}
    return WIFEXITED(status)?WEXITSTATUS(status):128+WTERMSIG(status);
#endif
}
Json Load(const fs::path &path) {std::ifstream stream(path);return Json::parse(stream);}
class ExternalProcess : public testing::Test {
public:
    explicit ExternalProcess(Json job):job_(std::move(job)){}
protected:
    Json job_;
    void SetUp() override {fs::create_directories(Utf8(job_.at("scratch").get<std::string>()));}
    void CheckMetrics(const std::string &kind,const std::string &log) {
        const bool layout=kind=="ui-layout";
        if(!layout && kind!="ui-variable" && kind!="ui-stress")return;
        std::string pattern="\\[script\\] "+std::string(layout?"14801":kind=="ui-variable"?"14900":"14910")+"([0-5])";
        for(int i=0;i<(layout?3:4);++i)pattern+="\n\\[script\\] ([0-9.e+-]+)";
        std::regex re(pattern);std::map<int,std::vector<double>> metrics;
        for(std::sregex_iterator it(log.begin(),log.end(),re),end;it!=end;++it){
            std::vector<double> fields;for(size_t i=2;i<it->size();++i)fields.push_back(std::stod((*it)[i]));
            metrics[std::stoi((*it)[1])]=std::move(fields);
        }
        ASSERT_EQ(metrics.size(),6u)<<"missing measured dynamic transition metrics\n"<<log;
        for(int i=0;i<6;++i)ASSERT_TRUE(metrics.contains(i));
        if(layout){for(int short_stage:{0,3}){int long_stage=short_stage+1,shrunk=short_stage+2;
            EXPECT_TRUE(metrics[long_stage][0]>metrics[short_stage][0] &&
                        std::abs(metrics[shrunk][0]-metrics[short_stage][0])<.01 &&
                        metrics[long_stage][1]>106 && metrics[shrunk][1]<106 && metrics[shrunk][2]==0)
                <<"grow/shrink/scroll stages "<<short_stage<<","<<long_stage<<","<<shrunk<<"\n"<<log;
        }} else if(kind=="ui-variable") {
            bool range=true;for(int i=0;i<5;++i)range=range && 0<metrics[i][0] && metrics[i][0]<16;
            EXPECT_TRUE(metrics[1][3]>metrics[0][3] && std::abs(metrics[2][3]-metrics[0][3])<.01 &&
                        metrics[1][1]>metrics[0][1] && std::abs(metrics[2][1]-metrics[0][1])<.1 && range &&
                        std::abs(metrics[4][2]-(metrics[4][1]-170))<.1 && metrics[5][0]==3 &&
                        metrics[5][1]<170 && metrics[5][2]==0)<<"grow/shrink/anchor/range\n"<<log;
        } else {
            bool long_ok=true,short_ok=true;
            for(int i:{1,4})long_ok=long_ok && metrics[i][1]>500 && metrics[i][2]>500 && std::abs(metrics[i][3]-(metrics[i][2]-330))<.1;
            for(int i:{0,2,3,5})short_ok=short_ok && metrics[i][1]<100 && metrics[i][2]<330 && metrics[i][3]==0;
            EXPECT_TRUE(long_ok && short_ok)<<"short/long/short/scroll\n"<<log;
        }
    }
    void CheckReporter(const Json &r) {
        EXPECT_EQ(r.at("alias_hits"),r.at("hits"))<<"suffixed CI gcov selected wrong companion";
        EXPECT_EQ(r.at("companion"),"/usr/bin/x86_64-linux-gnu-gcov-dump-14");
        auto count=[&](const char *m,const char *phase){return r.at("counts").at(m).at(phase).get<long long>();};
        EXPECT_TRUE(count("USED_OPEN","after")>0 && count("USED_BODY","after")>0);
        if(count("UNCALLED_OPEN","before")==0)EXPECT_EQ(count("UNCALLED_OPEN","after"),0)<<"uncalled callback declaration inferred hit";
        EXPECT_EQ(count("FALSE_BRANCH_BODY","after"),0)<<"unexecuted branch inferred hit";
        EXPECT_TRUE(count("FALSE_TERNARY_BODY","before")==0 && count("FALSE_TERNARY_BODY","after")==0);
        EXPECT_EQ(count("FALSE_TERNARY_ARM","after"),count("FALSE_TERNARY_ARM","before"));
        EXPECT_GT(count("TRUE_TERNARY_ARM","after"),0);
        EXPECT_EQ(count("UNCALLED_BODY","after"),0);
        EXPECT_TRUE(count("ABSENT_OPEN","after")==0 && count("ABSENT_BODY","after")==0);
        EXPECT_EQ(r.at("denominator_before"),r.at("denominator_after"));
        EXPECT_TRUE(r.at("idempotent_changes").empty());EXPECT_TRUE(r.at("no_proof_changes").empty());
    }
    void CheckPadding(const Json &r) {
        for(const auto &p:r.at("padding").at("pairs"))EXPECT_EQ(p.at("actual"),p.at("expected"))<<p.at("before");
        EXPECT_EQ(r.at("padding").at("nested_nil_count"),r.at("padding").at("expected_nested_nil_count"));
        EXPECT_EQ(r.at("padding").at("missing_required_error"),"ValueError");
        for(auto it=r.at("definitions").begin();it!=r.at("definitions").end();++it)EXPECT_EQ(it.value(),1)<<it.key();
    }
    void CheckHeadless(const Json &r) {
        const std::string text=r.at("stdout");std::vector<double> codes;
        std::regex pattern("FIX_CODE ([0-9.e+-]+)");
        for(std::sregex_iterator it(text.begin(),text.end(),pattern),end;it!=end;++it)codes.push_back(std::stod((*it)[1]));
        EXPECT_NE(text.find("FIX_DONE"),std::string::npos)<<"scenario did not finish; exit="<<r.at("returncode")<<"\n"<<r.at("log");
        for(const auto &check:job_.at("expected_checks")){
            const double lo=check.at("lo"),hi=check.at("hi");const std::string kind=check.at("kind");
            const bool found=std::any_of(codes.begin(),codes.end(),[&](double code){return code>=lo && code<=hi;});
            if(kind=="forbid")EXPECT_FALSE(found)<<check.at("label")<<" unexpected code "<<lo<<"\n"<<r.at("log");
            else EXPECT_TRUE(found)<<check.at("label")<<" expected code/range "<<lo<<".."<<hi<<"\n"<<r.at("log");
        }
    }
    void CheckPackage(const std::string &kind) {
        auto stage=Utf8(job_.at("stage").get<std::string>());
        ASSERT_TRUE(fs::is_directory(stage/"assets"));
        if(kind=="package_static"){
            for(const auto &rel:job_.at("required_files")){
                auto file=stage/Utf8(rel.get<std::string>());
                EXPECT_TRUE(fs::exists(file))<<file;
                if(fs::is_regular_file(file))EXPECT_GT(fs::file_size(file),0u)<<file;
            }
            for(const char *name:{"run.sh","run.command","run.bat"})if(fs::is_regular_file(stage/name)){
                std::ifstream input(stage/name);std::string text((std::istreambuf_iterator<char>(input)),{});
                EXPECT_NE(text.find("--strict-assets"),std::string::npos)<<name;
            }
        }else{
            std::vector<std::string> after;
            for(const auto &entry:fs::recursive_directory_iterator(stage/"assets"))
                if(entry.symlink_status().type()==fs::file_type::regular)after.push_back("./"+fs::relative(entry.path(),stage/"assets").generic_string());
            std::sort(after.begin(),after.end());EXPECT_EQ(Json(after),job_.at("before"))<<"package generated assets at runtime";
        }
    }
    void CheckDocs(const Json &r) {
        const auto root=Utf8(r.at("root").get<std::string>());
        auto read=[&](const std::string &rel){std::ifstream f(root/Utf8(rel));return std::string((std::istreambuf_iterator<char>(f)),{});};
        const auto scripting=read("docs/SCRIPTING.md");
        for(const char *symbol:{"SetVariableRowText","SetVariableMeasureBudget","ui_vlist_text","ui_vlist_budget","UI_UNSCOPED","rich_batches"})
            EXPECT_NE(scripting.find(symbol),std::string::npos)<<"SCRIPTING.md missing "<<symbol;
        ASSERT_TRUE(r.at("suite_block").get<bool>())<<"FAKE2D_TEST_SUITES not found";
        long long total=0;
        for(const auto &item:r.at("logs")){
            const std::string suite=item.at("suite"),tag=suite=="map-object"?"map-obj":suite,text=item.at("text");
            EXPECT_EQ(item.at("returncode"),0)<<suite<<"\n"<<text;
            std::regex pattern("\\["+tag+"\\] PASS: ([0-9]+) checks");
            std::set<long long> values;
            for(std::sregex_iterator it(text.begin(),text.end(),pattern),end;it!=end;++it)values.insert(std::stoll((*it)[1]));
            EXPECT_FALSE(values.empty())<<suite<<" missing PASS count";
            EXPECT_LE(values.size(),1u)<<suite<<" conflicting PASS counts";
            if(!values.empty())total+=*values.begin();
        }
        for(const auto &p:r.at("patterns")){
            std::string key=p.at("expected_key");if(key=="total" && !r.at("run").get<bool>())continue;
            long long expected=key=="total"?total:key=="len(suites)"?static_cast<long long>(r.at("suites").size()):r.at(key).get<long long>();
            std::string text=read(p.at("source")),regex=p.at("pattern");std::regex pattern(regex);bool found=false;
            for(std::sregex_iterator it(text.begin(),text.end(),pattern),end;it!=end;++it){found=true;EXPECT_EQ(std::stoll((*it)[1]),expected)<<p.at("source")<<" "<<p.at("label");}
            EXPECT_TRUE(found)<<p.at("source")<<" pattern not found "<<regex;
        }
    }
    void TestBody() override {
        SCOPED_TRACE(job_.at("suite").get<std::string>()+"."+job_.at("name").get<std::string>());
        if(job_.value("kind","")=="package_static" || job_.value("kind","")=="package_after"){
            CheckPackage(job_.at("kind"));return;
        }
        auto scratch=Utf8(job_.at("scratch").get<std::string>());auto input=scratch/"job.json",output=scratch/"result.json";
        {std::ofstream file(input);file<<job_.dump(2);}
        ASSERT_EQ(Transport(input,output),0)<<"subprocess adapter failed; job "<<input;
        ASSERT_TRUE(fs::is_regular_file(output));auto result=Load(output);
        const std::string kind=job_.value("kind","");
        if(kind=="reporter"){CheckReporter(result);return;}
        if(kind=="padding"){CheckPadding(result);return;}
        if(kind=="docs"){CheckDocs(result);return;}
        if(kind=="headless"){CheckHeadless(result);return;}
        std::string log=result.at("log");
        if(job_.value("expected_exit","")=="nonzero")EXPECT_NE(result.at("returncode").get<int>(),0)<<log;
        else EXPECT_EQ(result.at("returncode").get<int>(),0)<<log;
        if(job_.value("script_errors",false))EXPECT_FALSE(std::regex_search(log,std::regex("syntax error|CompileFile.*failed|update\\(\\) failed")))<<log;
        if(job_.contains("forbidden_literals"))for(auto &value:job_["forbidden_literals"]){auto s=value.get<std::string>();EXPECT_EQ(log.find(s),std::string::npos)<<"forbidden literal "<<s<<"\n"<<log;}
        if(job_.contains("contains"))for(auto &value:job_["contains"]){auto s=value.get<std::string>();EXPECT_NE(log.find(s),std::string::npos)<<"missing marker "<<s<<"\n"<<log;}
        if(job_.contains("forbids"))for(auto &value:job_["forbids"]){auto s=value.get<std::string>();EXPECT_FALSE(std::regex_search(log,std::regex(s)))<<"forbidden regex "<<s<<"\n"<<log;}
        if(job_.contains("screenshot") && !job_["screenshot"].is_null()){
            auto shot=Utf8(job_["screenshot"].get<std::string>());if(shot.is_relative())shot=Utf8(FAKE2D_SOURCE_ROOT)/shot;
            ASSERT_TRUE(fs::is_regular_file(shot))<<shot;EXPECT_GT(fs::file_size(shot),0u)<<shot;
        }
        const auto &args=job_["args"];for(size_t i=0;i+1<args.size();++i)if(args[i]=="--shot-min-colors"){
            const int minimum=std::stoi(args[i+1].get<std::string>());std::smatch m;
            ASSERT_TRUE(std::regex_search(log,m,std::regex("fake2d_screenshot_colors=([0-9]+)")))<<"missing real framebuffer colour telemetry\n"<<log;
            EXPECT_GE(std::stoi(m[1]),minimum)<<"actual sampled GL framebuffer colours\n"<<log;
        }
        if(job_.contains("metrics"))CheckMetrics(job_["metrics"],log);
    }
};
}
int main(int argc,char **argv){
#ifdef _WIN32
    // Windows CRT narrow argv is ACP, not necessarily UTF-8. Rebuild it from
    // the Unicode command line; CreateProcessW below also avoids cmd.exe.
    int wide_count=0;wchar_t **wide=CommandLineToArgvW(GetCommandLineW(),&wide_count);
    if(!wide)return 2;
    std::vector<std::string> utf8_args;utf8_args.reserve(wide_count);
    for(int i=0;i<wide_count;++i){
        int length=WideCharToMultiByte(CP_UTF8,0,wide[i],-1,nullptr,0,nullptr,nullptr);
        std::string arg(length,'\0');WideCharToMultiByte(CP_UTF8,0,wide[i],-1,arg.data(),length,nullptr,nullptr);
        arg.pop_back();utf8_args.push_back(std::move(arg));
    }
    LocalFree(wide);std::vector<char*> pointers;for(auto &arg:utf8_args)pointers.push_back(arg.data());pointers.push_back(nullptr);
    argc=wide_count;argv=pointers.data();
#endif
    std::string manifest;for(int i=1;i<argc;){std::string arg=argv[i];if(arg.starts_with("--manifest=")){manifest=arg.substr(11);for(int j=i;j+1<argc;++j)argv[j]=argv[j+1];--argc;argv[argc]=nullptr;}else ++i;}
    try {
        if(manifest.empty()){std::fprintf(stderr,"Supply --manifest=PATH from the workload driver\n");return 2;}
        auto jobs=Load(Utf8(manifest));if(jobs.empty())return 2;
        for(const auto &job:jobs)testing::RegisterTest(job.at("suite").get<std::string>().c_str(),job.at("name").get<std::string>().c_str(),nullptr,nullptr,__FILE__,__LINE__,[job](){return new ExternalProcess(job);});
        testing::InitGoogleTest(&argc,argv);return RUN_ALL_TESTS();
    }catch(const std::exception &e){std::fprintf(stderr,"integration fixture: %s\n",e.what());return 2;}
}
