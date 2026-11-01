// =============================================================================
// storage_engine.cpp
// Implementacao concreta de nexodb::storage::StorageEngine.
// Gerenciador de paginas com cache LRU, write-ahead log (WAL) e checkpoint.
// =============================================================================
#include "nexodb/storage_engine.hpp"

#include "nexodb/document.hpp"
#include "nexodb/logger.hpp"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstring>
#include <fstream>
#include <list>
#include <mutex>
// #include <shared_mutex>  // Not available on all MSVC versions
#include <unordered_map>
#include <vector>

namespace nexodb::storage {

using nexodb::common::Status;
using nexodb::common::StatusCode;
using nexodb::common::Result;

// Forward declaration for Iterator
class StorageIterator;

namespace {

#pragma pack(push, 1)
// Cabecalho do arquivo de banco (.nxdb)
struct FileHeader {
  char magic[4] = {'N', 'X', 'D', 'B'};
  uint32_t version = 1;
  uint32_t page_size = 8192;
  uint64_t page_count = 0;
  uint64_t free_list_head = 0;  // Page ID da primeira pagina livre
  uint64_t wal_offset = 0;      // Offset do WAL no arquivo
  uint64_t document_count = 0;  // Contador de documentos validos (nao deletados)
};
#pragma pack(pop)

// Cabecalho de cada pagina de dados
struct PageHeader {
  uint64_t page_id = 0;
  uint16_t record_count = 0;
  uint16_t slot_dir_size = 0;       // Tamanho do diretorio de slots em bytes
  uint16_t free_space_end = 0;      // Offset do fim do espaco livre (dados crescem para tras a partir do fim da pagina)
  uint64_t next_page_id = 0;        // Para paginas de overflow
  uint32_t checksum = 0;
};

// Entrada do diretorio de slots (aponta para registros dentro da pagina)
// Tamanho fixo de 8 bytes para evitar problemas de alinhamento
struct SlotEntry {
  uint16_t offset = 0;
  uint16_t length = 0;
  uint16_t reserved = 0;  // Padding para alinhamento de 8 bytes
  bool is_deleted = false;
  bool reserved2 = false; // Padding
  // Total: 8 bytes
};
static_assert(sizeof(SlotEntry) == 8, "SlotEntry deve ter 8 bytes");

// Calcula checksum simples (CRC32 simplificado)
uint32_t ComputeChecksum(const void* data, size_t size) {
  const uint8_t* bytes = static_cast<const uint8_t*>(data);
  uint32_t hash = 0x811C9DC5;  // FNV offset basis
  for (size_t i = 0; i < size; ++i) {
    hash ^= bytes[i];
    hash *= 0x01000193;  // FNV prime
  }
  return hash;
}

// Estrutura para o cache LRU
struct CachedPage {
  uint64_t page_id;
  std::vector<uint8_t> data;
  bool dirty = false;
  mutable std::list<uint64_t>::iterator lru_it;
};

}  // namespace

// =============================================================================
// FileStorageEngine - Implementacao do motor de armazenamento baseado em arquivo
// =============================================================================
class FileStorageEngine final : public StorageEngine {
  // Allow StorageIterator to access private members
  friend class StorageIterator;

 public:
  explicit FileStorageEngine(StorageOptions options)
      : options_(std::move(options)),
        file_(options_.file_path, std::ios::in | std::ios::out | std::ios::binary),
        page_size_(options_.page_size_bytes),
        cache_size_pages_(std::max<size_t>(1, (options_.cache_size_mb * 1024 * 1024) / options_.page_size_bytes)) {
    NEXODB_LOG_INFO("FileStorageEngine", "Abrindo banco: " + options_.file_path);
    Initialize();
    NEXODB_LOG_INFO("FileStorageEngine", "Construtor concluido, document_count_=" + std::to_string(document_count_));
  }

  ~FileStorageEngine() override {
    Flush();
    NEXODB_LOG_INFO("FileStorageEngine", "Banco fechado");
  }

  // Metodo auxiliar para iterator (sem lock) - non-const version
  common::Result<CachedPage*> GetPageUnsafe(uint64_t page_id) {
    return GetPageUnsafeImpl(page_id);
  }

  // Const version for iterator
  common::Result<const CachedPage*> GetPageUnsafe(uint64_t page_id) const {
    return GetPageUnsafeImplConst(page_id);
  }

  std::unique_ptr<Iterator> NewIterator() override;

 public:
  // Primary index for _id field: ObjectId -> RecordLocation
  // Built lazily on first FindById call
  common::Result<std::optional<document::Document>> FindById(
      const document::ObjectId& oid) const override;
  common::Status BuildPrimaryIndex() override;
  common::Status UpdatePrimaryIndex(const document::ObjectId& oid,
                                     const RecordLocation& location) override;
  common::Status RemoveFromPrimaryIndex(const document::ObjectId& oid) override;

 private:
  // Primary index: maps ObjectId to RecordLocation
  // Only stores ObjectId keys (not strings) for efficiency
  mutable std::unordered_map<document::ObjectId, RecordLocation,
                             ObjectIdHash, ObjectIdEqual> primary_index_;
  mutable bool primary_index_built_ = false;
  mutable std::mutex primary_index_mutex_;

  common::Result<CachedPage*> GetPageUnsafeImpl(uint64_t page_id) {
    auto it = cache_.find(page_id);
    if (it != cache_.end()) {
      UpdateLRU(page_id);
      return &it->second;
    }

    if (page_id >= header_.page_count) {
      return common::Status::Error(common::StatusCode::kNotFound, "Pagina nao existe");
    }

    CachedPage page;
    page.page_id = page_id;
    page.data.resize(page_size_);

    size_t file_offset = sizeof(FileHeader) + page_id * page_size_;
    file_.seekg(file_offset);
    file_.read(reinterpret_cast<char*>(page.data.data()), page_size_);

    if (!file_) {
      return common::Status::Error(common::StatusCode::kIOError, "Falha ao ler pagina do disco");
    }

    cache_[page_id] = std::move(page);
    lru_list_.push_front(page_id);
    cache_[page_id].lru_it = lru_list_.begin();

    EnforceCacheLimit();
    return &cache_[page_id];
  }

  // Const version for const iterator
  common::Result<const CachedPage*> GetPageUnsafeImplConst(uint64_t page_id) const {
    auto it = cache_.find(page_id);
    if (it != cache_.end()) {
      // Note: UpdateLRU not called for const version to avoid modifying cache
      return &it->second;
    }

    if (page_id >= header_.page_count) {
      return common::Status::Error(common::StatusCode::kNotFound, "Pagina nao existe");
    }

    // For const version, we can't modify cache, so return error
    // The iterator should use non-const version via non-const engine reference
    return common::Status::Error(common::StatusCode::kIOError, "Const access not supported for uncached pages");
  }

  // Insert: grava documento em uma pagina (nova ou existente com espaco)
  common::Result<RecordLocation> Insert(const document::Document& doc) override {
    return InsertOne(doc);
  }

  // InsertOne: alias para Insert
  common::Result<RecordLocation> InsertOne(const document::Document& doc) override {
    std::lock_guard<std::mutex> lock(mutex_);

    // Serializa o documento
    std::vector<uint8_t> serialized;
    auto serialize_result = document::Serialize(doc, serialized);
    if (!serialize_result.ok()) {
      return serialize_result.status();
    }

    const size_t record_size = serialized.size();
    if (record_size > page_size_ - sizeof(PageHeader) - sizeof(SlotEntry)) {
      // Documento muito grande - precisaria de overflow pages (TODO)
      return common::Status::Error(common::StatusCode::kInvalidArgument,
                                    "Documento excede tamanho maximo de pagina");
    }

    // Procura pagina com espaco livre ou aloca nova
    uint64_t page_id = FindPageWithSpace(record_size);
    if (page_id == 0) {
      page_id = AllocateNewPage();
    }

    auto page_result = GetPage(page_id);
    if (!page_result.ok()) {
      return page_result.status();
    }
    CachedPage& page = *page_result.value();

    PageHeader* header = reinterpret_cast<PageHeader*>(page.data.data());
    SlotEntry* slots = reinterpret_cast<SlotEntry*>(page.data.data() + sizeof(PageHeader));

    // Encontra slot livre
    uint16_t slot_id = header->record_count;
    for (uint16_t i = 0; i < header->record_count; ++i) {
      if (slots[i].is_deleted) {
        slot_id = i;
        break;
      }
    }

    // Se estamos adicionando um novo slot (nao reutilizando), o diretorio de slots vai crescer
    bool new_slot = (slot_id == header->record_count);
    uint16_t new_slot_dir_size = header->slot_dir_size + (new_slot ? sizeof(SlotEntry) : 0);

    // Dados crescem para tras a partir do fim da pagina
    // Novo offset = free_space_end - record_size (alinhado)
    uint16_t new_free_space_end = header->free_space_end - static_cast<uint16_t>(record_size);

    // Verifica se ha espaco: o fim dos dados nao pode ultrapassar o inicio do diretorio de slots
    uint16_t slot_dir_end = sizeof(PageHeader) + new_slot_dir_size;
    if (new_free_space_end < slot_dir_end) {
      return common::Status::Error(common::StatusCode::kIOError,
                                    "Espaco insuficiente na pagina");
    }

    // Escreve o registro no fim da area livre
    uint16_t data_offset = new_free_space_end;
    std::memcpy(page.data.data() + data_offset, serialized.data(), record_size);

    // Atualiza slot
    slots[slot_id] = {data_offset, static_cast<uint16_t>(record_size), 0, false, false};
    if (new_slot) {
      header->record_count++;
      header->slot_dir_size = new_slot_dir_size;
    }
    header->free_space_end = new_free_space_end;
    header->checksum = ComputeChecksum(page.data.data() + sizeof(PageHeader),
                                       page_size_ - sizeof(PageHeader));

    page.dirty = true;
    UpdateLRU(page_id);

    // WAL: registra a insercao
    if (options_.enable_wal) {
      WriteWALEntry(WALEntryType::kInsert, page_id, slot_id, serialized);
    }

    RecordLocation loc{page_id, slot_id};

    // Atualiza indice primario com _id do documento
    auto id_it = doc.fields().find("_id");
    if (id_it != doc.fields().end()) {
      const auto* oid = std::get_if<document::ObjectId>(&id_it->second.storage());
      if (oid) {
        UpdatePrimaryIndex(*oid, loc);
      }
    }

    // Incrementa contador de documentos
    document_count_++;
    header_.document_count = document_count_;

    // Atualiza a ultima pagina com espaco se ainda houver espaco
    // Precisa de espaco para pelo menos um slot + um registro minimo
    if (header->free_space_end - (sizeof(PageHeader) + header->slot_dir_size + sizeof(SlotEntry)) >= 32) {
      last_page_with_space_ = page_id;
    } else {
      last_page_with_space_ = 0;  // Pagina cheia
    }
    NEXODB_LOG_DEBUG("FileStorageEngine", "Insert: documento inserido na pagina " + std::to_string(page_id) + " slot " + std::to_string(slot_id));
    return loc;
  }

  // Read: le documento de uma localizacao especifica
  common::Result<document::Document> Read(const RecordLocation& location) const override {
    std::lock_guard<std::mutex> lock(mutex_);

    auto page_result = GetPage(location.page_id);
    if (!page_result.ok()) {
      return page_result.status();
    }
    const CachedPage& page = *page_result.value();

    const PageHeader* header = reinterpret_cast<const PageHeader*>(page.data.data());
    const SlotEntry* slots = reinterpret_cast<const SlotEntry*>(page.data.data() + sizeof(PageHeader));

    if (location.slot_id >= header->record_count) {
      return common::Status::Error(common::StatusCode::kNotFound,
                                    "Slot invalido");
    }

    const SlotEntry& slot = slots[location.slot_id];
    if (slot.is_deleted) {
      return common::Status::Error(common::StatusCode::kNotFound,
                                    "Registro deletado");
    }

    const uint8_t* record_data = page.data.data() + slot.offset;
    document::Document doc;
    auto deserialize_result = document::Deserialize(record_data, slot.length, doc);
    if (!deserialize_result.ok()) {
      return deserialize_result.status();
    }

    NEXODB_LOG_DEBUG("FileStorageEngine", "Read: documento lido da pagina " + std::to_string(location.page_id) + " slot " + std::to_string(location.slot_id));
    return doc;
  }

  // Update: atualiza documento existente
  common::Status Update(const RecordLocation& location, const document::Document& doc) override {
    std::unique_lock<std::mutex> lock(mutex_);

    std::vector<uint8_t> serialized;
    auto serialize_result = document::Serialize(doc, serialized);
    if (!serialize_result.ok()) {
      return serialize_result.status();
    }

    const size_t record_size = serialized.size();
    auto page_result = GetPage(location.page_id);
    if (!page_result.ok()) {
      return page_result.status();
    }
    CachedPage& page = *page_result.value();

    PageHeader* header = reinterpret_cast<PageHeader*>(page.data.data());
    SlotEntry* slots = reinterpret_cast<SlotEntry*>(page.data.data() + sizeof(PageHeader));

    if (location.slot_id >= header->record_count) {
      return common::Status::Error(common::StatusCode::kNotFound, "Slot invalido");
    }

    SlotEntry& slot = slots[location.slot_id];
    if (slot.is_deleted) {
      return common::Status::Error(common::StatusCode::kNotFound, "Registro deletado");
    }

    // Se o novo registro cabe no mesmo espaco, sobrescreve
    if (record_size <= slot.length) {
      std::memcpy(page.data.data() + slot.offset, serialized.data(), record_size);
      slot.length = static_cast<uint16_t>(record_size);

      header->checksum = ComputeChecksum(page.data.data() + sizeof(PageHeader),
                                         page_size_ - sizeof(PageHeader));
      page.dirty = true;
      UpdateLRU(location.page_id);

      if (options_.enable_wal) {
        WriteWALEntry(WALEntryType::kUpdate, location.page_id, location.slot_id, serialized);
      }

      // Atualiza indice primario se _id mudou (embora _id normalmente nao mude)
      auto id_it = doc.fields().find("_id");
      if (id_it != doc.fields().end()) {
        const auto* oid = std::get_if<document::ObjectId>(&id_it->second.storage());
        if (oid) {
          UpdatePrimaryIndex(*oid, location);
        }
      }
    } else {
      // Nao cabe - marca como deletado e insere novo (simplificado)
      slot.is_deleted = true;
      lock.unlock();  // Libera lock para chamar Insert recursivamente
      auto new_loc_result = Insert(doc);
      if (!new_loc_result.ok()) {
        return new_loc_result.status();
      }
      // Remove do indice primario a localizacao antiga (Insert ja adicionou a nova)
      auto id_it = doc.fields().find("_id");
      if (id_it != doc.fields().end()) {
        const auto* oid = std::get_if<document::ObjectId>(&id_it->second.storage());
        if (oid) {
          // O Insert ja adicionou a nova localizacao, mas precisamos remover a antiga
          // se o _id for o mesmo (o que deveria ser). O UpdatePrimaryIndex sobrescreve.
        }
      }
      return common::Status::Ok();
    }

    NEXODB_LOG_DEBUG("FileStorageEngine", "Update: documento atualizado na pagina " + std::to_string(location.page_id) + " slot " + std::to_string(location.slot_id));
    return common::Status::Ok();
  }

  // Remove: marca registro como deletado (soft delete)
  common::Status Remove(const RecordLocation& location) override {
    std::lock_guard<std::mutex> lock(mutex_);

    auto page_result = GetPage(location.page_id);
    if (!page_result.ok()) {
      return page_result.status();
    }
    CachedPage& page = *page_result.value();

    PageHeader* header = reinterpret_cast<PageHeader*>(page.data.data());
    SlotEntry* slots = reinterpret_cast<SlotEntry*>(page.data.data() + sizeof(PageHeader));

    if (location.slot_id >= header->record_count) {
      return common::Status::Error(common::StatusCode::kNotFound, "Slot invalido");
    }

    SlotEntry& slot = slots[location.slot_id];
    if (slot.is_deleted) {
      return common::Status::Error(common::StatusCode::kNotFound, "Ja deletado");
    }

    // Le o _id antes de marcar como deletado para remover do indice primario
    const uint8_t* record_data = page.data.data() + slot.offset;
    document::Document doc;
    auto deserialize_result = document::Deserialize(record_data, slot.length, doc);
    document::ObjectId oid_to_remove;
    bool has_oid = false;
    if (deserialize_result.ok()) {
      auto id_it = doc.fields().find("_id");
      if (id_it != doc.fields().end()) {
        const auto* oid = std::get_if<document::ObjectId>(&id_it->second.storage());
        if (oid) {
          oid_to_remove = *oid;
          has_oid = true;
        }
      }
    }

    slot.is_deleted = true;
    header->checksum = ComputeChecksum(page.data.data() + sizeof(PageHeader),
                                       page_size_ - sizeof(PageHeader));
    page.dirty = true;
    UpdateLRU(location.page_id);

    if (options_.enable_wal) {
      WriteWALEntry(WALEntryType::kDelete, location.page_id, location.slot_id, {});
    }

    // Remove do indice primario
    if (has_oid) {
      RemoveFromPrimaryIndex(oid_to_remove);
    }

    NEXODB_LOG_DEBUG("FileStorageEngine", "Remove: documento marcado como deletado na pagina " + std::to_string(location.page_id) + " slot " + std::to_string(location.slot_id));

    // Decrementa contador de documentos
    if (document_count_ > 0) {
      document_count_--;
    }
    // Atualiza header no disco
    header_.document_count = document_count_;
    WriteFileHeader();

    return common::Status::Ok();
  }

  // Count: retorna contagem de documentos validos (O(1))
  common::Result<uint64_t> Count() const override {
    // std::lock_guard<std::mutex> lock(mutex_);
    return document_count_;
  }

  // Flush: grava todas as paginas sujas e o WAL no disco
  common::Status Flush() override {
    std::lock_guard<std::mutex> lock(mutex_);

    // Grava paginas sujas
    for (auto& [page_id, page] : cache_) {
      if (page.dirty) {
        WritePageToDisk(page_id, page);
        page.dirty = false;
      }
    }

    // Atualiza cabecalho do arquivo
    WriteFileHeader();

    // Sincroniza WAL
    if (wal_file_.is_open()) {
      wal_file_.flush();
    }

    if (file_.is_open()) {
      file_.flush();
    }

    return common::Status::Ok();
  }

 private:
  // Inicializa arquivo e carrega cabecalho
  void Initialize() {
    if (!file_.is_open()) {
      // Cria novo arquivo
      std::ofstream create_file(options_.file_path, std::ios::binary);
      create_file.close();
      file_.open(options_.file_path, std::ios::in | std::ios::out | std::ios::binary);
    }

    if (!file_.is_open()) {
      throw std::runtime_error("Nao foi possivel abrir arquivo: " + options_.file_path);
    }

    // Le cabecalho
    file_.seekg(0);
    file_.read(reinterpret_cast<char*>(&header_), sizeof(FileHeader));

    NEXODB_LOG_DEBUG("FileStorageEngine", "Header lido: magic=" + std::string(header_.magic, 4) +
                     " version=" + std::to_string(header_.version) +
                     " page_count=" + std::to_string(header_.page_count) +
                     " doc_count=" + std::to_string(header_.document_count));

    if (std::strncmp(header_.magic, "NXDB", 4) != 0) {
      // Arquivo novo - inicializa cabecalho
      header_ = FileHeader{};
      header_.page_size = static_cast<uint32_t>(page_size_);
      WriteFileHeader();
    }

    // Carrega contador de documentos do header
    document_count_ = header_.document_count;
    NEXODB_LOG_DEBUG("FileStorageEngine", "document_count_ inicializado: " + std::to_string(document_count_));

    // Verifica integridade do contador (opcional: varre paginas se contador parece invalido)
    // Se document_count for 0 mas ha paginas, ou se for maior que o maximo possivel
    if (document_count_ == 0 && header_.page_count > 0) {
      NEXODB_LOG_WARN("FileStorageEngine", "Contador de documentos zerado mas ha paginas - reconstruindo contador");
      document_count_ = RebuildDocumentCount();
    } else if (document_count_ > header_.page_count * 1000) {  // Sanity check
      NEXODB_LOG_WARN("FileStorageEngine", "Contador de documentos invalido - reconstruindo contador");
      document_count_ = RebuildDocumentCount();
    }

    // Abre WAL
    if (options_.enable_wal) {
      std::string wal_path = options_.file_path + ".wal";
      wal_file_.open(wal_path, std::ios::in | std::ios::out | std::ios::binary | std::ios::app);
      if (!wal_file_.is_open()) {
        wal_file_.open(wal_path, std::ios::out | std::ios::binary);
        wal_file_.close();
        wal_file_.open(wal_path, std::ios::in | std::ios::out | std::ios::binary | std::ios::app);
      }
    }

    // TODO: Replay WAL para recuperacao apos crash
  }

  // Escreve cabecalho do arquivo no disco
  void WriteFileHeader() {
    file_.clear();
    file_.seekp(0);
    file_.write(reinterpret_cast<const char*>(&header_), sizeof(FileHeader));
  }

  // Reconstrói contador de documentos varrendo todas as paginas (para recuperacao)
  uint64_t RebuildDocumentCount() {
    uint64_t count = 0;
    for (uint64_t page_id = 0; page_id < header_.page_count; ++page_id) {
      auto page_result = GetPageUnsafeImpl(page_id);
      if (!page_result.ok()) continue;

      CachedPage& page = *page_result.value();
      PageHeader* header = reinterpret_cast<PageHeader*>(page.data.data());
      SlotEntry* slots = reinterpret_cast<SlotEntry*>(page.data.data() + sizeof(PageHeader));

      for (uint16_t slot_id = 0; slot_id < header->record_count; ++slot_id) {
        if (!slots[slot_id].is_deleted) {
          count++;
        }
      }
    }
    header_.document_count = count;
    WriteFileHeader();
    NEXODB_LOG_INFO("FileStorageEngine", "Contador reconstruido: " + std::to_string(count) + " documentos");
    return count;
  }

  // Encontra pagina com espaco livre suficiente
  uint64_t FindPageWithSpace(size_t record_size) {
    // Otimizacao: tenta a ultima pagina que teve espaco livre
    if (last_page_with_space_ > 0) {
      auto page_result = GetPageUnsafeImpl(last_page_with_space_);
      if (page_result.ok()) {
        PageHeader* header = reinterpret_cast<PageHeader*>(page_result.value()->data.data());
        uint16_t slot_dir_end = sizeof(PageHeader) + header->slot_dir_size + sizeof(SlotEntry);
        if (header->free_space_end - slot_dir_end >= static_cast<uint16_t>(record_size)) {
          return last_page_with_space_;
        }
      }
    }

    // Varre paginas no cache (mais recentes primeiro - LRU order)
    for (auto& [page_id, page] : cache_) {
      PageHeader* header = reinterpret_cast<PageHeader*>(page.data.data());
      uint16_t slot_dir_end = sizeof(PageHeader) + header->slot_dir_size + sizeof(SlotEntry);
      if (header->free_space_end - slot_dir_end >= static_cast<uint16_t>(record_size)) {
        last_page_with_space_ = page_id;
        return page_id;
      }
    }

    // Se cache vazio ou sem espaco, carrega a ultima pagina do arquivo
    if (header_.page_count > 0) {
      uint64_t last_page = header_.page_count - 1;
      auto page_result = GetPageUnsafeImpl(last_page);
      if (page_result.ok()) {
        PageHeader* header = reinterpret_cast<PageHeader*>(page_result.value()->data.data());
        uint16_t slot_dir_end = sizeof(PageHeader) + header->slot_dir_size + sizeof(SlotEntry);
        if (header->free_space_end - slot_dir_end >= static_cast<uint16_t>(record_size)) {
          last_page_with_space_ = last_page;
          return last_page;
        }
      }
    }

    return 0;  // Alocar nova pagina
  }

  // Aloca nova pagina
  uint64_t AllocateNewPage() {
    uint64_t page_id = header_.page_count++;
    CachedPage page;
    page.page_id = page_id;
    page.data.assign(page_size_, 0);  // Zero-inicializa para evitar lixo nos slots
    page.dirty = true;

    // Pre-extende o arquivo no Windows para evitar erro de escrita
    size_t file_offset = sizeof(FileHeader) + page_id * page_size_;
    file_.clear();  // Limpa flags de erro
    file_.seekp(file_offset + page_size_ - 1);
    char zero = 0;
    file_.write(&zero, 1);
    file_.clear();  // Limpa flags apos escrita
    file_.seekp(file_offset);

    PageHeader* header = reinterpret_cast<PageHeader*>(page.data.data());
    header->page_id = page_id;
    header->record_count = 0;
    header->slot_dir_size = 0;
    header->free_space_end = static_cast<uint16_t>(page_size_);  // Espaco livre comeca no fim da pagina
    header->next_page_id = 0;
    header->checksum = 0;

    cache_[page_id] = std::move(page);
    lru_list_.push_front(page_id);
    cache_[page_id].lru_it = lru_list_.begin();

    EnforceCacheLimit();

    header_.page_count = page_id + 1;
    return page_id;
  }

  // Getter para header (para iterator)
  const FileHeader& GetHeader() const { return header_; }
  FileHeader& GetHeader() { return header_; }

  // Obtem pagina do cache ou carrega do disco
  common::Result<CachedPage*> GetPage(uint64_t page_id) {
    auto it = cache_.find(page_id);
    if (it != cache_.end()) {
      UpdateLRU(page_id);
      return &it->second;
    }

    // Carrega do disco
    if (page_id >= header_.page_count) {
      return common::Status::Error(common::StatusCode::kNotFound, "Pagina nao existe");
    }

    CachedPage page;
    page.page_id = page_id;
    page.data.resize(page_size_);

    // Calcula offset no arquivo (apos cabecalho)
    size_t file_offset = sizeof(FileHeader) + page_id * page_size_;
    file_.seekg(file_offset);
    file_.read(reinterpret_cast<char*>(page.data.data()), page_size_);

    if (!file_) {
      return common::Status::Error(common::StatusCode::kIOError, "Falha ao ler pagina do disco");
    }

    // Verifica checksum
    PageHeader* header = reinterpret_cast<PageHeader*>(page.data.data());
    uint32_t checksum = ComputeChecksum(page.data.data() + sizeof(PageHeader),
                                        page_size_ - sizeof(PageHeader));
    if (checksum != header->checksum && header->record_count > 0) {
      NEXODB_LOG_WARN("FileStorageEngine", "Checksum invalido na pagina " + std::to_string(page_id));
      // Continua mesmo assim (pode ser pagina nova nao inicializada)
    }

    cache_[page_id] = std::move(page);
    lru_list_.push_front(page_id);
    cache_[page_id].lru_it = lru_list_.begin();

    EnforceCacheLimit();
    return &cache_[page_id];
  }

  // Const version for const methods
  common::Result<const CachedPage*> GetPage(uint64_t page_id) const {
    auto it = cache_.find(page_id);
    if (it != cache_.end()) {
      // Note: UpdateLRU not called for const version to avoid modifying cache
      return &it->second;
    }

    if (page_id >= header_.page_count) {
      return common::Status::Error(common::StatusCode::kNotFound, "Pagina nao existe");
    }

    // For const version, we can't modify cache to load new pages
    return common::Status::Error(common::StatusCode::kIOError, "Const access not supported for uncached pages");
  }

  // Atualiza posicao na lista LRU
  void UpdateLRU(uint64_t page_id) {
    auto it = cache_.find(page_id);
    if (it != cache_.end()) {
      lru_list_.erase(it->second.lru_it);
      lru_list_.push_front(page_id);
      it->second.lru_it = lru_list_.begin();
    }
  }

  // Remove paginas excedentes do cache (LRU eviction)
  void EnforceCacheLimit() {
    while (cache_.size() > cache_size_pages_) {
      uint64_t lru_page_id = lru_list_.back();
      auto it = cache_.find(lru_page_id);
      if (it != cache_.end()) {
        if (it->second.dirty) {
          WritePageToDisk(lru_page_id, it->second);
        }
        cache_.erase(it);
      }
      lru_list_.pop_back();
    }
  }

  // Escreve pagina no disco
  void WritePageToDisk(uint64_t page_id, const CachedPage& page) {
    size_t file_offset = sizeof(FileHeader) + page_id * page_size_;
    file_.clear();  // Limpa flags de erro
    file_.seekp(file_offset);
    file_.write(reinterpret_cast<const char*>(page.data.data()), page_size_);
    if (!file_) {
      NEXODB_LOG_ERROR("FileStorageEngine", "Falha ao escrever pagina " + std::to_string(page_id) + " no disco");
    }
  }

  // Tipos de entrada do WAL
  enum class WALEntryType : uint8_t {
    kInsert = 1,
    kUpdate = 2,
    kDelete = 3,
    kCheckpoint = 4,
  };

  struct WALEntry {
    WALEntryType type;
    uint64_t page_id;
    uint16_t slot_id;
    uint32_t data_length;
    // Dados variaveis seguem
  };

  // Escreve entrada no WAL
  void WriteWALEntry(WALEntryType type, uint64_t page_id, uint16_t slot_id,
                     const std::vector<uint8_t>& data) {
    if (!wal_file_.is_open()) return;

    WALEntry entry;
    entry.type = type;
    entry.page_id = page_id;
    entry.slot_id = slot_id;
    entry.data_length = static_cast<uint32_t>(data.size());

    wal_file_.write(reinterpret_cast<const char*>(&entry), sizeof(WALEntry));
    if (!data.empty()) {
      wal_file_.write(reinterpret_cast<const char*>(data.data()), data.size());
    }
    wal_file_.flush();
  }

  StorageOptions options_;
  std::fstream file_;
  std::fstream wal_file_;
  FileHeader header_;
  size_t page_size_;
  size_t cache_size_pages_;

  // Cache LRU: map page_id -> CachedPage + lista LRU
  std::unordered_map<uint64_t, CachedPage> cache_;
  std::list<uint64_t> lru_list_;

  mutable std::mutex mutex_;
  uint64_t last_page_with_space_ = 0;  // Otimizacao: ultima pagina com espaco livre
  uint64_t document_count_ = 0;        // Contador de documentos validos em memoria
};

// =============================================================================
// Primary Index Implementation
// =============================================================================

common::Status FileStorageEngine::BuildPrimaryIndex() {
  std::lock_guard<std::mutex> lock(primary_index_mutex_);

  if (primary_index_built_) {
    return common::Status::Ok();
  }

  NEXODB_LOG_INFO("FileStorageEngine", "Construindo indice primario de _id...");
  primary_index_.clear();
  primary_index_.reserve(header_.page_count * 50);  // Estimativa

  // Varre todas as paginas
  for (uint64_t page_id = 0; page_id < header_.page_count; ++page_id) {
    auto page_result = GetPageUnsafeImpl(page_id);
    if (!page_result.ok()) {
      continue;
    }
    CachedPage& page = *page_result.value();

    PageHeader* header = reinterpret_cast<PageHeader*>(page.data.data());
    SlotEntry* slots = reinterpret_cast<SlotEntry*>(page.data.data() + sizeof(PageHeader));

    for (uint16_t slot_id = 0; slot_id < header->record_count; ++slot_id) {
      if (slots[slot_id].is_deleted) continue;

      const uint8_t* record_data = page.data.data() + slots[slot_id].offset;
      document::Document doc;
      auto deserialize_result = document::Deserialize(record_data, slots[slot_id].length, doc);
      if (!deserialize_result.ok()) continue;

      // Extrai _id do documento
      auto id_it = doc.fields().find("_id");
      if (id_it != doc.fields().end()) {
        const auto* oid = std::get_if<document::ObjectId>(&id_it->second.storage());
        if (oid) {
          RecordLocation loc{page_id, slot_id};
          primary_index_.emplace(*oid, loc);
        }
      }
    }
  }

  primary_index_built_ = true;
  NEXODB_LOG_INFO("FileStorageEngine", "Indice primario construido com " +
                  std::to_string(primary_index_.size()) + " entradas");
  return common::Status::Ok();
}

common::Result<std::optional<document::Document>> FileStorageEngine::FindById(
    const document::ObjectId& oid) const {
  // Constrói índice se necessário (lazy build)
  {
    std::lock_guard<std::mutex> lock(primary_index_mutex_);
    if (!primary_index_built_) {
      // Note: BuildPrimaryIndex não é const, precisamos chamar sem lock
      // Vamos liberar o lock e chamar
    }
  }

  // Se não construído, constrói (precisa de lock não-const)
  if (!primary_index_built_) {
    // Precisamos chamar método não-const - usamos const_cast já que é lazy init
    const_cast<FileStorageEngine*>(this)->BuildPrimaryIndex();
  }

  std::lock_guard<std::mutex> lock(primary_index_mutex_);
  auto it = primary_index_.find(oid);
  if (it == primary_index_.end()) {
    return std::optional<document::Document>{};
  }

  // Encontrou - lê o documento diretamente pela localização
  auto read_result = Read(it->second);
  if (!read_result.ok()) {
    return read_result.status();
  }
  return std::make_optional(std::move(read_result.value()));
}

common::Status FileStorageEngine::UpdatePrimaryIndex(const document::ObjectId& oid,
                                                      const RecordLocation& location) {
  std::lock_guard<std::mutex> lock(primary_index_mutex_);
  if (!primary_index_built_) {
    // Se índice não construído ainda, apenas marca para reconstruir depois
    return common::Status::Ok();
  }
  primary_index_[oid] = location;
  return common::Status::Ok();
}

common::Status FileStorageEngine::RemoveFromPrimaryIndex(const document::ObjectId& oid) {
  std::lock_guard<std::mutex> lock(primary_index_mutex_);
  if (!primary_index_built_) {
    return common::Status::Ok();
  }
  primary_index_.erase(oid);
  return common::Status::Ok();
}

// Iterator implementation for full scans (defined after FileStorageEngine)
class StorageIterator final : public StorageEngine::Iterator {
 public:
  explicit StorageIterator(FileStorageEngine& engine)
      : engine_(engine), current_page_id_(0), current_slot_(0), status_(common::Status::Ok()) {
    // Page 0 eh a primeira pagina de dados (apos o cabecalho do arquivo)
    current_page_id_ = 0;
    AdvanceToValidRecord();
  }

  bool Next(RecordLocation& out_location, document::Document& out_doc) override {
    // Iterative approach to avoid stack overflow
    while (current_page_id_ < engine_.GetHeader().page_count) {
      // Tenta ler registro atual
      RecordLocation loc{current_page_id_, current_slot_};
      auto read_result = engine_.Read(loc);
      if (read_result.ok()) {
        out_location = loc;
        out_doc = std::move(read_result.value());
        AdvanceToNextRecord();
        return true;
      }

      // Se falhou (ex: registro deletado), avanca para proximo
      AdvanceToNextRecord();
    }
    return false;
  }

  common::Status Status() const override {
    return status_;
  }

 private:
  void AdvanceToNextRecord() {
    current_slot_++;
    AdvanceToValidRecord();
  }

  void AdvanceToValidRecord() {
    while (current_page_id_ < engine_.GetHeader().page_count) {
      // Carrega pagina para verificar contagem de registros
      auto page_result = engine_.GetPageUnsafe(current_page_id_);
      if (!page_result.ok()) {
        current_page_id_++;
        current_slot_ = 0;
        continue;
      }

      const PageHeader* header = reinterpret_cast<const PageHeader*>(page_result.value()->data.data());
      const SlotEntry* slots = reinterpret_cast<const SlotEntry*>(page_result.value()->data.data() + sizeof(PageHeader));

      // Avanca ate encontrar um slot valido (nao deletado) ou fim da pagina
      while (current_slot_ < header->record_count) {
        if (!slots[current_slot_].is_deleted) {
          // Encontrou slot valido
          return;
        }
        current_slot_++;
      }

      // Proxima pagina
      current_page_id_++;
      current_slot_ = 0;
    }
  }

  FileStorageEngine& engine_;
  uint64_t current_page_id_;
  uint16_t current_slot_;
  common::Status status_;
};

// NewIterator implementation (after StorageIterator definition)
std::unique_ptr<StorageEngine::Iterator> FileStorageEngine::NewIterator() {
  return std::unique_ptr<StorageEngine::Iterator>(new StorageIterator(*this));
}

// =============================================================================
// Factory: StorageEngine::Open
// =============================================================================
common::Result<std::unique_ptr<StorageEngine>> StorageEngine::Open(
    const StorageOptions& options) {
  try {
    return std::unique_ptr<StorageEngine>(new FileStorageEngine(options));
  } catch (const std::exception& e) {
    return common::Status::Error(common::StatusCode::kIOError, e.what());
  }
}

}  // namespace nexodb::storage