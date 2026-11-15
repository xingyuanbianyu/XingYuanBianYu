#pragma once

// ============================================================
// privilege.h —— 权限级别
//   R0  预留（最高）
//   R1  内核本身 / 前台
//   R2  预留
//   R3  第一个使用者
//   R4  一边去
//   R5  后台（应用）
//   R6  预留（最低）
// ============================================================

enum class Priv {
    R0 = 0,   // 预留
    R1 = 1,   // 内核本身 / 前台
    R2 = 2,   // 预留
    R3 = 3,   // 第一个使用者
    R4 = 4,   // 一边去
    R5 = 5,   // 后台（应用）
    R6 = 6    // 预留（最低）
};

// 线程当前权限，默认 R5（后台）
static thread_local Priv g_priv = Priv::R5;

// 检查：当前权限是否达到或高于 level（数字越小权限越高）
inline bool has_priv(Priv level) {
    return static_cast<int>(g_priv) <= static_cast<int>(level);
}