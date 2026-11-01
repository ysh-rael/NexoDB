// =============================================================================
// Test count persistence - simple test
// =============================================================================
'use strict';

const { connect } = require('../index');
const os = require('node:os');
const path = require('node:path');
const fs = require('node:fs');

async function testCountSimple() {
  const filePath = path.join(os.tmpdir(), `nexodb-simple-${Date.now()}.nxdb`);

  // Primeira conexao - apenas insert
  console.log('=== Conexao 1: Insert 5000 ===');
  let conn = connect(filePath, { cacheSizeMb: 256 });
  const { Schema, model } = require('../index');
  const UserSchema = new Schema({ name: String, age: Number });
  const User = model('User', UserSchema, conn);

  for (let i = 0; i < 5000; i++) {
    await User.create({ name: `User ${i}`, age: i % 100 });
  }

  let count = await User.countDocuments({});
  console.log(`Count: ${count}`);

  await conn.close();

  // Segunda conexao - apenas count
  console.log('\n=== Conexao 2: Count apenas ===');
  conn = connect(filePath, { cacheSizeMb: 256 });
  const User2 = model('User', UserSchema, conn);

  count = await User2.countDocuments({});
  console.log(`Count apos reabrir: ${count}`);

  await conn.close();

  // Cleanup
  try { fs.unlinkSync(filePath); } catch (e) {}

  console.log('\n=== OK ===');
}

testCountSimple().catch(err => {
  console.error('Error:', err);
  process.exit(1);
});