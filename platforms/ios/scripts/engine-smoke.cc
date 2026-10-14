#include <rime_api.h>
#include <cstdio>
#include <string>
#include <filesystem>
int main(int argc, char** argv) {
    if (argc != 3) return 2;
    const std::string shared = argv[1], user = argv[2], compiled = shared + "/build";
    std::filesystem::create_directories(user);
    RIME_STRUCT(RimeTraits, traits);
    traits.shared_data_dir = shared.c_str(); traits.user_data_dir = user.c_str();
    traits.prebuilt_data_dir = compiled.c_str(); traits.staging_dir = compiled.c_str();
    traits.app_name = "rime.rimes_ios_smoke";
    auto api = rime_get_api(); api->setup(&traits); api->initialize(&traits);
    const auto session = api->create_session();
    struct Case { const char* schema; const char* code; const char* expected; const char* hint = ""; };
    Case cases[] = {{"rimes_pinyin","nihao","你好"},{"rimes_ziranma","nihk","你好"},{ "rimes_wubi","wq","你"},
                    {"rimes_wubi","zni","你","wq"},{"rimes_wubi","znihao","你好","wqvb"},
                    {"lingxi_wubi","wq","你"},{"lingxi_wubi","nihao","你好","wqvb"},{"lingxi_wubi","zni","你","wq"},
                    {"rimes_pinyin9","64426","你好"},{"rimes_pinyin9","ni'426","你好"},{"rimes_pinyin9","94'26","西安"}};
    int failures = 0;
    for (const auto& c : cases) {
        if (!api->select_schema(session,c.schema)) { std::printf("FAIL schema %s\n",c.schema); ++failures; continue; }
        for (const char* key=c.code; *key; ++key) api->process_key(session,*key,0);
        RimeCandidateListIterator it = {}; int index=0; bool found=false;
        if (api->candidate_list_begin(session,&it)) {
            while (index<60 && api->candidate_list_next(&it)) {
                if (it.candidate.text && std::string(it.candidate.text)==c.expected) { found=true;
                    if (*c.hint) {
                        const std::string hint = it.candidate.comment ? it.candidate.comment : "";
                        found = hint.size() > 2 && hint.front() == '(' && hint.back() == ')' && hint.find(c.hint) != std::string::npos;
                        if (!found) std::printf("Unexpected hint: %s\n", hint.c_str());
                    }
                    break; }
                ++index;
            }
            api->candidate_list_end(&it);
        }
        if (found) {
            api->select_candidate(session,index); RIME_STRUCT(RimeCommit, commit);
            found=api->get_commit(session,&commit) && commit.text && std::string(commit.text)==c.expected;
            api->free_commit(&commit);
        }
        std::printf("%s %s / %s\n",found?"PASS":"FAIL",c.schema,c.code);
        if (!found) ++failures; api->clear_composition(session);
    }
    for (const char* schema : {"rimes_wubi", "lingxi_wubi"}) {
        api->select_schema(session,schema);
        for (const char* code : {"voni", "wqiy", "zfiao", "fiao"}) {
            if (std::string(code)=="fiao" && std::string(schema)=="rimes_wubi") continue;
            api->clear_composition(session);
            bool passed=true; int typed=0;
            for (const char* key=code;*key;++key) {
                api->process_key(session,*key,0); ++typed;
                RIME_STRUCT(RimeCommit,commit);
                bool got=api->get_commit(session,&commit);
                const bool expected=std::string(code)=="voni" && typed==4;
                passed &= got==expected && (!got || std::string(commit.text)=="灵犀");
                if(got) api->free_commit(&commit);
            }
            std::printf("%s four-key unique %s / %s\n",passed?"PASS":"FAIL",schema,code);
            if (!passed) ++failures;
        }
    }
    api->destroy_session(session); api->finalize(); return failures ? 1 : 0;
}
