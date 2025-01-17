// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/tracing/public/cpp/trace_event_agent.h"

#include <string>
#include <utility>
#include <vector>

#include "base/bind.h"
#include "base/memory/ref_counted.h"
#include "base/memory/ref_counted_memory.h"
#include "base/no_destructor.h"
#include "base/strings/string_util.h"
#include "base/threading/thread_checker.h"
#include "base/time/time.h"
#include "base/trace_event/trace_log.h"
#include "base/values.h"
#include "build/build_config.h"
#include "services/tracing/public/cpp/perfetto/producer_client.h"
#include "services/tracing/public/cpp/perfetto/trace_event_data_source.h"
#include "services/tracing/public/cpp/tracing_features.h"

namespace {

const char kTraceEventLabel[] = "traceEvents";

}  // namespace

namespace tracing {

// static
TraceEventAgent* TraceEventAgent::GetInstance() {
  static base::NoDestructor<TraceEventAgent> instance;
  return instance.get();
}

TraceEventAgent::TraceEventAgent()
    : BaseAgent(kTraceEventLabel,
                mojom::TraceDataType::ARRAY,
                base::trace_event::TraceLog::GetInstance()->process_id()),
      enabled_tracing_modes_(0),
      weak_ptr_factory_(this) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);

  base::trace_event::TraceLog::GetInstance()->AddAsyncEnabledStateObserver(
      weak_ptr_factory_.GetWeakPtr());

  ProducerClient::Get()->AddDataSource(TraceEventDataSource::GetInstance());
}

TraceEventAgent::~TraceEventAgent() {
  DCHECK(!tracing_enabled_callback_);
}

void TraceEventAgent::GetCategories(std::set<std::string>* category_set) {
  for (size_t i = base::trace_event::BuiltinCategories::kVisibleCategoryStart;
       i < base::trace_event::BuiltinCategories::Size(); ++i) {
    category_set->insert(base::trace_event::BuiltinCategories::At(i));
  }
}

void TraceEventAgent::AddMetadataGeneratorFunction(
    MetadataGeneratorFunction generator) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  metadata_generator_functions_.push_back(generator);

  // Instantiate and register the metadata data source on the first
  // call.
  static TraceEventMetadataSource* metadata_source = []() {
    static base::NoDestructor<TraceEventMetadataSource> instance;
    ProducerClient::Get()->AddDataSource(instance.get());
    return instance.get();
  }();

  metadata_source->AddGeneratorFunction(generator);
}

void TraceEventAgent::StartTracing(const std::string& config,
                                   base::TimeTicks coordinator_time,
                                   StartTracingCallback callback) {
  DCHECK(!IsBoundForTesting() || !TracingUsesPerfettoBackend());
  DCHECK(!recorder_);
  DCHECK(!tracing_enabled_callback_);
#if defined(__native_client__)
  // NaCl and system times are offset by a bit, so subtract some time from
  // the captured timestamps. The value might be off by a bit due to messaging
  // latency.
  base::TimeDelta time_offset = TRACE_TIME_TICKS_NOW() - coordinator_time;
  TraceLog::GetInstance()->SetTimeOffset(time_offset);
#endif
  enabled_tracing_modes_ = base::trace_event::TraceLog::RECORDING_MODE;
  const base::trace_event::TraceConfig trace_config(config);
  if (!trace_config.event_filters().empty())
    enabled_tracing_modes_ |= base::trace_event::TraceLog::FILTERING_MODE;
  base::trace_event::TraceLog::GetInstance()->SetEnabled(
      trace_config, enabled_tracing_modes_);
  std::move(callback).Run(true);
}

void TraceEventAgent::StopAndFlush(mojom::RecorderPtr recorder) {
  DCHECK(!IsBoundForTesting() || !TracingUsesPerfettoBackend());
  DCHECK(!recorder_);

  recorder_ = std::move(recorder);
  base::trace_event::TraceLog::GetInstance()->SetDisabled(
      enabled_tracing_modes_);
  enabled_tracing_modes_ = 0;
  for (const auto& generator : metadata_generator_functions_) {
    auto metadata = generator.Run();
    if (metadata)
      recorder_->AddMetadata(std::move(*metadata));
  }

  base::trace_event::TraceLog::GetInstance()->Flush(
      base::Bind(&TraceEventAgent::OnTraceLogFlush, base::Unretained(this)));
}

void TraceEventAgent::RequestBufferStatus(
    RequestBufferStatusCallback callback) {
  DCHECK(!IsBoundForTesting() || !TracingUsesPerfettoBackend());
  base::trace_event::TraceLogStatus status =
      base::trace_event::TraceLog::GetInstance()->GetStatus();
  std::move(callback).Run(status.event_capacity, status.event_count);
}

void TraceEventAgent::WaitForTracingEnabled(
    Agent::WaitForTracingEnabledCallback callback) {
  DCHECK(TracingUsesPerfettoBackend());
  DCHECK(!tracing_enabled_callback_);
  if (base::trace_event::TraceLog::GetInstance()->IsEnabled()) {
    std::move(callback).Run();
    return;
  }

  tracing_enabled_callback_ = std::move(callback);
}

// This callback will always come on the same sequence
// that TraceLog::AddAsyncEnabledStateObserver was called
// on to begin with, i.e. the same as any WaitForTracingEnabled()
// calls are run on.
void TraceEventAgent::OnTraceLogEnabled() {
  if (tracing_enabled_callback_) {
    std::move(tracing_enabled_callback_).Run();
  }
}

void TraceEventAgent::OnTraceLogDisabled() {}

void TraceEventAgent::OnTraceLogFlush(
    const scoped_refptr<base::RefCountedString>& events_str,
    bool has_more_events) {
  if (!events_str->data().empty())
    recorder_->AddChunk(events_str->data());
  if (!has_more_events) {
    recorder_.reset();
  }
}

}  // namespace tracing
