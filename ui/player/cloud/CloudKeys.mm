#include "cloud/CloudKeys.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#import <Foundation/Foundation.h>
#import <Security/Security.h>

namespace cloudkeys {

namespace {

constexpr const char* kService = "TechieVideoEditor";

// Tests: a JSON file in TVE_KEY_STORE.
QString testStore()
{
    const QString folder = qEnvironmentVariable("TVE_KEY_STORE");
    return folder.isEmpty() ? QString() : folder + QStringLiteral("/keys.json");
}

QJsonObject readTestStore()
{
    QFile file(testStore());
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

bool writeTestStore(const QJsonObject& keys)
{
    QDir().mkpath(QFileInfo(testStore()).absolutePath());
    QSaveFile file(testStore());
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(QJsonDocument(keys).toJson());
    return file.commit();
}

NSMutableDictionary* query(const QString& provider)
{
    NSMutableDictionary* q = [NSMutableDictionary dictionary];
    q[(id)kSecClass] = (id)kSecClassGenericPassword;
    q[(id)kSecAttrService] = @(kService);
    q[(id)kSecAttrAccount] = provider.toNSString();
    return q;
}

} // namespace

bool store(const QString& provider, const QString& key, QString* error)
{
    const QString trimmed = key.trimmed();
    if (trimmed.isEmpty()) {
        if (error)
            *error = QStringLiteral("The key is empty.");
        return false;
    }
    if (!testStore().isEmpty()) {
        QJsonObject keys = readTestStore();
        keys.insert(provider, trimmed);
        return writeTestStore(keys);
    }
    @autoreleasepool {
        NSData* data = [trimmed.toNSString() dataUsingEncoding:NSUTF8StringEncoding];
        NSMutableDictionary* existing = query(provider);
        OSStatus status = SecItemUpdate((CFDictionaryRef)existing, (CFDictionaryRef) @{(id)kSecValueData: data});
        if (status == errSecItemNotFound) {
            NSMutableDictionary* item = query(provider);
            item[(id)kSecValueData] = data;
            item[(id)kSecAttrLabel] = [NSString stringWithFormat:@"TechieVideoEditor — %@", provider.toNSString()];
            item[(id)kSecAttrAccessible] = (id)kSecAttrAccessibleWhenUnlocked;
            status = SecItemAdd((CFDictionaryRef)item, nullptr);
        }
        if (status != errSecSuccess) {
            if (error) {
                CFStringRef message = SecCopyErrorMessageString(status, nullptr);
                *error = message ? QString::fromCFString(message) : QStringLiteral("The Keychain refused the key.");
                if (message)
                    CFRelease(message);
            }
            return false;
        }
    }
    return true;
}

QString read(const QString& provider)
{
    if (!testStore().isEmpty())
        return readTestStore().value(provider).toString();
    QString key;
    @autoreleasepool {
        NSMutableDictionary* q = query(provider);
        q[(id)kSecReturnData] = @YES;
        q[(id)kSecMatchLimit] = (id)kSecMatchLimitOne;
        CFTypeRef result = nullptr;
        if (SecItemCopyMatching((CFDictionaryRef)q, &result) == errSecSuccess && result) {
            NSData* data = (NSData*)result;
            key = QString::fromUtf8(static_cast<const char*>(data.bytes), qsizetype(data.length));
            CFRelease(result);
        }
    }
    return key;
}

bool has(const QString& provider)
{
    if (!testStore().isEmpty())
        return readTestStore().contains(provider);
    bool found = false;
    @autoreleasepool {
        NSMutableDictionary* q = query(provider);
        q[(id)kSecMatchLimit] = (id)kSecMatchLimitOne;
        found = SecItemCopyMatching((CFDictionaryRef)q, nullptr) == errSecSuccess;
    }
    return found;
}

void remove(const QString& provider)
{
    if (!testStore().isEmpty()) {
        QJsonObject keys = readTestStore();
        keys.remove(provider);
        writeTestStore(keys);
        return;
    }
    @autoreleasepool {
        SecItemDelete((CFDictionaryRef)query(provider));
    }
}

} // namespace cloudkeys
