// fsuipc.cc
#include "FSUIPC.h"

#include <napi.h>
#include <uv.h>
#include <windows.h>

#include <v8.h>

#include <string>

#include "IPCUser.h"

namespace FSUIPC {

Napi::FunctionReference FSUIPC::constructor;
Napi::Object error = Napi::Persistent(FSUIPCError);

// class FSUIPC : public Napi::Addon<FSUIPC> {
//  public:
// FSUIPC(Napi::Env env, Napi::Object exports) {
//   DefineAddon(exports, {InstanceMethod("open", &FSUIPC::Open),
//                         InstanceMethod("close", &FSUIPC::Close),
//                         InstanceMethod("process", &FSUIPC::Process),
//                         InstanceMethod("add", &FSUIPC::Add),
//                         InstanceMethod("remove", &FSUIPC::Remove),
//                         InstanceMethod("write", &FSUIPC::Write)});
// }

void FSUIPC::Init(Napi::Env env, Napi::Object exports) {
  Napi::Function fsuipcNew = Napi::Function::New(env, FSUIPC::New);
  Napi::FunctionReference ctor = Napi::Persistent(fsuipcNew);

  constructor.Reset(fsuipcNew);

  Napi::Function tpl = DefineClass(env, "FSUIPC",
                                   {InstanceMethod("open", &FSUIPC::Open, nai),
                                    InstanceMethod("close", &FSUIPC::Close),
                                    InstanceMethod("process", &FSUIPC::Process),
                                    InstanceMethod("add", &FSUIPC::Add),
                                    InstanceMethod("remove", &FSUIPC::Remove),
                                    InstanceMethod("write", &FSUIPC::Write)}););

  Napi::FunctionReference* constructor = new Napi::FunctionReference();
  *constructor = Napi::Persistent(tpl);
  env.SetInstanceData(constructor);

  exports.Set("FSUIPC", tpl);
}

Napi::Value FSUIPC::New(const Napi::CallbackInfo& info) {
  // throw an error if constructor is called without new keyword
  Napi::Env env = Napi::Env(env);
  if (!info.IsConstructCall()) {
    return Napi::Error::New(env, "FSUIPC.new - called without new keyword")
        .Value();
  }

  if (info.Length() != 0) {
    return Napi::Error::New(env, "FSUIPC.new - expected no arguments").Value();
  }

  FSUIPC* fsuipc = new FSUIPC();
  fsuipc->ipc = new IPCUser();

  return Napi::Value();
}

Napi::Value FSUIPC::Open(const Napi::CallbackInfo& info) {
  FSUIPC* self = this;
  Napi::Env env = Napi::Env(env);
  Simulator requestedSim = Simulator::ANY;

  if (info.Length() > 0) {
    if (!info[0].IsNumber()) {
      return Napi::Error::New(
                 env, "FSUIPC.open - expected first argument to be Simulator")
          .Value();
    }

    requestedSim = static_cast<Simulator>(info[0].ToNumber().Int32Value());
  }

  auto worker = new OpenAsyncWorker(self, requestedSim);

  PromiseQueueWorker(worker);

  return worker->GetPromise();
}

Napi::Value FSUIPC::Close(const Napi::CallbackInfo& info) {
  FSUIPC* self = this;

  auto worker = new CloseAsyncWorker(self);

  PromiseQueueWorker(worker);

  return worker->GetPromise();
}

Napi::Value FSUIPC::Process(const Napi::CallbackInfo& info) {
  FSUIPC* self = this;

  auto worker = new ProcessAsyncWorker(self);

  PromiseQueueWorker(worker);

  return worker->GetPromise();
}

Napi::Value FSUIPC::Add(const Napi::CallbackInfo& info) {
  Napi::Env env = Napi::Env(env);
  FSUIPC* self = this;

  if (info.Length() < 3) {
    return Napi::Error::New(env, "FSUIPC.Add: requires at least 3 arguments")
        .Value();
  }

  if (!info[0].IsString()) {
    return Napi::TypeError::New(
               env, "FSUIPC.Add: expected first argument to be string")
        .Value();
  }

  if (!info[1].IsNumber()) {
    return Napi::TypeError::New(
               env, "FSUIPC.Add: expected second argument to be uint")
        .Value();
  }

  if (!info[2].IsNumber()) {
    return Napi::TypeError::New(env,
                                "FSUIPC.Add: expected third argument to be int")
        .Value();
  }

  std::string name =
      std::string(info[0].As<Napi::String>().Utf8Value().c_str());
  DWORD offset = info[1].ToNumber().Uint32Value();
  Type type = (Type)info[2].ToNumber().Uint32Value();

  DWORD size;

  if (type == Type::ByteArray || type == Type::BitArray ||
      type == Type::String) {
    if (info.Length() < 4) {
      return Napi::TypeError::New(
                 env,
                 "FSUIPC.Add: requires at least 4 arguments if type is "
                 "byteArray, bitArray or string")
          .Value();
    }

    if (!info[3].ToNumber().Uint32Value()) {
      return Napi::TypeError::New(
                 env, "FSUIPC.Add: expected fourth argument to be uint")
          .Value();
    }

    size = (int)info[3].ToNumber().Uint32Value();
  } else {
    size = get_size_of_type(type);
  }

  if (size == 0) {
    return Napi::TypeError::New(
               env, "FSUIPC.Add: expected fourth argument to be a size > 0")
        .Value();
  }

  self->offsets[name] = Offset{name, type, offset, size, malloc(size)};

  Napi::Object obj = Napi::Object::New(env);

  (obj).Set("name", name);
  (obj).Set("offset", info[1]);
  (obj).Set("type", (int)type);
  (obj).Set("size", (int)size);

  return obj;
}

Napi::Value FSUIPC::Remove(const Napi::CallbackInfo& info) {
  Napi::Env env = Napi::Env(env);
  FSUIPC* self = this;

  if (info.Length() != 1) {
    return Napi::TypeError::New(env, "FSUIPC.Remove: requires one argument")
        .Value();
  }

  if (!info[0].IsString()) {
    return Napi::TypeError::New(
               env, "FSUIPC.Remove: expected first argument to be string")
        .Value();
  }

  std::string name =
      std::string(info[0].As<Napi::String>().Utf8Value().c_str());

  auto it = self->offsets.find(name);

  Napi::Object obj = Napi::Object::New(env);

  (obj).Set("name", it->second.name);
  (obj).Set("offset", it->second.offset);
  (obj).Set("type", (int)it->second.type);
  (obj).Set("size", (int)it->second.size);

  self->offsets.erase(it);

  return obj;
}

Napi::Value FSUIPC::Write(const Napi::CallbackInfo& info) {
  Napi::Env env = Napi::Env(env);
  FSUIPC* self = this;

  if (info.Length() < 3) {
    return Napi::TypeError::New(env,
                                "FSUIPC.Write: requires at least 3 arguments")
        .Value();
  }

  if (!info[0].IsNumber()) {
    return Napi::TypeError::New(
               env, "FSUIPC.Write: expected first argument to be uint")
        .Value();
  }

  if (!info[1].IsNumber()) {
    return Napi::TypeError::New(
               env, "FSUIPC.Write: expected second argument to be int")
        .Value();
  }

  DWORD offset = info[0].ToNumber().Uint32Value();
  Type type = (Type)info[1].ToNumber().Uint32Value();

  DWORD size;
  void* value;

  if (type == Type::ByteArray || type == Type::BitArray ||
      type == Type::String) {
    if (info.Length() < 4) {
      return Napi::TypeError::New(
                 env,
                 "FSUIPC.Write: requires at least 4 arguments if type is "
                 "byteArray, bitArray or string")
          .Value();
    }

    if (!info[2].IsNumber()) {
      return Napi::TypeError::New(
                 env, "FSUIPC.Write: expected third argument to be uint")
          .Value();
    }

    size = (int)info[2].ToNumber().Uint32Value();
  } else {
    size = get_size_of_type(type);
  }

  if (size == 0) {
    return Napi::TypeError::New(env, "FSUIPC.Add: expected size to be > 0")
        .Value();
  }

  value = malloc(size);

  switch (type) {
    case Type::Byte: {
      uint8_t x = (uint8_t)info[2].ToNumber().Uint32Value();

      std::copy(
          static_cast<const uint8_t*>(static_cast<const void*>(&x)),
          static_cast<const uint8_t*>(static_cast<const void*>(&x)) + sizeof x,
          static_cast<uint8_t*>(value));

      break;
    }
    case Type::SByte: {
      int8_t x = (int8_t)info[2].ToNumber().Int32Value();

      std::copy(
          static_cast<const uint8_t*>(static_cast<const void*>(&x)),
          static_cast<const uint8_t*>(static_cast<const void*>(&x)) + sizeof x,
          static_cast<uint8_t*>(value));

      break;
    }
    case Type::Int16: {
      int16_t x = (int16_t)info[2].ToNumber().Int32Value();

      std::copy(
          static_cast<const uint8_t*>(static_cast<const void*>(&x)),
          static_cast<const uint8_t*>(static_cast<const void*>(&x)) + sizeof x,
          static_cast<uint8_t*>(value));

      break;
    }
    case Type::Int32: {
      int32_t x = info[2].ToNumber().Int32Value();

      std::copy(
          static_cast<const uint8_t*>(static_cast<const void*>(&x)),
          static_cast<const uint8_t*>(static_cast<const void*>(&x)) + sizeof x,
          static_cast<uint8_t*>(value));

      break;
    }
    case Type::UInt16: {
      uint16_t x = (uint16_t)info[2].ToNumber().Uint32Value();

      std::copy(
          static_cast<const uint8_t*>(static_cast<const void*>(&x)),
          static_cast<const uint8_t*>(static_cast<const void*>(&x)) + sizeof x,
          static_cast<uint8_t*>(value));

      break;
    }
    case Type::UInt32: {
      uint32_t x = info[2].ToNumber().Uint32Value();

      std::copy(
          static_cast<const uint8_t*>(static_cast<const void*>(&x)),
          static_cast<const uint8_t*>(static_cast<const void*>(&x)) + sizeof x,
          static_cast<uint8_t*>(value));

      break;
    }
    case Type::Double: {
      double x = info[2].ToNumber().DoubleValue();

      std::copy(
          static_cast<const uint8_t*>(static_cast<const void*>(&x)),
          static_cast<const uint8_t*>(static_cast<const void*>(&x)) + sizeof x,
          static_cast<uint8_t*>(value));

      break;
    }
    case Type::Single: {
      float x = (float)info[2].ToNumber().DoubleValue();

      std::copy(
          static_cast<const uint8_t*>(static_cast<const void*>(&x)),
          static_cast<const uint8_t*>(static_cast<const void*>(&x)) + sizeof x,
          static_cast<uint8_t*>(value));

      break;
    }
    case Type::Int64: {
      int64_t x;

      if (info[3].IsString()) {
        std::string x_str =
            std::string(info[3].As<Napi::String>().Utf8Value().c_str());
        x = std::stoll(x_str);
      } else if (info[3].IsNumber()) {
        x = (int64_t)info[3].ToNumber().Int32Value();
      } else {
        return Napi::TypeError::New(
                   env,
                   "FSUIPC.Write: expected fourth argument to be a string or "
                   "int when type is int64")
            .Value();
      }

      std::copy(
          static_cast<const uint8_t*>(static_cast<const void*>(&x)),
          static_cast<const uint8_t*>(static_cast<const void*>(&x)) + sizeof x,
          static_cast<uint8_t*>(value));

      break;
    }
    case Type::UInt64: {
      uint64_t x;

      if (info[3].IsString()) {
        std::string x_str =
            std::string(info[3].As<Napi::String>().Utf8Value().c_str());
        x = std::stoll(x_str);
      } else if (info[3].IsNumber()) {
        x = (uint64_t)info[3].ToNumber().Uint32Value();
      } else {
        return Napi::TypeError::New(
                   env,
                   "FSUIPC.Write: expected fourth argument to be a string or "
                   "int when type is uint64")
            .Value();
      }

      std::copy(
          static_cast<const uint8_t*>(static_cast<const void*>(&x)),
          static_cast<const uint8_t*>(static_cast<const void*>(&x)) + sizeof x,
          static_cast<uint8_t*>(value));

      break;
    }
    case Type::String: {
      std::memset(value, 0, size);

      std::string x_str =
          std::string(info[3].As<Napi::String>().Utf8Value().c_str());
      if (x_str.length() >= size) {
        return Napi::TypeError::New(
                   env,
                   "FSUIPC.Write: expected string's length to be less than "
                   "the supplied size")
            .Value();
      }

      const char* x_c_str = x_str.c_str();

      strcpy_s((char*)value, size, x_str.c_str());

      break;
    }
    case Type::ByteArray: {
      std::memset(value, 0, size);

      if (info[3].IsArrayBuffer()) {
        v8::Local<v8::ArrayBufferView> view =
            info[3].As<v8::Local<v8::ArrayBufferView>>();

        view->CopyContents(value, size);
      } else {
        return Napi::TypeError::New(
                   env,
                   "FSUIPC.Write: expected to receive ArrayBufferView for "
                   "byte array type")
            .Value();
      }

      break;
    }
    default: {
      return Napi::TypeError::New(env,
                                  "FSUIPC.Write: unsupported type for write")
          .Value();
    }
  }

  self->offset_writes.push_back(OffsetWrite{type, offset, size, value});
}

void ProcessAsyncWorker::Execute() {
  Error result;

  std::lock_guard<std::mutex> guard(this->fsuipc->offsets_mutex);
  std::lock_guard<std::mutex> fsuipc_guard(this->fsuipc->fsuipc_mutex);

  auto offsets = this->fsuipc->offsets;

  std::map<std::string, Offset>::iterator it = offsets.begin();

  for (; it != offsets.end(); ++it) {
    if (!this->fsuipc->ipc->Read(it->second.offset, it->second.size,
                                 it->second.dest, &result)) {
      this->SetErrorMessage(ErrorToString(result));
      this->errorCode = static_cast<int>(result);
      return;
    }
  }

  auto offset_writes = this->fsuipc->offset_writes;

  std::vector<OffsetWrite>::iterator write_it = offset_writes.begin();

  for (; write_it != offset_writes.end(); ++write_it) {
    if (!this->fsuipc->ipc->Write(write_it->offset, write_it->size,
                                  write_it->src, &result)) {
      this->SetErrorMessage(ErrorToString(result));
      this->errorCode = static_cast<int>(result);
      return;
    }

    free(write_it->src);
  }

  this->fsuipc->offset_writes.clear();

  if (!this->fsuipc->ipc->Process(&result)) {
    this->SetErrorMessage(ErrorToString(result));
    this->errorCode = static_cast<int>(result);
    return;
  }
}

void ProcessAsyncWorker::OnOK() {
  Napi::Env env = Napi::Env(env);
  Napi::HandleScope scope(env);

  std::lock_guard<std::mutex> guard(this->fsuipc->offsets_mutex);

  auto offsets = this->fsuipc->offsets;

  Napi::Object obj = Napi::Object::New(env);
  std::map<std::string, Offset>::iterator it = offsets.begin();

  for (; it != offsets.end(); ++it) {
    (obj).Set(it->second.name, this->GetValue(it->second.type, it->second.dest,
                                              it->second.size));
  }

  resolver->Resolve(obj);
}

void ProcessAsyncWorker::OnError() {
  Napi::Env env = Napi::Env(env);
  Napi::HandleScope scope(env);

  Napi::Value argv[] = {Napi::Value::From(env, ErrorMessage()),
                        Napi::Value::From(env, this->errorCode)};
  Napi::Value error = Napi::Error::New(env, ErrorMessage()).Value();
  // Napi::CallAsConstructor(Napi::New(env, FSUIPCError), 2, argv);

  resolver->Reject(error);
}

Napi::Value ProcessAsyncWorker::GetValue(Type type, void* data, size_t length) {
  Napi::Env env = Napi::Env(env);
  Napi::EscapableHandleScope scope(env);

  switch (type) {
    case Type::Byte:
      return scope.Escape(Napi::Value::From(env, *((uint8_t*)data)));
    case Type::SByte:
      return scope.Escape(Napi::Value::From(env, *((int8_t*)data)));
    case Type::Int16:
      return scope.Escape(Napi::Value::From(env, *((int16_t*)data)));
    case Type::Int32:
      return scope.Escape(Napi::Value::From(env, *((int32_t*)data)));
    case Type::Int64:
      return scope.Escape(
          Napi::Value::From(env, std::to_string(*((int64_t*)data))));
    case Type::UInt16:
      return scope.Escape(Napi::Value::From(env, *((uint16_t*)data)));
    case Type::UInt32:
      return scope.Escape(Napi::Value::From(env, *((uint32_t*)data)));
    case Type::UInt64:
      return scope.Escape(
          Napi::Value::From(env, std::to_string(*((uint64_t*)data))));
    case Type::Double:
      return scope.Escape(Napi::Value::From(env, *((double*)data)));
    case Type::Single:
      return scope.Escape(Napi::Value::From(env, *((float*)data)));
    case Type::String: {
      char* str = (char*)data;
      return scope.Escape(Napi::Value::From(env, str));
    }
    case Type::BitArray: {
      Napi::Array arr = Napi::Array::New(env, length * 8);
      uint8_t* bits = (uint8_t*)data;
      for (int i = 0; i < length * 8; i++) {
        int byte_index = i / 8;
        int bit_index = i % 8;
        int mask = 1 << bit_index;
        bool value = (bits[byte_index] & mask) != 0;
        arr.Set(i, value);
      }
      return scope.Escape(arr);
    }
    case Type::ByteArray: {
      Napi::Array arr = Napi::Array::New(env, length);
      uint8_t* bytes = (uint8_t*)data;
      for (int i = 0; i < length; i++) {
        arr.Set(i, *bytes);
        bytes++;
      }
      return scope.Escape(arr);
    }
  }

  return scope.Escape(env.Undefined());
}

DWORD get_size_of_type(Type type) {
  switch (type) {
    case Type::Byte:
    case Type::SByte:
      return 1;
    case Type::Int16:
    case Type::UInt16:
      return 2;
    case Type::Int32:
    case Type::UInt32:
      return 4;
    case Type::Int64:
    case Type::UInt64:
      return 8;
    case Type::Double:
      return 8;
    case Type::Single:
      return 4;
  }
  return 0;
}

void OpenAsyncWorker::Execute() {
  Error result;

  std::lock_guard<std::mutex> fsuipc_guard(this->fsuipc->fsuipc_mutex);

  if (!this->fsuipc->ipc->Open(this->requestedSim, &result)) {
    this->SetErrorMessage(ErrorToString(result));
    this->errorCode = static_cast<int>(result);
    return;
  }
}

void OpenAsyncWorker::OnOK() {
  Napi::Env env = Napi::Env(env);
  Napi::HandleScope scope(env);

  resolver->Resolve(this->fsuipc->handle());
}

void OpenAsyncWorker::OnError() {
  Napi::Env env = Napi::Env(env);
  Napi::HandleScope scope(env);

  Napi::Value argv[] = {Napi::Value::From(env, ErrorMessage()),
                        Napi::Value::From(env, this->errorCode)};
  Napi::Value error = Napi::Error::New(env, ErrorMessage()).Value();

  resolver->Reject(error);
}

void CloseAsyncWorker::Execute() {
  std::lock_guard<std::mutex> fsuipc_guard(this->fsuipc->fsuipc_mutex);

  this->fsuipc->ipc->Close();
}

void CloseAsyncWorker::OnOK() {
  Napi::Env env = Napi::Env(env);
  Napi::HandleScope scope(env);

  resolver->Resolve(this->fsuipc->handle());
}

Napi::Object InitType(Napi::Env env, Napi::Object exports) {
  Napi::Object obj = Napi::Object::New(env);

  obj.DefineProperties({
      Napi::PropertyDescriptor::Value(
          "Byte", Napi::Value::From(env, (int)Type::Byte), napi_default),
      Napi::PropertyDescriptor::Value(
          "SByte", Napi::Value::From(env, (int)Type::SByte), napi_default),
      Napi::PropertyDescriptor::Value(
          "Int16", Napi::Value::From(env, (int)Type::Int16), napi_default),
      Napi::PropertyDescriptor::Value(
          "Int32", Napi::Value::From(env, (int)Type::Int32), napi_default),
      Napi::PropertyDescriptor::Value(
          "Int64", Napi::Value::From(env, (int)Type::Int64), napi_default),
      Napi::PropertyDescriptor::Value(
          "UInt16", Napi::Value::From(env, (int)Type::UInt16), napi_default),
      Napi::PropertyDescriptor::Value(
          "UInt32", Napi::Value::From(env, (int)Type::UInt32), napi_default),
      Napi::PropertyDescriptor::Value(
          "UInt64", Napi::Value::From(env, (int)Type::UInt64), napi_default),
      Napi::PropertyDescriptor::Value(
          "Double", Napi::Value::From(env, (int)Type::Double), napi_default),
      Napi::PropertyDescriptor::Value(
          "Single", Napi::Value::From(env, (int)Type::Single), napi_default),
      Napi::PropertyDescriptor::Value(
          "ByteArray", Napi::Value::From(env, (int)Type::ByteArray),
          napi_default),
      Napi::PropertyDescriptor::Value(
          "String", Napi::Value::From(env, (int)Type::String), napi_default),
      Napi::PropertyDescriptor::Value(
          "BitArray", Napi::Value::From(env, (int)Type::BitArray),
          napi_default),
  });

  exports.Set(Napi::Value::From(env, "Type"), obj);
}

Napi::Object InitError(Napi::Env env, Napi::Object exports) {
  Napi::Env env = Napi::Env(env);
  std::string code =
      "class FSUIPCError extends Error {"
      "  constructor (message, code) {"
      "    super(message);"
      "    this.name = this.constructor.name;"
      "    Error.captureStackTrace(this, this.constructor);"
      "    this.code = code;"
      "  }"
      "};"
      "FSUIPCError";

  Napi::Object errorFunc = env.RunScript(code).ToObject();

  FSUIPCError.Reset(errorFunc);

  exports.Set(Napi::Value::From(env, "FSUIPCError"), errorFunc);

  Napi::Object obj = Napi::Object::New(env);
  obj.DefineProperties({
      Napi::PropertyDescriptor::Value(
          "OK", Napi::Value::From(env, static_cast<int>(Error::OK)),
          napi_default),
      Napi::PropertyDescriptor::Value(
          "OPEN", Napi::Value::From(env, static_cast<int>(Error::OPEN)),
          napi_default),
      Napi::PropertyDescriptor::Value(
          "NOFS", Napi::Value::From(env, static_cast<int>(Error::NOFS)),
          napi_default),
      Napi::PropertyDescriptor::Value(
          "REGMSG", Napi::Value::From(env, static_cast<int>(Error::REGMSG)),
          napi_default),
      Napi::PropertyDescriptor::Value(
          "ATOM", Napi::Value::From(env, static_cast<int>(Error::ATOM)),
          napi_default),
      Napi::PropertyDescriptor::Value(
          "MAP", Napi::Value::From(env, static_cast<int>(Error::MAP)),
          napi_default),
      Napi::PropertyDescriptor::Value(
          "VIEW", Napi::Value::From(env, static_cast<int>(Error::VIEW)),
          napi_default),
      Napi::PropertyDescriptor::Value(
          "VERSION", Napi::Value::From(env, static_cast<int>(Error::VERSION)),
          napi_default),
      Napi::PropertyDescriptor::Value(
          "WRONGFS", Napi::Value::From(env, static_cast<int>(Error::WRONGFS)),
          napi_default),
      Napi::PropertyDescriptor::Value(
          "NOTOPEN", Napi::Value::From(env, static_cast<int>(Error::NOTOPEN)),
          napi_default),
      Napi::PropertyDescriptor::Value(
          "NODATA", Napi::Value::From(env, static_cast<int>(Error::NODATA)),
          napi_default),
      Napi::PropertyDescriptor::Value(
          "TIMEOUT", Napi::Value::From(env, static_cast<int>(Error::TIMEOUT)),
          napi_default),
      Napi::PropertyDescriptor::Value(
          "SENDMSG", Napi::Value::From(env, static_cast<int>(Error::SENDMSG)),
          napi_default),
      Napi::PropertyDescriptor::Value(
          "DATA", Napi::Value::From(env, static_cast<int>(Error::DATA)),
          napi_default),
      Napi::PropertyDescriptor::Value(
          "RUNNING", Napi::Value::From(env, static_cast<int>(Error::RUNNING)),
          napi_default),
      Napi::PropertyDescriptor::Value(
          "SIZE", Napi::Value::From(env, static_cast<int>(Error::SIZE)),
          napi_default),
  });

  exports.Set(Napi::Value::From(env, "ErrorCode"), obj);
}

Napi::Object InitSimulator(Napi::Env env, Napi::Object exports) {
  Napi::Object obj = Napi::Object::New(env);
  obj.DefineProperties({
      Napi::PropertyDescriptor::Value(
          "ANY", Napi::Value::From(env, static_cast<int>(Simulator::ANY)),
          napi_default),
      Napi::PropertyDescriptor::Value(
          "FS98", Napi::Value::From(env, static_cast<int>(Simulator::FS98)),
          napi_default),
      Napi::PropertyDescriptor::Value(
          "FS2K", Napi::Value::From(env, static_cast<int>(Simulator::FS2K)),
          napi_default),
      Napi::PropertyDescriptor::Value(
          "CFS2", Napi::Value::From(env, static_cast<int>(Simulator::CFS2)),
          napi_default),
      Napi::PropertyDescriptor::Value(
          "CFS1", Napi::Value::From(env, static_cast<int>(Simulator::CFS1)),
          napi_default),
      Napi::PropertyDescriptor::Value(
          "FLY", Napi::Value::From(env, static_cast<int>(Simulator::FLY)),
          napi_default),
      Napi::PropertyDescriptor::Value(
          "FS2K2", Napi::Value::From(env, static_cast<int>(Simulator::FS2K2)),
          napi_default),
      Napi::PropertyDescriptor::Value(
          "FS2K4", Napi::Value::From(env, static_cast<int>(Simulator::FS2K4)),
          napi_default),
      Napi::PropertyDescriptor::Value(
          "FSX", Napi::Value::From(env, static_cast<int>(Simulator::FSX)),
          napi_default),
      Napi::PropertyDescriptor::Value(
          "ESP", Napi::Value::From(env, static_cast<int>(Simulator::ESP)),
          napi_default),
      Napi::PropertyDescriptor::Value(
          "P3D", Napi::Value::From(env, static_cast<int>(Simulator::P3D)),
          napi_default),
      Napi::PropertyDescriptor::Value(
          "FSX64", Napi::Value::From(env, static_cast<int>(Simulator::FSX64)),
          napi_default),
      Napi::PropertyDescriptor::Value(
          "P3D64", Napi::Value::From(env, static_cast<int>(Simulator::P3D64)),
          napi_default),
      Napi::PropertyDescriptor::Value(
          "MSFS", Napi::Value::From(env, static_cast<int>(Simulator::MSFS)),
          napi_default),
  });

  exports.Set(Napi::Value::From(env, "Simulator"), obj);
}

}  // namespace FSUIPC
