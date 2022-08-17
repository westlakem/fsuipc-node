#ifndef HELPERS_H
#define HELPERS_H

namespace FSUIPC {

// https://github.com/nodejs/nan/blob/v2.8.0/napi.h#L1504
class PromiseWorker {
 public:
  explicit PromiseWorker() : errmsg_(NULL) {
    request.data = this;

    Napi::HandleScope scope(Napi::Env env);

    resolver.Reset(
        v8::Promise::Resolver::New(Isolate::GetCurrentContext()));

    Napi::Object obj = Napi::Object::New(env);
    persistentHandle.Reset(obj);

    async_resource = new Napi::AsyncResource("PromiseWorker", obj);
  }

  virtual ~PromiseWorker() {
    Napi::HandleScope scope(Napi::Env env);

    if (!persistentHandle.IsEmpty())
      persistentHandle.Reset();
    if (!resolver.IsEmpty())
      resolver.Reset();
    delete[] errmsg_;

    delete async_resource;
  }

  virtual void WorkComplete() {
    Napi::HandleScope scope(Napi::Env env);

    if (errmsg_ == NULL)
      OnOK();
    else
      OnError();

    // KickNextTick(), which will make sure our promises work even with
    // setTimeout or setInterval See https://github.com/nodejs/nan/issues/539
    Napi::FunctionReference(Napi::Function::New(env, 
                      [](const Napi::CallbackInfo& info) {
Napi::Env env = info.Env();
},
                      env.Null()))
        .Call(0, nullptr, async_resource);
  }

  inline void SaveToPersistent(const char* key,
                               const Napi::Value& value) {
    Napi::HandleScope scope(Napi::Env env);
    Napi::New(env, persistentHandle)
        .Set(Napi::GetCurrentContext(), Napi::New(env, key), value);
  }

  inline void SaveToPersistent(const Napi::String& key,
                               const Napi::Value& value) {
    Napi::HandleScope scope(Napi::Env env);
    Napi::New(env, persistentHandle).Set(Napi::GetCurrentContext(), key, value);
  }

  inline void SaveToPersistent(uint32_t index,
                               const Napi::Value& value) {
    Napi::HandleScope scope(Napi::Env env);
    Napi::New(env, persistentHandle).Set(Napi::GetCurrentContext(), index, value);
  }

  inline Napi::Value GetFromPersistent(const char* key) const {
    Napi::HandleScope scope(Napi::Env env);
    return scope.Escape(
        Napi::New(env, persistentHandle)
            ->Get(Napi::GetCurrentContext(), Napi::New(env, key))
            );
  }

  inline Napi::Value GetFromPersistent(
      const Napi::String& key) const {
    Napi::HandleScope scope(Napi::Env env);
    return scope.Escape(Napi::New(env, persistentHandle)
                            ->Get(Napi::GetCurrentContext(), key)
                            );
  }

  inline Napi::Value GetFromPersistent(uint32_t index) const {
    Napi::HandleScope scope(Napi::Env env);
    return scope.Escape(Napi::New(env, persistentHandle)
                            ->Get(Napi::GetCurrentContext(), index)
                            );
  }

  virtual void Execute() = 0;

  uv_work_t request;

  virtual void Destroy() { delete this; }

  v8::Local<v8::Promise> GetPromise() {
    Napi::HandleScope scope(Napi::Env env);
    return scope.Escape(Napi::New(env, resolver)->GetPromise());
  }

 protected:
  Napi::Persistent<v8::Object> persistentHandle;
  Napi::Persistent<v8::Promise::Resolver> resolver;
  Napi::AsyncResource* async_resource;

  virtual void OnOK() {
    Napi::HandleScope scope(Napi::Env env);

    Napi::New(env, resolver)->Resolve(Napi::GetCurrentContext(), env.Undefined());
  }

  virtual void OnError() {
    Napi::HandleScope scope(Napi::Env env);

    Napi::New(env, resolver)->Reject(
        Napi::GetCurrentContext(),
        v8::Exception::Error(
            Napi::String::New(env, ErrorMessage())));
  }

  void SetErrorMessage(const char* msg) {
    delete[] errmsg_;

    size_t size = strlen(msg) + 1;
    errmsg_ = new char[size];
    memcpy(errmsg_, msg, size);
  }

  const char* ErrorMessage() const { return errmsg_; }

 private:
  int NAN_DISALLOW_ASSIGN_COPY_MOVE(PromiseWorker);
  char* errmsg_;
};

inline void PromiseExecute(uv_work_t* req) {
  PromiseWorker* worker = static_cast<PromiseWorker*>(req->data);
  worker->Execute();
}

inline void PromiseExecuteComplete(uv_work_t* req) {
  PromiseWorker* worker = static_cast<PromiseWorker*>(req->data);
  worker->WorkComplete();
  worker->Destroy();
}

inline void PromiseQueueWorker(PromiseWorker* worker) {
  uv_queue_work(uv_default_loop(), &worker->request, PromiseExecute,
                reinterpret_cast<uv_after_work_cb>(PromiseExecuteComplete));
}

}  // namespace FSUIPC

#endif
