#pragma once

#include <QString>

// The person's own API keys for cloud providers ("anthropic", "elevenlabs"), kept in their
// login Keychain (service "TechieVideoEditor", the provider as the account) — never in
// project files, settings or logs. TVE_KEY_STORE names a folder to keep them in a plain file
// instead (tests only: never the real Keychain from a test).
namespace cloudkeys {

bool store(const QString& provider, const QString& key, QString* error = nullptr);
// Empty when there is none.
QString read(const QString& provider);
bool has(const QString& provider);
void remove(const QString& provider);

} // namespace cloudkeys
