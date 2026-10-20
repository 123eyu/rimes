#import <Foundation/Foundation.h>
NS_ASSUME_NONNULL_BEGIN
@interface RimeMobile : NSObject
- (nullable instancetype)initWithResources:(NSString *)resources userDirectory:(NSString *)directory;
- (BOOL)selectSchema:(NSString *)schema;
- (void)setInputOptions:(NSDictionary<NSString *, NSNumber *> *)options;
+ (NSDictionary *)deployResources:(NSString *)resources schemaIDs:(NSArray<NSString *> *)schemaIDs;
+ (NSDictionary *)validateResources:(NSString *)resources schemaIDs:(NSArray<NSString *> *)schemaIDs;
/// Read a deployed schema without starting, switching or compiling an engine.
+ (NSDictionary *)keyboardSchemaInfoAtPath:(NSString *)path;
- (NSDictionary *)processKey:(int32_t)key;
- (NSDictionary *)selectCandidate:(NSUInteger)index;
- (void)clear;
/// Release this session's user-dictionary locks before the keyboard is suspended.
/// The selected schema is retained and recreated lazily by the next engine call.
- (void)suspend;
@property(nonatomic, readonly) NSString *rawInput;
@property(nonatomic, readonly) NSUInteger inputCaret;
@property(nonatomic, readonly) NSString *lastError;
@end
NS_ASSUME_NONNULL_END
