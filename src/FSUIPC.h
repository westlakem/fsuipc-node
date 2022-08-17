#ifndef FSUIPC_H
#define FSUIPC_H

#include <napi.h>
#include <uv.h>

#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "IPCUser.h"
#include "helpers.h"

namespace FSUIPC {
enum class Type {
  Byte,
  SByte,
  Int16,
  Int32,
  Int64,
  UInt16,
  UInt32,
  UInt64,
  Double,
  Single,
  ByteArray,
  String,
  BitArray,
};

Napi::Object InitType(Napi::Env env, Napi::Object exports);
Napi::Object InitError(Napi::Env env, Napi::Object exports);
Napi::Object InitSimulator(Napi::Env env, Napi::Object exports);

DWORD get_size_of_type(Type type);

struct Offset {
  std::string name;
  Type type;
  DWORD offset;
  DWORD size;
  void* dest;
};

struct OffsetWrite {
  Type type;
  DWORD offset;
  DWORD size;
  void* src;  // Will be freed on Process()
};

// https://medium.com/netscape/tutorial-building-native-c-modules-for-node-js-using-nan-part-1-755b07389c7c
class FSUIPC : public Napi::ObjectWrap<FSUIPC> {
  friend class ProcessAsyncWorker;
  friend class OpenAsyncWorker;
  friend class CloseAsyncWorker;

 public:
  static void Init(Napi::Env env, Napi::Object exports);

  static Napi::Value New(const Napi::CallbackInfo& info);
  static Napi::Value Open(const Napi::CallbackInfo& info);
  static Napi::Value Close(const Napi::CallbackInfo& info);

  static Napi::Value Process(const Napi::CallbackInfo& info);
  static Napi::Value Add(const Napi::CallbackInfo& info);
  static Napi::Value Remove(const Napi::CallbackInfo& info);
  static Napi::Value Write(const Napi::CallbackInfo& info);

  static Napi::FunctionReference constructor;

  ~FSUIPC() {
    if (this->ipc) {
      delete this->ipc;
    }
  }

 protected:
  std::map<std::string, Offset> offsets;
  std::vector<OffsetWrite> offset_writes;
  std::mutex offsets_mutex;
  std::mutex fsuipc_mutex;
  IPCUser* ipc;
};

class ProcessAsyncWorker : public PromiseWorker {
 public:
  FSUIPC* fsuipc;

  ProcessAsyncWorker(FSUIPC* fsuipc) : PromiseWorker() {
    this->fsuipc = fsuipc;
  }

  void Execute();

  void OnOK();
  void OnError();

  Napi::Value GetValue(Type type, void* data, size_t length);

 private:
  int errorCode;
};

class OpenAsyncWorker : public PromiseWorker {
 public:
  FSUIPC* fsuipc;

  OpenAsyncWorker(FSUIPC* fsuipc, Simulator requestedSim) : PromiseWorker() {
    this->fsuipc = fsuipc;
    this->requestedSim = requestedSim;
  }

  void Execute();

  void OnOK();
  void OnError();

 private:
  Simulator requestedSim;
  int errorCode;
};

class CloseAsyncWorker : public PromiseWorker {
 public:
  FSUIPC* fsuipc;

  CloseAsyncWorker(FSUIPC* fsuipc) : PromiseWorker() { this->fsuipc = fsuipc; }

  void Execute();

  void OnOK();
};

}  // namespace FSUIPC

#endif
