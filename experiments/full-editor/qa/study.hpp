#pragma once
#include <cstdint>
#include <functional>
namespace np4qa {
using Send = std::function<intptr_t(unsigned, uintptr_t, intptr_t)>;
void attach(Send send, const char *filePath = nullptr);
void tick(int editorOriginX = 0, int editorOriginY = 0);
void notify(unsigned code, int ch, int characterSource, int listType);
void complete();
}
