/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * Local image OCR via Apple Vision (on-device, no cloud).
 */

#include <rtl/string.h>
#include <sal/types.h>

#include <premac.h>
#import <Foundation/Foundation.h>
#import <ImageIO/ImageIO.h>
#import <Vision/Vision.h>
#include <postmac.h>

#include <cstdlib>
#include <cstring>
#include <string>

extern "C" sal_Bool kqoffice_ocr_vision(const char* utf8Path, char** outUtf8, sal_Int32* outLen)
{
    if (!utf8Path || !outUtf8 || !outLen)
        return sal_False;
    *outUtf8 = nullptr;
    *outLen = 0;

    @autoreleasepool
    {
        NSString* path = [NSString stringWithUTF8String:utf8Path];
        if (!path.length)
            return sal_False;

        NSURL* url = [NSURL fileURLWithPath:path];
        if (!url)
            return sal_False;

        CGImageSourceRef src = CGImageSourceCreateWithURL((__bridge CFURLRef)url, nullptr);
        if (!src)
            return sal_False;
        CGImageRef image = CGImageSourceCreateImageAtIndex(src, 0, nullptr);
        CFRelease(src);
        if (!image)
            return sal_False;

        __block NSMutableString* collected = [NSMutableString string];
        __block NSError* blockError = nil;

        VNRecognizeTextRequest* request = [[VNRecognizeTextRequest alloc]
            initWithCompletionHandler:^(VNRequest* req, NSError* error) {
                if (error)
                {
                    blockError = error;
                    return;
                }
                for (VNRecognizedTextObservation* obs in req.results)
                {
                    if (![obs isKindOfClass:[VNRecognizedTextObservation class]])
                        continue;
                    VNRecognizedText* top = [[obs topCandidates:1] firstObject];
                    if (top.string.length)
                    {
                        [collected appendString:top.string];
                        [collected appendString:@"\n"];
                    }
                }
            }];

        // Accurate is slower but better for screenshots / scanned pages.
        request.recognitionLevel = VNRequestTextRecognitionLevelAccurate;
        request.usesLanguageCorrection = YES;
        if (@available(macOS 10.15, *))
        {
            // Prefer Chinese + English; Vision ignores unsupported codes.
            request.recognitionLanguages = @[ @"zh-Hans", @"zh-Hant", @"en-US" ];
        }
        if (@available(macOS 13.0, *))
        {
            request.automaticallyDetectsLanguage = YES;
        }

        VNImageRequestHandler* handler =
            [[VNImageRequestHandler alloc] initWithCGImage:image options:@{}];
        NSError* err = nil;
        const BOOL ok = [handler performRequests:@[ request ] error:&err];
        CGImageRelease(image);

        if (!ok || err || blockError || collected.length == 0)
            return sal_False;

        const char* utf8 = [collected UTF8String];
        if (!utf8)
            return sal_False;
        const size_t n = std::strlen(utf8);
        char* copy = static_cast<char*>(std::malloc(n + 1));
        if (!copy)
            return sal_False;
        std::memcpy(copy, utf8, n + 1);
        *outUtf8 = copy;
        *outLen = static_cast<sal_Int32>(n);
        return sal_True;
    }
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
