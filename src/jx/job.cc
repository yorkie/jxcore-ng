// Copyright & License details are available under JXCORE_LICENSE file

#include "commons.h"
#include "job.h"
#include "extend.h"
#include "jx_instance.h"
#include "thread_wrap.h"

namespace jxcore {

void SendMessage(const int threadId, const char *msg_data_, const int length,
                 bool same_thread) {
  const char *msg_data = msg_data_ == NULL ? "null" : msg_data_;
  char *str = cpystr(msg_data, length);

  bool hasIt = false;
  threadLock(threadId);
  pushThreadQueue(threadId, str);

  // check if thread already received a ping
  if (uv_thread_hasmsg(threadId)) 
    hasIt = true;

  uv_thread_setmsg(threadId, 1);
  threadUnlock(threadId);

  node::commons *com = node::commons::getInstanceByThreadId(threadId);
  if (com == NULL || com->instance_status_ != node::JXCORE_INSTANCE_ALIVE ||
      com->expects_reset)
    return;

  if (!hasIt) com->PingThread();
}

int CreateThread(void (*entry)(void *arg), void *param) {
  uv_thread_t thread;
  return uv_thread_create(&thread, entry, param);
}

int CreateInstances(const int count) {
  int rc = 0;
  for (int i = 0; i < count; i++) {
    uv_thread_t thread;
    rc = uv_thread_create(&thread, JXInstance::runScript, NULL);
    if (rc != 0) break;
  }

  return rc;
}

}  // namespace jxcore
