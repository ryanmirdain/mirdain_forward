#include "platform/DragWatcher.h"
#include "platform/macos/MirdainStyle.h"

#include <QFileInfo>
#include <QSystemTrayIcon>

#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>

static NSArray<NSURL *> *modelURLs(NSPasteboard *pasteboard)
{
    NSArray<NSURL *> *urls = [pasteboard readObjectsForClasses:@[NSURL.class]
        options:@{NSPasteboardURLReadingFileURLsOnlyKey: @YES}];
    NSMutableArray<NSURL *> *models = [NSMutableArray array];
    for (NSURL *url in urls) {
        NSString *extension = url.pathExtension.lowercaseString;
        if ([extension isEqualToString:@"stl"] || [extension isEqualToString:@"3mf"]) [models addObject:url];
    }
    return models;
}

// MARK: Mirdain style

static NSColor *Surface() { return mirdainSurface(); }
static NSColor *Border() { return mirdainBorder(); }
static NSColor *Text() { return mirdainText(); }
static NSColor *Muted() { return mirdainMuted(); }
static NSColor *Accent() { return mirdainAccent(); }

// Draws one line, truncated to rect's width, in a flipped view.
static void drawText(NSString *text, NSFont *font, NSColor *color, NSRect rect)
{
    auto *style = [[NSMutableParagraphStyle alloc] init];
    style.lineBreakMode = NSLineBreakByTruncatingTail;
    [text drawWithRect:rect options:NSStringDrawingUsesLineFragmentOrigin | NSStringDrawingTruncatesLastVisibleLine
        attributes:@{NSFontAttributeName: font, NSForegroundColorAttributeName: color, NSParagraphStyleAttributeName: style}];
}

// A label for the card's header, wrapped to width.
static NSTextField *headerLabel(NSString *text, NSFont *font, NSColor *color, CGFloat width)
{
    auto *label = [NSTextField wrappingLabelWithString:text];
    label.font = font;
    label.textColor = color;
    label.preferredMaxLayoutWidth = width;
    label.frame = NSMakeRect(0, 0, width, label.fittingSize.height);
    return label;
}

// MARK: Views

// One printer's drop target.
@interface PrinterBox : NSView
@property (copy) NSString *name;
@property (copy) void (^dropped)(NSArray<NSURL *> *urls);
@property (nonatomic) BOOL highlighted;
@end

@implementation PrinterBox

- (instancetype)initWithFrame:(NSRect)frame
{
    self = [super initWithFrame:frame];
    [self registerForDraggedTypes:@[NSPasteboardTypeFileURL]];
    return self;
}

- (BOOL)isFlipped { return YES; }

- (void)setHighlighted:(BOOL)highlighted
{
    _highlighted = highlighted;
    self.needsDisplay = YES;
}

- (void)drawRect:(NSRect)dirtyRect
{
    auto *box = [NSBezierPath bezierPathWithRoundedRect:NSInsetRect(self.bounds, 0.5, 0.5) xRadius:3 yRadius:3];
    [(_highlighted ? mirdainColor(0xe967c8, 0.12) : Surface()) setFill];
    [box fill];
    [(_highlighted ? Accent() : Border()) setStroke];
    [box stroke];
    drawText(_name.length ? _name : @"No printers yet", aileron(true, 13), _highlighted ? Accent() : Text(),
        NSMakeRect(14, (self.bounds.size.height - 18) / 2, self.bounds.size.width - 28, 18));
}

- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender
{
    if (!modelURLs(sender.draggingPasteboard).count) return NSDragOperationNone;
    self.highlighted = YES;
    return NSDragOperationCopy;
}

- (void)draggingExited:(id<NSDraggingInfo>)sender { self.highlighted = NO; }

- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender
{
    self.highlighted = NO;
    NSArray<NSURL *> *urls = modelURLs(sender.draggingPasteboard);
    if (!urls.count) return NO;
    _dropped(urls);
    return YES;
}

@end

// The notes step after a drop: Return or Slice sends the notes, Skip sends none, and × or
// Esc cancels the drop.
@interface DropNotes : NSObject <NSTextFieldDelegate>
@property (copy) void (^send)(NSString *notes);
@property (copy) void (^cancel)(void);
@property (weak) NSTextField *field;
@end

@implementation DropNotes

- (void)submit:(id)sender { _send(_field.stringValue); }
- (void)skip:(id)sender { _send(@""); }
- (void)dismiss:(id)sender { _cancel(); }

- (BOOL)control:(NSControl *)control textView:(NSTextView *)textView doCommandBySelector:(SEL)command
{
    if (command != @selector(cancelOperation:)) return NO;
    _cancel();
    return YES;
}

@end

// Edits the notes field. Unlike the standard field editor it takes no drops, so models dragged
// over the field fall through to the card instead of pasting their paths into the notes.
@interface NotesEditor : NSTextView
@end

@implementation NotesEditor
- (void)updateDragTypeRegistration { [self unregisterDraggedTypes]; }
@end

// Gives the notes field its own editor.
@interface NotesPanelDelegate : NSObject <NSWindowDelegate>
@end

@implementation NotesPanelDelegate {
    NotesEditor *_editor;
}

- (id)windowWillReturnFieldEditor:(NSWindow *)window toObject:(id)client
{
    if (![client isKindOfClass:NSTextField.class]) return nil;
    if (!_editor) {
        _editor = [[NotesEditor alloc] initWithFrame:NSZeroRect];
        _editor.fieldEditor = YES;
    }
    return _editor;
}

@end

// The notes card as a drop target: more models dropped on it join the drop. While models are
// being dragged anywhere (armed), the notes fade out for a "drop to add" prompt with a dashed
// outline, which brightens while the drag is over the card.
@interface NotesDropZone : NSView
@property (copy) void (^added)(NSArray<NSURL *> *urls);
@property (strong) NSView *hint;
- (void)setArmed:(BOOL)armed;
@end

@implementation NotesDropZone {
    BOOL _highlighted;
    BOOL _armed;
}

- (void)setArmed:(BOOL)armed
{
    if (armed == _armed) return;
    _armed = armed;
    self.needsDisplay = YES;
    [NSAnimationContext runAnimationGroup:^(NSAnimationContext *context) {
        context.duration = 0.16;
        for (NSView *view in self.subviews) view.animator.alphaValue = (view == self.hint) == armed ? 1 : 0;
    }];
}

- (instancetype)initWithFrame:(NSRect)frame
{
    self = [super initWithFrame:frame];
    [self registerForDraggedTypes:@[NSPasteboardTypeFileURL]];
    return self;
}

- (BOOL)isFlipped { return YES; }

- (void)setHighlighted:(BOOL)highlighted
{
    _highlighted = highlighted;
    self.needsDisplay = YES;
}

- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender
{
    if (!modelURLs(sender.draggingPasteboard).count) return NSDragOperationNone;
    [self setHighlighted:YES];
    return NSDragOperationCopy;
}

- (void)draggingExited:(id<NSDraggingInfo>)sender { [self setHighlighted:NO]; }

- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender
{
    [self setHighlighted:NO];
    NSArray<NSURL *> *urls = modelURLs(sender.draggingPasteboard);
    if (!urls.count) return NO;
    _added(urls);
    return YES;
}

- (void)drawRect:(NSRect)dirtyRect
{
    if (!_highlighted && !_armed) return;
    auto *outline = [NSBezierPath bezierPathWithRoundedRect:NSInsetRect(self.bounds, 4.5, 4.5) xRadius:4 yRadius:4];
    [mirdainColor(0xe967c8, _highlighted ? 0.12 : 0.04) setFill];
    [outline fill];
    const CGFloat dash[] = {4, 3};
    [outline setLineDash:dash count:2 phase:0];
    [mirdainColor(0xe967c8, _highlighted ? 1 : 0.55) setStroke];
    [outline stroke];
}

@end

// The notes field: Mirdain's dark fill and border, padded, with its line centered vertically.
@interface NotesFieldCell : NSTextFieldCell
@end

@implementation NotesFieldCell

- (NSRect)padded:(NSRect)rect
{
    const CGFloat line = [[[NSLayoutManager alloc] init] defaultLineHeightForFont:self.font];
    return NSMakeRect(rect.origin.x + 9, rect.origin.y + (rect.size.height - line) / 2, rect.size.width - 18, line);
}

- (NSRect)drawingRectForBounds:(NSRect)rect { return [self padded:rect]; }

- (void)editWithFrame:(NSRect)rect inView:(NSView *)view editor:(NSText *)editor delegate:(id)delegate event:(NSEvent *)event
{
    [super editWithFrame:[self padded:rect] inView:view editor:editor delegate:delegate event:event];
}

- (void)selectWithFrame:(NSRect)rect inView:(NSView *)view editor:(NSText *)editor delegate:(id)delegate
    start:(NSInteger)start length:(NSInteger)length
{
    [super selectWithFrame:[self padded:rect] inView:view editor:editor delegate:delegate start:start length:length];
}

@end

// Its border turns pink while it has the keyboard.
@interface NotesField : NSTextField
@end

@implementation NotesField

+ (Class)cellClass { return NotesFieldCell.class; }

// Dropped files belong to the card (NotesDropZone), not the text.
- (instancetype)initWithFrame:(NSRect)frame
{
    self = [super initWithFrame:frame];
    [self unregisterDraggedTypes];
    return self;
}

- (BOOL)becomeFirstResponder
{
    const BOOL focused = [super becomeFirstResponder];
    if (focused) {
        self.layer.borderColor = Accent().CGColor;
        // The field editor draws the caret.
        if (auto *editor = (NSTextView *)[self currentEditor]) editor.insertionPointColor = Accent();
    }
    return focused;
}

- (void)textDidEndEditing:(NSNotification *)notification
{
    [super textDidEndEditing:notification];
    self.layer.borderColor = Border().CGColor;
}

@end

static NSButton *noteButton(NSString *title, bool primary, id target, SEL action)
{
    auto *button = [NSButton buttonWithTitle:title target:target action:action];
    button.bordered = NO;
    button.attributedTitle = [[NSAttributedString alloc] initWithString:title attributes:@{
        NSFontAttributeName: aileron(true, 12), NSForegroundColorAttributeName: primary ? mirdainColor(0x010305) : Muted()}];
    if (primary) {
        button.wantsLayer = YES;
        button.layer.backgroundColor = Accent().CGColor;
        button.layer.cornerRadius = 4;
    }
    return button;
}

// MARK: Watcher

static constexpr CGFloat CardWidth = 240, Padding = 10, BoxHeight = 40, Gap = 8;
// The header's inset matches the printer names inside their boxes.
static constexpr CGFloat HeaderInset = Padding + 4;
// How far the card slides while it fades in and out.
static constexpr CGFloat Slide = 8;

struct DragWatcher::Impl {
    QSystemTrayIcon &tray;
    Targets targets;
    Dropped dropped;
    NSPanel *panel = nil;
    id monitor = nil;
    NSTimer *poll = nil;
    bool shown = false;
    // Asking for notes after a drop: the panel stays up through mouse-ups and ignores new drags.
    bool prompting = false;
    DropNotes *notes = nil;
    // The drop being prepared: its models, which more drops on the notes card add to, and printer.
    QStringList files;
    Target fileTarget;
    NSTextField *fileLine = nil;
    NotesDropZone *zone = nil;
    // The panel only keeps a weak reference to its delegate.
    NotesPanelDelegate *panelDelegate = nil;

    NSView *layout(const QList<Target> &printers);
    NSView *notesLayout();
    QString fileText() const;
    void addFiles(NSArray<NSURL *> *urls);
    void prompt(const QStringList &paths, const Target &target);
    void show();
    // slide moves the card up as it fades; without it, it fades in place, as when it hands the
    // drop to Gwaith's notification in the same spot.
    void hide(bool slide = true);
};

NSView *DragWatcher::Impl::layout(const QList<Target> &printers)
{
    const auto boxes = printers.isEmpty() ? QList<Target>{Target{}} : printers;
    // The header says what a drop does: Gwaith slices the models and prints them.
    const CGFloat textWidth = CardWidth - 2 * HeaderInset;
    auto *title = headerLabel(@"Slice & Send", aileron(true, 14), Text(), textWidth);
    auto *detail = headerLabel(@"Drop models on a printer. Gwaith slices them and starts the print.",
        aileron(false, 12), Muted(), textWidth);
    const CGFloat header = Padding + 2 + title.frame.size.height + 2 + detail.frame.size.height + 10;
    const CGFloat height = header + boxes.size() * BoxHeight + (boxes.size() - 1) * Gap + Padding;
    NSView *content;
    auto *root = mirdainCard(NSMakeSize(CardWidth, height), &content);
    [title setFrameOrigin:NSMakePoint(HeaderInset, Padding + 2)];
    [detail setFrameOrigin:NSMakePoint(HeaderInset, NSMaxY(title.frame) + 2)];
    [content addSubview:title];
    [content addSubview:detail];
    CGFloat y = header;
    // By value: the drop block keeps its own copy, since boxes is gone by the time of a drop.
    for (const Target target : boxes) {
        auto *box = [[PrinterBox alloc] initWithFrame:NSMakeRect(Padding, y, CardWidth - 2 * Padding, BoxHeight)];
        box.name = target.name.toNSString();
        box.dropped = ^(NSArray<NSURL *> *urls) {
            QStringList paths;
            for (NSURL *url in urls) paths.append(QString::fromNSString(url.path));
            prompt(paths, target);
        };
        [content addSubview:box];
        y += BoxHeight + Gap;
    }
    return root;
}

// One model by name; more by count ("3 files"), with the printer.
QString DragWatcher::Impl::fileText() const
{
    const auto models = files.size() == 1 ? QFileInfo(files.first()).fileName() : QString("%1 files").arg(files.size());
    return fileTarget.name.isEmpty() ? models : models + " on " + fileTarget.name;
}

void DragWatcher::Impl::addFiles(NSArray<NSURL *> *urls)
{
    for (NSURL *url in urls) {
        const auto path = QString::fromNSString(url.path);
        if (!files.contains(path)) files.append(path);
    }
    fileLine.stringValue = fileText().toNSString();
}

NSView *DragWatcher::Impl::notesLayout()
{
    const CGFloat width = CardWidth - 2 * HeaderInset - 20, fieldHeight = 30, buttonHeight = 26;
    auto *title = headerLabel(@"Add notes for Gwaith", aileron(true, 14), Text(), width);
    auto *detail = headerLabel(fileText().toNSString(), aileron(false, 12), Muted(), width);
    fileLine = detail;
    auto *field = [[NotesField alloc] initWithFrame:NSZeroRect];
    field.cell.scrollable = YES;
    field.cell.usesSingleLineMode = YES;
    field.cell.wraps = NO;
    field.editable = YES;
    field.bezeled = NO;
    field.bordered = NO;
    field.drawsBackground = YES;
    field.backgroundColor = mirdainColor(0x010305);
    field.textColor = Text();
    field.font = aileron(false, 13);
    field.placeholderAttributedString = [[NSAttributedString alloc] initWithString:@"e.g. PETG, add supports, 3 copies"
        attributes:@{NSFontAttributeName: aileron(false, 13), NSForegroundColorAttributeName: mirdainColor(0x687985)}];
    field.focusRingType = NSFocusRingTypeNone;
    field.wantsLayer = YES;
    field.layer.cornerRadius = 4;
    field.layer.borderWidth = 1;
    field.layer.borderColor = Border().CGColor;
    field.delegate = notes;
    field.target = notes;
    field.action = @selector(submit:);
    notes.field = field;
    auto *skip = noteButton(@"Skip", false, notes, @selector(skip:));
    auto *slice = noteButton(@"Slice", true, notes, @selector(submit:));

    const CGFloat fieldTop = Padding + 2 + title.frame.size.height + 2 + detail.frame.size.height + 10;
    const CGFloat buttonsTop = fieldTop + fieldHeight + 10;
    NSView *content;
    auto *root = mirdainCard(NSMakeSize(CardWidth, buttonsTop + buttonHeight + Padding), &content);
    [title setFrameOrigin:NSMakePoint(HeaderInset, Padding + 2)];
    [detail setFrameOrigin:NSMakePoint(HeaderInset, NSMaxY(title.frame) + 2)];
    field.frame = NSMakeRect(Padding, fieldTop, CardWidth - 2 * Padding, fieldHeight);
    slice.frame = NSMakeRect(CardWidth - Padding - 64, buttonsTop, 64, buttonHeight);
    skip.frame = NSMakeRect(NSMinX(slice.frame) - 8 - 48, buttonsTop, 48, buttonHeight);
    auto *close = [NSButton buttonWithImage:[NSImage imageWithSystemSymbolName:@"xmark" accessibilityDescription:@"Cancel"]
        target:notes action:@selector(dismiss:)];
    close.bordered = NO;
    close.contentTintColor = Muted();
    close.toolTip = @"Cancel";
    close.frame = NSMakeRect(CardWidth - Padding - 18, Padding, 18, 18);
    // Everything sits in the drop zone, so models dropped anywhere on the card are taken.
    zone = [[NotesDropZone alloc] initWithFrame:content.bounds];
    zone.added = ^(NSArray<NSURL *> *urls) { addFiles(urls); };
    [content addSubview:zone];
    for (NSView *view in @[title, detail, field, skip, slice, close]) [zone addSubview:view];
    auto *hint = headerLabel(@"Drop to add more models", aileron(true, 13), Text(), CardWidth - 2 * HeaderInset);
    hint.alignment = NSTextAlignmentCenter;
    [hint setFrameOrigin:NSMakePoint(HeaderInset, (content.bounds.size.height - hint.frame.size.height) / 2)];
    hint.alphaValue = 0;
    zone.hint = hint;
    [zone addSubview:hint];
    return root;
}

// Fades the printers out, grows the card down into the notes (its top stays under the icon), and
// fades them in, focusing the field. Sending, with notes or skipped, closes the panel and passes
// the drop on; cancelling closes it and drops nothing.
void DragWatcher::Impl::prompt(const QStringList &droppedPaths, const Target &droppedTarget)
{
    // Kept on the watcher, not captured: blocks capture C++ references as bare addresses, and more
    // models can join before sending.
    files = droppedPaths;
    fileTarget = droppedTarget;
    prompting = true;
    notes = [[DropNotes alloc] init];
    notes.send = ^(NSString *text) {
        if (!prompting) return;
        prompting = false;
        hide(false);
        dropped(files, fileTarget, QString::fromNSString(text).trimmed());
    };
    notes.cancel = ^{
        if (!prompting) return;
        prompting = false;
        hide();
    };
    NSView *old = mirdainCardContent(panel.contentView);
    const auto from = old.frame.size;
    [NSAnimationContext runAnimationGroup:^(NSAnimationContext *context) {
        context.duration = 0.12;
        context.timingFunction = [CAMediaTimingFunction functionWithName:kCAMediaTimingFunctionEaseIn];
        old.animator.alphaValue = 0;
    } completionHandler:^{
        if (!prompting) return;
        auto *root = notesLayout();
        NSView *content = mirdainCardContent(root);
        content.alphaValue = 0;
        [panel setFrame:mirdainTrayPanelFrame(tray, root.frame.size) display:NO];
        panel.contentView = root;
        mirdainGrowCard(root, from, 0.22);
        [NSAnimationContext runAnimationGroup:^(NSAnimationContext *context) {
            context.duration = 0.22;
            context.timingFunction = [CAMediaTimingFunction functionWithName:kCAMediaTimingFunctionEaseOut];
            content.animator.alphaValue = 1;
        }];
        [panel makeKeyWindow];
        [panel makeFirstResponder:notes.field];
    }];
}

void DragWatcher::Impl::show()
{
    // A drag while asking for notes invites another drop instead.
    if (prompting) {
        [zone setArmed:YES];
        return;
    }
    auto *content = layout(targets());
    const auto frame = mirdainTrayPanelFrame(tray, content.frame.size);
    [panel setFrame:NSOffsetRect(frame, 0, Slide) display:NO];
    panel.contentView = content;
    panel.alphaValue = 0;
    [panel orderFrontRegardless];
    shown = true;
    [NSAnimationContext runAnimationGroup:^(NSAnimationContext *context) {
        context.duration = 0.18;
        context.timingFunction = [CAMediaTimingFunction functionWithName:kCAMediaTimingFunctionEaseOut];
        panel.animator.alphaValue = 1;
        [panel.animator setFrame:frame display:YES];
    }];
}

void DragWatcher::Impl::hide(bool slide)
{
    if (!shown) return;
    shown = false;
    [NSAnimationContext runAnimationGroup:^(NSAnimationContext *context) {
        context.duration = 0.14;
        context.timingFunction = [CAMediaTimingFunction functionWithName:kCAMediaTimingFunctionEaseIn];
        panel.animator.alphaValue = 0;
        if (slide) [panel.animator setFrame:NSOffsetRect(panel.frame, 0, Slide) display:YES];
    } completionHandler:^{
        // A drag that started during the fade shows the panel again.
        if (!shown) [panel orderOut:nil];
    }];
}

DragWatcher::DragWatcher(QSystemTrayIcon &tray, Targets targets, Dropped dropped)
    : impl(new Impl{tray, std::move(targets), std::move(dropped)})
{
    auto *panel = mirdainPanel(true);
    impl->panelDelegate = [[NotesPanelDelegate alloc] init];
    panel.delegate = impl->panelDelegate;
    impl->panel = panel;

    // A drag in another app writes the system drag pasteboard once it starts. Each mouse down
    // polls it until the button is released, so nothing runs while the mouse is idle; comparing
    // its change count with the one at mouse down ignores what a previous drag left there.
    // Drags out of a Dock stack don't write this pasteboard, so they show no panel.
    Impl *state = impl.get();
    NSPasteboard *pasteboard = [NSPasteboard pasteboardWithName:NSPasteboardNameDrag];
    impl->monitor = [NSEvent addGlobalMonitorForEventsMatchingMask:NSEventMaskLeftMouseDown handler:^(NSEvent *) {
        [state->poll invalidate];
        const auto start = pasteboard.changeCount;
        __block bool checked = false;
        state->poll = [NSTimer scheduledTimerWithTimeInterval:0.05 repeats:YES block:^(NSTimer *timer) {
            if (!(NSEvent.pressedMouseButtons & 1)) {
                [timer invalidate];
                // Let a drop on the panel finish before it closes.
                dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 300 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
                    if (NSEvent.pressedMouseButtons & 1) return;
                    // The notes come back, now counting any models dropped on the card.
                    if (state->prompting) [state->zone setArmed:NO];
                    else state->hide();
                });
                return;
            }
            if (checked || pasteboard.changeCount == start) return;
            checked = true;
            if (modelURLs(pasteboard).count) state->show();
        }];
    }];
}

DragWatcher::~DragWatcher()
{
    [NSEvent removeMonitor:impl->monitor];
    [impl->poll invalidate];
    [impl->panel orderOut:nil];
}
