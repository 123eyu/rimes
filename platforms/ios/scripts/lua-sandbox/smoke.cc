// Run against the exact host library produced by build-engine.py.
#include "rimes_lua_sandbox.h"
#include <rime_api.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstdlib>
extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}
void types_init(lua_State *L);

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    std::cout << "Policy: 50M VM instructions / 1 second per entry; 32 MiB Lua allocation" << std::endl;
    std::filesystem::path root(argv[1]);
    std::filesystem::create_directories(root / "user/lua");
    std::filesystem::create_directories(root / "shared/lua");
    std::ofstream(root / "shared/lua/safe_module.lua") << "return {value=42}";
    std::ofstream(root / "shared/fallback.txt") << "shared source";
    std::ofstream(root / "shared/override.txt") << "shared original";
    std::ofstream(root / "user/override.txt") << "user override";
    std::filesystem::create_directories(root / "user/lua/priority");
    std::ofstream(root / "user/lua/priority/init.lua") << "return 'user init'";
    std::ofstream(root / "shared/lua/priority.lua") << "return 'shared module'";
    std::ofstream(root / "outside.txt") << "private";
    std::filesystem::create_symlink(root / "outside.txt", root / "user/escape.txt");
    std::filesystem::create_symlink(root / "outside.txt", root / "shared/escape.txt");
    std::filesystem::create_symlink(root / "outside.txt", root / "shared/fallback-escape.txt");
    std::filesystem::create_symlink(root / "outside-created.txt", root / "user/dangling.txt");
    std::filesystem::create_directory_symlink(root, root / "user/outside-link");
    std::filesystem::create_symlink(root / "absent-db", root / "user/dangling.userdb");
    std::filesystem::create_symlink(root / "outside.txt", root / "user/escaped-text.userdb.txt");
    std::filesystem::create_symlink(root / "absent-text", root / "user/dangling-text.userdb.txt");
    const auto user = (root / "user").string(), shared = (root / "shared").string();
    RIME_STRUCT(RimeTraits, traits);
    traits.user_data_dir = user.c_str(); traits.shared_data_dir = shared.c_str();
    traits.app_name = "rimes.lua.sandbox.tests";
    const char *modules[] = {"default", "lua", "deployer", nullptr}; traits.modules = modules;
    auto api = rime_get_api(); api->setup(&traits); api->initialize(&traits);

    auto L = rimes_lua_newstate(); luaL_openlibs(L); types_init(L); rimes_lua_install(L);
    lua_pushstring(L, user.c_str()); lua_setglobal(L, "userdir");
    lua_pushstring(L, shared.c_str()); lua_setglobal(L, "shareddir");
    auto check = [&](const char *label, const char *source, const char *expectedError = nullptr) {
        lua_settop(L, 0);
        int status = luaL_loadstring(L, source);
        if (!status) { RimesLuaBudgetScope budget(L); status = lua_pcall(L, 0, 0, 0); }
        const std::string error = status && lua_tostring(L, -1) ? lua_tostring(L, -1) : "";
        const bool ok = expectedError ? status && error.find(expectedError) != std::string::npos : !status;
        std::cout << (ok ? "PASS " : "FAIL ") << label << (ok ? "" : ": " + error) << std::endl;
        if (!ok) std::exit(1);
    };
    check("user files, shared source require, utf8 and date", R"(
        local f=assert(io.open(userdir..'/words.txt','w')); f:write('你好'); f:close()
        local f=assert(io.open(userdir..'/words.txt','r')); assert(f:read('*a')=='你好'); f:close()
        package.path=shareddir..'/lua/?.lua'; assert(require('safe_module').value==42)
        assert(utf8.len('你好')==2); assert(type(os.date('%Y'))=='string')
        assert(debug.getinfo(1).source)
        assert(dofile(shareddir..'/lua/safe_module.lua').value==42)
        assert(loadfile(shareddir..'/lua/safe_module.lua')().value==42)
    )");
    check("file traversal, shared writes and symlink escape denied", R"(
        assert(not io.open('/etc/passwd','r'))
        assert(not io.open(userdir..'/../outside.txt','r'))
        assert(not io.open(userdir..'/escape.txt','r'))
        assert(not io.open(userdir..'/dangling.txt','w'))
        assert(not io.open(shareddir..'/new.txt','w'))
        assert(not os.remove(shareddir..'/lua/safe_module.lua'))
        assert(not os.rename(userdir..'/words.txt',shareddir..'/moved.txt'))
        assert(not loadfile('/etc/passwd'))
        assert(not pcall(dofile,'/etc/passwd'))
        package.path='/etc/?.lua'; assert(not pcall(require,'passwd'))
    )");
    check("missing user resources read shared without copying", R"(
        local f=assert(io.open(userdir..'/fallback.txt','r')); assert(f:read('*a')=='shared source'); f:close()
        assert(io.lines(userdir..'/fallback.txt')()=='shared source')
        local f=assert(io.open(userdir..'/override.txt','r')); assert(f:read('*a')=='user override'); f:close()
        assert(loadfile(userdir..'/lua/safe_module.lua')().value==42)
        assert(dofile(userdir..'/lua/safe_module.lua').value==42)
        package.path=userdir..'/lua/?.lua;'..userdir..'/lua/?/init.lua;'..shareddir..'/lua/?.lua'
        assert(require('priority')=='user init')
        local f=assert(io.open(userdir..'/fallback.txt','w')); f:write('user copy'); f:close()
        local f=assert(io.open(userdir..'/fallback.txt','r')); assert(f:read('*a')=='user copy'); f:close()
        local f=assert(io.open(shareddir..'/fallback.txt','r')); assert(f:read('*a')=='shared source'); f:close()
        assert(not io.open(userdir..'/new/../../outside.txt','r'))
        assert(not io.open(userdir..'/escape.txt','r'))
        assert(not io.open(shareddir..'/escape.txt','r'))
        assert(not io.open(userdir..'/fallback-escape.txt','r'))
    )");
    check("process/native/network/debug escape denied", R"(
        assert(not pcall(os.execute,'touch /tmp/rimes-lua-escape'))
        assert(not pcall(io.popen,'id'))
        assert(not pcall(os.exit,0)); assert(not pcall(os.getenv,'HOME'))
        assert(not pcall(package.loadlib,'/usr/lib/libSystem.B.dylib','system'))
        assert(not pcall(require,'socket')); assert(not pcall(require,'ffi'))
        assert(not debug.getregistry and not debug.getupvalue and not debug.sethook)
        assert(require('debug')==debug)
        assert(not pcall(io.lines)); assert(not pcall(io.input))
        assert(not pcall(io.output)); assert(not pcall(io.tmpfile))
        assert(not load(string.dump(function() return 1 end),'binary','b'))
    )");
    check("native Rime file bindings bounded", R"(
        assert(not pcall(Config,'/etc/passwd'))
        assert(not Config():load_from_file('/etc/passwd'))
        assert(not Config():save_to_file(shareddir..'/blocked.yaml'))
        assert(not UserDb('../../outside','userdb'))
        assert(not LevelDb('../../outside')); assert(not TableDb('../../outside'))
        assert(not ReverseLookup('../../outside'))
        assert(not ReverseDb('../../outside'))
        assert(not Schema('../../outside'))
    )");
    check("namespaced user databases preserve upstream paths inside user data", R"(
        local db = assert(UserDb('lua/sequence', 'userdb'))
        assert(db:open()); assert(db:update('test-key', 'test-value'))
        assert(db:fetch('test-key') == 'test-value'); db:close()
        local text = assert(UserDb('nested/phrases', 'plain_userdb'))
        assert(text:open()); assert(text:update('word', 'value')); text:close()
        assert(not UserDb('/absolute', 'userdb'))
        assert(not UserDb('lua/../outside', 'userdb'))
        assert(not UserDb('lua//empty', 'userdb'))
        assert(not UserDb('lua/./dot', 'userdb'))
        assert(not UserDb('outside-link/escaped', 'userdb'))
        assert(not UserDb('dangling', 'userdb'))
        assert(not TableDb('escaped-text')); assert(not TableDb('dangling-text'))
        assert(not UserDb('lua/sequence', '../../escape'))
    )");
    check("plain infinite loop bounded", "while true do end", "budget exceeded");
    check("pcall cannot swallow execution budget", "while true do pcall(function() while true do end end) end", "budget exceeded");
    check("xpcall cannot swallow execution budget", "while true do xpcall(function() while true do end end,function(e) return e end) end", "budget exceeded");
    check("coroutine cannot swallow execution budget", "while true do coroutine.resume(coroutine.create(function() while true do end end)) end", "budget exceeded");
    check("Lua allocation bounded", "local x=string.rep('x',40*1024*1024)", "memory");
    check("engine remains usable after rejected scripts", "assert(1+1==2)");
    rimes_lua_close(L); api->finalize();
    return 0;
}
