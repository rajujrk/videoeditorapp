#pragma once

// Marks a message the app shows as one to translate (the core has no Qt): lupdate reads
// TVE_TR(context, text) like QT_TRANSLATE_NOOP, and the app looks the text up in `context`
// with QCoreApplication::translate when it shows it. The text itself is unchanged here.
#define TVE_TR(context, text) text
