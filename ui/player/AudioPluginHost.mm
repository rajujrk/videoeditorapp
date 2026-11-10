#include "AudioPluginHost.h"
#include "Language.h"

#import <AppKit/AppKit.h>

#include <algorithm>
#include <functional>
#include <memory>

// Tells the host when a plug-in's window closes.
@interface TVEPluginWindowDelegate : NSObject <NSWindowDelegate> {
    std::function<void()>* _closed;
}
- (instancetype)initWithClosed:(std::function<void()>)closed;
- (void)forget;
@end

@implementation TVEPluginWindowDelegate
- (instancetype)initWithClosed:(std::function<void()>)closed
{
    if ((self = [super init]))
        _closed = new std::function<void()>(std::move(closed));
    return self;
}
- (void)forget
{
    delete _closed;
    _closed = nullptr;
}
- (void)windowWillClose:(NSNotification*)notification
{
    (void)notification;
    if (_closed) {
        auto closed = *_closed;
        closed();
    }
}
- (void)dealloc
{
    delete _closed;
    [super dealloc];
}
@end

AudioPluginHost::AudioPluginHost(QObject* parent) : QObject(parent) {}

AudioPluginHost::~AudioPluginHost()
{
    closeAll();
}

void AudioPluginHost::bring(Session& session, const tve::ClipEffect& effect)
{
    // Its state when that changed (from the project: undo, another window…), then its values.
    if (session.state != effect.state) {
        if (!effect.state.empty())
            session.plugin->loadState(tve::decodeBase64(effect.state));
        session.state = effect.state;
    }
    if (session.values != effect.values) {
        for (const auto& [param, value] : effect.values)
            if (!value.empty())
                session.plugin->setParameter(param, value.front());
        session.values = effect.values;
    }
}

tve::AudioPlugin* AudioPluginHost::instance(const QString& key, const tve::ClipEffect& effect, QString* error)
{
    auto it = m_sessions.find(key);
    if (it != m_sessions.end() && it->second.effectId != effect.effectId) {
        dropWindow(it->second);
        m_sessions.erase(it);
        it = m_sessions.end();
    }
    if (it == m_sessions.end()) {
        std::string why;
        auto plugin = tve::AudioPlugin::open(effect.effectId, &why);
        if (!plugin) {
            if (error)
                *error = coreText(why);
            return nullptr;
        }
        Session session;
        session.plugin = std::move(plugin);
        session.effectId = effect.effectId;
        session.pause = new QTimer(this);
        session.pause->setSingleShot(true);
        session.pause->setInterval(500);
        connect(session.pause, &QTimer::timeout, this, [this, key] { send(key); });
        it = m_sessions.emplace(key, std::move(session)).first;
        if (!effect.state.empty())
            it->second.plugin->loadState(tve::decodeBase64(effect.state));
        it->second.state = effect.state;
        it->second.plugin->setChangeHandler([this, key] {
            if (auto found = m_sessions.find(key); found != m_sessions.end())
                found->second.pause->start();
        });
    }
    bring(it->second, effect);
    return it->second.plugin.get();
}

QVariantList AudioPluginHost::parameters(const QString& key, const tve::ClipEffect& effect)
{
    QVariantList list;
    tve::AudioPlugin* plugin = instance(key, effect);
    if (!plugin)
        return list;
    for (const tve::PluginParameter& p : plugin->parameters()) {
        const bool stepped = p.steps > 1 && p.steps <= 64;
        list << QVariantMap{{QStringLiteral("id"), QString::fromStdString(p.id)},
                            {QStringLiteral("title"), QString::fromStdString(p.name)},
                            {QStringLiteral("type"), QStringLiteral("float")},
                            {QStringLiteral("min"), 0.0},
                            {QStringLiteral("max"), 1.0},
                            {QStringLiteral("default"), plugin->parameter(p.id)},
                            {QStringLiteral("steps"), stepped ? p.steps : 0},
                            {QStringLiteral("unit"), QString::fromStdString(p.unit)},
                            {QStringLiteral("keyframable"), true}};
    }
    return list;
}

QString AudioPluginHost::valueText(const QString& key, const tve::ClipEffect& effect, const QString& param, double value)
{
    tve::AudioPlugin* plugin = instance(key, effect);
    return plugin ? QString::fromStdString(plugin->displayValue(param.toStdString(), value)) : QString();
}

bool AudioPluginHost::openWindow(const QString& key, const tve::ClipEffect& effect, const QString& title, QString* error)
{
    tve::AudioPlugin* plugin = instance(key, effect, error);
    if (!plugin)
        return false;
    Session& session = m_sessions.at(key);
    if (session.window) {
        [static_cast<NSWindow*>(session.window) makeKeyAndOrderFront:nil];
        return true;
    }
    auto made = std::make_shared<bool>(false);
    plugin->requestEditor([this, made, key, title](void* view) {
        auto found = m_sessions.find(key);
        NSView* content = static_cast<NSView*>(view);
        if (found == m_sessions.end() || !content) {
            [content release];
            return;
        }
        Session& s = found->second;
        const NSRect frame = NSMakeRect(0, 0, std::max<CGFloat>(content.frame.size.width, 320),
                                        std::max<CGFloat>(content.frame.size.height, 160));
        NSWindow* window = [[NSWindow alloc] initWithContentRect:frame
                                                       styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
                                                                 | NSWindowStyleMaskMiniaturizable
                                                         backing:NSBackingStoreBuffered
                                                           defer:NO];
        window.releasedWhenClosed = NO;
        window.title = title.toNSString();
        window.contentView = content;
        [content release];
        auto* delegate = [[TVEPluginWindowDelegate alloc] initWithClosed:[this, key] {
            auto open = m_sessions.find(key);
            if (open == m_sessions.end())
                return;
            // What was changed goes to the project before the window goes.
            open->second.pause->stop();
            send(key);
            dropWindow(open->second);
        }];
        window.delegate = delegate;
        [window center];
        [window makeKeyAndOrderFront:nil];
        s.window = window;
        s.delegate = delegate;
        s.openedState = s.plugin->saveState();
        *made = true;
    });
    // Some give their view a moment later; the window opens then.
    (void)made;
    return true;
}

bool AudioPluginHost::isWindowOpen(const QString& key) const
{
    const auto it = m_sessions.find(key);
    return it != m_sessions.end() && it->second.window;
}

void AudioPluginHost::dropWindow(Session& session)
{
    if (!session.window)
        return;
    NSWindow* window = static_cast<NSWindow*>(session.window);
    auto* delegate = static_cast<TVEPluginWindowDelegate*>(session.delegate);
    session.window = nullptr;
    session.delegate = nullptr;
    [delegate forget];
    window.delegate = nil;
    session.plugin->editorClosed();
    [window orderOut:nil];
    window.contentView = [[[NSView alloc] init] autorelease];
    [window release];
    [delegate release];
}

void AudioPluginHost::closeWindow(const QString& key)
{
    const auto it = m_sessions.find(key);
    if (it == m_sessions.end() || !it->second.window)
        return;
    it->second.pause->stop();
    send(key);
    dropWindow(it->second);
}

void AudioPluginHost::closeAll()
{
    for (auto& [key, session] : m_sessions)
        dropWindow(session);
}

bool AudioPluginHost::editLive(const QString& key, const tve::ClipEffect& effect, const QString& param, double value)
{
    tve::AudioPlugin* plugin = instance(key, effect);
    if (!plugin)
        return false;
    plugin->setParameter(param.toStdString(), value);
    send(key);
    return true;
}

void AudioPluginHost::send(const QString& key)
{
    const auto it = m_sessions.find(key);
    if (it == m_sessions.end())
        return;
    Session& session = it->second;
    const std::string bytes = session.plugin->saveState();
    if (bytes.empty())
        return;
    const std::string state = tve::encodeBase64(bytes);
    if (state == session.state)
        return;
    // The project will hold this state and no single values over it.
    session.state = state;
    session.values.clear();
    emit edited(key, QString::fromStdString(state));
}

void AudioPluginHost::sync(const std::function<const tve::ClipEffect*(const QString&)>& find)
{
    for (auto it = m_sessions.begin(); it != m_sessions.end();) {
        const tve::ClipEffect* effect = find(it->first);
        if (!effect || effect->effectId != it->second.effectId) {
            dropWindow(it->second);
            it->second.pause->deleteLater();
            it = m_sessions.erase(it);
            continue;
        }
        bring(it->second, *effect);
        ++it;
    }
}
