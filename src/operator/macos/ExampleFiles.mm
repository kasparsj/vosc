#include "../ExampleFiles.hpp"

#if VOSC_OPERATOR_UI && defined(__APPLE__)
#import <AppKit/AppKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

namespace vosc::operator_ui {

void chooseExampleClip(std::function<void(ClipPickResult, std::filesystem::path)> completion) {
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    panel.message = @"Choose a local MOV or MP4 to load Video";
    panel.prompt = @"Choose";
    panel.canChooseFiles = YES;
    panel.canChooseDirectories = NO;
    panel.allowsMultipleSelection = NO;
    panel.allowedContentTypes = @[UTTypeQuickTimeMovie, UTTypeMPEG4Movie];

    auto finished = ^(NSModalResponse response) {
        if (response == NSModalResponseCancel) {
            completion(ClipPickResult::Cancelled, {});
        } else if (response == NSModalResponseOK && panel.URL.isFileURL && panel.URL.fileSystemRepresentation) {
            completion(ClipPickResult::Chosen, panel.URL.fileSystemRepresentation);
        } else {
            completion(ClipPickResult::Unavailable, {});
        }
    };

    NSWindow* owner = NSApp.keyWindow;
    if (owner) [panel beginSheetModalForWindow:owner completionHandler:finished];
    else [panel beginWithCompletionHandler:finished];
}

bool revealExampleFile(const std::filesystem::path& path) {
    NSURL* url = [NSURL fileURLWithFileSystemRepresentation:path.c_str() isDirectory:NO relativeToURL:nil];
    if (!url) return false;
    [[NSWorkspace sharedWorkspace] activateFileViewerSelectingURLs:@[url]];
    return true;
}

bool openDataFolder(const std::filesystem::path& path) {
    std::error_code error;
    if (!std::filesystem::is_directory(std::filesystem::symlink_status(path, error))) return false;
    NSURL* url = [NSURL fileURLWithFileSystemRepresentation:path.c_str() isDirectory:YES relativeToURL:nil];
    return url && [[NSWorkspace sharedWorkspace] openURL:url];
}

} // namespace vosc::operator_ui
#endif
