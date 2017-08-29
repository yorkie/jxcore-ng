// Copyright & License details are available under JXCORE_LICENSE file

#include "node.h"
#include "node_buffer.h"
#include "slab_allocator.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

namespace node {

SlabAllocator::SlabAllocator(unsigned int size, commons* com) {
  if (com == NULL)
    com_ = node::commons::getInstance();
  else
    com_ = com;
  size_ = ROUND_UP(size ? size : 1, 8192);
  initialized_ = false;
  last_ptr_ = NULL;
  offset_ = 0;
}

SlabAllocator::~SlabAllocator() {
  if (!initialized_) return;
  // TODO(obastemur) For both engines this would leak when jxcore is embedded ?
  if (v8::V8::IsDead()) return;

  slab_sym_.Reset();
  slab_.Reset();
}

void SlabAllocator::Initialize() {
  // ENGINE_LOG_THIS("SlabAllocator", "Initialize");
  v8::EscapableHandleScope scope(com_->node_isolate);
  v8::Isolate* __contextORisolate = (com_ != NULL) ? com_->node_isolate : v8::Isolate::GetCurrent();

  char sym[256];
  int ln = snprintf(sym, sizeof(sym), "slab_%p", this);  // namespace object key
  offset_ = 0;
  last_ptr_ = NULL;
  initialized_ = true;

  v8::Local<v8::String> str_sym = v8::String::NewFromUtf8(__contextORisolate, sym, v8::String::kNormalString, ln);
  slab_sym_.Reset(__contextORisolate, str_sym);
}

#define NewSlab(size)                                                                           \
  v8::Local<v8::Value> arg = v8::Integer::New(__contextORisolate, ROUND_UP(size, 16));          \
  v8::Local<v8::FunctionTemplate> bct =                                                         \
      v8::Local<v8::FunctionTemplate>::New(__contextORisolate, com_->bf_constructor_template);  \
  v8::Local<v8::Object> buf = bct->GetFunction()->NewInstance(1, &arg);

char* SlabAllocator::Allocate(v8::Handle<v8::Object> obj, unsigned int size) {
  // ENGINE_LOG_THIS("SlabAllocator", "Allocate");
  v8::EscapableHandleScope scope(com_->node_isolate);
  v8::Isolate* __contextORisolate = (com_ != NULL) ? com_->node_isolate : v8::Isolate::GetCurrent();

  assert(!obj->IsEmpty());

  if (size == 0) return NULL;
  if (!initialized_) Initialize();
  v8::Local<v8::String> strl = v8::Local<v8::String>::New(__contextORisolate, slab_sym_);

  if (size > size_) {
    NewSlab(size_);
    obj->Set(strl, buf);
    return (char*)buf.As<v8::ArrayBuffer>()->GetContents().Data();
  }

  if (slab_.IsEmpty() || offset_ + size > size_) {
    slab_.Reset();
    NewSlab(size_);
    slab_.Reset(__contextORisolate, buf);
    offset_ = 0;
    last_ptr_ = NULL;
  }

  v8::Local<v8::Object> objl = v8::Local<v8::Object>::New(__contextORisolate, slab_);
  obj->Set(strl, objl);
  last_ptr_ = (char*)objl.As<v8::ArrayBuffer>()->GetContents().Data() + offset_;
  offset_ += size;

  return last_ptr_;
}

v8::Local<v8::Object> SlabAllocator::Shrink(v8::Handle<v8::Object> obj, char* ptr,
                                      unsigned int size) {
  // ENGINE_LOG_THIS("SlabAllocator", "Shrink");
  v8::EscapableHandleScope scope(com_->node_isolate);
  v8::Isolate* __contextORisolate = (com_ != NULL) ? com_->node_isolate : v8::Isolate::GetCurrent();
  // JS_DEFINE_STATE_MARKER(com_);

  v8::Local<v8::String> strl = v8::Local<v8::String>::New(__contextORisolate, slab_sym_);
  v8::Local<v8::Value> slab_v = obj->Get(strl);
  obj->Set(strl, v8::Null(__contextORisolate));

  assert(!slab_v->IsEmpty());
  assert(slab_v->IsObject());
  v8::Local<v8::Object> slab = slab_v->ToObject();

  assert(ptr != NULL);
  if (ptr == last_ptr_) {
    last_ptr_ = NULL;
    offset_ = ptr - (char*)slab.As<v8::ArrayBuffer>()->GetContents().Data() + ROUND_UP(size, 16);
  }
  return scope.Escape(slab);
}

}  // namespace node
