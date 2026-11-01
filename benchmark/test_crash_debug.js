// =============================================================================
// Debug crash on second connection
// =============================================================================
'use strict';

const { connect, Schema, model } = require('../index');
const os = require('node:os');
const path = require('node:path');
const fs = require('node:fs');

async function testCrashDebug() {
  const filePath = path.join(os.tmpdir(), `nexodb-crash-${Date.now()}.nxdb`);

  // Primeira conexao
  console.log('=== Conexao 1 ===');
  let conn = connect(filePath, { cacheSizeMb: 256 });
  console.log('connect() retornou');

  const UserSchema = new Schema({ name: String, age: Number });
  console.log('Schema criado');

  const User = model('User', UserSchema, conn);
  console.log('model() retornou');

  for (let i = 0; i < 100; i++) {
    await User.create({ name: `User ${i}`, age: i % 100 });
  }
  console.log('Inserts concluidos');

  let count = await User.countDocuments({});
  console.log(`Count: ${count}`);

  await conn.close();
  console.log('Conexao 1 fechada');

  // Segunda conexao - step by step
  console.log('\n=== Conexao 2: connect() ===');
  conn = connect(filePath, { cacheSizeMb: 256 });
  console.log('connect() retornou');

  console.log('Criando Schema...');
  const UserSchema2 = new Schema({ name: String, age: Number });
  console.log('Schema criado');

  console.log('Criando model...');
  const User2 = model('User', UserSchema2, conn);
  console.log('model() retornou');

  console.log('Chamando countDocuments...');
  count = await User2.countDocuments({});
  console.log(`Count: ${count}`);

  await conn.close();
  console.log('Conexao 2 fechada');

  // Cleanup
  try { fs.unlinkSync(filePath); } catch (e) {}

  console.log('\n=== OK ===');
}

testCrashDebug().catch(err => {
  console.error('Error:', err);
  console.error(err.stack);
  process.exit(1);
});