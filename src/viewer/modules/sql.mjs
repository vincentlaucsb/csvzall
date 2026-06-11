export const VIEWER_SQL_TABLE_NAME = 'data';

export function defaultSqlQuery(tableName = VIEWER_SQL_TABLE_NAME) {
  return `SELECT *\nFROM ${tableName}\nLIMIT 100;`;
}
