// =============================================================================
// test/js/connection.test.js
// Teste de integracao basico: abre uma conexao em um arquivo temporario e
// garante que ela fecha sem erro. Serve de "smoke test" ate o core do
// storage engine estar implementado; sera expandido com CRUD real.
// =============================================================================
'use strict';

const test = require('node:test');
const assert = require('node:assert');
const os = require('node:os');
const path = require('node:path');

const { connect } = require('../../index');

test('abre e fecha uma conexao em arquivo temporario', () => {
  const filePath = path.join(os.tmpdir(), `nexodb-test-${Date.now()}.nxdb`);
  const conn = connect(filePath);
  assert.ok(conn);
  conn.close();
});
