// NSURLSession backend of platform/http_client.hpp (macOS + iOS). Compiled with ARC
// (CMakeLists.txt). An ephemeral session: no cookies, no cache, no credentials; redirects are
// followed only to https:// URLs (NSURLSession already refuses https -> http under ATS, this
// makes it explicit). The plain-http local test server works because ATS exempts IP addresses
// and localhost.

#include "platform/http_backends.hpp"

#import <Foundation/Foundation.h>

#include <string>

using d2k::platform::http::Callbacks;
using d2k::platform::http::Request;
using d2k::platform::http::Response;

@interface D2kHttpDelegate : NSObject <NSURLSessionDataDelegate>
@end

@implementation D2kHttpDelegate {
@public
    const Callbacks* cb;
    const std::atomic<bool>* cancel;
    Response* resp;
    dispatch_semaphore_t done;
    bool writeFailed, aborted, badRedirect;
}

- (void)URLSession:(NSURLSession*)session
                          task:(NSURLSessionTask*)task
    willPerformHTTPRedirection:(NSHTTPURLResponse*)response
                    newRequest:(NSURLRequest*)request
             completionHandler:(void (^)(NSURLRequest*))completionHandler {
    const bool https = [[[request.URL scheme] lowercaseString] isEqualToString:@"https"];
    if (!https) badRedirect = true;
    completionHandler(https ? request : nil);  // nil: the redirect response becomes the final one
}

- (void)URLSession:(NSURLSession*)session
              dataTask:(NSURLSessionDataTask*)dataTask
    didReceiveResponse:(NSURLResponse*)response
     completionHandler:(void (^)(NSURLSessionResponseDisposition))completionHandler {
    const int status = [response isKindOfClass:[NSHTTPURLResponse class]] ? int(((NSHTTPURLResponse*)response).statusCode) : 0;
    resp->status = status;
    resp->partial = status == 206;
    resp->contentLength = response.expectedContentLength >= 0 ? std::int64_t(response.expectedContentLength) : -1;
    if (status < 200 || status >= 300 || cancel->load()) {
        completionHandler(NSURLSessionResponseCancel);
        return;
    }
    if (cb->onStart && !cb->onStart(status, resp->partial, resp->contentLength)) {
        aborted = true;
        completionHandler(NSURLSessionResponseCancel);
        return;
    }
    completionHandler(NSURLSessionResponseAllow);
}

- (void)URLSession:(NSURLSession*)session dataTask:(NSURLSessionDataTask*)dataTask didReceiveData:(NSData*)data {
    if (cancel->load() || writeFailed) {
        [dataTask cancel];
        return;
    }
    __block bool ok = true;
    [data enumerateByteRangesUsingBlock:^(const void* bytes, NSRange range, BOOL* stop) {
        if (cb->onData && !cb->onData(bytes, range.length)) {
            ok = false;
            *stop = YES;
        }
    }];
    if (!ok) {
        writeFailed = true;
        [dataTask cancel];
    }
}

- (void)URLSession:(NSURLSession*)session task:(NSURLSessionTask*)task didCompleteWithError:(NSError*)error {
    if (cancel->load()) {
        resp->cancelled = true;
        resp->error = "cancelled";
    } else if (writeFailed) {
        resp->error = "write failed";
    } else if (aborted) {
        resp->error = "aborted";
    } else if (badRedirect) {
        resp->error = "refused a redirect to a non-https URL";
    } else if (resp->status != 0 && (resp->status < 200 || resp->status >= 300)) {
        resp->error = "HTTP " + std::to_string(resp->status);
    } else if (error) {
        resp->error = std::string([[error localizedDescription] UTF8String] ?: "network error");
    } else if (resp->status == 0) {
        resp->error = "no response";
    } else {
        resp->ok = true;
    }
    dispatch_semaphore_signal(done);
}

@end

namespace d2k::platform::http::native {

const char* name() { return "NSURLSession"; }

bool available(std::string*) { return true; }

Response get(const Request& req, const Callbacks& cb, const std::atomic<bool>& cancel) {
    Response r;
    @autoreleasepool {
        NSURL* url = [NSURL URLWithString:[NSString stringWithUTF8String:req.url.c_str()]];
        if (!url) {
            r.error = "bad URL";
            return r;
        }
        NSURLSessionConfiguration* cfg = [NSURLSessionConfiguration ephemeralSessionConfiguration];
        cfg.HTTPCookieStorage = nil;
        cfg.HTTPShouldSetCookies = NO;
        cfg.URLCache = nil;
        cfg.URLCredentialStorage = nil;
        cfg.requestCachePolicy = NSURLRequestReloadIgnoringLocalCacheData;
        cfg.timeoutIntervalForRequest = req.stallTimeoutSec;  // idle time between packets
        cfg.timeoutIntervalForResource = 6 * 3600;
        cfg.waitsForConnectivity = NO;

        D2kHttpDelegate* d = [[D2kHttpDelegate alloc] init];
        d->cb = &cb;
        d->cancel = &cancel;
        d->resp = &r;
        d->done = dispatch_semaphore_create(0);
        d->writeFailed = d->aborted = d->badRedirect = false;
        NSOperationQueue* q = [[NSOperationQueue alloc] init];
        q.maxConcurrentOperationCount = 1;
        NSURLSession* session = [NSURLSession sessionWithConfiguration:cfg delegate:d delegateQueue:q];

        NSMutableURLRequest* rq = [NSMutableURLRequest requestWithURL:url];
        rq.HTTPMethod = @"GET";
        [rq setValue:[NSString stringWithUTF8String:userAgent().c_str()] forHTTPHeaderField:@"User-Agent"];
        [rq setValue:@"*/*" forHTTPHeaderField:@"Accept"];
        if (req.resumeFrom > 0)
            [rq setValue:[NSString stringWithFormat:@"bytes=%lld-", (long long)req.resumeFrom] forHTTPHeaderField:@"Range"];
        NSURLSessionDataTask* task = [session dataTaskWithRequest:rq];
        [task resume];
        bool cancelSent = false;
        while (dispatch_semaphore_wait(d->done, dispatch_time(DISPATCH_TIME_NOW, 100 * NSEC_PER_MSEC)) != 0) {
            if (cancel.load() && !cancelSent) {
                cancelSent = true;
                [task cancel];
            }
        }
        [session finishTasksAndInvalidate];  // releases the delegate
    }
    return r;
}

}  // namespace d2k::platform::http::native
