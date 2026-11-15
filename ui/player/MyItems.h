#pragma once

#include <QString>
#include <QVariantList>
#include <QVariantMap>

// Your own things for the library, beside the built-in ones (all in "Mine"):
// - effect stacks: a clip's effects with their settings, saved under a name and put on other
//   clips in one go (my-effect-stacks.json, TVE_MY_EFFECTS_FILE);
// - stickers: pictures (PNG, JPEG, WebP, GIF) and Lottie animations of your own, kept in your
//   stickers folder (TVE_MY_STICKERS_DIR; else "My Stickers" in the app's data);
// - LUTs kept for filters made from them ("My LUTs", TVE_MY_LUTS_DIR).
namespace myitems {

// Effect stacks: [{id "stack.<n>", title, effects: [{effectId, values: {param: [numbers]}}]}].
QVariantList stacks();
QVariantMap stack(const QString& id);
// Saves effects under a title (a stack of that title is replaced); its id, or "".
QString saveStack(const QString& title, const QVariantList& effects);
bool removeStack(const QString& id);

QString stickersFolder();
// Copies a picture or Lottie file into your stickers; the sticker's id ("mine/<file>"), or ""
// with `error` saying why.
QString addSticker(const QString& path, QString* error);
bool removeSticker(const QString& id);
// Whether a file can be a sticker of yours, and whether it is a picture (not an animation).
bool isStickerFile(const QString& path);
bool isPictureFile(const QString& path);

QString lutsFolder();
// Copies a LUT (.cube, .3dl) into your LUTs; its new path, or "".
QString keepLut(const QString& path, QString* error);

} // namespace myitems
