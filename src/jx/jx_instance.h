// Copyright & License details are available under JXCORE_LICENSE file

#ifndef SRC_JX_JX_INSTANCE_H_
#define SRC_JX_JX_INSTANCE_H_

#include <v8.h>

namespace jxcore {

class ArrayBufferAllocator : public v8::ArrayBuffer::Allocator {
 public:
  // Impose an upper limit to avoid out of memory errors that bring down
  // the process.
  static const size_t kMaxLength = 0x3fffffff;
  static ArrayBufferAllocator the_singleton;
  virtual ~ArrayBufferAllocator() {}
  virtual void* Allocate(size_t length);
  virtual void* AllocateUninitialized(size_t length);
  virtual void Free(void* data, size_t length);

 private:
  ArrayBufferAllocator() {}
  ArrayBufferAllocator(const ArrayBufferAllocator&);
  void operator=(const ArrayBufferAllocator&);
};

class JXInstance {
 public:
  static void runScript(void *x);
  static void sendMessage(const v8::FunctionCallbackInfo<v8::Value>&);
  static void LoopBreaker(const v8::FunctionCallbackInfo<v8::Value>&);
  static void Compiler(const v8::FunctionCallbackInfo<v8::Value>&);
  static void CallBack(const v8::FunctionCallbackInfo<v8::Value>&);
  static void refWaitCounter(const v8::FunctionCallbackInfo<v8::Value>&);
  static void setThreadOnHold(const v8::FunctionCallbackInfo<v8::Value>&);
};
}  // namespace jxcore
#endif  // SRC_JX_JX_INSTANCE_H_
