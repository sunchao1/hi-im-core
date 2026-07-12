// Copyright 2026 chao.sun
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// =============================================================================
// 文件: status.hpp
// 职责: 定义全库统一的错误码与轻量 Status 返回值类型，替代异常传递业务失败
// 在系统中的位置: 基础层，被 hub/router、context、bridge 等所有业务模块引用
// =============================================================================

#pragma once

#include <string>

namespace hiim {

/// 业务/系统错误码枚举；kOk 表示成功，其余为可预期的失败场景
enum class StatusCode {
  kOk = 0,
  kInvalidArgument,    // 参数非法
  kNotFound,           // 订阅者或 nid 路由不存在（Publish/AsyncSend 常见）
  kAlreadyExists,      // 资源已存在
  kUnauthenticated,    // 认证失败
  kPermissionDenied,   // 权限不足
  kQueueFull,          // 无锁队列满，需调用方退避或丢弃
  kIoError,            // 网络/文件 IO 错误
  kInternal,           // 内部逻辑错误
};

/// 轻量状态对象：code + 可选 message，用于函数返回值而非抛异常
struct Status {
  StatusCode code{StatusCode::kOk};
  std::string message;  // 人类可读错误描述，成功时通常为空

  /// 构造成功状态；调用方用于快速返回
  static Status Ok() { return {}; }

  /// 构造失败状态；code 必填，msg 可选
  static Status Error(StatusCode code, std::string msg = {}) {
    Status s;
    s.code = code;
    s.message = std::move(msg);
    return s;
  }

  /// 判断是否成功
  bool ok() const { return code == StatusCode::kOk; }

  /// 允许 if (status) 写法，语义同 ok()
  explicit operator bool() const { return ok(); }
};

}  // namespace hiim
