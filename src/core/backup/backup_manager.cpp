// =============================================================================
// backup_manager.cpp
// TODO(core/backup): implementar copia consistente de paginas (snapshot)
// enquanto novas escritas continuam sendo aceitas (copy-on-write ou
// bloqueio breve + replay do WAL), e a rotina simetrica de restauracao.
// =============================================================================
#include "nexodb/backup_manager.hpp"

namespace nexodb::backup {

common::Status BackupManager::CreateBackup(const BackupOptions& /*options*/) {
  return common::Status::Error(common::StatusCode::kUnsupported,
                                "BackupManager::CreateBackup: TODO");
}

common::Status BackupManager::RestoreFrom(const std::string& /*backup_path*/) {
  return common::Status::Error(common::StatusCode::kUnsupported,
                                "BackupManager::RestoreFrom: TODO");
}

}  // namespace nexodb::backup
