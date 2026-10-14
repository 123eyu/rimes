#include <rime_api.h>
#include <rime_levers_api.h>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
static void require(bool value,const char* label) { if(!value) { std::fprintf(stderr,"FAIL %s\n",label); std::exit(1); } }
int main(int argc,char** argv) {
    if(argc!=4) return 2;
    std::string shared=argv[1], user=argv[2], mode=argv[3], compiled=shared+"/build";
    std::filesystem::create_directories(user);
    auto api=rime_get_api();
    RIME_STRUCT(RimeTraits,traits);
    traits.shared_data_dir=shared.c_str(); traits.user_data_dir=user.c_str();
    traits.prebuilt_data_dir=compiled.c_str(); traits.staging_dir=compiled.c_str(); traits.app_name="rime.android_contract";
    api->setup(&traits); api->initialize(&traits);
    if(mode!="export") {
        auto session=api->create_session(); require(session!=0,"session");
        struct Case { const char* schema; const char* code; const char* expected; const char* hint = ""; };
        Case cases[]={{"rimes_pinyin","nihao","你好"},{"rimes_pinyin9","64426","你好"},{"rimes_ziranma","nihk","你好"},{"rimes_wubi","wq","你"},{"rimes_wubi","zni","你","wq"},{"rimes_wubi","znihao","你好","wqvb"},{"lingxi_wubi","wq","你"},{"lingxi_wubi","nihao","你好","wqvb"},{"lingxi_wubi","zni","你","wq"}};
        for(const auto& item:cases) {
            std::string schema=item.schema;
            if(mode.rfind("private",0)==0) schema+="_private";
            require(api->select_schema(session,schema.c_str()),"select schema");
            for(const char* c=item.code;*c;++c) require(api->process_key(session,*c,0),"key handled");
            RIME_STRUCT(RimeContext,context);
            require(api->get_context(session,&context),"context");
            require(context.menu.page_size==9,"nine candidates per page");
            require(context.menu.num_candidates>0,"candidates available");
            require(std::string(context.menu.candidates[0].text)==item.expected,"first candidate");
            if (*item.hint) {
                std::string hint=context.menu.candidates[0].comment?context.menu.candidates[0].comment:"";
                require(hint.size()>2 && hint.front()=='(' && hint.back()==')' && hint.find(item.hint)!=std::string::npos,"parenthesized Wubi code hint");
            }
            api->free_context(&context);
            require(api->select_candidate(session,0),"select first");
            RIME_STRUCT(RimeCommit,commit);
            require(api->get_commit(session,&commit),"commit available");
            require(std::string(commit.text)==item.expected,"committed text"); api->free_commit(&commit);
            std::printf("PASS %s %s -> %s\n",schema.c_str(),item.code,item.expected);
            api->clear_composition(session);
        }
        for (const char* base : {"rimes_wubi", "lingxi_wubi"}) {
            const std::string schema=std::string(base)+(mode.rfind("private",0)==0?"_private":"");
            require(api->select_schema(session,schema.c_str()),"auto-commit schema");
            for (const char* code : {"voni", "wqiy", "zfiao", "fiao"}) {
                if (std::string(code)=="fiao" && std::string(base)=="rimes_wubi") continue;
                api->clear_composition(session); int typed=0;
                for(const char* key=code;*key;++key) {
                    api->process_key(session,*key,0); ++typed;
                    RIME_STRUCT(RimeCommit,commit); bool got=api->get_commit(session,&commit);
                    require(got==(std::string(code)=="voni" && typed==4),"only unique fourth Wubi key auto-commits");
                    if(got) { require(std::string(commit.text)=="灵犀","exact auto commit"); api->free_commit(&commit); }
                }
            }
            api->clear_composition(session);
        }
        require(api->select_schema(session,mode.rfind("private",0)==0?"rimes_pinyin_private":"rimes_pinyin"),"pinyin");
        api->process_key(session,'n',0); api->process_key(session,'i',0);
        require(api->process_key(session,0xff56,0),"next page");
        RIME_STRUCT(RimeContext,page); require(api->get_context(session,&page),"page");
        require(page.menu.page_no==1,"page index"); api->free_context(&page);
        require(api->select_candidate(session,10),"non-first candidate on second page");
        RIME_STRUCT(RimeCommit,other); require(api->get_commit(session,&other),"page candidate commit"); api->free_commit(&other);
        api->process_key(session,',',0);
        RIME_STRUCT(RimeCommit,punctuation); require(api->get_commit(session,&punctuation),"punctuation");
        require(std::string(punctuation.text)=="，","Chinese punctuation"); api->free_commit(&punctuation);
        api->process_key(session,'n',0); api->process_key(session,'i',0); api->process_key(session,0xff08,0);
        require(std::string(api->get_input(session))=="n","backspace preedit"); api->clear_composition(session);
        require(std::string(api->get_input(session)).empty(),"clear composition");
        api->destroy_session(session); api->cleanup_all_sessions();
    }
    auto module=api->find_module("levers"); require(module && module->get_api,"levers");
    auto levers=reinterpret_cast<RimeLeversApi*>(module->get_api());
    for(const auto* dict:{"pinyin_simp","wubi86"}) {
        std::string file=user+"/"+dict+".export.txt";
        bool existed=std::filesystem::exists(user+"/"+dict+".userdb");
        int entries=levers->export_user_dict(dict,file.c_str());
        std::printf("EXPORT %s entries=%d\n",dict,entries);
        if(mode=="learn" || mode=="export" || mode=="private-existing") require(entries>0,"learning persists across process restart");
        else require(entries==0 || (entries==-1 && !existed),"private schemas do not learn");
    }
    api->finalize(); std::puts("PASS engine contract");
}
