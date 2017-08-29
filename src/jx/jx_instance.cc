// Copyright & License details are available under JXCORE_LICENSE file

#include "jx_instance.h"
#include "job_store.h"
#include "extend.h"
#include "job.h"
#include "thread_wrap.h"
#include "jxcore.h"

#if !defined(_MSC_VER)
#include <strings.h>
#else
#define snprintf _snprintf
#endif

namespace jxcore {

void JXInstance::runScript(void *x) {

  customLock(CSLOCK_NEWINSTANCE);

  if (node::commons::process_status_ != node::JXCORE_INSTANCE_ALIVE) {
    customUnlock(CSLOCK_NEWINSTANCE);
    return;
  }

  int threadId = Job::getNewThreadId();

  Job::fillTasks(threadId);

  node::commons *com = node::commons::newInstance(threadId + 1);
  jxcore::JXEngine engine(com);

  int resetCount = 0;
  bool reset;

// #ifdef JS_ENGINE_V8
  do {
// #elif defined(JS_ENGINE_MOZJS)
//   ENGINE_NS::Isolate *isolate = com->node_isolate;
//   JSContext *context = isolate->GetRaw();
//   JSRuntime *rt = JS_GetRuntime(context);
//   do {
// #endif
    do {
// #ifdef JS_ENGINE_V8
      v8::Isolate* isolate = com->node_isolate;
      if (isolate == NULL) {
        v8::Isolate::CreateParams params;
        params.array_buffer_allocator =
          &jxcore::ArrayBufferAllocator::the_singleton;
        isolate = v8::Isolate::New(params);
      }
      v8::Locker locker(isolate);
      v8::Isolate::Scope isolate_scope(isolate);
      v8::HandleScope scope(isolate);
      isolate->SetData(0, &com->threadId);
      v8::Isolate* __contextORisolate = (com != NULL) ? com->node_isolate : v8::Isolate::GetCurrent();
      v8::Local<v8::Context> context = v8::Context::New(isolate);
      v8::Context::Scope context_scope(context);

      v8::Local<v8::Object> global = context->Global();
// #elif defined(JS_ENGINE_MOZJS)
//       JSAutoRequest ar(context);
//       JS::RootedObject _global(context);
//       jxcore::NewGlobalObject(context, &_global);
//       assert(_global != NULL);
//       JSAutoCompartment ac(context, _global);
//       v8::Local<v8::Object> global(_global, context);
//       JS_DEFINE_STATE_MARKER(com);
//       JS_SetErrorReporter(context, node::OnFatalError);
// #endif

// #if defined(JS_ENGINE_V8)
// #ifndef V8_IS_3_14
//       engine.pContext_.Reset(isolate, context);
// #else
      engine.pContext_.Reset(isolate, context);
// #endif
// #endif
      uv_idle_t *t = com->tick_spinner;
      uv_idle_init(com->loop, t);

      uv_check_init(com->loop, com->check_immediate_watcher);
      uv_unref((uv_handle_t *)com->check_immediate_watcher);
      uv_idle_init(com->loop, com->idle_immediate_dummy);

      v8::Local<v8::Object> inner = v8::Object::New(__contextORisolate);
// #ifdef JS_ENGINE_MOZJS
//       JS::RootedObject r_inner(context, inner.GetRawObjectPointer());
// #endif

      node::SetMethod(inner, "compiler", Compiler);
      node::SetMethod(inner, "callBack", CallBack);
      node::SetMethod(inner, "refWaitCounter", refWaitCounter);
      node::SetMethod(inner, "setThreadOnHold", setThreadOnHold);
      global->Set(v8::String::NewFromUtf8(__contextORisolate, "tools"), inner);

      node::SetupProcessObject(threadId + 1, false);
      v8::Handle<v8::Object> process_l = com->getProcess();
      customUnlock(CSLOCK_NEWINSTANCE);
      JXEngine::InitializeProxyMethods(com);
      com->loop->id = threadId + 1;

      node::Load(process_l);
      uv_run_jx(com->loop, UV_RUN_DEFAULT, node::commons::CleanPinger,
                threadId + 1);

      // JS_FORCE_GC();
      __contextORisolate->LowMemoryNotification();
      if (!com->expects_reset)
        node::EmitExit(process_l);
      else
        resetCount = com->waitCounter;

      node::RunAtExit();
      com->Dispose();

    } while (0);

    reset = com->expects_reset;
// #ifdef JS_ENGINE_V8
    com->node_isolate->Dispose();
// #elif defined(JS_ENGINE_MOZJS)
//     JS_DestroyContext(context);
//     com->instance_status_ = node::JXCORE_INSTANCE_EXITED;
//     // SM can't do GC under GC. we need the destroy other contexts separately
//     std::list<JSContext *>::iterator itc = com->free_context_list_.begin();
//     while (itc != com->free_context_list_.end()) {
//       JS_DestroyContext(*itc);
//       itc++;
//     }
//     com->free_context_list_.clear();
//     com->node_isolate->Dispose();
// #endif
    node::removeCommons();
  } while (0);

#ifdef JS_ENGINE_MOZJS
  JS_DestroyRuntime(rt);
#endif

  reduceThreadCount();
  Job::removeTasker(threadId);

  if (reset) {
    char mess[64];
    int ln = snprintf(mess, sizeof(mess),
                      "{\"threadId\":%d , \"resetMe\":true, \"counter\":%d}",
                      threadId, resetCount);
    mess[ln] = '\0';
    jxcore::SendMessage(0, mess, strlen(mess), false);
  }
}

static void handleJob(node::commons *com, Job *j,
                      const v8::Handle<v8::Function> &runner) {
  JS_ENTER_SCOPE_WITH(com->node_isolate);
  JS_DEFINE_STATE_MARKER(com);

  if (com->expects_reset) return;

  JS_HANDLE_ARRAY arr = JS_NEW_ARRAY_WITH_COUNT(3);
  JS_INDEX_SET(arr, 0, STD_TO_INTEGER(j->taskId));
  JS_INDEX_SET(arr, 1, STD_TO_INTEGER(j->cbId));

  if (j->hasParam) {
    JS_INDEX_SET(arr, 2, UTF8_TO_STRING(j->param));
  } else {
    JS_INDEX_SET(arr, 2, JS_UNDEFINED());
  }

  JS_HANDLE_VALUE argv[1] = {arr};
  JS_HANDLE_VALUE result;
  JS_TRY_CATCH(try_catch);

  JS_HANDLE_OBJECT glob = JS_GET_GLOBAL();
  result = JS_METHOD_CALL(runner, glob, 1, argv);
  if (try_catch.HasCaught()) {
    if (try_catch.CanContinue()) node::ReportException(try_catch, true);
    result = JS_UNDEFINED();
  }

  if (!JS_IS_UNDEFINED(result) && !JS_IS_NULL(result)) {
    jxcore::JXString param1(result);
    SendMessage(0, *param1, param1.length(), false);
  } else {
    SendMessage(0, "null", 4, false);
  }
}

static void handleTasks(node::commons *com, 
                        const v8::Handle<v8::Function> &func,
                        const v8::Handle<v8::Function> &runner, 
                        const int threadId) {
  JS_ENTER_SCOPE_WITH(com->node_isolate);
  JS_DEFINE_STATE_MARKER(com);

  if (com->expects_reset) return;

  std::queue<int> tasks;
  Job::getTasks(&tasks, threadId);

  while (!tasks.empty()) {
    JS_HANDLE_ARRAY arr = JS_NEW_ARRAY_WITH_COUNT(2);
    JS_INDEX_SET(arr, 0, STD_TO_INTEGER(tasks.front()));
    Job *j = getTaskDefinition(tasks.front());

    tasks.pop();

    if (node::commons::process_status_ != node::JXCORE_INSTANCE_ALIVE) {
      break;
    }

    assert(j->script != NULL &&
           "Something is wrong job->script shouldn't be null!");

    JS_INDEX_SET(arr, 1, UTF8_TO_STRING(j->script));
    JS_HANDLE_VALUE argv[1] = {arr};
    {
      JS_TRY_CATCH(try_catch);

      func->Call(JS_GET_GLOBAL(), 1, argv);
      if (try_catch.HasCaught()) {
        if (try_catch.CanContinue()) {
          node::ReportException(try_catch, true);
        }
      }
    }

    if (j->cbId == -2) {
      handleJob(com, j, runner);
    }
  }
}

void JXInstance::Compiler(const v8::FunctionCallbackInfo<v8::Value>& info) {
  v8::HandleScope scope(info.GetIsolate());
  node::commons* com = node::commons::getInstanceIso(info.GetIsolate());
  v8::Isolate* __contextORisolate = info.GetIsolate();
  jxcore::PArguments args(info);
  if (com->expects_reset)
    return;

  int threadId = args.GetInteger(0);

  v8::Local<v8::Object> func_value = info[1]->ToObject();
  assert(func_value->IsFunction());
  v8::Local<v8::Function> func = v8::Local<v8::Function>::Cast(func_value);

  v8::Local<v8::Object> runner_value = info[1]->ToObject();
  assert(runner_value->IsFunction());
  v8::Local<v8::Function> runner = v8::Local<v8::Function>::Cast(runner_value);

  int was = 0;
  int succ = 0;
  int mn = 0;

  int directions[2];
  if (threadId % 2 == 0) {  // reverse lookups
    directions[0] = 0;
    directions[1] = 1;
  } else {
    directions[0] = 1;
    directions[1] = 0;
  }

  handleTasks(com, func, runner, threadId);
start:
  if (com->expects_reset) {
    args.SetReturnValue(v8::Integer::New(__contextORisolate, -1));
    return;
  }
  Job *j = getJob(directions[mn]);
  if (j != NULL) {
    succ++;
    handleTasks(com, func, runner, threadId);
    handleJob(com, j, runner);

    decreaseJobCount();
    j->Dispose();
    if (!j->hasScript) j = NULL;
    goto start;
  }

  mn++;
  if (mn < 2) goto start;

  if (node::commons::process_status_ != node::JXCORE_INSTANCE_ALIVE) {
    args.SetReturnValue(v8::Integer::New(__contextORisolate, -1));
    return;
  } else {
    if (succ > 0) {
      was++;
      succ = 0;
      mn = 0;
      goto start;
    }
    args.SetReturnValue(v8::Integer::New(__contextORisolate, was));
    return;
  }
}

void JXInstance::setThreadOnHold(const v8::FunctionCallbackInfo<v8::Value>& info) {
  v8::HandleScope scope(info.GetIsolate());
  node::commons* com = node::commons::getInstanceIso(info.GetIsolate());
  v8::Isolate* __contextORisolate = info.GetIsolate();
  jxcore::PArguments args(info);
  if (com->expects_reset)
    return;

  if (args.Length() == 1 && args.IsInteger(0)) {
    com->threadOnHold = args.GetInteger(0);
    if (com->threadOnHold == 0) {
      node::ThreadWrap::EmitOnMessage(com->threadId);
    }
  }
}

void JXInstance::refWaitCounter(const v8::FunctionCallbackInfo<v8::Value>& info) {
  v8::HandleScope scope(info.GetIsolate());
  node::commons* com = node::commons::getInstanceIso(info.GetIsolate());
  v8::Isolate* __contextORisolate = info.GetIsolate();
  jxcore::PArguments args(info);
  if (com->expects_reset)
    return;

  if (args.Length() == 1 && args.IsInteger(0)) {
    com->waitCounter = args.GetInteger(0);
  }
}

void JXInstance::CallBack(const v8::FunctionCallbackInfo<v8::Value>& info) {
  v8::HandleScope scope(info.GetIsolate());
  node::commons* com = node::commons::getInstanceIso(info.GetIsolate());
  v8::Isolate* __contextORisolate = info.GetIsolate();
  jxcore::PArguments args(info);
  if (com->expects_reset)
    return;

  if (args.Length() == 1 && args.IsString(0)) {
    jxcore::JXString jxs;
    int ln = args.GetString(0, &jxs);
    jxcore::SendMessage(0, *jxs, ln, false);
  } else {
    v8::Local<v8::String> msg = v8::String::NewFromUtf8(
      __contextORisolate, "JXInstance::Callback expects a string argument");
    v8::Local<v8::Value> err = v8::Exception::Error(msg);
    __contextORisolate->ThrowException(err);
    return;
  }
}
}  // namespace jxcore
