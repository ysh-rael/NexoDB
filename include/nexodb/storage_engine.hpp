// =============================================================================
// storage_engine.hpp
// Camada de persistencia em disco. Responsavel por ler e escrever paginas de
// dados no arquivo de banco (.nxdb), sem conhecer nada sobre consultas,
// indices ou a API estilo Mongoose. Projetado para operar com uso de RAM
// previsivel: mantem um cache de paginas com tamanho limitado (LRU) em vez
// de carregar a colecao inteira em memoria, ao contrario de bancos
// puramente in-memory.
// =============================================================================
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "nexodb/common.hpp"
#include "nexodb/document.hpp"

namespace nexodb::storage {

// Opcoes de abertura do banco, definidas pelo chamador ao instanciar o DB
// (ver src/napi/database_binding.cpp e lib/Connection.js).
struct StorageOptions {
  std::string file_path;        // caminho do arquivo .nxdb no disco
  size_t cache_size_mb = 64;     // tamanho maximo do cache de paginas (RAM)
  size_t page_size_bytes = 8192; // tamanho de pagina, trade-off I/O x RAM
  bool enable_wal = true;        // write-ahead log para durabilidade/crash-safety
  bool read_only = false;
};

// Hash de ObjectId para uso em unordered_map
struct ObjectIdHash {
  size_t operator()(const document::ObjectId& oid) const noexcept {
    const uint8_t* bytes = oid.Bytes();
    size_t hash = 0xcbf29ce484222325;  // FNV offset basis 64-bit
    for (int i = 0; i < 12; ++i) {
      hash ^= bytes[i];
      hash *= 0x100000001b3;  // FNV prime 64-bit
    }
    return hash;
  }
};

struct ObjectIdEqual {
  bool operator()(const document::ObjectId& a, const document::ObjectId& b) const noexcept {
    for (int i = 0; i < 12; ++i) {
      if (a.Bytes()[i] != b.Bytes()[i]) return false;
    }
    return true;
  }
};

// Identificador logico de um registro dentro do arquivo de armazenamento
// (pagina + slot), usado internamente pelos indices para apontar para o
// documento real sem duplicar os dados.
struct RecordLocation {
  uint64_t page_id;
  uint16_t slot_id;

  bool operator==(const RecordLocation& other) const {
    return page_id == other.page_id && slot_id == other.slot_id;
  }
  bool operator!=(const RecordLocation& other) const {
    return !(*this == other);
  }
};

// Interface do motor de armazenamento. A implementacao concreta (em
// src/core/storage/*.cpp) cuida de: gerenciamento de paginas, cache LRU,
// write-ahead log e checkpoint/compactacao do arquivo.
class StorageEngine {
 public:
  static common::Result<std::unique_ptr<StorageEngine>> Open(
      const StorageOptions& options);

  virtual ~StorageEngine() = default;

  // Insere um documento e retorna sua localizacao
  virtual common::Result<RecordLocation> Insert(
      const document::Document& doc) = 0;

  // Alias para Insert (compatibilidade com API estilo Mongoose)
  virtual common::Result<RecordLocation> InsertOne(
      const document::Document& doc) = 0;
  virtual common::Result<document::Document> Read(
      const RecordLocation& location) const = 0;
  virtual common::Status Update(const RecordLocation& location,
                                 const document::Document& doc) = 0;
  virtual common::Status Remove(const RecordLocation& location) = 0;

  // Forca a escrita do WAL e das paginas sujas para o disco.
  virtual common::Status Flush() = 0;

  // Iterador para varredura completa da colecao
  class Iterator {
   public:
    virtual ~Iterator() = default;
    virtual bool Next(RecordLocation& out_location, document::Document& out_doc) = 0;
    virtual common::Status Status() const = 0;
  };

  virtual std::unique_ptr<Iterator> NewIterator() = 0;

  // Busca direta por _id usando indice primario interno (O(1) medio)
  // Retorna documento se encontrado, nullopt se nao existe
  virtual common::Result<std::optional<document::Document>> FindById(
      const document::ObjectId& oid) const = 0;

  // Constrói/reconstrói o índice primário de _id (varre todas as páginas)
  // Chamado automaticamente na primeira busca por ID ou explicitamente
  virtual common::Status BuildPrimaryIndex() = 0;

  // Insere/atualiza entrada no índice primário
  virtual common::Status UpdatePrimaryIndex(const document::ObjectId& oid,
                                             const RecordLocation& location) = 0;

  // Remove entrada do índice primário
  virtual common::Status RemoveFromPrimaryIndex(const document::ObjectId& oid) = 0;

  // Retorna contagem de documentos validos (O(1) - mantido em memoria e persistido no header)
  virtual common::Result<uint64_t> Count() const = 0;
};

}  // namespace nexodb::storage
