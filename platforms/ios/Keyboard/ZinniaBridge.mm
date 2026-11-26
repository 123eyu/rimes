#import "ZinniaBridge.h"
#include <memory>
#include "../ThirdParty/zinnia/zinnia.h"

@implementation RIMESZinniaRecognizer {
    zinnia::Recognizer *_recognizer;
}

- (nullable instancetype)initWithModelPath:(NSString *)path {
    if (!(self = [super init])) return nil;
    _recognizer = zinnia::Recognizer::create();
    // The model is memory-mapped; opening it costs little until pages are touched.
    if (!_recognizer || !_recognizer->open(path.fileSystemRepresentation)) {
        delete _recognizer; _recognizer = nullptr;
        return nil;
    }
    return self;
}

- (void)dealloc { delete _recognizer; }

- (NSArray<NSString *> *)classifyStrokes:(NSArray<NSArray<NSNumber *> *> *)strokes
                                   width:(CGFloat)width height:(CGFloat)height limit:(NSInteger)limit {
    if (!_recognizer || strokes.count == 0 || limit <= 0) return @[];
    std::unique_ptr<zinnia::Character> character(zinnia::Character::create());
    character->set_width((size_t)MAX(1.0, width));
    character->set_height((size_t)MAX(1.0, height));
    size_t index = 0;
    for (NSArray<NSNumber *> *stroke in strokes) {
        if (stroke.count < 2) continue;
        for (NSUInteger i = 0; i + 1 < stroke.count; i += 2) {
            character->add(index, (int)lround(stroke[i].doubleValue), (int)lround(stroke[i + 1].doubleValue));
        }
        index++;
    }
    if (index == 0) return @[];
    std::unique_ptr<zinnia::Result> result(_recognizer->classify(*character, (size_t)limit));
    if (!result) return @[];
    NSMutableArray<NSString *> *values = [NSMutableArray arrayWithCapacity:result->size()];
    for (size_t i = 0; i < result->size(); i++) {
        const char *value = result->value(i);
        NSString *text = value ? [NSString stringWithUTF8String:value] : nil;
        if (text.length) [values addObject:text];
    }
    return values;
}

@end
