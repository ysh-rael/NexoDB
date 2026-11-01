// =============================================================================
// Teste de insercao basico: abre uma conexao em um arquivo temporario e garante que ela fecha sem erro.
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
