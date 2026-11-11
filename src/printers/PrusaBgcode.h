#pragma once

#include <QByteArray>

// Encode Orca G-code with Prusa's binary writer, keeping commands and comments.
QByteArray prusaBgcode(const QByteArray &gcode);
