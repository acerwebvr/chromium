// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/log_console_message.h"

#include "base/logging.h"

namespace content {

void LogConsoleMessage(int32_t level,
                       const base::string16& message,
                       int32_t line_number,
                       bool is_builtin_component,
                       bool is_off_the_record,
                       const base::string16& source_id) {
  const int32_t resolved_level =
      is_builtin_component ? level : ::logging::LOG_INFO;
  if (::logging::GetMinLogLevel() > resolved_level)
    return;

  // LogMessages can be persisted so this shouldn't be logged in incognito mode.
  // This rule is not applied to WebUI pages or other builtin components,
  // because WebUI and builtin components source code is a part of Chrome source
  // code, and we want to treat messages from WebUI and other builtin components
  // the same way as we treat log messages from native code.
  if (is_off_the_record && !is_builtin_component)
    return;

  logging::LogMessage("CONSOLE", line_number, resolved_level).stream()
      << "\"" << message << "\", source: " << source_id << " (" << line_number
      << ")";
}

}  // namespace content
