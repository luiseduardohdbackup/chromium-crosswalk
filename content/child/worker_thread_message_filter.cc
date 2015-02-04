// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/child/worker_thread_message_filter.h"

#include "base/thread_task_runner_handle.h"
#include "content/child/thread_safe_sender.h"
#include "content/child/worker_thread_task_runner.h"
#include "ipc/ipc_message_macros.h"

namespace content {

WorkerThreadMessageFilter::WorkerThreadMessageFilter(
    ThreadSafeSender* thread_safe_sender)
    : main_thread_task_runner_(base::ThreadTaskRunnerHandle::Get()),
      thread_safe_sender_(thread_safe_sender) {
}

WorkerThreadMessageFilter::~WorkerThreadMessageFilter() {
}

base::TaskRunner* WorkerThreadMessageFilter::OverrideTaskRunnerForMessage(
    const IPC::Message& msg) {
  if (!ShouldHandleMessage(msg))
    return nullptr;
  int ipc_thread_id = 0;
  const bool success = GetWorkerThreadIdForMessage(msg, &ipc_thread_id);
  DCHECK(success);
  if (!ipc_thread_id)
    return main_thread_task_runner_.get();
  return new WorkerThreadTaskRunner(ipc_thread_id);
}

bool WorkerThreadMessageFilter::OnMessageReceived(const IPC::Message& msg) {
  if (!ShouldHandleMessage(msg))
    return false;
  OnFilteredMessageReceived(msg);
  return true;
}

}  // namespace content
