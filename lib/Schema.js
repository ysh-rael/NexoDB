// =============================================================================
// lib/Schema.js
// Definicao de schema de uma colecao, no mesmo espirito do mongoose.Schema:
// descreve os campos esperados (para validacao/defaults no lado JS) e quais
// indices devem existir. A validacao de schema e feita em JavaScript, antes
// de qualquer chamada ao addon nativo, para manter o core em C++ livre de
// logica de validacao especifica de aplicacao.
// =============================================================================
'use strict';

class Schema {
  /**
   * @param {object} definition - mapa campo -> tipo/opcoes, ex.:
   *   { name: { type: String, required: true }, age: Number }
   * @param {object} [options]
   */
  constructor(definition, options = {}) {
    this.definition = definition;
    this.options = options;
    this._indexes = [];
  }

  /** Declara um indice, ex.: schema.index({ email: 1 }, { unique: true }) */
  index(fields, opts = {}) {
    this._indexes.push({ fields, options: opts });
    return this;
  }

  /** Retorna os indices declarados, usados por Connection ao abrir a colecao. */
  get indexes() {
    return this._indexes;
  }
}

module.exports = Schema;
