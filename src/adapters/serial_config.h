#pragma once

#include "ports/ports.h"

namespace tab5 {

// シリアルの config KEY VALUE を、設定範囲付きのアプリケーション命令へ変換する。
bool serialConfigCommand(const char *key, const char *value,
                         const vega::Settings &current, vega::Command &out);

}  // namespace tab5
