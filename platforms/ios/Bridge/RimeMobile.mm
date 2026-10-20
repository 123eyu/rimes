#import "RimeMobile.h"
#include <rime_api.h>
#include <string>
#include <mutex>
#include "../../../Shared/InputHabits/rime_options.hpp"
extern "C" const char *rimes_lua_last_error(void);
extern "C" void rimes_lua_clear_error(void);

namespace {
std::recursive_mutex engineMutex;
std::string activeResources, activeUser;
NSUInteger engineGeneration = 0;
bool engineInitialized = false;
const char *runtimeModules[] = {"default", "lua", nullptr};
const char *deploymentModules[] = {"default", "lua", "deployer", nullptr};

void shutdownEngine() {
    if (engineInitialized) rime_get_api()->finalize();
    engineInitialized = false; activeResources.clear(); activeUser.clear(); ++engineGeneration;
}
void activateEngine(NSString *resources, NSString *directory, bool deployment = false) {
    if (engineInitialized && activeResources == resources.UTF8String && activeUser == directory.UTF8String && !deployment) return;
    shutdownEngine();
    activeResources = resources.UTF8String; activeUser = directory.UTF8String;
    std::string prebuilt = activeResources + "/build";
    RIME_STRUCT(RimeTraits, traits);
    traits.shared_data_dir = activeResources.c_str(); traits.user_data_dir = activeUser.c_str();
    traits.prebuilt_data_dir = prebuilt.c_str(); traits.staging_dir = prebuilt.c_str();
    traits.distribution_name = "RIMES iOS"; traits.distribution_code_name = "rimes_ios";
    traits.distribution_version = "0.1"; traits.app_name = "rime.rimes_ios"; traits.min_log_level = 3;
    traits.modules = deployment ? deploymentModules : runtimeModules;
    rime_get_api()->setup(&traits); rime_get_api()->initialize(&traits);
    engineInitialized = true;
}
}


// A process-global engine is serialized. Sessions carry a generation so switching
// an isolated package or deploying in the main app never reuses a retired session.
@implementation RimeMobile {
    RimeSessionId _session;
    NSUInteger _generation;
    NSString *_resources;
    NSString *_directory;
    NSString *_schema;
    NSDictionary<NSString *, NSNumber *> *_inputOptions;
    std::map<std::string, bool> _inputBaseline;
    std::set<std::string> _inputApplied;
    BOOL _inputPending;
}
- (instancetype)initWithResources:(NSString *)resources userDirectory:(NSString *)directory {
    self = [super init];
    if (!self) return nil;
    _resources = [resources copy]; _directory = [directory copy];
    std::lock_guard<std::recursive_mutex> lock(engineMutex);
    return [self ensureSession] ? self : nil;
}
- (BOOL)ensureSession {
    activateEngine(_resources, _directory);
    if (_session && _generation == engineGeneration) return YES;
    rimes_lua_clear_error();
    _session = rime_get_api()->create_session(); _generation = engineGeneration;
    if (_session && rimes_lua_last_error() && rimes_lua_last_error()[0]) {
        rime_get_api()->destroy_session(_session); _session = 0; return NO;
    }
    if (_session && _schema) {
        BOOL valid = rime_get_api()->select_schema(_session, _schema.UTF8String);
        if (!valid || (rimes_lua_last_error() && rimes_lua_last_error()[0])) {
            rime_get_api()->destroy_session(_session); _session = 0;
            return NO;
        }
    }
    _inputBaseline.clear(); _inputApplied.clear(); _inputPending = YES;
    [self applyInputOptions];
    return _session != 0;
}
- (void)dealloc {
    std::lock_guard<std::recursive_mutex> lock(engineMutex);
    if (_session && _generation == engineGeneration) rime_get_api()->destroy_session(_session);
}
- (BOOL)selectSchema:(NSString *)schema {
    std::lock_guard<std::recursive_mutex> lock(engineMutex);
    if (![self ensureSession]) return NO;
    rime_get_api()->clear_composition(_session);
    rimes_lua_clear_error();
    BOOL selected = rime_get_api()->select_schema(_session, schema.UTF8String);
    if (rimes_lua_last_error() && rimes_lua_last_error()[0]) selected = NO;
    if (selected) {
        _schema = [schema copy]; _inputBaseline.clear(); _inputApplied.clear(); _inputPending = YES;
        [self applyInputOptions];
    }
    return selected;
}
- (void)setInputOptions:(NSDictionary<NSString *, NSNumber *> *)options {
    std::lock_guard<std::recursive_mutex> lock(engineMutex);
    _inputOptions = [options copy]; _inputPending = YES;
    if (_session && _generation == engineGeneration) [self applyInputOptions];
}
- (void)applyInputOptions {
    if (!_inputPending || !_session || !_schema) return;
    auto api = rime_get_api();
    const char *input = api->get_input(_session);
    if (input && input[0]) return;
    if (_inputBaseline.empty()) {
        for (const auto& option : xsime::input::Switches<RimeConfig>(api, _schema.UTF8String))
            _inputBaseline[option.name] = api->get_option(_session, option.name.c_str());
    }
    for (const auto& [name, baseline] : _inputBaseline) {
        NSNumber *value = _inputOptions[[NSString stringWithUTF8String:name.c_str()]];
        if (value) { api->set_option(_session, name.c_str(), value.boolValue); _inputApplied.insert(name); }
        else if (_inputApplied.erase(name)) api->set_option(_session, name.c_str(), baseline);
    }
    _inputPending = NO;
}
+ (NSDictionary *)deployResources:(NSString *)resources schemaIDs:(NSArray<NSString *> *)schemaIDs {
    std::lock_guard<std::recursive_mutex> lock(engineMutex);
    NSMutableArray *failures = [NSMutableArray array];
    @try {
        try {
            activateEngine(resources, resources, true);
            if (!rime_get_api()->deploy_config_file("default.yaml", "config_version")) [failures addObject:@"default.yaml"];
            for (NSString *schema in schemaIDs) {
                NSString *path = [resources stringByAppendingPathComponent:[schema stringByAppendingString:@".schema.yaml"]];
                if (!rime_get_api()->deploy_schema(path.UTF8String)) [failures addObject:schema];
            }
        } catch (const std::exception &exception) {
            [failures addObject:[NSString stringWithUTF8String:exception.what()] ?: @"Rime deployment failed"];
        } catch (...) { [failures addObject:@"Rime deployment failed"]; }
    } @catch (NSException *exception) { [failures addObject:exception.reason ?: @"Rime deployment failed"]; }
    shutdownEngine();
    return @{@"success": @(failures.count == 0), @"failures": failures};
}
+ (NSDictionary *)validateResources:(NSString *)resources schemaIDs:(NSArray<NSString *> *)schemaIDs {
    std::lock_guard<std::recursive_mutex> lock(engineMutex);
    NSMutableArray *failures = [NSMutableArray array];
    NSString *user = [resources stringByAppendingPathComponent:@".validation-user"];
    [[NSFileManager defaultManager] createDirectoryAtPath:user withIntermediateDirectories:YES attributes:nil error:nil];
    @try {
        try {
            activateEngine(resources, user);
            for (NSString *schema in schemaIDs) {
                rimes_lua_clear_error();
                RimeSessionId session = rime_get_api()->create_session();
                BOOL valid = session && rime_get_api()->select_schema(session, schema.UTF8String);
                if (valid) {
                    rime_get_api()->set_option(session, "ascii_mode", false);
                    for (const char *sample : {"a", "nihao", "1234567890"}) {
                        for (const char *key = sample; *key; ++key) rime_get_api()->process_key(session, *key, 0);
                        rime_get_api()->clear_composition(session);
                    }
                }
                if (session) rime_get_api()->destroy_session(session);
                NSString *luaError = [NSString stringWithUTF8String:rimes_lua_last_error() ?: ""];
                if (!valid || luaError.length) [failures addObject:[NSString stringWithFormat:@"%@: %@", schema, luaError.length ? luaError : @"无法加载已部署方案"]];
            }
        } catch (const std::exception &exception) {
            [failures addObject:[NSString stringWithUTF8String:exception.what()] ?: @"Rime validation failed"];
        } catch (...) { [failures addObject:@"Rime validation failed"]; }
    } @catch (NSException *exception) { [failures addObject:exception.reason ?: @"Rime validation failed"]; }
    shutdownEngine();
    [[NSFileManager defaultManager] removeItemAtPath:user error:nil];
    return @{@"success": @(failures.count == 0), @"failures": failures};
}
+ (NSDictionary *)keyboardSchemaInfoAtPath:(NSString *)path {
    std::lock_guard<std::recursive_mutex> lock(engineMutex);
    // Parse only the deployed, resolved YAML. Looking at the archive's source
    // would miss custom patches and includes; opening an engine would retire
    // an existing composition just to refresh a layout menu.
    NSDictionary *attributes = [[NSFileManager defaultManager] attributesOfItemAtPath:path error:nil];
    if (![attributes[NSFileType] isEqual:NSFileTypeRegular] ||
        [attributes[NSFileSize] unsignedLongLongValue] > 1024 * 1024) return @{};
    NSString *yaml = [NSString stringWithContentsOfFile:path encoding:NSUTF8StringEncoding error:nil];
    if (!yaml) return @{};
    RimeConfig config = {0};
    auto api = rime_get_api();
    @try {
        try {
            if (!api->config_load_string(&config, yaml.UTF8String)) {
                if (config.ptr) api->config_close(&config);
                return @{};
            }
            auto string = [&](const char *key) -> NSString * {
                const char *value = api->config_get_cstring(&config, key);
                return value ? ([NSString stringWithUTF8String:value] ?: @"") : @"";
            };
            auto list = [&](const char *key) -> NSArray<NSString *> * {
                NSMutableArray *values = [NSMutableArray array];
                size_t count = api->config_list_size(&config, key);
                for (size_t i = 0; i < count && i < 4096; ++i) {
                    std::string item = std::string(key) + "/@" + std::to_string(i);
                    [values addObject:string(item.c_str())];
                }
                return values;
            };
            NSString *alphabet = string("speller/alphabet"), *initials = string("speller/initials");
            BOOL script = [list("engine/translators") containsObject:@"script_translator"];
            BOOL t9 = NO;
            // Require the standard letter-to-digit conversion to be the final
            // algebra rule: a later rule could erase or reinterpret the digits.
            for (NSString *rule in list("speller/algebra")) {
                NSArray<NSString *> *parts = [rule componentsSeparatedByString:@"/"];
                t9 = parts.count == 4 && [parts[0] isEqual:@"xlit"] &&
                    ([parts[1] isEqual:@"ABCDEFGHIJKLMNOPQRSTUVWXYZ"] ||
                     [parts[1] isEqual:@"abcdefghijklmnopqrstuvwxyz"]) &&
                    [parts[2] isEqual:@"22233344455566677778889999"];
            }
            for (unichar c : std::string("abcdefghijklmnopqrstuvwxyz23456789")) {
                if ([alphabet rangeOfString:[NSString stringWithFormat:@"%C", c]].location == NSNotFound) t9 = NO;
            }
            for (unichar c : std::string("23456789")) {
                if (initials.length && [initials rangeOfString:[NSString stringWithFormat:@"%C", c]].location == NSNotFound) t9 = NO;
            }
            t9 = t9 && script && [string("speller/delimiter") containsString:@"'"];
            NSMutableArray *switches = [NSMutableArray array];
            for (const auto& option : xsime::input::ReadSwitches(api, config)) {
                [switches addObject:@{@"name": [NSString stringWithUTF8String:option.name.c_str()],
                    @"off": [NSString stringWithUTF8String:option.off.c_str()], @"on": [NSString stringWithUTF8String:option.on.c_str()]}];
            }
            NSDictionary *result = @{@"schemaID":string("schema/schema_id"),
                @"dictionary":string("translator/dictionary"), @"script":@(script), @"nineKey":@(t9), @"inputOptions": switches};
            api->config_close(&config);
            return result;
        } catch (...) {}
    } @catch (NSException *exception) {}
    if (config.ptr) api->config_close(&config);
    return @{};
}
- (NSString *)lastError {
    std::lock_guard<std::recursive_mutex> lock(engineMutex);
    return [NSString stringWithUTF8String:rimes_lua_last_error() ?: ""] ?: @"";
}
- (NSDictionary *)snapshot {
    NSMutableArray *candidates = [NSMutableArray array], *readings = [NSMutableArray array]; NSString *preedit = @"", *committed = @"";
    RIME_STRUCT(RimeCommit, commit);
    if (rime_get_api()->get_commit(_session, &commit)) {
        if (commit.text) committed = [NSString stringWithUTF8String:commit.text] ?: @"";
        rime_get_api()->free_commit(&commit);
    }
    RIME_STRUCT(RimeContext, context);
    if (rime_get_api()->get_context(_session, &context)) {
        if (context.composition.preedit) preedit = [NSString stringWithUTF8String:context.composition.preedit] ?: @"";
        rime_get_api()->free_context(&context);
    }
    RimeCandidateListIterator iterator = {0};
    if (rime_get_api()->candidate_list_begin(_session, &iterator)) {
        while (candidates.count < 60 && rime_get_api()->candidate_list_next(&iterator)) {
            if (iterator.candidate.text) {
                [candidates addObject:[NSString stringWithUTF8String:iterator.candidate.text] ?: @""];
                [readings addObject:iterator.candidate.comment ? ([NSString stringWithUTF8String:iterator.candidate.comment] ?: @"") : @""];
            }
        }
        rime_get_api()->candidate_list_end(&iterator);
    }
    return @{@"preedit":preedit, @"commit":committed, @"candidates":candidates, @"readings":readings};
}
- (NSDictionary *)processKey:(int32_t)key {
    std::lock_guard<std::recursive_mutex> lock(engineMutex);
    if (![self ensureSession]) return @{@"handled": @NO, @"error": self.lastError};
    [self applyInputOptions];
    rimes_lua_clear_error();
    BOOL handled = rime_get_api()->process_key(_session, key, 0);
    NSMutableDictionary *snapshot = [[self snapshot] mutableCopy]; snapshot[@"handled"] = @(handled);
    if (self.lastError.length) snapshot[@"error"] = self.lastError;
    return snapshot;
}
- (NSDictionary *)selectCandidate:(NSUInteger)index {
    std::lock_guard<std::recursive_mutex> lock(engineMutex);
    if (![self ensureSession]) return @{@"error": self.lastError};
    rimes_lua_clear_error();
    rime_get_api()->select_candidate(_session, index);
    NSMutableDictionary *snapshot = [[self snapshot] mutableCopy];
    if (self.lastError.length) snapshot[@"error"] = self.lastError;
    return snapshot;
}
- (void)clear {
    std::lock_guard<std::recursive_mutex> lock(engineMutex);
    // Clearing a retired view must not switch the active package back.
    if (_session && _generation == engineGeneration) rime_get_api()->clear_composition(_session);
}
- (void)suspend {
    std::lock_guard<std::recursive_mutex> lock(engineMutex);
    // Clearing composition leaves the user database open with its POSIX file
    // lock held. Retire this session before the host can suspend the extension.
    // A stale instance must never destroy a new generation's reused session ID.
    if (_session && _generation == engineGeneration) rime_get_api()->destroy_session(_session);
    _session = 0;
}
- (NSString *)rawInput {
    std::lock_guard<std::recursive_mutex> lock(engineMutex);
    if (!_session || _generation != engineGeneration) return @"";
    const char *input = rime_get_api()->get_input(_session);
    return input ? ([NSString stringWithUTF8String:input] ?: @"") : @"";
}
- (NSUInteger)inputCaret {
    std::lock_guard<std::recursive_mutex> lock(engineMutex);
    return _session && _generation == engineGeneration ? rime_get_api()->get_caret_pos(_session) : 0;
}
@end
