// =============================================================================
// lib/Connection.js
// Representa uma conexao com um arquivo de banco NexoDB, equivalente a uma
// Connection do Mongoose. Mantem a instancia nativa (Database, do addon C++)
// e e responsavel por abrir/fechar o arquivo e expor operacoes de
// administracao (backup/restore).
// =============================================================================
'use strict';

const { loadNative } = require('./NativeBinding');

class Connection {
  /**
   * @param {object} options
   * @param {string} options.filePath - caminho do arquivo .nxdb no disco.
   * @param {number} [options.cacheSizeMb=64] - tamanho do cache de paginas.
   * @param {boolean} [options.readOnly=false] - abre o banco somente leitura.
   */
  constructor(options) {
    if (!options || !options.filePath) {
      throw new TypeError('Connection: "filePath" e obrigatorio');
    }

    const { Database } = loadNative();
    this._native = new Database({
      filePath: options.filePath,
      cacheSizeMb: options.cacheSizeMb ?? 64,
      readOnly: options.readOnly ?? false,
    });
  }

  /** Cria um backup consistente do banco no caminho informado. */
  backup(destinationPath, opts = {}) {
    return this._native.backup({ destinationPath, ...opts });
  }

  /** Restaura o banco a partir de um arquivo de backup. */
  restore(backupPath) {
    return this._native.restore({ backupPath });
  }

  /** Fecha a conexao, garantindo que os dados pendentes sejam gravados. */
  close() {
    return this._native.close();
  }
}

module.exports = Connection;
