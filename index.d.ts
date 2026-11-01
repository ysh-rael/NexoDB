// =============================================================================
// index.d.ts
// Tipagens publicas do pacote. Mantidas propositalmente simples por ora;
// devem evoluir junto com lib/*.js, espelhando os tipos do @types/mongoose
// onde fizer sentido para facilitar a migracao de projetos existentes.
// =============================================================================

export interface ConnectionOptions {
  cacheSizeMb?: number;
  readOnly?: boolean;
}

export class Connection {
  constructor(options: ConnectionOptions & { filePath: string });
  backup(destinationPath: string, opts?: Record<string, unknown>): Promise<void>;
  restore(backupPath: string): Promise<void>;
  close(): Promise<void> | void;
}

export class Schema<T = unknown> {
  constructor(definition: Record<string, unknown>, options?: Record<string, unknown>);
  index(fields: Record<string, 1 | -1>, options?: { unique?: boolean }): this;
}

export class Query<T> implements PromiseLike<T> {
  sort(spec: Record<string, 1 | -1>): this;
  limit(n: number): this;
  skip(n: number): this;
  select(projection: Record<string, 0 | 1>): this;
  exec(): Promise<T>;
  lean(): Promise<T>;
  then<TResult1 = T, TResult2 = never>(
    onfulfilled?: ((value: T) => TResult1 | PromiseLike<TResult1>) | null,
    onrejected?: ((reason: unknown) => TResult2 | PromiseLike<TResult2>) | null
  ): PromiseLike<TResult1 | TResult2>;
}

export class Model<T = unknown> {
  find(filter?: Record<string, unknown>): Query<T[]>;
  findOne(filter?: Record<string, unknown>): Query<T | null>;
  findById(id: string): Query<T | null>;
  count(filter?: Record<string, unknown>): Promise<number>;
  countDocuments(filter?: Record<string, unknown>): Promise<number>;
  findOneAndUpdate(filter: Record<string, unknown>, update: Record<string, unknown>, options?: Record<string, unknown>): Promise<T | null>;
  findByIdAndUpdate(id: string, update: Record<string, unknown>, options?: Record<string, unknown>): Promise<T | null>;
  updateOne(filter: Record<string, unknown>, update: Record<string, unknown>): Promise<{ matched: number; modified: number }>;
  updateMany(filter: Record<string, unknown>, update: Record<string, unknown>): Promise<{ matched: number; modified: number }>;
  replaceOne(filter: Record<string, unknown>, replacement: Record<string, unknown>): Promise<{ matched: number; modified: number }>;
  deleteOne(filter: Record<string, unknown>): Promise<{ deletedCount: number }>;
  findOneAndRemove(filter: Record<string, unknown>): Promise<T | null>;
  deleteMany(filter: Record<string, unknown>): Promise<{ deletedCount: number }>;
  remove(filter: Record<string, unknown>): Promise<{ deletedCount: number }>;
  findOneAndDelete(filter: Record<string, unknown>): Promise<T | null>;
  findOneAndReplace(filter: Record<string, unknown>, replacement: Record<string, unknown>): Promise<T | null>;
  aggregate(pipeline?: Record<string, unknown>[]): Promise<unknown[]>;
  create(data: Partial<T>): Promise<T>;
}

export function connect(filePath: string, options?: ConnectionOptions): Connection;
export function model<T = unknown>(name: string, schema?: Schema<T>, connection?: Connection): Model<T>;
export const models: Record<string, Model<unknown>>;
