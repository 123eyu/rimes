// Deploy and load a staged scheme package the way the iOS app does.
//
// Mirrors RimeMobile.deployResources / validateResources in
// platforms/ios/Bridge/RimeMobile.mm: the staged package is both the shared
// and the user data directory, only the default, lua and deployer modules are
// available, and a scheme counts as loaded when a session can select it
// without the restricted Lua runtime recording an error.
//
// Build against the host slice of the iOS engine (platforms/ios/scripts/
// build-engine.py host). Output is one JSON object per line.
//
//   host_probe <staged-root> deploy <schema>...
//   host_probe <staged-root> load   <schema>...
//   host_probe <staged-root> type   <schema> <keys>
#include <rime_api.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#ifdef RIMES_NATIVE_LUA_PLUGIN
// Desktop cross-check only: the stock Lua plugin has no mobile error recorder.
// Inspect stderr as well as candidate output; do not equate an empty luaError
// field in this build with the instrumented iOS runtime's clean-error guarantee.
#include <dlfcn.h>
static const char* rimes_lua_last_error() { return ""; }
static void rimes_lua_clear_error() {}
#else
extern "C" const char* rimes_lua_last_error(void);
extern "C" void rimes_lua_clear_error(void);
#endif

namespace {

std::string escaped(const char* text) {
    std::string out;
    for (const char* c = text ? text : ""; *c; ++c) {
        switch (*c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(*c) < 0x20) {
                    char buffer[8];
                    std::snprintf(buffer, sizeof buffer, "\\u%04x", *c);
                    out += buffer;
                } else {
                    out += *c;
                }
        }
    }
    return out;
}

RimeApi* start(const std::string& shared, const std::string& user, bool deployment) {
    static const char* runtime[] = {"default", "lua", nullptr};
    static const char* deploying[] = {"default", "lua", "deployer", nullptr};
    static std::string prebuilt;
    prebuilt = shared + "/build";
    RIME_STRUCT(RimeTraits, traits);
    traits.shared_data_dir = shared.c_str();
    traits.user_data_dir = user.c_str();
    traits.prebuilt_data_dir = prebuilt.c_str();
    traits.staging_dir = prebuilt.c_str();
    traits.distribution_name = "RIMES host probe";
    traits.distribution_code_name = "rimes_host_probe";
    traits.distribution_version = "0.1";
    traits.app_name = "rime.rimes_host_probe";
    traits.min_log_level = 3;
#ifdef RIMES_NATIVE_LUA_PLUGIN
    traits.min_log_level = 2;
#endif
    traits.modules = deployment ? deploying : runtime;
    RimeApi* api = rime_get_api();
    api->setup(&traits);
    api->initialize(&traits);
    return api;
}

std::string lua_error() {
    const char* error = rimes_lua_last_error();
    return error ? error : "";
}

}  // namespace

int main(int argc, char** argv) {
#ifdef RIMES_NATIVE_LUA_PLUGIN
    if (!dlopen(RIMES_NATIVE_LUA_PLUGIN, RTLD_NOW | RTLD_GLOBAL)) {
        std::fprintf(stderr, "cannot load desktop Lua plugin: %s\n", dlerror());
        return 2;
    }
#endif
    if (argc < 4) {
        std::fprintf(stderr, "usage: host_probe <staged-root> deploy|load|type <schema>... [keys [enabled-option...]]\n");
        return 2;
    }
    const std::string root = argv[1], mode = argv[2];
    int failures = 0;
    if (mode == "deploy") {
        RimeApi* api = start(root, root, true);
        const bool defaults = api->deploy_config_file("default.yaml", "config_version");
        std::printf("{\"step\":\"deploy\",\"target\":\"default.yaml\",\"ok\":%s}\n", defaults ? "true" : "false");
        failures += !defaults;
        for (int i = 3; i < argc; ++i) {
            const std::string path = root + "/" + argv[i] + ".schema.yaml";
            const bool ok = api->deploy_schema(path.c_str());
            const bool compiled = std::filesystem::exists(root + "/build/" + argv[i] + ".schema.yaml");
            std::printf("{\"step\":\"deploy\",\"target\":\"%s\",\"ok\":%s,\"compiled\":%s}\n",
                        escaped(argv[i]).c_str(), ok ? "true" : "false", compiled ? "true" : "false");
            failures += !(ok && compiled);
        }
        api->finalize();
    } else if (mode == "load") {
        const std::string user = root + "/.validation-user";
        std::filesystem::create_directories(user);
        RimeApi* api = start(root, user, false);
        for (int i = 3; i < argc; ++i) {
            rimes_lua_clear_error();
            const RimeSessionId session = api->create_session();
            const bool selected = session && api->select_schema(session, argv[i]);
            const std::string error = lua_error();
            const bool ok = selected && error.empty();
            std::printf("{\"step\":\"load\",\"target\":\"%s\",\"ok\":%s,\"luaError\":\"%s\"}\n",
                        escaped(argv[i]).c_str(), ok ? "true" : "false", escaped(error.c_str()).c_str());
            failures += !ok;
            if (session) api->destroy_session(session);
        }
        api->finalize();
        std::filesystem::remove_all(user);
    } else if (mode == "type" && argc >= 5) {
        const std::string user = root + "/.validation-user";
        std::filesystem::create_directories(user);
        RimeApi* api = start(root, user, false);
        rimes_lua_clear_error();
        const RimeSessionId session = api->create_session();
        const bool selected = session && api->select_schema(session, argv[3]);
        std::string committed;
        if (selected) {
            for (int i = 5; i < argc; ++i) api->set_option(session, argv[i], true);
            for (const char* key = argv[4]; *key; ++key) {
                api->process_key(session, *key, 0);
                RIME_STRUCT(RimeCommit, commit);
                if (api->get_commit(session, &commit)) {
                    if (commit.text) committed += commit.text;
                    api->free_commit(&commit);
                }
            }
        }
        std::string preedit;
        RIME_STRUCT(RimeContext, context);
        if (selected && api->get_context(session, &context)) {
            if (context.composition.preedit) preedit = context.composition.preedit;
            api->free_context(&context);
        }
        std::vector<std::string> candidates;
        RimeCandidateListIterator iterator = {};
        if (selected && api->candidate_list_begin(session, &iterator)) {
            while (candidates.size() < 10 && api->candidate_list_next(&iterator)) {
                if (iterator.candidate.text) candidates.emplace_back(iterator.candidate.text);
            }
            api->candidate_list_end(&iterator);
        }
        const std::string error = lua_error();
        std::printf("{\"step\":\"type\",\"target\":\"%s\",\"keys\":\"%s\",\"ok\":%s,\"commit\":\"%s\",\"preedit\":\"%s\",\"luaError\":\"%s\",\"candidates\":[",
                    escaped(argv[3]).c_str(), escaped(argv[4]).c_str(), selected ? "true" : "false",
                    escaped(committed.c_str()).c_str(), escaped(preedit.c_str()).c_str(), escaped(error.c_str()).c_str());
        for (size_t i = 0; i < candidates.size(); ++i) {
            std::printf("%s\"%s\"", i ? "," : "", escaped(candidates[i].c_str()).c_str());
        }
        std::printf("]}\n");
        failures += !selected || !error.empty();
        if (session) api->destroy_session(session);
        api->finalize();
        std::filesystem::remove_all(user);
    } else {
        std::fprintf(stderr, "unknown mode %s\n", mode.c_str());
        return 2;
    }
    return failures ? 1 : 0;
}
