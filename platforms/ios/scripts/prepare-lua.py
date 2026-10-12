#!/usr/bin/env python3
"""Fetch pinned Lua sources and apply the reviewable mobile capability boundary."""
import hashlib, json, pathlib, re, shutil, subprocess, tarfile, urllib.request

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[2]
WORK = ROOT / 'Vendor/ios-build'
POLICY = HERE / 'lua-sandbox'

def prepare(work=WORK):
    lock = json.loads((POLICY / 'versions.json').read_text())
    plugin = work / 'librime/plugins/lua'
    revision = lock['librimeLua']['commit']
    if not (plugin / '.git').exists():
        subprocess.run(['git', 'clone', '--no-checkout', lock['librimeLua']['repository'], str(plugin)], check=True)
        subprocess.run(['git', '-C', str(plugin), 'checkout', '--detach', revision], check=True)
    head = subprocess.check_output(['git', '-C', str(plugin), 'rev-parse', 'HEAD'], text=True).strip()
    if head != revision:
        raise RuntimeError(f'librime-lua checkout is {head}, expected pinned {revision}; refusing to overwrite it')
    archive = work / f'lua-{lock["lua"]["version"]}.tar.gz'
    if not archive.exists(): urllib.request.urlretrieve(lock['lua']['url'], archive)
    if hashlib.sha256(archive.read_bytes()).hexdigest() != lock['lua']['sha256']:
        raise RuntimeError('Lua archive SHA256 mismatch')
    extracted = work / f'lua-{lock["lua"]["version"]}'
    with tarfile.open(archive) as files: files.extractall(work, filter='data')
    lua = plugin / 'thirdparty/lua5.4'
    lua.mkdir(parents=True, exist_ok=True)
    for source in (extracted / 'src').iterdir():
        if source.suffix in ('.c', '.h') and source.name not in ('lua.c', 'luac.c'):
            shutil.copy2(source, lua / source.name)
    for destination in [plugin / 'src', plugin / 'src/lib', lua]:
        shutil.copy2(POLICY / 'rimes_lua_sandbox.h', destination / 'rimes_lua_sandbox.h')
    shutil.copy2(POLICY / 'rimes_lua_sandbox.cc', plugin / 'src/rimes_lua_sandbox.cc')

    def upstream(name):
        return subprocess.check_output(['git', '-C', str(plugin), 'show', f'{revision}:{name}'], text=True)
    def replace(text, old, new):
        if old not in text: raise RuntimeError(f'Upstream patch context changed: {old[:80]}')
        return text.replace(old, new)
    def patch(name, substitutions, c_header=True):
        text = upstream(name)
        if c_header: text = '#include "rimes_lua_sandbox.h"\n' + text
        for old, new in substitutions: text = replace(text, old, new)
        (plugin / name).write_text(text)

    patch('CMakeLists.txt', [('LUA_USE_POSIX;LUA_USE_DLOPEN', 'LUA_USE_POSIX')], False)
    patch('src/modules.cc', [
        ('  types_init(L);', '  types_init(L);\n  rimes_lua_install(L);'),
        ('      const char *e = lua_tostring(L, -1);', '      const char *e = lua_tostring(L, -1);\n      rimes_lua_record_error(e);'),
        ('  lua_getfield(L, -2, "path");\n  lua_concat(L, 2);', '// Do not inherit process-wide LUA_PATH or system search locations.')])
    patch('src/lib/lua.cc', [
        ('L_ = luaL_newstate();', 'L_ = rimes_lua_newstate();'),
        ('lua_close(L_);', 'rimes_lua_close(L_);'),
        ('void Lua::to_state(std::function<void (lua_State *)> f) {',
         'void Lua::to_state(std::function<void (lua_State *)> f) {\n  RimesLuaBudgetScope budget(L_);')])
    patch('src/lib/lua_templates.h', [
        ('return LuaResult<O>::Err({status, e});', 'rimes_lua_record_error(e.c_str());\n    return LuaResult<O>::Err({status, e});'),
        ('return LuaResult<void>::Err({status, e});', 'rimes_lua_record_error(e.c_str());\n    return LuaResult<void>::Err({status, e});'),
        ('  int status = xlua_resume(C, 0);', '  RimesLuaBudgetScope budget(C);\n  int status = xlua_resume(C, 0);'),
         ('  int status = lua_pcall(L_, sizeof...(input)', '  RimesLuaBudgetScope budget(L_);\n  int status = lua_pcall(L_, sizeof...(input)')])
    patch('src/lua_gears.cc', [
        ('  int top = lua_gettop(L);', '  RimesLuaBudgetScope budget(L);\n  int top = lua_gettop(L);'),
        ('const char *e = lua_tostring(L, -1);', 'const char *e = lua_tostring(L, -1);\n      rimes_lua_record_error(e);'),
        ('      LOG(ERROR) << ostr.str();', '      rimes_lua_record_error(ostr.str().c_str());\n      LOG(ERROR) << ostr.str();'),
        ('  if (t.klass.empty()) {', '  if (t.klass.empty()) {\n    rimes_lua_record_error("Lua component has an empty module name");'),
        ('  if (lua_type(L, -1) != LUA_TFUNCTION) {', '  if (lua_type(L, -1) != LUA_TFUNCTION) {\n    rimes_lua_record_error(("Lua component has no callable function: " + t.klass).c_str());')])
    patch('src/types.cc', [
        ('return New<ReverseDb>(target_path);', 'auto safe_path = rimes_lua_path(std::filesystem::path(target_path).string());\n    return safe_path.empty() ? an<ReverseDb>() : New<ReverseDb>(target_path);'),
        ('    db->Load();', '    if (!db) return {};\n    db->Load();'),
        ('    return std::unique_ptr<T>(new T(schema_id));', '    if (!rimes_lua_resource_name(schema_id)) return {};\n    return std::unique_ptr<T>(new T(schema_id));'),
        ('      config->LoadFromFile(COMPAT<Deployer>::to_path(cstr));',
         '      auto safe_path = rimes_lua_path(cstr);\n      if (safe_path.empty()) return luaL_error(L, "RIMES: configuration path is outside engine directories");\n      config->LoadFromFile(COMPAT<Deployer>::to_path(safe_path));'),
        ('    return t.LoadFromFile(COMPAT<Deployer>::to_path(f));',
         '    auto safe_path = rimes_lua_path(f);\n    return !safe_path.empty() && t.LoadFromFile(COMPAT<Deployer>::to_path(safe_path));'),
        ('    return t.SaveToFile(COMPAT<Deployer>::to_path(f));',
         '    auto safe_path = rimes_lua_path(f, true);\n    return !safe_path.empty() && t.SaveToFile(COMPAT<Deployer>::to_path(safe_path));')])
    patch('src/types_ext.cc', [
        ('  an<T> make(const string& db_name, const string& db_class) {',
         '  an<T> make(const string& db_name, const string& db_class) {\n    if (!rimes_lua_userdb_name(db_name, db_class)) return {};'),
        ('  an<T> make(const string& dict_name) {',
         '  an<T> make(const string& dict_name) {\n    if (!rimes_lua_resource_name(dict_name)) return {};')])
    patch('src/opencc.cc', [
        ('  converter_ = config.NewFromFile(utf8_config_path);',
         '  auto safe_path = rimes_lua_path(utf8_config_path);\n  if (safe_path.empty()) throw std::runtime_error("RIMES: OpenCC path denied");\n  converter_ = config.NewFromFile(safe_path);')])

    for name in ['lauxlib.c', 'liolib.c', 'loadlib.c', 'loslib.c', 'lbaselib.c', 'lcorolib.c']:
        text = '#include "rimes_lua_sandbox.h"\n' + (lua / name).read_text()
        if name in ['lauxlib.c', 'liolib.c', 'loadlib.c']:
            text = text.replace('fopen(', 'rimes_lua_fopen(').replace('freopen(', 'rimes_lua_freopen(')
        if name == 'loadlib.c':
            # Preserve package.path ordering: a shared foo.lua must not mask an
            # existing user foo/init.lua merely because the first probe overlays.
            text = text.replace('rimes_lua_fopen(', 'rimes_lua_fopen_direct(')
        if name == 'lauxlib.c':
            text = replace(text, '  LoadF lf;', '  if (!filename) { lua_pushliteral(L, "RIMES: loading stdin is disabled"); return LUA_ERRFILE; }\n  mode = "t";\n  LoadF lf;')
        if name == 'liolib.c':
            text = replace(text, 'static int io_lines (lua_State *L) {', 'static int io_lines (lua_State *L) {\n  luaL_checkstring(L, 1);')
            text, count = re.subn(r'static int io_popen \(lua_State \*L\) \{.*?\n\}',
                                 'static int io_popen (lua_State *L) {\n  return luaL_error(L, "RIMES: subprocesses are disabled");\n}', text, flags=re.S)
            if count != 1: raise RuntimeError('io_popen patch context changed')
        if name == 'loslib.c':
            text = text.replace('remove(filename)', 'rimes_lua_remove(filename)').replace('rename(fromname, toname)', 'rimes_lua_rename(fromname, toname)')
            text, count = re.subn(r'static int os_execute \(lua_State \*L\) \{.*?\n\}',
                                 'static int os_execute (lua_State *L) {\n  return luaL_error(L, "RIMES: shell execution is disabled");\n}', text, flags=re.S)
            if count != 1: raise RuntimeError('os_execute patch context changed')
        if name == 'lbaselib.c':
            text = replace(text, 'const char *mode = luaL_optstring(L, 3, "bt");', 'const char *mode = "t"; /* source text only */')
            text = replace(text, 'static int finishpcall (lua_State *L, int status, lua_KContext extra) {',
                           'static int finishpcall (lua_State *L, int status, lua_KContext extra) {\n  rimes_lua_check_budget(L);')
        if name == 'lcorolib.c':
            text = replace(text, '  status = lua_resume(co, L, narg, &nres);', '  status = lua_resume(co, L, narg, &nres);\n  rimes_lua_check_budget(L);')
        (lua / name).write_text(text)
    print(f'Prepared librime-lua {revision} + Lua {lock["lua"]["version"]} with mobile sandbox')

if __name__ == '__main__': prepare()
