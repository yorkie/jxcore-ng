// Copyright & License details are available under JXCORE_LICENSE file

#ifndef SRC_WRAPPERS_THREAD_WRAP_H_
#define SRC_WRAPPERS_THREAD_WRAP_H_

#include "jx/commons.h"

namespace node {

class ThreadWrap {
  static void CpuCount(const v8::FunctionCallbackInfo<v8::Value>& info);
  static void Free(const v8::FunctionCallbackInfo<v8::Value>& info);
  static void Kill(const v8::FunctionCallbackInfo<v8::Value>& info);
  static void AddTask(const v8::FunctionCallbackInfo<v8::Value>& info);
  static void ShutDown(const v8::FunctionCallbackInfo<v8::Value>& info);
  static void ResetThread(const v8::FunctionCallbackInfo<v8::Value>& info);
  static void SendToThreads(const v8::FunctionCallbackInfo<v8::Value>& info);
  static void GetResults(const v8::FunctionCallbackInfo<v8::Value>& info);
  static void ThreadCount(const v8::FunctionCallbackInfo<v8::Value>& info);
  static void GetCPUCount(const v8::FunctionCallbackInfo<v8::Value>& info);
  static void SetCPUCount(const v8::FunctionCallbackInfo<v8::Value>& info);
  static void SetExiting(const v8::FunctionCallbackInfo<v8::Value>& info);
  static void JobsCount(const v8::FunctionCallbackInfo<v8::Value>& info);
  static void CPU(const v8::FunctionCallbackInfo<v8::Value>& info);

 public:
  static v8::Handle<v8::Value> collectResults(node::commons* com, const int tid,
                                        bool emit_call);

  static void EmitOnMessage(const int tid);

 public:
  static void Initialize(v8::Handle<v8::Object> constructor) {
    v8::Isolate* __contextORisolate = v8::Isolate::GetCurrent();
    v8::HandleScope scope(__contextORisolate);
    node::SetMethod(constructor, "addTask", AddTask);
    node::SetMethod(constructor, "resetThread", ResetThread);
    node::SetMethod(constructor, "sendToAll", SendToThreads);
    node::SetMethod(constructor, "getResults", GetResults);
    node::SetMethod(constructor, "jobsCount", JobsCount);
    node::SetMethod(constructor, "setCPUCount", SetCPUCount);
    node::SetMethod(constructor, "getCPUCount", GetCPUCount);
    node::SetMethod(constructor, "threadCount", ThreadCount);
    node::SetMethod(constructor, "setProcessExiting", SetExiting);
    node::SetMethod(constructor, "cpuCount", CpuCount);
    node::SetMethod(constructor, "freeGC", Free);
    node::SetMethod(constructor, "killThread", Kill);
  }
};

}  // namespace node
#endif  // SRC_WRAPPERS_THREAD_WRAP_H_
