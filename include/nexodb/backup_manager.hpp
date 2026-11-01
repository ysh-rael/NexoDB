// =============================================================================
// backup_manager.hpp
// Backup e restauracao do arquivo de banco. Suporta backup "a quente"
// (com o banco em uso) via copia consistente das paginas + WAL, e
// restauracao a partir de um arquivo de backup previamente gerado.
// =============================================================================
#pragma once

#include <string>

#include "nexodb/common.hpp"
#include "nexodb/storage_engine.hpp"

namespace nexodb::backup {

struct BackupOptions {
  std::string destination_path;
  bool compress = true;
};

class BackupManager {
 public:
  explicit BackupManager(storage::StorageEngine& storage) : storage_(storage) {}

  common::Status CreateBackup(const BackupOptions& options);
  common::Status RestoreFrom(const std::string& backup_path);

 private:
  storage::StorageEngine& storage_;
};

}  // namespace nexodb::backup
