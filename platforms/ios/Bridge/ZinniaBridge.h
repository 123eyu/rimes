#import <Foundation/Foundation.h>
#import <CoreGraphics/CoreGraphics.h>

NS_ASSUME_NONNULL_BEGIN

/// Objective-C wrapper over zinnia's online handwriting recognizer (ThirdParty/zinnia).
/// One instance per model; not thread-safe, so callers keep it on one queue.
@interface RIMESZinniaRecognizer : NSObject
- (nullable instancetype)initWithModelPath:(NSString *)path NS_DESIGNATED_INITIALIZER;
- (instancetype)init NS_UNAVAILABLE;
/// `strokes`: each stroke is a flat [x0, y0, x1, y1, …] list inside a `width`×`height` box,
/// in writing order. Returns up to `limit` characters, best first.
- (NSArray<NSString *> *)classifyStrokes:(NSArray<NSArray<NSNumber *> *> *)strokes
                                   width:(CGFloat)width height:(CGFloat)height limit:(NSInteger)limit;
@end

NS_ASSUME_NONNULL_END
