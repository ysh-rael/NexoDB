// =============================================================================
// document.cpp
// Implementacao de nexodb::document::Document, Value, ObjectId e serializacao
// binaria eficiente (formato interno tipo-BSON).
// =============================================================================
#include "nexodb/document.hpp"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <sstream>
#include <iomanip>

#if defined(_WIN32)
#include <process.h>
#define GETPID _getpid
#else
#include <unistd.h>
#define GETPID getpid
#endif

namespace nexodb::document {

namespace {

// Tipos BSON-like para serializacao
enum class BSONType : uint8_t {
  kDouble = 0x01,
  kString = 0x02,
  kObject = 0x03,
  kArray = 0x04,
  kBinary = 0x05,
  kObjectId = 0x07,
  kBoolean = 0x08,
  kDateTime = 0x09,
  kNull = 0x0A,
  kRegex = 0x0B,
  kInt32 = 0x10,
  kInt64 = 0x12,
  kMinKey = 0xFF,
  kMaxKey = 0x7F,
};

// Escreve inteiro little-endian no buffer
template <typename T>
void WriteLE(std::vector<uint8_t>& buf, T value) {
  static_assert(std::is_integral_v<T>, "WriteLE so aceita inteiros");
  uint8_t bytes[sizeof(T)];
  for (size_t i = 0; i < sizeof(T); ++i) {
    bytes[i] = static_cast<uint8_t>((value >> (i * 8)) & 0xFF);
  }
  buf.insert(buf.end(), bytes, bytes + sizeof(T));
}

// Le inteiro little-endian do buffer
template <typename T>
bool ReadLE(const uint8_t*& ptr, const uint8_t* end, T& out) {
  if (ptr + sizeof(T) > end) return false;
  T value = 0;
  for (size_t i = 0; i < sizeof(T); ++i) {
    value |= static_cast<T>(ptr[i]) << (i * 8);
  }
  out = value;
  ptr += sizeof(T);
  return true;
}

// Escreve string C-style (null-terminated)
void WriteCString(std::vector<uint8_t>& buf, const std::string& str) {
  buf.insert(buf.end(), str.begin(), str.end());
  buf.push_back(0);
}

// Le string C-style
bool ReadCString(const uint8_t*& ptr, const uint8_t* end, std::string& out) {
  const uint8_t* start = ptr;
  while (ptr < end && *ptr != 0) ++ptr;
  if (ptr == end) return false;
  out.assign(reinterpret_cast<const char*>(start), ptr - start);
  ++ptr;  // pula null terminator
  return true;
}

// Forward declarations
nexodb::common::Result<nexodb::common::Void> SerializeValue(const Value& value, std::vector<uint8_t>& buf);
nexodb::common::Result<nexodb::common::Void> SerializeObject(const Object& obj, std::vector<uint8_t>& buf);
nexodb::common::Result<nexodb::common::Void> SerializeArray(const Array& arr, std::vector<uint8_t>& buf);

nexodb::common::Result<nexodb::common::Void> SerializeValue(const Value& value, std::vector<uint8_t>& buf) {
  const auto& storage = value.storage();

  if (std::holds_alternative<std::nullptr_t>(storage)) {
    buf.push_back(static_cast<uint8_t>(BSONType::kNull));
    return {};
  }

  if (std::holds_alternative<bool>(storage)) {
    buf.push_back(static_cast<uint8_t>(BSONType::kBoolean));
    buf.push_back(std::get<bool>(storage) ? 1 : 0);
    return {};
  }

  if (std::holds_alternative<int64_t>(storage)) {
    int64_t v = std::get<int64_t>(storage);
    if (v >= INT32_MIN && v <= INT32_MAX) {
      buf.push_back(static_cast<uint8_t>(BSONType::kInt32));
      WriteLE(buf, static_cast<int32_t>(v));
    } else {
      buf.push_back(static_cast<uint8_t>(BSONType::kInt64));
      WriteLE(buf, v);
    }
    return {};
  }

  if (std::holds_alternative<double>(storage)) {
    buf.push_back(static_cast<uint8_t>(BSONType::kDouble));
    double v = std::get<double>(storage);
    WriteLE(buf, *reinterpret_cast<const int64_t*>(&v));
    return {};
  }

  if (std::holds_alternative<std::string>(storage)) {
    buf.push_back(static_cast<uint8_t>(BSONType::kString));
    const std::string& str = std::get<std::string>(storage);
    WriteLE(buf, static_cast<int32_t>(str.size() + 1));  // +1 para null terminator
    buf.insert(buf.end(), str.begin(), str.end());
    buf.push_back(0);
    return {};
  }

  if (std::holds_alternative<Object>(storage)) {
    buf.push_back(static_cast<uint8_t>(BSONType::kObject));
    return SerializeObject(std::get<Object>(storage), buf);
  }

  if (std::holds_alternative<Array>(storage)) {
    buf.push_back(static_cast<uint8_t>(BSONType::kArray));
    return SerializeArray(std::get<Array>(storage), buf);
  }

  if (std::holds_alternative<ObjectId>(storage)) {
    buf.push_back(static_cast<uint8_t>(BSONType::kObjectId));
    const ObjectId& oid = std::get<ObjectId>(storage);
    buf.insert(buf.end(), oid.Bytes(), oid.Bytes() + 12);
    return {};
  }

  return common::Status::Error(common::StatusCode::kInvalidArgument, "Tipo de valor nao suportado para serializacao");
}

nexodb::common::Result<nexodb::common::Void> SerializeObject(const Object& obj, std::vector<uint8_t>& buf) {
  size_t start_pos = buf.size();
  // Reservar espaco para tamanho total (4 bytes) - sera preenchido depois
  WriteLE(buf, int32_t(0));

  for (const auto& [key, value] : obj) {
    SerializeValue(value, buf);
    // Em BSON, chave vem ANTES do valor. Vamos ajustar:
    // Na verdade, formato BSON: type + key (cstring) + value
    // Vamos reescrever: primeiro escreve tipo, depois chave, depois valor
  }

  // Reescrever corretamente
  buf.resize(start_pos);
  WriteLE(buf, int32_t(0));  // placeholder para tamanho

  for (const auto& [key, value] : obj) {
    // Determina tipo do valor
    const auto& storage = value.storage();
    BSONType type;
    if (std::holds_alternative<std::nullptr_t>(storage)) type = BSONType::kNull;
    else if (std::holds_alternative<bool>(storage)) type = BSONType::kBoolean;
    else if (std::holds_alternative<int64_t>(storage)) {
      int64_t v = std::get<int64_t>(storage);
      type = (v >= INT32_MIN && v <= INT32_MAX) ? BSONType::kInt32 : BSONType::kInt64;
    }
    else if (std::holds_alternative<double>(storage)) type = BSONType::kDouble;
    else if (std::holds_alternative<std::string>(storage)) type = BSONType::kString;
    else if (std::holds_alternative<Object>(storage)) type = BSONType::kObject;
    else if (std::holds_alternative<Array>(storage)) type = BSONType::kArray;
    else if (std::holds_alternative<ObjectId>(storage)) type = BSONType::kObjectId;
    else return common::Status::Error(common::StatusCode::kInvalidArgument, "Tipo nao suportado");

    buf.push_back(static_cast<uint8_t>(type));
    WriteCString(buf, key);
    SerializeValue(value, buf);
  }

  buf.push_back(0);  // terminador do objeto

  // Atualiza tamanho total
  int32_t total_size = static_cast<int32_t>(buf.size() - start_pos);
  std::memcpy(buf.data() + start_pos, &total_size, 4);

  return {};
}

nexodb::common::Result<nexodb::common::Void> SerializeArray(const Array& arr, std::vector<uint8_t>& buf) {
  size_t start_pos = buf.size();
  WriteLE(buf, int32_t(0));  // placeholder

  for (size_t i = 0; i < arr.size(); ++i) {
    std::string key = std::to_string(i);
    const Value& value = arr[i];

    const auto& storage = value.storage();
    BSONType type;
    if (std::holds_alternative<std::nullptr_t>(storage)) type = BSONType::kNull;
    else if (std::holds_alternative<bool>(storage)) type = BSONType::kBoolean;
    else if (std::holds_alternative<int64_t>(storage)) {
      int64_t v = std::get<int64_t>(storage);
      type = (v >= INT32_MIN && v <= INT32_MAX) ? BSONType::kInt32 : BSONType::kInt64;
    }
    else if (std::holds_alternative<double>(storage)) type = BSONType::kDouble;
    else if (std::holds_alternative<std::string>(storage)) type = BSONType::kString;
    else if (std::holds_alternative<Object>(storage)) type = BSONType::kObject;
    else if (std::holds_alternative<Array>(storage)) type = BSONType::kArray;
    else if (std::holds_alternative<ObjectId>(storage)) type = BSONType::kObjectId;
    else return common::Status::Error(common::StatusCode::kInvalidArgument, "Tipo nao suportado em array");

    buf.push_back(static_cast<uint8_t>(type));
    WriteCString(buf, key);
    SerializeValue(value, buf);
  }

  buf.push_back(0);  // terminador do array

  int32_t total_size = static_cast<int32_t>(buf.size() - start_pos);
  std::memcpy(buf.data() + start_pos, &total_size, 4);

  return {};
}

// Deserializacao
nexodb::common::Result<nexodb::common::Void> DeserializeValue(const uint8_t*& ptr, const uint8_t* end, Value& out);
nexodb::common::Result<nexodb::common::Void> DeserializeObject(const uint8_t*& ptr, const uint8_t* end, Object& out);
nexodb::common::Result<nexodb::common::Void> DeserializeArray(const uint8_t*& ptr, const uint8_t* end, Array& out);

nexodb::common::Result<nexodb::common::Void> DeserializeValue(const uint8_t*& ptr, const uint8_t* end, Value& out) {
  if (ptr >= end) return common::Status::Error(common::StatusCode::kCorruptedData, "Fim inesperado do buffer");

  BSONType type = static_cast<BSONType>(*ptr++);

  switch (type) {
    case BSONType::kNull: {
      out = Value(nullptr);
      return {};
    }
    case BSONType::kBoolean: {
      if (ptr >= end) return common::Status::Error(common::StatusCode::kCorruptedData, "Boolean truncado");
      out = Value(*ptr++ != 0);
      return {};
    }
    case BSONType::kInt32: {
      int32_t v;
      if (!ReadLE(ptr, end, v)) return common::Status::Error(common::StatusCode::kCorruptedData, "Int32 truncado");
      out = Value(static_cast<int64_t>(v));
      return {};
    }
    case BSONType::kInt64: {
      int64_t v;
      if (!ReadLE(ptr, end, v)) return common::Status::Error(common::StatusCode::kCorruptedData, "Int64 truncado");
      out = Value(v);
      return {};
    }
    case BSONType::kDouble: {
      int64_t bits;
      if (!ReadLE(ptr, end, bits)) return common::Status::Error(common::StatusCode::kCorruptedData, "Double truncado");
      double v = *reinterpret_cast<const double*>(&bits);
      out = Value(v);
      return {};
    }
    case BSONType::kString: {
      int32_t len;
      if (!ReadLE(ptr, end, len)) return common::Status::Error(common::StatusCode::kCorruptedData, "String len truncado");
      if (len < 1 || ptr + len > end) return common::Status::Error(common::StatusCode::kCorruptedData, "String invalida");
      std::string str(reinterpret_cast<const char*>(ptr), len - 1);  // -1 para null terminator
      ptr += len;
      out = Value(std::move(str));
      return {};
    }
    case BSONType::kObject: {
      Object obj;
      auto result = DeserializeObject(ptr, end, obj);
      if (!result.ok()) return result;
      out = Value(std::move(obj));
      return {};
    }
    case BSONType::kArray: {
      Array arr;
      auto result = DeserializeArray(ptr, end, arr);
      if (!result.ok()) return result;
      out = Value(std::move(arr));
      return {};
    }
    case BSONType::kObjectId: {
      if (ptr + 12 > end) return common::Status::Error(common::StatusCode::kCorruptedData, "ObjectId truncado");
      ObjectId oid;
      std::memcpy(oid.Bytes(), ptr, 12);
      ptr += 12;
      out = Value(oid);
      return {};
    }
    default:
      return common::Status::Error(common::StatusCode::kCorruptedData, "Tipo BSON desconhecido");
  }
}

nexodb::common::Result<nexodb::common::Void> DeserializeObject(const uint8_t*& ptr, const uint8_t* end, Object& out) {
  if (ptr + 4 > end) return common::Status::Error(common::StatusCode::kCorruptedData, "Object size truncado");

  int32_t obj_size;
  ReadLE(ptr, end, obj_size);
  if (obj_size < 5) return common::Status::Error(common::StatusCode::kCorruptedData, "Object tamanho invalido");

  const uint8_t* obj_end = ptr + obj_size - 4;  // -4 pois ja lemos o tamanho

  while (ptr < obj_end) {
    if (*ptr == 0) {  // terminador
      ++ptr;
      break;
    }

    BSONType type = static_cast<BSONType>(*ptr++);
    std::string key;
    if (!ReadCString(ptr, obj_end, key)) {
      return common::Status::Error(common::StatusCode::kCorruptedData, "Chave invalida");
    }

    Value value;
    auto result = DeserializeValue(ptr, obj_end, value);
    if (!result.ok()) return result;

    out.emplace(std::move(key), std::move(value));
  }

  return {};
}

nexodb::common::Result<nexodb::common::Void> DeserializeArray(const uint8_t*& ptr, const uint8_t* end, Array& out) {
  if (ptr + 4 > end) return common::Status::Error(common::StatusCode::kCorruptedData, "Array size truncado");

  int32_t arr_size;
  ReadLE(ptr, end, arr_size);
  if (arr_size < 5) return common::Status::Error(common::StatusCode::kCorruptedData, "Array tamanho invalido");

  const uint8_t* arr_end = ptr + arr_size - 4;

  size_t index = 0;
  while (ptr < arr_end) {
    if (*ptr == 0) {  // terminador
      ++ptr;
      break;
    }

    BSONType type = static_cast<BSONType>(*ptr++);
    std::string key;
    if (!ReadCString(ptr, arr_end, key)) {
      return common::Status::Error(common::StatusCode::kCorruptedData, "Indice de array invalido");
    }

    Value value;
    auto result = DeserializeValue(ptr, arr_end, value);
    if (!result.ok()) return result;

    // Verifica se o indice eh sequencial
    if (key != std::to_string(index)) {
      // Nao sequencial - preenche gaps com null (simplificado)
      while (out.size() < index) {
        out.emplace_back(Value(nullptr));
      }
    }
    out.push_back(std::move(value));
    ++index;
  }

  return {};
}

}  // namespace

// =============================================================================
// ObjectId implementation
// =============================================================================
namespace {
std::atomic<uint32_t> g_counter{0};
uint32_t g_machine_id = 0;
uint16_t g_process_id = 0;
bool g_machine_init = false;
}

void InitializeMachineId() {
  if (g_machine_init) return;
  // Gera machine id baseado em hash do hostname + pid
  // Simplificado: usa valores fixos para reprodutibilidade
  g_machine_id = 0x123456;
  g_process_id = static_cast<uint16_t>(GETPID());
  g_machine_init = true;
}

ObjectId ObjectId::Generate() {
  InitializeMachineId();
  ObjectId id;

  // Timestamp: 4 bytes (segundos desde epoch)
  uint32_t timestamp = static_cast<uint32_t>(time(nullptr));
  id.bytes_[0] = static_cast<uint8_t>(timestamp >> 24);
  id.bytes_[1] = static_cast<uint8_t>(timestamp >> 16);
  id.bytes_[2] = static_cast<uint8_t>(timestamp >> 8);
  id.bytes_[3] = static_cast<uint8_t>(timestamp);

  // Machine ID: 3 bytes
  id.bytes_[4] = static_cast<uint8_t>(g_machine_id >> 16);
  id.bytes_[5] = static_cast<uint8_t>(g_machine_id >> 8);
  id.bytes_[6] = static_cast<uint8_t>(g_machine_id);

  // Process ID: 2 bytes
  id.bytes_[7] = static_cast<uint8_t>(g_process_id >> 8);
  id.bytes_[8] = static_cast<uint8_t>(g_process_id);

  // Counter: 3 bytes (atomico)
  uint32_t counter = g_counter.fetch_add(1, std::memory_order_relaxed);
  id.bytes_[9] = static_cast<uint8_t>(counter >> 16);
  id.bytes_[10] = static_cast<uint8_t>(counter >> 8);
  id.bytes_[11] = static_cast<uint8_t>(counter);

  return id;
}

std::string ObjectId::ToHexString() const {
  static const char* hex = "0123456789abcdef";
  std::string result;
  result.reserve(24);
  for (int i = 0; i < 12; ++i) {
    result.push_back(hex[bytes_[i] >> 4]);
    result.push_back(hex[bytes_[i] & 0xF]);
  }
  return result;
}

// =============================================================================
// Document implementation
// =============================================================================
void Document::EnsureId() const {
  if (fields_.find("_id") == fields_.end()) {
    fields_["_id"] = Value(ObjectId::Generate());
  }
}

// =============================================================================
// Serializacao/Deserializacao publica
// =============================================================================
nexodb::common::Result<nexodb::common::Void> Serialize(const Document& doc, std::vector<uint8_t>& buf) {
  doc.EnsureId();
  return SerializeObject(doc.fields(), buf);
}

nexodb::common::Result<nexodb::common::Void> Deserialize(const uint8_t* data, size_t length, Document& doc) {
  const uint8_t* ptr = data;
  const uint8_t* end = data + length;
  Object obj;
  auto result = DeserializeObject(ptr, end, obj);
  if (!result.ok()) return result;
  doc = Document(std::move(obj));
  return {};
}

}  // namespace nexodb::document