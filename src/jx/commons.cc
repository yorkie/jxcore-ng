// Copyright & License details are available under JXCORE_LICENSE file

#include "commons.h"
#if defined(_MSC_VER)
#include <windows.h>
#else
#include <unistd.h>
#include <sys/time.h>
#include <sys/resource.h>
#define Sleep(x) usleep((x)*1000)
#endif
#if defined(_MSC_VER)
#include <process.h>
#define strcasecmp _stricmp
#define getpid _getpid
#define umask _umask
typedef int mode_t;
#else
#include <pthread.h>
#endif
#include "thread_wrap.h"

namespace node {

bool commons::ssl_initialized_ = false;
char *commons::embedded_source_ = NULL;
jxcore_instance_status commons::process_status_ = JXCORE_INSTANCE_NONE;
int32_t commons::max_header_size = 0;  // disabled
int commons::mapCount = 0;
int commons::bTCP = -1;
int commons::bTCPS = -1;
int commons::allowSysExec = -1;
int commons::allowLocalNativeModules = -1;
int commons::maxCPU = -1;
int commons::maxCPUInterval = -1;
int commons::allowMonitoringAPI = -1;
std::string commons::globalModulePath = "";
int64_t commons::maxMemory = -1;
int commons::threadPoolCount = 0;
BTStore *commons::mapData[MAX_JX_THREADS + 1];
bool commons::embedded_multithreading_ = false;
bool commons::self_hosted_process_ = true;
static int threadIdCounter = 0;
static bool jxcore_multithreaded = false;
static bool main_thread_created_ = false;

#if defined(_MSC_VER)
static std::map<DWORD, int> coms;
#else
static std::map<pthread_t, int> coms;  // pthread_t.__sig
#endif

static commons *isolates[MAX_JX_THREADS] = {NULL};
static uv_mutex_t comLock;

inline commons *getCommonsISO(v8::Isolate* isolate) {
  int *id = (int *)isolate->GetData(0);
  return isolates[*id];
}

inline int l_threadIdFromThreadPrivate() {
#if defined(_MSC_VER)
  DWORD th = GetCurrentThreadId();
  std::map<DWORD, int>::iterator it;
#else
  pthread_t th = pthread_self();
  std::map<pthread_t, int>::iterator it;
#endif
  it = coms.find(th);

  if (it != coms.end()) {
    return it->second;
  }

  return -1;
}

int commons::threadIdFromThreadPrivate() {
  return l_threadIdFromThreadPrivate();
}

inline int queryThreadId() {
  if (!jxcore_multithreaded) return 0;

  // ENGINE_LOG_THIS("commons", "getCurrentThreadId");

  return l_threadIdFromThreadPrivate();
}

inline commons *getCommons() {
  if (!jxcore_multithreaded) return isolates[0];
  commons *iso;

#ifdef JS_ENGINE_V8
  JS_ENGINE_MARKER current = JS_CURRENT_ENGINE();
  int *id = (int *)JS_GET_ENGINE_DATA(current);
  iso = isolates[*id];

  if (iso->node_isolate != current) {
    return NULL;
  }

  return iso;
#elif defined(JS_ENGINE_MOZJS)
  int n = queryThreadId();
  if (n < 0) n = 0;
  return isolates[n];
#endif
}

void InitThreadId(const int threadId, bool lock_mutex = true) {
#if defined(_MSC_VER)
  DWORD th = GetCurrentThreadId();
#else
  pthread_t th = pthread_self();
#endif

  if (lock_mutex) uv_mutex_lock(&comLock);

  coms[th] = threadId;

  if (lock_mutex) uv_mutex_unlock(&comLock);
}

int CreateNewThreadId() {
  uv_mutex_lock(&comLock);
  int threadId = threadIdCounter++;
  InitThreadId(threadId, false);
  uv_mutex_unlock(&comLock);

  return threadId;
}

commons *setCommons(commons *iso) {
  assert(iso->node_isolate != NULL &&
         "This shouldn't be null, beware using a thread Id before destroying "
         "the previous one");

  uv_mutex_lock(&comLock);
  InitThreadId(iso->threadId, false);
  isolates[iso->threadId] = iso;
  iso->node_isolate->SetData(0, &iso->threadId);
  uv_mutex_unlock(&comLock);

  return iso;
}

void removeCommons() {
#if defined(_MSC_VER)
  DWORD th = GetCurrentThreadId();
  std::map<DWORD, int>::iterator it;
#else
  pthread_t th = pthread_self();
  std::map<pthread_t, int>::iterator it;
#endif

  uv_mutex_lock(&comLock);

  it = coms.find(th);
  if (it != coms.end()) {
    commons *com = isolates[it->second];
    if (com != NULL) {
      if (com->threadId == it->second) {
        isolates[it->second] = NULL;
      }
      delete com;
    }

    coms.erase(it);
  }

  uv_mutex_unlock(&comLock);
}

uv_loop_t *commons::getThreadLoop() { return getCommons()->loop; }

#ifdef JS_ENGINE_MOZJS
MozJS::Value commons::CreateJSObject(const char *type_name) {
  JS_DEFINE_STATE_MARKER(this);
  if (JSObjectMaker_.IsEmpty()) {
    JS_LOCAL_FUNCTION objectMethod = JS_CAST_FUNCTION(
        JS_COMPILE_AND_RUN(STD_TO_STRING(
                               "(function(type) {\n"
                               "  if(type == 'Error')\n"
                               "    return new Error();\n"
                               "  else if(type == 'TypeError')\n"
                               "    return new TypeError();\n"
                               "  else if(type == 'RangeError')\n"
                               "    return new RangeError();\n"
                               "  else\n"
                               "    return new Error();\n"
                               "})"),
                           STD_TO_STRING("native:jxcore_js_object")));
    JS_NEW_PERSISTENT_FUNCTION(JSObjectMaker_, objectMethod);
  }

  MozJS::Value glob = jxcore::getGlobal(threadId);
  JS_LOCAL_STRING tp = STD_TO_STRING(type_name);
  return JS_METHOD_CALL(JSObjectMaker_, glob, 1, &tp);
}

MozJS::Value commons::GetPropertyNames(MozJS::Value *obj) {
  JS_DEFINE_STATE_MARKER(this);
  if (JSObjectLister_.IsEmpty()) {
    JS_LOCAL_FUNCTION objectMethod = JS_CAST_FUNCTION(
        JS_COMPILE_AND_RUN(STD_TO_STRING(
                               "(function(obj) {\n"
                               "  var arr=[];\n"
                               "  for(var o in obj) {\n"
                               "    if(typeof obj[o] != 'function')arr.push(o);"
                               "  }\n"
                               "  return arr;"
                               "})"),
                           STD_TO_STRING("native:jxcore_js_prop_names")));
    JS_NEW_PERSISTENT_FUNCTION(JSObjectLister_, objectMethod);
  }

  MozJS::Value glob = jxcore::getGlobal(threadId);
  return JS_METHOD_CALL(JSObjectLister_, glob, 1, obj);
}

void commons::CreateNewNonCallableInstance(MozJS::Value *obj,
                                           JS::MutableHandleValue val) {
  JS_DEFINE_STATE_MARKER(this);
  if (JSObjectNew_.IsEmpty()) {
    JS_LOCAL_FUNCTION objectMethod = JS_CAST_FUNCTION(
        JS_COMPILE_AND_RUN(STD_TO_STRING(
                               "(function(obj) {\n"
                               "  return new obj();\n"
                               "})"),
                           STD_TO_STRING("native:jxcore_js_new_instance")));
    JS_NEW_PERSISTENT_FUNCTION(JSObjectNew_, objectMethod);
  }

  MozJS::Value glob = jxcore::getGlobal(threadId);
  val.set(JS_METHOD_CALL(JSObjectNew_, glob, 1, obj).GetRawValue());
}
#endif

#define JS_CLEAR_STRING(str) pstr_##str.Reset()

void commons::Dispose() {
  uv_loop_delete(this->loop);

  if (instance_status_ == JXCORE_INSTANCE_ALIVE)
    instance_status_ = JXCORE_INSTANCE_EXITED;
  udp_constructor.Reset();
  secure_context_constructor.Reset();
  tcpConstructor.Reset();
  bf_constructor_template.Reset();
  fast_buffer_constructor.Reset();
  nf_stats_constructor_template.Reset();
  wc_constructor_template.Reset();
  ws_constructor_template.Reset();
  process_tickCallback.Reset();
  process_tickFromSpinner.Reset();
  binding_cache.Reset();
  module_load_list.Reset();

  dv_ft_cache.Reset();
  for (int i = 0; i < 16; i++) {
    ft_cache[i].Reset();
    ta_ft_cache[i].Reset();
  }

  cloneObjectMethod.Reset();
  JSONstringify.Reset();
  JSONparse.Reset();
  process_.Reset();

  stringOPS(false);
}

#define MAKE_STR_IN(a) #a

#define NEW_PERSISTENT_STRING(a)                                        \
  do {                                                                  \
    if (construct) {                                                    \
      v8::Local<v8::String> str_##a =                                   \
        v8::String::NewFromUtf8(__contextORisolate, MAKE_STR_IN(a));    \
      pstr_##a.Reset(__contextORisolate, str_##a);                      \
    } else {                                                            \
      pstr_##a.Reset();                                                 \
    }                                                                   \
  } while (0)

void commons::setProcess(v8::Handle<v8::Object> ps) {
  v8::Isolate* __contextORisolate = (this != NULL) ? this->node_isolate : v8::Isolate::GetCurrent();
  process_.Reset(__contextORisolate, ps);
  stringOPS(true);
}

void commons::stringOPS(bool construct) {
  v8::Isolate* __contextORisolate = (this != NULL) ? this->node_isolate : v8::Isolate::GetCurrent();

  NEW_PERSISTENT_STRING(callback);
  NEW_PERSISTENT_STRING(onerror);
  NEW_PERSISTENT_STRING(domain);
  NEW_PERSISTENT_STRING(onstop);
  NEW_PERSISTENT_STRING(exports);
  NEW_PERSISTENT_STRING(Buffer);
  NEW_PERSISTENT_STRING(stdio);
  NEW_PERSISTENT_STRING(type);
  NEW_PERSISTENT_STRING(ignore);
  NEW_PERSISTENT_STRING(pipe);
  NEW_PERSISTENT_STRING(fd);
  NEW_PERSISTENT_STRING(handle);
  NEW_PERSISTENT_STRING(tcp);
  NEW_PERSISTENT_STRING(tty);
  NEW_PERSISTENT_STRING(udp);
  NEW_PERSISTENT_STRING(wrapType);
  NEW_PERSISTENT_STRING(windowsVerbatimArguments);
  NEW_PERSISTENT_STRING(wrap);
  NEW_PERSISTENT_STRING(onexit);
  NEW_PERSISTENT_STRING(address);
  NEW_PERSISTENT_STRING(port);
  NEW_PERSISTENT_STRING(family);
  NEW_PERSISTENT_STRING(IPv4);
  NEW_PERSISTENT_STRING(IPv6);
  NEW_PERSISTENT_STRING(uid);
  NEW_PERSISTENT_STRING(gid);
  NEW_PERSISTENT_STRING(file);
  NEW_PERSISTENT_STRING(args);
  NEW_PERSISTENT_STRING(cwd);
  NEW_PERSISTENT_STRING(envPairs);
  NEW_PERSISTENT_STRING(detached);
  NEW_PERSISTENT_STRING(pid);
  NEW_PERSISTENT_STRING(_charsWritten);
  NEW_PERSISTENT_STRING(Signal);
  NEW_PERSISTENT_STRING(onsignal);
  NEW_PERSISTENT_STRING(sessionId);
  NEW_PERSISTENT_STRING(length);
  NEW_PERSISTENT_STRING(incoming);
  NEW_PERSISTENT_STRING(parser);
  NEW_PERSISTENT_STRING(__ptype);
  NEW_PERSISTENT_STRING(ondata);
  NEW_PERSISTENT_STRING(owner);
  NEW_PERSISTENT_STRING(_tickCallback);
  NEW_PERSISTENT_STRING(_errno);
  NEW_PERSISTENT_STRING(buffer);
  NEW_PERSISTENT_STRING(bytes);
  NEW_PERSISTENT_STRING(onMessageComplete);
  NEW_PERSISTENT_STRING(onHeaders);
  NEW_PERSISTENT_STRING(onBody);
  NEW_PERSISTENT_STRING(onHeadersComplete);
  NEW_PERSISTENT_STRING(headers);
  NEW_PERSISTENT_STRING(url);
  NEW_PERSISTENT_STRING(method);
  NEW_PERSISTENT_STRING(statusCode);
  NEW_PERSISTENT_STRING(upgrade);
  NEW_PERSISTENT_STRING(shouldKeepAlive);
  NEW_PERSISTENT_STRING(versionMinor);
  NEW_PERSISTENT_STRING(versionMajor);
  NEW_PERSISTENT_STRING(_immediateCallback);

  NEW_PERSISTENT_STRING(onmessage);
  NEW_PERSISTENT_STRING(threadMessage);
  NEW_PERSISTENT_STRING(emit);
  NEW_PERSISTENT_STRING(close);
  NEW_PERSISTENT_STRING(ontimeout);
  NEW_PERSISTENT_STRING(oncomplete);
  NEW_PERSISTENT_STRING(onchange);
  NEW_PERSISTENT_STRING(onconnection);
  NEW_PERSISTENT_STRING(onread);
  NEW_PERSISTENT_STRING(onnewsession);
  NEW_PERSISTENT_STRING(onhandshakestart);
  NEW_PERSISTENT_STRING(onhandshakedone);
  NEW_PERSISTENT_STRING(onclienthello);
  NEW_PERSISTENT_STRING(ondone);
  NEW_PERSISTENT_STRING(onselect);
  NEW_PERSISTENT_STRING(GET);
  NEW_PERSISTENT_STRING(HEAD);
  NEW_PERSISTENT_STRING(POST);
}

commons::~commons() {
  delete parser_settings;
  delete tick_spinner;
  delete signal_watcher;
  delete check_immediate_watcher;
  delete idle_immediate_dummy;
  delete dispatch_debug_messages_async;
  delete ares_timer;
#if !defined(JS_ENGINE_MOZJS)
  // uses from ArrayBuffer's memory
  delete tick_infobox;
#endif
  delete NQ;
#ifndef _MSC_VER
  delete threadPing;
#endif

  if (_ares_channel != NULL) {
    // clear_ares(_ares_channel);
    free(_ares_channel);
  }
}

void commons::TriggerDummy(int tid) {
  commons *com = commons::getInstanceByThreadId(tid);
  node::ThreadWrap::EmitOnMessage(com->threadId);
}

void commons::TriggerDummy(uv_async_t *handle) {
  int tid = handle->threadId;
  commons::TriggerDummy(handle->threadId);
}

commons::commons(const int tid) {
  if (tid > 0) {
    jxcore_multithreaded = true;
    uv_multithreaded();
  }

  instance_status_ = JXCORE_INSTANCE_ALIVE;
#ifdef JS_ENGINE_MOZJS
  should_interrupt_ = false;
#endif
  handle_has_symbol_ = false;
  counter_gc_start_time = 0;
  counter_gc_end_time = 0;
  udp_slab_allocator = NULL;
  s_slab_allocator = NULL;
  _ares_channel = NULL;
  threadId = tid;
#ifdef JS_ENGINE_V8  // typed arrays
  ta_ft_cc_id = 0;
  ta_ft_id = 0;
#endif
  threadOnHold = 0;
  waitCounter = 0;
  is_packaged_ = false;

  expects_reset = false;
  using_domains = false;
  print_eval = false;
  force_repl = false;
  trace_deprecation = false;
  throw_deprecation = false;
  eval_string = NULL;
  option_end_index = 0;
  use_debug_agent = false;
  debug_wait_connect = false;
  debug_port = 5858;
  max_stack_size = 0;
  need_immediate_cb = false;
  need_tick_cb = false;

  NQ = new nQueue;

  debugger_running = false;
  if (tid != 0) {
    v8::Isolate::CreateParams params;
    ArrayBufferAllocator allocator;
    params.array_buffer_allocator = &allocator;
    node_isolate = v8::Isolate::New(params);
  }
  prog_start_time = 0;

  // uses from ArrayBuffer's memory
  tick_infobox = new __tickbox;
  tick_infobox->depth = 0;
  tick_infobox->length = 0;
  tick_infobox->index = 0;

  at_exit_functions_ = NULL;
  pa_current_buffer_len = 0;
  pa_current_buffer_data = NULL;
  pab_current_buffer = false;
  stream_initialized = false;

  tick_spinner = new uv_idle_t;
  signal_watcher = new uv_signal_t;
  check_immediate_watcher = new uv_check_t;
  idle_immediate_dummy = new uv_idle_t;
  dispatch_debug_messages_async = new uv_async_t;
  ares_timer = new uv_timer_t;

  parser_settings = new http_parser_settings;

  uv_threadkey_setid(&threadId);
  if (tid == 0) {
    loop = uv_default_loop();
    threadPing = NULL;
  } else {
    loop = uv_loop_new();
    uv_thread_loops_add(threadId - 1, loop);
    loop->id = threadId;

#ifndef _MSC_VER
    threadPing = new uv_async_t;
    uv_async_init(loop, threadPing, TriggerDummy);
    // loop->fakeHandle = 1;
    threadPing->threadId = threadId;
#endif
  }

// #ifdef _MSC_VER
//   loop->fakeHandle = 0;
// #endif
}

void commons::PingThread() {
#ifndef _MSC_VER
  uv_async_send(threadPing);
#endif
}

void commons::CleanPinger(int threadId) {
  if (threadId >= 0) {
#ifdef _MSC_VER
    TriggerDummy(threadId);
#else
    commons *com = commons::getInstanceByThreadId(threadId);
    if (com == NULL) return;
    if (com->threadPing == NULL) return;

    uv_async_t *t = com->threadPing;
    uv_close((uv_handle_t *)t, NULL);
    // com->loop->fakeHandle = 0;
#endif
  }
}

void commons::setMainIsolate() { setCommons(this); }

commons *commons::newInstance(const int id) {  // for sub threads
  commons *com = setCommons(new commons(id));

  return com;
}

int commons::getCurrentThreadId() { return queryThreadId(); }

// threadId generator for embedded multithreading
int commons::getAvailableThreadId(bool multi_threaded_) {
  if (multi_threaded_) jxcore_multithreaded = true;

  int threadId = queryThreadId();

  if (!main_thread_created_) {
    main_thread_created_ = true;
    uv_mutex_init(&comLock);
    uv_threadkey_new(0);
  } else if (threadId == 0) {
    uv_threadkey_new(1);
  }

  if (threadId <= 0) {
    // we don't have an instance lets create one
    threadId = CreateNewThreadId();
  }

  if (threadId > 0) commons::embedded_multithreading_ = true;

  return threadId;
}

commons *commons::getInstance() { return getCommons(); }

commons *commons::getInstanceByThreadId(const int id) { return isolates[id]; }

commons *commons::getInstanceIso(v8::Isolate* iso) {
  return getCommonsISO(iso);
}

void commons::SetPortBoundaries(const int tcp, const int tcps,
                                const bool allowCustom) {
  if (commons::bTCP != -1) {
    return;
  }
  if (tcp < 0 && !allowCustom)
    commons::bTCP = -3;
  else
    commons::bTCP = tcp;

  if (tcps < 0 && !allowCustom)
    commons::bTCPS = -3;
  else
    commons::bTCPS = tcps;
}

int commons::GetTCPBoundary() { return commons::bTCP; }

int commons::GetTCPSBoundary() { return commons::bTCPS; }

void commons::SetMaxMemory(const int64_t mem) {
  if (commons::maxMemory != -1) {
    return;
  }
  commons::maxMemory = mem;
}

void commons::SetSysExec(const int sysExec) {
  if (commons::allowSysExec != -1) {
    return;
  }
  commons::allowSysExec = sysExec;
}

void commons::SetAllowLocalNativeModules(const int allow) {
  if (commons::allowLocalNativeModules != -1) {
    return;
  }
  commons::allowLocalNativeModules = allow;
}

bool commons::AllowLocalNativeModules() {
  return commons::allowLocalNativeModules == 0 ? false : true;
}

void commons::SetMonitoringAPI(const bool allow) {
  if (commons::allowMonitoringAPI != -1) {
    return;
  }
  commons::allowMonitoringAPI = allow ? 1 : 0;
}

bool commons::AllowMonitoringAPI() {
  return commons::allowMonitoringAPI == 0 ? false : true;
}

void commons::SetMaxCPU(const int maxcpu, const int interval) {
  if (commons::maxCPU != -1) {
    return;
  }
  commons::maxCPU = maxcpu;
  commons::maxCPUInterval = interval * 1000;
}

int commons::GetMaxCPU() { return commons::maxCPU; }

int commons::GetMaxCPUInterval() { return commons::maxCPUInterval; }

void commons::SetGlobalModulePath(const std::string path) {
  if (commons::globalModulePath.length() > 0) {
    return;
  }
  commons::globalModulePath = path;
}

std::string commons::GetGlobalModulePath() { return commons::globalModulePath; }

bool commons::CanSysExec() { return commons::allowSysExec == 0 ? false : true; }

bool commons::CheckMemoryLimit() {
  size_t rss;

  if (commons::maxMemory < 0) {
    return false;
  }

  int err = uv_resident_set_memory(&rss);
  if (err != 0) {
    return false;
  }

  if (rss >= (size_t)commons::maxMemory) {
    // FIXME(Yorkie): use common error report
    printf("The application has reached beyond the pre-defined memory limits (%ld >= %ld)\n", 
      rss / 1024, commons::maxMemory / 1024);
    abort();
  }

  return true;
}

#if defined(_MSC_VER)
double GetCPU(const int64_t msecs, double *last) {
  FILETIME eft;
  uint64_t utime;

  if (!GetProcessTimes(GetCurrentProcess(), &eft, &eft, &eft,
                       reinterpret_cast<FILETIME *>(&utime)))
    return 0;

  utime /= 10;

  uint32_t secs = static_cast<uint32_t>(utime / 1000000);
  uint32_t usecs = static_cast<uint32_t>(utime % 1000000);

  double ctime = (secs * 1000) + (usecs / 1000);
  ctime -= *last;
  *last += ctime;

  assert(msecs > 0);
  return (ctime * 100) / msecs;
}
#else
double GetCPU(const int64_t msecs, double *last) {
  struct rusage usage;

  if (getrusage(RUSAGE_SELF, &usage) < 0) return 0;
  uint32_t secs = usage.ru_utime.tv_sec;
  uint32_t usecs = usage.ru_utime.tv_usec;

  double ctime = (secs * 1000) + (usecs / 1000);
  ctime -= *last;
  *last += ctime;

  assert(msecs > 0);
  return (ctime * 100) / msecs;
}
#endif

double commons::GetCPUUsage(const int64_t timer, const int64_t diff) {
  static int64_t before = 0;
  static double last = 0;
  static int64_t counter = 0;

  if (before > 0) {
    counter += timer - before;
  } else {
    before = timer;
    return 0;
  }
  before = timer;

  double usage = 0;
  if (counter >= diff) {
    if (last == 0) {  // dont check the initial consumption
      GetCPU(counter, &last);
    } else {
      usage = GetCPU(counter, &last);
    }
    counter = 0;
  }

  return usage;
}

void commons::CheckCPUUsage(const int64_t timer) {
  static double last = 0;
  static int counter = 0;

  if (commons::maxCPU <= 0) return;

  counter += timer;
  if (counter >= GetMaxCPUInterval()) {  // every 2 secs
    if (last == 0) {                     // dont check the initial consumption
      GetCPU(counter, &last);
    } else {
      double usage = GetCPU(counter, &last);
      if (usage >= commons::maxCPU + 1) {
        // FIXME(yorkie): use common error report
        printf("The application has reached beyond the pre-defined cpu limits. The CPU usage was (%f)\n", usage);
        abort();
      }
    }
    counter = 0;
  }
}
}  // namespace node
