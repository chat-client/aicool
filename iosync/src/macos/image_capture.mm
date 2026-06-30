#include "iosync.h"

#import <Foundation/Foundation.h>
#import <ImageCaptureCore/ImageCaptureCore.h>

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <unordered_set>

namespace {

constexpr NSTimeInterval kDeviceWaitSeconds = 8.0;
constexpr NSTimeInterval kOperationWaitSeconds = 60.0;
constexpr int kOpenSessionAttempts = 3;

iosync::MediaItem to_media_item(ICCameraFile* file);

void log_verbose(bool enabled, const std::string& message)
{
    if (enabled) {
        std::cerr << "iosync: " << message << '\n';
    }
}

std::string to_utf8(NSString* text)
{
    return text ? std::string([text UTF8String]) : std::string();
}

std::string ns_error(NSError* error)
{
    if (!error) {
        return {};
    }
    return to_utf8([error localizedDescription]);
}

bool run_until(BOOL (^predicate)(void), NSTimeInterval timeout_seconds)
{
    NSDate* deadline = [NSDate dateWithTimeIntervalSinceNow:timeout_seconds];
    while (!predicate()) {
        @autoreleasepool {
            if ([[NSDate date] compare:deadline] != NSOrderedAscending) {
                return false;
            }
            [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode
                                     beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
        }
    }
    return true;
}

void run_loop_slice(NSTimeInterval seconds)
{
    [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode
                             beforeDate:[NSDate dateWithTimeIntervalSinceNow:seconds]];
}

} // namespace

@interface DeviceBrowserDelegate : NSObject <ICDeviceBrowserDelegate>
@property(nonatomic, strong) NSMutableArray<ICDevice*>* devices;
@end

@implementation DeviceBrowserDelegate

- (instancetype)init
{
    self = [super init];
    if (self) {
        _devices = [NSMutableArray array];
    }
    return self;
}

- (void)deviceBrowser:(ICDeviceBrowser*)browser didAddDevice:(ICDevice*)device moreComing:(BOOL)moreComing
{
    (void)browser;
    (void)moreComing;
    if ((device.type & ICDeviceTypeMaskCamera) == ICDeviceTypeCamera) {
        [_devices addObject:device];
    }
}

- (void)deviceBrowser:(ICDeviceBrowser*)browser didRemoveDevice:(ICDevice*)device moreGoing:(BOOL)moreGoing
{
    (void)browser;
    (void)moreGoing;
    [_devices removeObject:device];
}

@end

@interface CameraSessionDelegate : NSObject <ICCameraDeviceDelegate, ICCameraDeviceDownloadDelegate>
@property(nonatomic) BOOL opened;
@property(nonatomic) BOOL deviceReady;
@property(nonatomic) BOOL contentReady;
@property(nonatomic) BOOL accessRestrictionRemoved;
@property(nonatomic) BOOL accessRestrictionEnabled;
@property(nonatomic, strong) NSError* openError;
@property(nonatomic, strong) NSError* downloadError;
@property(nonatomic) NSInteger pendingDownloads;
@property(nonatomic, strong) ICDeviceBrowser* browser;
@property(nonatomic, strong) DeviceBrowserDelegate* browserDelegate;
@property(nonatomic, strong) NSMutableArray<ICCameraItem*>* addedItems;
@property(nonatomic, strong) NSMutableArray<ICCameraFile*>* downloadedFiles;
@property(nonatomic) BOOL showExportProgress;
@property(nonatomic) NSUInteger exportCompletedCount;
@end

@implementation CameraSessionDelegate

- (instancetype)init
{
    self = [super init];
    if (self) {
        _addedItems = [NSMutableArray array];
        _downloadedFiles = [NSMutableArray array];
    }
    return self;
}

- (void)device:(ICDevice*)device didOpenSessionWithError:(NSError*)error
{
    (void)device;
    self.openError = error;
    self.opened = YES;
}

- (void)device:(ICDevice*)device didCloseSessionWithError:(NSError*)error
{
    (void)device;
    (void)error;
}

- (void)didRemoveDevice:(ICDevice*)device
{
    (void)device;
}

- (void)deviceDidBecomeReady:(ICDevice*)device
{
    (void)device;
    self.deviceReady = YES;
}

- (void)deviceDidBecomeReadyWithCompleteContentCatalog:(ICCameraDevice*)device
{
    (void)device;
    self.contentReady = YES;
}

- (void)cameraDevice:(ICCameraDevice*)camera didAddItems:(NSArray<ICCameraItem*>*)items
{
    (void)camera;
    [self.addedItems addObjectsFromArray:items];
}

- (void)cameraDevice:(ICCameraDevice*)camera didRemoveItems:(NSArray<ICCameraItem*>*)items
{
    (void)camera;
    (void)items;
}

- (void)cameraDevice:(ICCameraDevice*)camera didRenameItems:(NSArray<ICCameraItem*>*)items
{
    (void)camera;
    (void)items;
}

- (void)cameraDeviceDidChangeCapability:(ICCameraDevice*)camera
{
    (void)camera;
}

- (void)cameraDevice:(ICCameraDevice*)camera didReceivePTPEvent:(NSData*)eventData
{
    (void)camera;
    (void)eventData;
}

- (void)cameraDeviceDidRemoveAccessRestriction:(ICDevice*)device
{
    (void)device;
    self.accessRestrictionRemoved = YES;
    self.accessRestrictionEnabled = NO;
}

- (void)cameraDeviceDidEnableAccessRestriction:(ICDevice*)device
{
    (void)device;
    self.accessRestrictionEnabled = YES;
    self.accessRestrictionRemoved = NO;
}

- (void)cameraDevice:(ICCameraDevice*)camera didReceiveMetadata:(NSDictionary*)metadata forItem:(ICCameraItem*)item error:(NSError*)error
{
    (void)camera;
    (void)metadata;
    (void)item;
    (void)error;
}

- (void)cameraDevice:(ICCameraDevice*)camera didReceiveThumbnail:(CGImageRef)thumbnail forItem:(ICCameraItem*)item error:(NSError*)error
{
    (void)camera;
    (void)thumbnail;
    (void)item;
    (void)error;
}

- (void)didDownloadFile:(ICCameraFile*)file error:(NSError*)error options:(NSDictionary*)options contextInfo:(void*)contextInfo
{
    (void)contextInfo;
    if (error && !self.downloadError) {
        self.downloadError = error;
    } else if (file) {
        [self.downloadedFiles addObject:file];
        self.exportCompletedCount += 1;
        if (self.showExportProgress) {
            NSString* savedName = [options objectForKey:ICSaveAsFilename];
            iosync::MediaItem item = to_media_item(file);
            if (!to_utf8(savedName).empty()) {
                item.name = to_utf8(savedName);
            }
            iosync::print_export_progress(static_cast<std::size_t>(self.exportCompletedCount), item);
        }
    }
    self.pendingDownloads -= 1;
}

@end

namespace {

ICDeviceBrowser* start_browser(DeviceBrowserDelegate* delegate)
{
    ICDeviceBrowser* browser = [[ICDeviceBrowser alloc] init];
    browser.delegate = delegate;
    browser.browsedDeviceTypeMask = static_cast<ICDeviceTypeMask>(ICDeviceTypeMaskCamera | ICDeviceLocationTypeMaskLocal);
    [browser start];
    return browser;
}

NSArray<ICDevice*>* discover_devices(bool verbose)
{
    log_verbose(verbose, "searching for local camera devices...");
    DeviceBrowserDelegate* delegate = [[DeviceBrowserDelegate alloc] init];
    ICDeviceBrowser* browser = start_browser(delegate);
    const bool found = run_until(^BOOL {
        return delegate.devices.count > 0;
    }, kDeviceWaitSeconds);
    [browser stop];
    if (found) {
        log_verbose(verbose, "found " + std::to_string(static_cast<unsigned long long>(delegate.devices.count)) + " device(s)");
    } else {
        log_verbose(verbose, "no camera device appeared within the discovery timeout");
    }
    return [delegate.devices copy];
}

NSArray<ICDevice*>* discover_devices_for_session(CameraSessionDelegate* sessionDelegate, bool verbose)
{
    log_verbose(verbose, "searching for local camera devices...");
    DeviceBrowserDelegate* browserDelegate = [[DeviceBrowserDelegate alloc] init];
    ICDeviceBrowser* browser = start_browser(browserDelegate);
    sessionDelegate.browserDelegate = browserDelegate;
    sessionDelegate.browser = browser;

    const bool found = run_until(^BOOL {
        return browserDelegate.devices.count > 0;
    }, kDeviceWaitSeconds);
    if (found) {
        log_verbose(verbose, "found " + std::to_string(static_cast<unsigned long long>(browserDelegate.devices.count)) + " device(s)");
    } else {
        log_verbose(verbose, "no camera device appeared within the discovery timeout");
    }
    return [browserDelegate.devices copy];
}

std::string item_id(ICCameraFile* file)
{
    @try {
        id value = [file valueForKey:@"objectID"];
        if (value) {
            return to_utf8([value description]);
        }
    } @catch (NSException*) {
    }

    std::ostringstream out;
    out << static_cast<const void*>((__bridge void*)file);
    return out.str();
}

std::string item_kind(ICCameraFile* file)
{
    NSString* uti = nil;
    @try {
        uti = [file valueForKey:@"UTI"];
    } @catch (NSException*) {
    }
    const std::string type = to_utf8(uti).empty() ? to_utf8(file.name) : to_utf8(uti);
    if (type.find("movie") != std::string::npos || type.find("video") != std::string::npos ||
        type.find(".MOV") != std::string::npos || type.find(".MP4") != std::string::npos) {
        return "video";
    }
    return "photo";
}

std::uint64_t file_size(ICCameraFile* file)
{
    @try {
        id value = [file valueForKey:@"fileSize"];
        if (value) {
            return static_cast<std::uint64_t>([value unsignedLongLongValue]);
        }
    } @catch (NSException*) {
    }
    return 0;
}

NSTimeInterval file_time(ICCameraFile* file)
{
    NSDate* date = file.creationDate;
    if (!date) {
        date = file.modificationDate;
    }
    if (!date) {
        return 0;
    }
    return [date timeIntervalSince1970];
}

void collect_files_from_item(ICCameraItem* item, NSMutableArray<ICCameraFile*>* files)
{
    if ([item isKindOfClass:[ICCameraFile class]]) {
        [files addObject:(ICCameraFile*)item];
        return;
    }
    if ([item isKindOfClass:[ICCameraFolder class]]) {
        ICCameraFolder* folder = (ICCameraFolder*)item;
        for (ICCameraItem* child in folder.contents) {
            collect_files_from_item(child, files);
        }
    }
}

void append_unique_file(ICCameraFile* file, NSMutableArray<ICCameraFile*>* files, std::unordered_set<std::string>& seen)
{
    const std::string id = item_id(file);
    if (seen.insert(id).second) {
        [files addObject:file];
    }
}

void append_unique_item(ICCameraItem* item, NSMutableArray<ICCameraFile*>* files, std::unordered_set<std::string>& seen)
{
    NSMutableArray<ICCameraFile*>* local = [NSMutableArray array];
    collect_files_from_item(item, local);
    for (ICCameraFile* file in local) {
        append_unique_file(file, files, seen);
    }
}

NSArray<ICCameraFile*>* collect_files(ICCameraDevice* camera, CameraSessionDelegate* delegate)
{
    NSMutableArray<ICCameraFile*>* files = [NSMutableArray array];
    std::unordered_set<std::string> seen;
    for (ICCameraItem* item in camera.mediaFiles) {
        append_unique_item(item, files, seen);
    }
    for (ICCameraItem* item in delegate.addedItems) {
        append_unique_item(item, files, seen);
    }
    for (ICCameraItem* item in camera.contents) {
        append_unique_item(item, files, seen);
    }
    return [files copy];
}

NSUInteger known_media_count(ICCameraDevice* camera, CameraSessionDelegate* delegate)
{
    NSArray<ICCameraFile*>* files = collect_files(camera, delegate);
    return files.count;
}

NSUInteger emit_new_files(ICCameraDevice* camera,
                          CameraSessionDelegate* delegate,
                          std::unordered_set<std::string>& seen,
                          const iosync::MediaItemCallback& callback)
{
    NSArray<ICCameraFile*>* files = collect_files(camera, delegate);
    NSUInteger emitted = 0;
    for (ICCameraFile* file in files) {
        const std::string id = item_id(file);
        if (seen.insert(id).second) {
            callback(to_media_item(file));
            emitted += 1;
        }
    }
    return emitted;
}

std::string unique_output_name(const std::string& originalName,
                               const std::filesystem::path& outputDir,
                               std::unordered_set<std::string>& reservedNames)
{
    const std::string clean = iosync::sanitize_filename(originalName);
    std::filesystem::path cleanPath(clean.empty() ? "unnamed" : clean);
    std::string stem = cleanPath.stem().string();
    std::string ext = cleanPath.extension().string();
    if (stem.empty()) {
        stem = "unnamed";
    }

    for (int index = 0;; ++index) {
        std::string candidate = index == 0 ? (stem + ext) : (stem + "_" + std::to_string(index) + ext);
        if (reservedNames.find(candidate) != reservedNames.end()) {
            continue;
        }
        if (std::filesystem::exists(outputDir / candidate)) {
            continue;
        }
        reservedNames.insert(candidate);
        return candidate;
    }
}

NSUInteger queue_new_exports(ICCameraDevice* camera,
                             CameraSessionDelegate* delegate,
                             const iosync::Options& options,
                             NSURL* outputURL,
                             const std::filesystem::path& outputDir,
                             std::unordered_set<std::string>& seenIds,
                             std::unordered_set<std::string>& reservedNames,
                             bool& foundTarget)
{
    NSArray<ICCameraFile*>* files = collect_files(camera, delegate);
    NSUInteger queued = 0;
    for (ICCameraFile* file in files) {
        const std::string id = item_id(file);
        if (!seenIds.insert(id).second) {
            continue;
        }
        if (!options.all && id != options.object_id) {
            continue;
        }

        foundTarget = true;
        const std::string outputName = unique_output_name(to_utf8(file.name), outputDir, reservedNames);
        NSString* saveName = [NSString stringWithUTF8String:outputName.c_str()];
        NSDictionary* downloadOptions = @{
            ICDownloadsDirectoryURL : outputURL,
            ICSaveAsFilename : saveName
        };
        delegate.pendingDownloads += 1;
        queued += 1;
        if (options.verbose) {
            iosync::MediaItem item = to_media_item(file);
            item.name = outputName;
            iosync::print_export_event("queued", seenIds.size(), item);
        }
        [camera requestDownloadFile:file
                            options:downloadOptions
                   downloadDelegate:delegate
                didDownloadSelector:@selector(didDownloadFile:error:options:contextInfo:)
                        contextInfo:nullptr];

        if (!options.all) {
            break;
        }
    }
    return queued;
}

bool request_export_file(ICCameraDevice* camera,
                         CameraSessionDelegate* delegate,
                         ICCameraFile* file,
                         const iosync::Options& options,
                         NSURL* outputURL,
                         const std::filesystem::path& outputDir,
                         std::unordered_set<std::string>& reservedNames)
{
    const std::string outputName = unique_output_name(to_utf8(file.name), outputDir, reservedNames);
    NSString* saveName = [NSString stringWithUTF8String:outputName.c_str()];
    NSDictionary* downloadOptions = @{
        ICDownloadsDirectoryURL : outputURL,
        ICSaveAsFilename : saveName
    };
    delegate.pendingDownloads += 1;
    if (options.verbose) {
        iosync::MediaItem item = to_media_item(file);
        item.name = outputName;
        iosync::print_export_event("queued", delegate.exportCompletedCount + 1, item);
    }
    [camera requestDownloadFile:file
                        options:downloadOptions
               downloadDelegate:delegate
            didDownloadSelector:@selector(didDownloadFile:error:options:contextInfo:)
                    contextInfo:nullptr];
    return true;
}

bool wait_for_complete_catalog(ICCameraDevice* camera, CameraSessionDelegate* delegate, const iosync::Options& options, std::string& error)
{
    log_verbose(options.verbose, "waiting for complete media catalog before time-sorted export...");
    const bool complete = run_until(^BOOL {
        return delegate.contentReady || camera.contentCatalogPercentCompleted >= 100;
    }, kOperationWaitSeconds);
    if (!complete) {
        error = "timed out waiting for complete media catalog required by --sort-time";
        return false;
    }
    return true;
}

void close_camera(ICCameraDevice* camera, CameraSessionDelegate* delegate)
{
    [camera requestCloseSession];
    [delegate.browser stop];
    delegate.browser = nil;
    delegate.browserDelegate = nil;
}

bool is_passcode_locked_error(NSError* error)
{
    if (!error) {
        return false;
    }
    return [error code] == ICReturnDeviceIsPasscodeLocked ||
           ns_error(error).find("unlock") != std::string::npos ||
           ns_error(error).find("Unlock") != std::string::npos;
}

bool wait_for_access(ICCameraDevice* camera, CameraSessionDelegate* delegate, bool verbose, NSTimeInterval timeout)
{
    log_verbose(verbose,
                "device accessRestricted=" +
                    std::string(camera.accessRestrictedAppleDevice ? "yes" : "no"));
    if (!camera.accessRestrictedAppleDevice) {
        return true;
    }
    log_verbose(verbose, "waiting for iPhone access restriction to be removed...");
    return run_until(^BOOL {
        return delegate.accessRestrictionRemoved || !camera.accessRestrictedAppleDevice;
    }, timeout);
}

bool wait_for_media_catalog(ICCameraDevice* camera, CameraSessionDelegate* delegate, const iosync::Options& options, std::string& error)
{
    log_verbose(options.verbose, "waiting for the media catalog...");
    const bool ready = run_until(^BOOL {
        return delegate.deviceReady ||
               delegate.contentReady ||
               camera.mediaFiles.count > 0 ||
               camera.contents.count > 0 ||
               delegate.addedItems.count > 0;
    }, kOperationWaitSeconds);
    if (!ready) {
        error = "timed out waiting for the iPhone media catalog; keep the phone unlocked and check iCloud Photos/Image Capture";
        return false;
    }
    log_verbose(options.verbose,
                "media catalog is ready; percent=" +
                    std::to_string(static_cast<unsigned long long>(camera.contentCatalogPercentCompleted)) +
                    ", mediaFiles=" +
                    std::to_string(static_cast<unsigned long long>(known_media_count(camera, delegate))));
    return true;
}

ICCameraDevice* open_camera(const iosync::Options& options, CameraSessionDelegate* delegate, bool waitForCatalog, std::string& error)
{
    NSArray<ICDevice*>* devices = discover_devices_for_session(delegate, options.verbose);
    if (devices.count == 0) {
        error = "no iPhone/iPad camera device found; unlock the phone and tap Trust This Computer";
        return nil;
    }
    if (options.device_index < 0 || static_cast<NSUInteger>(options.device_index) >= devices.count) {
        error = "device index is out of range";
        return nil;
    }

    ICDevice* device = devices[static_cast<NSUInteger>(options.device_index)];
    if (![device isKindOfClass:[ICCameraDevice class]]) {
        error = "selected device is not a camera/media device";
        return nil;
    }
    ICCameraDevice* camera = (ICCameraDevice*)device;
    camera.delegate = delegate;

    log_verbose(options.verbose,
                "device accessRestricted=" +
                    std::string(camera.accessRestrictedAppleDevice ? "yes" : "no"));

    for (int attempt = 1; attempt <= kOpenSessionAttempts; ++attempt) {
        delegate.opened = NO;
        delegate.openError = nil;

        log_verbose(options.verbose,
                    "opening media session for " + to_utf8(device.name) +
                        " (attempt " + std::to_string(attempt) + ")...");
        [camera requestOpenSession];
        if (!run_until(^BOOL {
                return delegate.opened;
            }, kOperationWaitSeconds)) {
            error = "timed out opening the iPhone media session";
            return nil;
        }
        if (!delegate.openError) {
            break;
        }
        if (!is_passcode_locked_error(delegate.openError) || attempt == kOpenSessionAttempts) {
            error = "failed to open iPhone media session: " + ns_error(delegate.openError);
            return nil;
        }
        log_verbose(options.verbose,
                    "macOS still reports the phone as locked; keep the screen awake, then retrying shortly...");
        wait_for_access(camera, delegate, options.verbose, 5.0);
    }

    if (waitForCatalog && !wait_for_media_catalog(camera, delegate, options, error)) {
        close_camera(camera, delegate);
        return nil;
    }
    return camera;
}

iosync::MediaItem to_media_item(ICCameraFile* file)
{
    iosync::MediaItem item;
    item.id = item_id(file);
    item.name = to_utf8(file.name);
    item.kind = item_kind(file);
    item.size = file_size(file);
    item.timestamp = static_cast<std::int64_t>(file_time(file));
    item.time_text = iosync::format_timestamp(item.timestamp);
    return item;
}

} // namespace

namespace iosync {

std::vector<Device> list_devices(const Options& options, std::string& error)
{
    (void)error;
    @autoreleasepool {
        NSArray<ICDevice*>* devices = discover_devices(options.verbose);
        std::vector<Device> out;
        for (ICDevice* device in devices) {
            Device entry;
            entry.id = to_utf8(device.UUIDString);
            entry.name = to_utf8(device.name);
            out.push_back(entry);
        }
        return out;
    }
}

std::vector<MediaItem> list_media(const Options& options, std::string& error)
{
    std::vector<MediaItem> out;
    list_media_stream(options, [&out](const MediaItem& item) {
        out.push_back(item);
    }, error);
    return out;
}

bool list_media_stream(const Options& options, const MediaItemCallback& callback, std::string& error)
{
    @autoreleasepool {
        CameraSessionDelegate* delegate = [[CameraSessionDelegate alloc] init];
        delegate.showExportProgress = YES;
        ICCameraDevice* camera = open_camera(options, delegate, false, error);
        if (!camera) {
            return false;
        }

        log_verbose(options.verbose, "streaming media file entries as they appear...");
        std::unordered_set<std::string> seen;
        NSUInteger emitted = emit_new_files(camera, delegate, seen, callback);
        NSDate* deadline = [NSDate dateWithTimeIntervalSinceNow:kOperationWaitSeconds];

        while (!delegate.contentReady) {
            @autoreleasepool {
                run_loop_slice(0.10);
                emitted += emit_new_files(camera, delegate, seen, callback);
                if ([[NSDate date] compare:deadline] != NSOrderedAscending) {
                    if (emitted > 0) {
                        log_verbose(options.verbose,
                                    "media catalog is still loading; returned " +
                                        std::to_string(static_cast<unsigned long long>(emitted)) +
                                        " discovered file(s)");
                        close_camera(camera, delegate);
                        return true;
                    }
                    error = "timed out waiting for media files; Image Capture may still be loading the device";
                    close_camera(camera, delegate);
                    return false;
                }
            }
        }

        emitted += emit_new_files(camera, delegate, seen, callback);
        log_verbose(options.verbose,
                    "stream completed; listed " +
                        std::to_string(static_cast<unsigned long long>(emitted)) +
                        " file(s)");
        close_camera(camera, delegate);
        return true;
    }
}

bool export_media(const Options& options, std::string& error)
{
    @autoreleasepool {
        const std::filesystem::path outputDir(options.output_dir);
        std::filesystem::create_directories(outputDir);

        CameraSessionDelegate* delegate = [[CameraSessionDelegate alloc] init];
        ICCameraDevice* camera = open_camera(options, delegate, false, error);
        if (!camera) {
            return false;
        }

        NSURL* outputURL = [NSURL fileURLWithPath:[NSString stringWithUTF8String:options.output_dir.c_str()]
                                      isDirectory:YES];
        std::unordered_set<std::string> seenIds;
        std::unordered_set<std::string> reservedNames;

        if (options.sort_time) {
            if (!wait_for_complete_catalog(camera, delegate, options, error)) {
                close_camera(camera, delegate);
                return false;
            }

            NSArray<ICCameraFile*>* collected = collect_files(camera, delegate);
            std::vector<ICCameraFile*> files;
            files.reserve(collected.count);
            for (ICCameraFile* file in collected) {
                if (!options.all && item_id(file) != options.object_id) {
                    continue;
                }
                files.push_back(file);
            }

            std::sort(files.begin(), files.end(), [](ICCameraFile* lhs, ICCameraFile* rhs) {
                const NSTimeInterval leftTime = file_time(lhs);
                const NSTimeInterval rightTime = file_time(rhs);
                if (leftTime != rightTime) {
                    return leftTime < rightTime;
                }
                return to_utf8(lhs.name) < to_utf8(rhs.name);
            });

            if (files.empty()) {
                close_camera(camera, delegate);
                error = options.all ? "no media files found" : "object id was not found";
                return false;
            }

            std::cerr << "iosync: exporting " << files.size()
                      << " file(s) sorted by time...\n";

            for (ICCameraFile* file : files) {
                request_export_file(camera, delegate, file, options, outputURL, outputDir, reservedNames);
                const bool done = run_until(^BOOL {
                    return delegate.pendingDownloads == 0 || delegate.downloadError != nil;
                }, kOperationWaitSeconds);
                if (delegate.downloadError) {
                    error = "download failed: " + ns_error(delegate.downloadError);
                    close_camera(camera, delegate);
                    return false;
                }
                if (!done) {
                    error = "timed out while exporting media";
                    close_camera(camera, delegate);
                    return false;
                }
            }

            close_camera(camera, delegate);
            std::cerr << "iosync: export completed; exported "
                      << static_cast<unsigned long long>(delegate.downloadedFiles.count)
                      << " file(s) to " << options.output_dir << '\n';
            return true;
        }

        bool foundTarget = false;
        NSUInteger queuedTotal = 0;
        NSUInteger completedCount = 0;
        NSInteger lastPending = -1;
        NSDate* idleDeadline = [NSDate dateWithTimeIntervalSinceNow:kOperationWaitSeconds];

        auto reset_idle_deadline = [&idleDeadline]() {
            idleDeadline = [NSDate dateWithTimeIntervalSinceNow:kOperationWaitSeconds];
        };
        auto idle_expired = [&idleDeadline]() {
            return [[NSDate date] compare:idleDeadline] != NSOrderedAscending;
        };
        auto catalog_done = [&]() {
            return delegate.contentReady || camera.contentCatalogPercentCompleted >= 100;
        };

        log_verbose(options.verbose, options.all ? "exporting media as files appear..." : "searching for requested media id...");

        while (true) {
            @autoreleasepool {
                const NSUInteger queuedNow = queue_new_exports(camera,
                                                               delegate,
                                                               options,
                                                               outputURL,
                                                               outputDir,
                                                               seenIds,
                                                               reservedNames,
                                                               foundTarget);
                if (queuedNow > 0) {
                    queuedTotal += queuedNow;
                    reset_idle_deadline();
                }

                if (delegate.downloadError) {
                    error = "download failed: " + ns_error(delegate.downloadError);
                    close_camera(camera, delegate);
                    return false;
                }

                if (completedCount != delegate.downloadedFiles.count || lastPending != delegate.pendingDownloads) {
                    completedCount = delegate.downloadedFiles.count;
                    lastPending = delegate.pendingDownloads;
                    reset_idle_deadline();
                    log_verbose(options.verbose,
                                "export progress: queued=" +
                                    std::to_string(static_cast<unsigned long long>(queuedTotal)) +
                                    ", completed=" +
                                    std::to_string(static_cast<unsigned long long>(completedCount)) +
                                    ", pending=" +
                                    std::to_string(static_cast<long long>(delegate.pendingDownloads)));
                }

                if (!options.all && foundTarget && delegate.pendingDownloads == 0) {
                    close_camera(camera, delegate);
                    std::cerr << "iosync: export completed; exported "
                              << static_cast<unsigned long long>(completedCount)
                              << " file(s) to " << options.output_dir << '\n';
                    return true;
                }

                if (options.all && catalog_done() && delegate.pendingDownloads == 0) {
                    close_camera(camera, delegate);
                    if (queuedTotal == 0) {
                        error = "no media files found";
                        return false;
                    }
                    std::cerr << "iosync: export completed; exported "
                              << static_cast<unsigned long long>(completedCount)
                              << " file(s) to " << options.output_dir << '\n';
                    return true;
                }

                if (!options.all && catalog_done() && !foundTarget) {
                    close_camera(camera, delegate);
                    error = "object id was not found";
                    return false;
                }

                if (idle_expired()) {
                    close_camera(camera, delegate);
                    if (!options.all && !foundTarget) {
                        error = "timed out before the requested object id appeared";
                    } else if (delegate.pendingDownloads > 0) {
                        error = "timed out while exporting media";
                    } else {
                        error = "timed out before the iPhone media catalog completed";
                    }
                    return false;
                }

                run_loop_slice(0.10);
            }
        }
    }
}

} // namespace iosync
