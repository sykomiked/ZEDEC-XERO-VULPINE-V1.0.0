/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* ZXVApp.m — the native macOS shell for ZXV.
 *
 * A small AppKit program. It starts the swarm engine (Contents/MacOS/
 * zxv-engine, built from kernel/arch/hosted/zxv_host.c) as a child process
 * and shows the engine's window in a WKWebView. It has a Dock icon and a
 * menu bar, and posts macOS notifications. It is a companion: it asks for
 * no admin rights, installs nothing outside its own data folders, does not
 * start at login, and the engine's compute is capped by swarm_governor.
 *
 *   ENGINE.   Started with --no-window --port 0 --exit-with-parent and a
 *             fresh 256-bit token (SecRandomCopyBytes) in ZXV_TOKEN. The
 *             engine prints "ZXV-URL: http://127.0.0.1:<port>/#token=<t>";
 *             the shell checks the token is its own and loads that URL. The
 *             engine's stdin is a pipe the shell holds, so if the shell dies
 *             the engine sees EOF and stops: it can never outlive the app.
 *   WEBVIEW.  Non-persistent data store (nothing written to disk). Only the
 *             engine's own origin may load inside it; any other link opens
 *             in the user's default browser. The page can post a
 *             notification with window.zxvNative.notify(title, body);
 *             messages from any other origin are ignored.
 *   MODELS.   Chosen in this order: the file picked with Model > Choose
 *             Model File, the first *.gguf in ~/Library/Application Support/
 *             ZXV/models, the first *.gguf bundled in Contents/Resources/
 *             models. With none, the app runs without one and, once, offers
 *             to download one (when the bundled manifest names any)
 *             or to choose a file. Nothing is downloaded without consent.
 *   SOCKET.   When the bundle carries Contents/Frameworks/libzxv.dylib, the
 *             engine is zxvd (kernel/arch/hosted/build_zxvd.sh) and is also
 *             given --rpc ~/Library/Application Support/ZXV/run/zxvd.sock:
 *             its JSON-RPC control socket (docs/ZXVD.md), mode 0600 in a
 *             0700 folder, same-user peers only. The shell uses it through
 *             zxv_rpc_client.c, off the main thread: quitting asks
 *             zxvd.shutdown first (closing stdin stays the fallback), and the
 *             Dock badge shows the unread count from notes.list. The window
 *             is unchanged and still served on 127.0.0.1 with the token.
 *   BOOKS.    zxvd also gets --books ~/Library/Application Support/ZXV/
 *             books.zxvb: the owner's books, saved as a hash-chained file
 *             and reloaded at every start (docs/ZXVD.md, "The books file").
 *   LOGS.     ~/Library/Logs/ZXV/engine.log and fetch.log.
 *
 * Build (universal): macos/build_macos_shell.sh. Needs Xcode or the Command
 * Line Tools; it cannot be built off a Mac.
 */
#import <Cocoa/Cocoa.h>
#import <Security/Security.h>
#import <UserNotifications/UserNotifications.h>
#import <WebKit/WebKit.h>
#include "zxv_rpc_client.h"
#if __has_include(<UniformTypeIdentifiers/UniformTypeIdentifiers.h>)
#    import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#endif

static NSString *const kModelPathKey = @"ZXVModelPath";
static NSString *const kAskedModelKey = @"ZXVAskedForModel";

@interface ZXVApp : NSObject <NSApplicationDelegate, NSWindowDelegate, WKNavigationDelegate,
                              WKUIDelegate, WKScriptMessageHandler,
                              UNUserNotificationCenterDelegate>
@property(strong) NSWindow *window;
@property(strong) WKWebView *web;
@property(strong) NSTask *engine;
@property(strong) NSPipe *engineIn;
@property(strong) NSMutableData *engineOut;
@property(strong) NSFileHandle *engineLog;
@property(strong) NSTask *fetcher;
@property(copy) NSString *token;
@property(assign) NSInteger port;
@property(assign) BOOL quitting;
@property(assign) BOOL restarting;
@property(copy) NSString *supportDir, *modelsDir, *logsDir;
@property(strong) NSMenuItem *fetchItem;
@property(copy) NSString *rpcPath;          /* nil: the engine has no control socket */
@property(strong) dispatch_queue_t rpcQueue; /* the socket is blocking: never the main thread */
@property(strong) NSTimer *notesTimer;
@end

@implementation ZXVApp

/* ---------------------------------------------------------------- setup */

- (void)applicationDidFinishLaunching:(NSNotification *)note
{
    (void) note;
    [self makeFolders];
    self.rpcQueue = dispatch_queue_create("zxv.rpc", DISPATCH_QUEUE_SERIAL);
    [self buildMenus];
    [self buildWindow];
    if (@available(macOS 10.14, *)) UNUserNotificationCenter.currentNotificationCenter.delegate = self;
    [self startEngine];
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t) (1.5 * NSEC_PER_SEC)),
                   dispatch_get_main_queue(), ^{
                     [self offerModelIfNone];
                   });
}

- (void)makeFolders
{
    NSFileManager *fm = NSFileManager.defaultManager;
    NSURL *support = [fm URLsForDirectory:NSApplicationSupportDirectory
                                inDomains:NSUserDomainMask].firstObject;
    NSURL *library = [fm URLsForDirectory:NSLibraryDirectory inDomains:NSUserDomainMask].firstObject;
    self.supportDir = [support.path stringByAppendingPathComponent:@"ZXV"];
    self.modelsDir = [self.supportDir stringByAppendingPathComponent:@"models"];
    self.logsDir = [library.path stringByAppendingPathComponent:@"Logs/ZXV"];
    for (NSString *d in @[ self.modelsDir, self.logsDir ])
        [fm createDirectoryAtPath:d withIntermediateDirectories:YES attributes:nil error:NULL];
}

- (NSMenuItem *)item:(NSString *)title action:(SEL)action key:(NSString *)key
{
    NSMenuItem *it = [[NSMenuItem alloc] initWithTitle:title action:action keyEquivalent:key];
    return it;
}

- (void)buildMenus
{
    NSMenu *bar = [[NSMenu alloc] init];

    NSMenuItem *appItem = [[NSMenuItem alloc] init];
    NSMenu *app = [[NSMenu alloc] initWithTitle:@"ZXV"];
    [app addItem:[self item:@"About ZXV" action:@selector(orderFrontStandardAboutPanel:) key:@""]];
    [app addItem:NSMenuItem.separatorItem];
    [app addItem:[self item:@"Hide ZXV" action:@selector(hide:) key:@"h"]];
    NSMenuItem *others = [self item:@"Hide Others" action:@selector(hideOtherApplications:)
                                key:@"h"];
    others.keyEquivalentModifierMask = NSEventModifierFlagCommand | NSEventModifierFlagOption;
    [app addItem:others];
    [app addItem:[self item:@"Show All" action:@selector(unhideAllApplications:) key:@""]];
    [app addItem:NSMenuItem.separatorItem];
    [app addItem:[self item:@"Quit ZXV" action:@selector(terminate:) key:@"q"]];
    appItem.submenu = app;
    [bar addItem:appItem];

    /* Edit: without it, copy and paste do not work inside the web view */
    NSMenuItem *editItem = [[NSMenuItem alloc] init];
    NSMenu *edit = [[NSMenu alloc] initWithTitle:@"Edit"];
    [edit addItem:[self item:@"Undo" action:NSSelectorFromString(@"undo:") key:@"z"]];
    [edit addItem:[self item:@"Redo" action:NSSelectorFromString(@"redo:") key:@"Z"]];
    [edit addItem:NSMenuItem.separatorItem];
    [edit addItem:[self item:@"Cut" action:@selector(cut:) key:@"x"]];
    [edit addItem:[self item:@"Copy" action:@selector(copy:) key:@"c"]];
    [edit addItem:[self item:@"Paste" action:@selector(paste:) key:@"v"]];
    [edit addItem:[self item:@"Select All" action:@selector(selectAll:) key:@"a"]];
    editItem.submenu = edit;
    [bar addItem:editItem];

    NSMenuItem *viewItem = [[NSMenuItem alloc] init];
    NSMenu *view = [[NSMenu alloc] initWithTitle:@"View"];
    [view addItem:[self item:@"Reload" action:@selector(reloadWindow:) key:@"r"]];
    [view addItem:[self item:@"Restart Engine" action:@selector(restartEngine:) key:@""]];
    viewItem.submenu = view;
    [bar addItem:viewItem];

    NSMenuItem *modelItem = [[NSMenuItem alloc] init];
    NSMenu *model = [[NSMenu alloc] initWithTitle:@"Model"];
    [model addItem:[self item:@"Choose Model File…" action:@selector(chooseModel:) key:@"o"]];
    self.fetchItem = [self item:@"Download Model…" action:@selector(fetchModel:) key:@""];
    [model addItem:self.fetchItem];
    [model addItem:[self item:@"Use No Model" action:@selector(clearModel:) key:@""]];
    [model addItem:NSMenuItem.separatorItem];
    [model addItem:[self item:@"Show Models Folder" action:@selector(showModels:) key:@""]];
    [model addItem:[self item:@"Show Logs Folder" action:@selector(showLogs:) key:@""]];
    modelItem.submenu = model;
    [bar addItem:modelItem];

    NSMenuItem *winItem = [[NSMenuItem alloc] init];
    NSMenu *win = [[NSMenu alloc] initWithTitle:@"Window"];
    [win addItem:[self item:@"Minimize" action:@selector(performMiniaturize:) key:@"m"]];
    [win addItem:[self item:@"Zoom" action:@selector(performZoom:) key:@""]];
    [win addItem:[self item:@"Close" action:@selector(performClose:) key:@"w"]];
    winItem.submenu = win;
    [bar addItem:winItem];
    NSApp.windowsMenu = win;

    NSMenuItem *helpItem = [[NSMenuItem alloc] init];
    NSMenu *help = [[NSMenu alloc] initWithTitle:@"Help"];
    [help addItem:[self item:@"ZXV Help" action:@selector(showHelp:) key:@"?"]];
    helpItem.submenu = help;
    [bar addItem:helpItem];
    NSApp.helpMenu = help;

    NSApp.mainMenu = bar;
    for (NSMenuItem *top in bar.itemArray)
        for (NSMenuItem *it in top.submenu.itemArray)
            if (it.action && [self respondsToSelector:it.action]) it.target = self;
}

- (void)buildWindow
{
    WKWebViewConfiguration *cfg = [[WKWebViewConfiguration alloc] init];
    cfg.websiteDataStore = WKWebsiteDataStore.nonPersistentDataStore;
    WKUserContentController *ucc = [[WKUserContentController alloc] init];
    [ucc addScriptMessageHandler:self name:@"zxv"];
    NSString *bridge = @"window.zxvNative={notify:function(t,b){try{window.webkit.messageHandlers."
                       @"zxv.postMessage({type:'notify',title:String(t),body:String(b||'')})}"
                       @"catch(e){}}};";
    [ucc addUserScript:[[WKUserScript alloc]
                             initWithSource:bridge
                              injectionTime:WKUserScriptInjectionTimeAtDocumentStart
                           forMainFrameOnly:YES]];
    cfg.userContentController = ucc;

    NSRect frame = NSMakeRect(0, 0, 1180, 780);
    self.window = [[NSWindow alloc]
        initWithContentRect:frame
                  styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                            NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable
                    backing:NSBackingStoreBuffered
                      defer:NO];
    self.window.title = @"ZXV";
    self.window.minSize = NSMakeSize(640, 480);
    self.window.delegate = self;
    self.window.releasedWhenClosed = NO; /* ARC owns it */
    self.web = [[WKWebView alloc] initWithFrame:frame configuration:cfg];
    self.web.navigationDelegate = self;
    self.web.UIDelegate = self;
    self.window.contentView = self.web;
    [self.window center];
    self.window.frameAutosaveName = @"ZXVMainWindow"; /* restores the last frame, if any */
    [self showMessage:@"Starting the swarm…" detail:@""];
    [self.window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
}

- (void)showMessage:(NSString *)title detail:(NSString *)detail
{
    NSString *(^esc)(NSString *) = ^NSString *(NSString *s) {
      s = [s stringByReplacingOccurrencesOfString:@"&" withString:@"&amp;"];
      s = [s stringByReplacingOccurrencesOfString:@"<" withString:@"&lt;"];
      return [s stringByReplacingOccurrencesOfString:@">" withString:@"&gt;"];
    };
    NSString *html = [NSString
        stringWithFormat:@"<!doctype html><meta charset=utf-8><body style=\"margin:0;"
                         @"background:#0e1116;color:#e6edf3;font:15px -apple-system,sans-serif;"
                         @"display:flex;align-items:center;justify-content:center;height:100vh\">"
                         @"<div style=\"max-width:520px;text-align:center\"><h2 style=\"font-"
                         @"weight:500\">%@</h2><p style=\"color:#8b949e\">%@</p></div>",
                         esc(title), esc(detail)];
    [self.web loadHTMLString:html baseURL:nil];
}

/* --------------------------------------------------------------- engine */

- (NSString *)newToken
{
    uint8_t raw[32];
    if (SecRandomCopyBytes(kSecRandomDefault, sizeof raw, raw) != errSecSuccess) return nil;
    NSMutableString *hex = [NSMutableString stringWithCapacity:64];
    for (size_t i = 0; i < sizeof raw; i++) [hex appendFormat:@"%02x", raw[i]];
    memset(raw, 0, sizeof raw);
    return hex;
}

- (NSString *)firstModelIn:(NSString *)dir
{
    NSArray *names = [[NSFileManager.defaultManager contentsOfDirectoryAtPath:dir error:NULL]
        sortedArrayUsingSelector:@selector(compare:)];
    for (NSString *n in names)
        if (![n hasPrefix:@"."] && [n.pathExtension.lowercaseString isEqualToString:@"gguf"])
            return [dir stringByAppendingPathComponent:n];
    return nil;
}

- (NSString *)bundledModelsDir
{
    return [NSBundle.mainBundle.resourcePath stringByAppendingPathComponent:@"models"];
}

/* The model the engine should use, or nil (see MODELS above). */
- (NSString *)currentModel
{
    NSString *chosen = [NSUserDefaults.standardUserDefaults stringForKey:kModelPathKey];
    if (chosen && [NSFileManager.defaultManager isReadableFileAtPath:chosen]) return chosen;
    NSString *m = [self firstModelIn:self.modelsDir];
    return m ? m : [self firstModelIn:[self bundledModelsDir]];
}

- (void)startEngine
{
    NSURL *exe = [NSBundle.mainBundle URLForAuxiliaryExecutable:@"zxv-engine"];
    self.token = [self newToken];
    if (!exe || !self.token) {
        [self showMessage:@"ZXV cannot start"
                   detail:exe ? @"No secure random source." : @"The engine is missing."];
        return;
    }
    self.port = 0;
    NSMutableArray *args = [@[ @"--no-window", @"--port", @"0", @"--exit-with-parent" ] mutableCopy];
    NSString *dylib = [NSBundle.mainBundle.privateFrameworksPath
        stringByAppendingPathComponent:@"libzxv.dylib"];
    self.rpcPath = nil;
    if ([NSFileManager.defaultManager fileExistsAtPath:dylib]) { /* the engine is zxvd */
        NSString *sock = [[self.supportDir stringByAppendingPathComponent:@"run"]
            stringByAppendingPathComponent:@"zxvd.sock"];
        if (strlen(sock.fileSystemRepresentation) < 100) { /* sun_path is 104 on macOS */
            [args addObjectsFromArray:@[ @"--rpc", sock ]];
            self.rpcPath = sock;
        }
        /* the owner's books are saved and reloaded at every start (zxvd_rpc.h F1) */
        [args addObjectsFromArray:@[
            @"--books", [self.supportDir stringByAppendingPathComponent:@"books.zxvb"]
        ]];
        /* Wake's history across restarts (zxv_journal.h) */
        [args addObjectsFromArray:@[
            @"--journal", [self.supportDir stringByAppendingPathComponent:@"journal"]
        ]];
        /* Shepherd's screen time and limits (zxv_screen.h) */
        [args addObjectsFromArray:@[
            @"--screen", [self.supportDir stringByAppendingPathComponent:@"screen.zxst"]
        ]];
        /* Waymark's saved places and location permissions (zxv_place.h) */
        [args addObjectsFromArray:@[
            @"--places", [self.supportDir stringByAppendingPathComponent:@"places.zxpl"]
        ]];
    }
    NSString *model = [self currentModel];
    if (model) {
        [args addObjectsFromArray:@[ @"--model", model ]];
    } else {
        [args addObjectsFromArray:@[ @"--models-dir", self.modelsDir ]];
    }
    NSMutableDictionary *env = [NSProcessInfo.processInfo.environment mutableCopy];
    env[@"ZXV_TOKEN"] = self.token;

    NSTask *t = [[NSTask alloc] init];
    t.executableURL = exe;
    t.arguments = args;
    t.environment = env;
    t.currentDirectoryURL = [NSURL fileURLWithPath:self.supportDir isDirectory:YES];
    self.engineIn = [NSPipe pipe];
    NSPipe *out = [NSPipe pipe];
    t.standardInput = self.engineIn;
    t.standardOutput = out;
    NSString *logPath = [self.logsDir stringByAppendingPathComponent:@"engine.log"];
    NSDictionary *st = [NSFileManager.defaultManager attributesOfItemAtPath:logPath error:NULL];
    if (!st || st.fileSize > 4u * 1024u * 1024u) /* start afresh rather than grow forever */
        [NSFileManager.defaultManager createFileAtPath:logPath
                                              contents:nil
                                            attributes:@{NSFilePosixPermissions : @0600}];
    self.engineLog = [NSFileHandle fileHandleForWritingAtPath:logPath];
    [self.engineLog seekToEndOfFile];
    t.standardError = self.engineLog ? (id) self.engineLog : (id) NSFileHandle.fileHandleWithNullDevice;

    self.engineOut = [NSMutableData data];
    __weak ZXVApp *weakSelf = self;
    out.fileHandleForReading.readabilityHandler = ^(NSFileHandle *h) {
      NSData *d = h.availableData;
      if (d.length == 0) {
          h.readabilityHandler = nil;
          return;
      }
      dispatch_async(dispatch_get_main_queue(), ^{
        [weakSelf engineSaid:d];
      });
    };
    t.terminationHandler = ^(NSTask *task) {
      dispatch_async(dispatch_get_main_queue(), ^{
        [weakSelf engineEnded:task];
      });
    };
    NSError *err = nil;
    if (![t launchAndReturnError:&err]) {
        [self showMessage:@"ZXV cannot start" detail:err.localizedDescription ?: @""];
        return;
    }
    self.engine = t;
}

- (void)engineSaid:(NSData *)d
{
    [self.engineOut appendData:d]; /* not logged: it carries the token */
    NSString *all = [[NSString alloc] initWithData:self.engineOut encoding:NSUTF8StringEncoding];
    if (self.port || !all) return;
    for (NSString *line in [all componentsSeparatedByString:@"\n"]) {
        if (![line hasPrefix:@"ZXV-URL: "]) continue;
        NSURLComponents *u = [NSURLComponents componentsWithString:[line substringFromIndex:9]];
        NSString *want = [@"token=" stringByAppendingString:self.token];
        if (!u || ![u.host isEqualToString:@"127.0.0.1"] || ![u.fragment isEqualToString:want] ||
            u.port.integerValue <= 0)
            continue; /* not our engine's line: never load it */
        self.port = u.port.integerValue;
        [self.web loadRequest:[NSURLRequest requestWithURL:u.URL]];
        [self startNotesBadge];
        return;
    }
}

- (void)engineEnded:(NSTask *)task
{
    if (task != self.engine) return;
    self.engine = nil;
    self.port = 0;
    [self.notesTimer invalidate];
    self.notesTimer = nil;
    NSApp.dockTile.badgeLabel = nil;
    [self.engineLog closeFile];
    self.engineLog = nil;
    if (self.quitting) {
        [NSApp replyToApplicationShouldTerminate:YES];
        return;
    }
    if (self.restarting) {
        self.restarting = NO;
        [self startEngine];
        return;
    }
    if (task.terminationStatus == 0 && task.terminationReason == NSTaskTerminationReasonExit) {
        [NSApp terminate:nil]; /* the window's own Quit button */
        return;
    }
    [self notify:@"ZXV stopped" body:@"The swarm engine stopped unexpectedly. View > Restart Engine starts it again."];
    [self showMessage:@"The swarm engine stopped"
               detail:@"Choose View > Restart Engine to start it again. Details are in "
                      @"~/Library/Logs/ZXV/engine.log."];
}

/* Stop the engine politely: zxvd is asked over its socket first; closing
 * its stdin makes any engine exit cleanly; a stubborn one is terminated
 * after two seconds. */
- (void)stopEngine
{
    NSTask *t = self.engine;
    if (!t) return;
    NSString *sock = self.rpcPath;
    NSFileHandle *in = self.engineIn.fileHandleForWriting;
    dispatch_async(self.rpcQueue, ^{
      if (sock) {
          char reply[512];
          zxv_rpc_call(sock.fileSystemRepresentation,
                       "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"zxvd.shutdown\"}", reply,
                       sizeof reply, 1000);
      }
      dispatch_async(dispatch_get_main_queue(), ^{
        [in closeFile];
      });
    });
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 2 * NSEC_PER_SEC), dispatch_get_main_queue(), ^{
      if (t.running) [t terminate];
    });
}

/* The Dock badge: the unread count from zxvd's notes.list, every 15 s. */
- (void)startNotesBadge
{
    [self.notesTimer invalidate];
    self.notesTimer = nil;
    if (!self.rpcPath) return;
    self.notesTimer = [NSTimer scheduledTimerWithTimeInterval:15
                                                       target:self
                                                     selector:@selector(refreshBadge:)
                                                     userInfo:nil
                                                      repeats:YES];
    [self refreshBadge:nil];
}

- (void)refreshBadge:(NSTimer *)timer
{
    (void) timer;
    NSString *sock = self.rpcPath;
    if (!sock) return;
    dispatch_async(self.rpcQueue, ^{
      static char reply[65536]; /* only ever touched on the serial rpcQueue */
      long n = -1;
      if (zxv_rpc_call(sock.fileSystemRepresentation,
                       "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"notes.list\"}", reply,
                       sizeof reply, 2000) >= 0)
          n = zxv_rpc_unread(reply);
      dispatch_async(dispatch_get_main_queue(), ^{
        if (!self.engine) return;
        NSApp.dockTile.badgeLabel = n > 0 ? [NSString stringWithFormat:@"%ld", n] : nil;
      });
    });
}

- (void)restartEngine:(id)sender
{
    (void) sender;
    if (self.engine) {
        self.restarting = YES;
        [self showMessage:@"Restarting the swarm…" detail:@""];
        [self stopEngine];
    } else {
        [self startEngine];
    }
}

- (void)reloadWindow:(id)sender
{
    (void) sender;
    if (self.port) {
        NSString *s = [NSString
            stringWithFormat:@"http://127.0.0.1:%ld/#token=%@", (long) self.port, self.token];
        [self.web loadRequest:[NSURLRequest requestWithURL:[NSURL URLWithString:s]]];
    }
}

/* ---------------------------------------------------------------- quit */

- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)sender
{
    (void) sender;
    if (self.fetcher.running) [self.fetcher terminate];
    if (!self.engine) return NSTerminateNow;
    self.quitting = YES;
    [self stopEngine];
    return NSTerminateLater; /* answered in engineEnded: */
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender
{
    (void) sender;
    return YES; /* closing the window stops the swarm: nothing runs unseen */
}

/* ------------------------------------------------------------ web view */

- (BOOL)isEngineURL:(NSURL *)u
{
    return self.port && [u.scheme isEqualToString:@"http"] && [u.host isEqualToString:@"127.0.0.1"] &&
           u.port.integerValue == self.port;
}

- (void)webView:(WKWebView *)webView
    decidePolicyForNavigationAction:(WKNavigationAction *)action
                    decisionHandler:(void (^)(WKNavigationActionPolicy))decisionHandler
{
    (void) webView;
    NSURL *u = action.request.URL;
    if ([self isEngineURL:u] || [u.absoluteString isEqualToString:@"about:blank"]) {
        decisionHandler(WKNavigationActionPolicyAllow);
        return;
    }
    if (action.navigationType == WKNavigationTypeLinkActivated &&
        ([u.scheme isEqualToString:@"https"] || [u.scheme isEqualToString:@"http"]))
        [NSWorkspace.sharedWorkspace openURL:u];
    decisionHandler(WKNavigationActionPolicyCancel);
}

- (WKWebView *)webView:(WKWebView *)webView
    createWebViewWithConfiguration:(WKWebViewConfiguration *)configuration
               forNavigationAction:(WKNavigationAction *)action
                    windowFeatures:(WKWindowFeatures *)features
{
    (void) webView;
    (void) configuration;
    (void) features;
    NSURL *u = action.request.URL;
    if ([u.scheme isEqualToString:@"https"] || [u.scheme isEqualToString:@"http"])
        [NSWorkspace.sharedWorkspace openURL:u];
    return nil; /* never a second web view */
}

- (void)webView:(WKWebView *)webView
    runJavaScriptAlertPanelWithMessage:(NSString *)message
                      initiatedByFrame:(WKFrameInfo *)frame
                     completionHandler:(void (^)(void))completionHandler
{
    (void) webView;
    (void) frame;
    NSAlert *a = [[NSAlert alloc] init];
    a.messageText = message;
    [a beginSheetModalForWindow:self.window
              completionHandler:^(NSModalResponse r) {
                (void) r;
                completionHandler();
              }];
}

/* The staple apps' Import buttons (an <input type=file>): the files the
 * owner picks are read by the page and written into the ZXV folder. */
- (void)webView:(WKWebView *)webView
    runOpenPanelWithParameters:(WKOpenPanelParameters *)parameters
              initiatedByFrame:(WKFrameInfo *)frame
             completionHandler:(void (^)(NSArray<NSURL *> *URLs))completionHandler
{
    (void) webView;
    (void) frame;
    NSOpenPanel *p = [NSOpenPanel openPanel];
    p.canChooseFiles = YES;
    p.canChooseDirectories = NO;
    p.allowsMultipleSelection = parameters.allowsMultipleSelection;
    [p beginSheetModalForWindow:self.window
              completionHandler:^(NSModalResponse r) {
                completionHandler(r == NSModalResponseOK ? p.URLs : nil);
              }];
}

- (void)webViewWebContentProcessDidTerminate:(WKWebView *)webView
{
    (void) webView;
    [self reloadWindow:nil];
}

- (void)userContentController:(WKUserContentController *)ucc
      didReceiveScriptMessage:(WKScriptMessage *)msg
{
    (void) ucc;
    WKSecurityOrigin *o = msg.frameInfo.securityOrigin;
    if (!self.port || ![o.protocol isEqualToString:@"http"] || ![o.host isEqualToString:@"127.0.0.1"] ||
        o.port != self.port)
        return; /* only the engine's own page may post notifications */
    if (![msg.body isKindOfClass:NSDictionary.class]) return;
    NSDictionary *b = msg.body;
    if ([b[@"type"] isEqual:@"notify"] && [b[@"title"] isKindOfClass:NSString.class])
        [self notify:b[@"title"] body:[b[@"body"] isKindOfClass:NSString.class] ? b[@"body"] : @""];
}

/* -------------------------------------------------------- notifications */

- (void)notify:(NSString *)title body:(NSString *)body
{
    if (title.length > 120) title = [title substringToIndex:120];
    if (body.length > 400) body = [body substringToIndex:400];
    if (@available(macOS 10.14, *)) {
        UNUserNotificationCenter *c = UNUserNotificationCenter.currentNotificationCenter;
        /* asked for the first time a notification is due, never at launch */
        [c requestAuthorizationWithOptions:UNAuthorizationOptionAlert | UNAuthorizationOptionSound
                         completionHandler:^(BOOL granted, NSError *error) {
                           (void) error;
                           if (!granted) return;
                           UNMutableNotificationContent *n =
                               [[UNMutableNotificationContent alloc] init];
                           n.title = title;
                           n.body = body;
                           UNNotificationRequest *r = [UNNotificationRequest
                               requestWithIdentifier:NSUUID.UUID.UUIDString
                                             content:n
                                             trigger:nil];
                           [c addNotificationRequest:r withCompletionHandler:nil];
                         }];
    }
}

- (void)userNotificationCenter:(UNUserNotificationCenter *)center
       willPresentNotification:(UNNotification *)notification
         withCompletionHandler:(void (^)(UNNotificationPresentationOptions))handler
{
    (void) center;
    (void) notification;
    if (@available(macOS 11.0, *)) {
        handler(UNNotificationPresentationOptionBanner | UNNotificationPresentationOptionList);
    } else {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        handler(UNNotificationPresentationOptionAlert);
#pragma clang diagnostic pop
    }
}

/* --------------------------------------------------------------- models */

- (NSString *)manifestPath
{
    return [NSBundle.mainBundle pathForResource:@"models" ofType:@"manifest"];
}

/* True when the bundled manifest names at least one model. */
- (BOOL)manifestHasEntries
{
    NSString *m = [NSString stringWithContentsOfFile:[self manifestPath] ?: @""
                                            encoding:NSUTF8StringEncoding
                                               error:NULL];
    for (NSString *line in [m componentsSeparatedByString:@"\n"]) {
        NSString *t = [line stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet];
        if (t.length && ![t hasPrefix:@"#"]) return YES;
    }
    return NO;
}

- (void)offerModelIfNone
{
    NSUserDefaults *d = NSUserDefaults.standardUserDefaults;
    if ([self currentModel] || [d boolForKey:kAskedModelKey]) return;
    [d setBool:YES forKey:kAskedModelKey];
    NSAlert *a = [[NSAlert alloc] init];
    a.messageText = @"No AI model is installed";
    a.informativeText = @"ZXV runs without one, but it cannot write answers until a GGUF model "
                        @"is installed. You can do this later from the Model menu.";
    BOOL canFetch = [self manifestHasEntries];
    if (canFetch) [a addButtonWithTitle:@"Download Model"];
    [a addButtonWithTitle:@"Choose GGUF File…"];
    [a addButtonWithTitle:@"Later"];
    [a beginSheetModalForWindow:self.window
              completionHandler:^(NSModalResponse r) {
                NSInteger i = r - NSAlertFirstButtonReturn;
                if (canFetch && i == 0)
                    [self fetchModel:nil];
                else if (i == (canFetch ? 1 : 0))
                    [self chooseModel:nil];
              }];
}

- (void)chooseModel:(id)sender
{
    (void) sender;
    NSOpenPanel *p = NSOpenPanel.openPanel;
    p.canChooseDirectories = NO;
    p.allowsMultipleSelection = NO;
    p.message = @"Choose a GGUF model file (it stays where it is; ZXV only reads it).";
#if __has_include(<UniformTypeIdentifiers/UniformTypeIdentifiers.h>)
    if (@available(macOS 11.0, *)) {
        UTType *g = [UTType typeWithFilenameExtension:@"gguf"];
        if (g) p.allowedContentTypes = @[ g ];
    } else
#endif
    {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        p.allowedFileTypes = @[ @"gguf" ];
#pragma clang diagnostic pop
    }
    [p beginSheetModalForWindow:self.window
              completionHandler:^(NSModalResponse r) {
                if (r != NSModalResponseOK || !p.URL.path) return;
                [NSUserDefaults.standardUserDefaults setObject:p.URL.path forKey:kModelPathKey];
                [self restartEngine:nil];
              }];
}

- (void)clearModel:(id)sender
{
    (void) sender;
    [NSUserDefaults.standardUserDefaults removeObjectForKey:kModelPathKey];
    [self restartEngine:nil];
}

- (void)fetchModel:(id)sender
{
    (void) sender;
    NSString *script = [NSBundle.mainBundle pathForResource:@"zxv-model-fetch" ofType:@"sh"];
    if (!script || ![self manifestHasEntries]) {
        NSAlert *a = [[NSAlert alloc] init];
        a.messageText = @"No model to fetch";
        a.informativeText = @"This build's model manifest names no models yet. Choose a GGUF file "
                            @"you already have instead (Model > Choose Model File).";
        [a beginSheetModalForWindow:self.window completionHandler:nil];
        return;
    }
    if (self.fetcher.running) return;
    NSTask *t = [[NSTask alloc] init];
    t.executableURL = [NSURL fileURLWithPath:@"/bin/sh"];
    t.arguments = @[ script, [self manifestPath], self.modelsDir ];
    NSString *logPath = [self.logsDir stringByAppendingPathComponent:@"fetch.log"];
    [NSFileManager.defaultManager createFileAtPath:logPath contents:nil attributes:nil];
    NSFileHandle *log = [NSFileHandle fileHandleForWritingAtPath:logPath];
    t.standardOutput = log ?: NSFileHandle.fileHandleWithNullDevice;
    t.standardError = log ?: NSFileHandle.fileHandleWithNullDevice;
    __weak ZXVApp *weakSelf = self;
    t.terminationHandler = ^(NSTask *task) {
      dispatch_async(dispatch_get_main_queue(), ^{
        ZXVApp *s = weakSelf;
        if (!s) return;
        s.fetchItem.title = @"Download Model…";
        s.fetchItem.enabled = YES;
        if (s.quitting) return;
        if (task.terminationStatus == 0) {
            [s notify:@"Model installed" body:@"The model was fetched and checked. Restarting the swarm with it."];
            [s restartEngine:nil];
        } else {
            [s notify:@"Model fetch failed" body:@"See ~/Library/Logs/ZXV/fetch.log. You can choose a GGUF file instead."];
        }
      });
    };
    NSError *err = nil;
    if ([t launchAndReturnError:&err]) {
        self.fetcher = t;
        self.fetchItem.title = @"Fetching Model…";
        self.fetchItem.enabled = NO;
        [self notify:@"Fetching a model" body:@"ZXV keeps running while the model downloads."];
    }
}

- (BOOL)validateMenuItem:(NSMenuItem *)item
{
    if (item == self.fetchItem) return !self.fetcher.running;
    return YES;
}

- (void)showModels:(id)sender
{
    (void) sender;
    [NSWorkspace.sharedWorkspace openURL:[NSURL fileURLWithPath:self.modelsDir isDirectory:YES]];
}

- (void)showLogs:(id)sender
{
    (void) sender;
    [NSWorkspace.sharedWorkspace openURL:[NSURL fileURLWithPath:self.logsDir isDirectory:YES]];
}

- (void)showHelp:(id)sender
{
    (void) sender;
    NSString *doc = [NSBundle.mainBundle pathForResource:@"MAC_APP" ofType:@"md"];
    if (doc) [NSWorkspace.sharedWorkspace openURL:[NSURL fileURLWithPath:doc]];
}

@end

static ZXVApp *gDelegate;

int main(int argc, const char *argv[])
{
    (void) argc;
    (void) argv;
    @autoreleasepool {
        NSApplication *app = NSApplication.sharedApplication;
        app.activationPolicy = NSApplicationActivationPolicyRegular; /* Dock icon, menu bar */
        gDelegate = [[ZXVApp alloc] init]; /* NSApp holds its delegate weakly */
        app.delegate = gDelegate;
        [app run];
    }
    return 0;
}
