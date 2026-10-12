// RIMES mobile Lua capability boundary. MIT, Copyright 2026 scholay.
#ifndef RIMES_LUA_SANDBOX_H
#define RIMES_LUA_SANDBOX_H
#include <stdio.h>
#ifdef __cplusplus
#include <string>
extern "C" {
#endif
struct lua_State;
FILE *rimes_lua_fopen(const char *name, const char *mode);
FILE *rimes_lua_fopen_direct(const char *name, const char *mode);
FILE *rimes_lua_freopen(const char *name, const char *mode, FILE *stream);
int rimes_lua_remove(const char *name);
int rimes_lua_rename(const char *from, const char *to);
void rimes_lua_check_budget(struct lua_State *L);
struct lua_State *rimes_lua_newstate(void);
void rimes_lua_close(struct lua_State *L);
void rimes_lua_install(struct lua_State *L);
void rimes_lua_begin(struct lua_State *L);
void rimes_lua_end(void);
// Diagnostics are sticky until cleared by the serialized bridge operation.
const char *rimes_lua_last_error(void);
void rimes_lua_clear_error(void);
void rimes_lua_record_error(const char *message);
#ifdef __cplusplus
}
std::string rimes_lua_path(const std::string &name, bool write = false, bool sharedFallback = true);
bool rimes_lua_resource_name(const std::string &name);
bool rimes_lua_userdb_name(const std::string &name, const std::string &dbClass);
class RimesLuaBudgetScope {
public:
    explicit RimesLuaBudgetScope(lua_State *L) { rimes_lua_begin(L); }
    ~RimesLuaBudgetScope() { rimes_lua_end(); }
};
#endif
#endif
